//! New Console runtime composition. No Mongo or legacy transport adapter.
//! The product executable is switched only after all domain ingress is connected.
mod application_api;
mod config;
mod deployment_api;
mod device_api;
pub mod error;
mod guest_api;
mod guest_source;
mod history_api;
mod identity;
mod license;
mod license_api;
mod management;
mod management_events;
mod node_api;
mod node_wire;
mod policy;
mod profile_api;
mod recording_cache_api;
mod recording_upload_api;
mod request;
mod resource_api;
mod saved_connection_api;
mod secrets;
mod static_files;
mod telemetry_alert_api;
mod update_api;
use axum::{
    extract::{DefaultBodyLimit, State},
    http::{header, HeaderValue, StatusCode},
    middleware::{self, Next},
    response::Response,
    routing::{get, patch, post},
    Router,
};
pub use config::{ConfigurationError, ConsoleLaunch, ConsoleLaunchConfig};
use error::ApiError;
pub use guest_source::GuestAdmission;
pub use license::{LicenseAdmissionError, LicenseEntitlement, LicenseLaunchConfig, LicenseStatus};
pub use policy::IngressPolicy;
use px_console_store::{
    CacheOptions, CacheRuntime, ConsoleDatabase, RuntimeEntitlement, RuntimeEpoch, WorkspaceVault,
};
use px_license::Feature;
use px_pg::{DatabaseConfig, LeaseStatus, Service, ServiceLease};
use px_private_files::CacheRoot;
pub use secrets::{RuntimeSecrets, WorkspaceKeyFile};
use std::{sync::Arc, time::Duration};
use tokio::{sync::Semaphore, task::JoinHandle};
use tokio_util::sync::CancellationToken;
use uuid::Uuid;
use zeroize::Zeroizing;

pub(crate) struct StateData {
    db: ConsoleDatabase,
    lease: LeaseStatus,
    cancellation: CancellationToken,
    policy: IngressPolicy,
    slots: Arc<Semaphore>,
    limits: px_credentials::LoginLimits,
    node_limits: px_credentials::LoginLimits,
    node_slots: Arc<Semaphore>,
    dummy: Zeroizing<String>,
    guests: GuestAdmission,
    epoch: RuntimeEpoch,
    recording_cache: Option<CacheRuntime>,
    uploads: Arc<recording_upload_api::UploadRegistry>,
    management_events: Arc<management_events::ManagementEvents>,
    license: LicenseEntitlement,
}
impl StateData {
    fn active(&self) -> Result<(), ApiError> {
        if self.cancellation.is_cancelled() {
            return Err(ApiError::Unavailable);
        }
        self.lease.check()?;
        self.license
            .validate_now()
            .map_err(|_| ApiError::Unavailable)?;
        Ok(())
    }

    fn entitlement(&self) -> RuntimeEntitlement {
        RuntimeEntitlement::new(
            self.license.payload.max_devices,
            self.license.payload.max_sessions,
            self.license
                .payload
                .features
                .contains(&Feature::CloudApplications),
            self.license.payload.features.contains(&Feature::Desktop),
            self.license.payload.features.contains(&Feature::Rdp),
        )
        .expect("a verified license always contains valid nonzero limits")
    }
}
/// Owner of the lease renewal task and the shared database pool.
pub struct ConsoleRuntime {
    state: Arc<StateData>,
    supervisor: JoinHandle<()>,
    license_supervisor: Option<JoinHandle<()>>,
    telemetry_retention: JoinHandle<()>,
    cache_expiration: Option<JoinHandle<()>>,
}
impl ConsoleRuntime {
    #[cfg(feature = "pg-integration")]
    pub async fn activate(
        database: &DatabaseConfig,
        deployment: Uuid,
        vault: Arc<WorkspaceVault>,
        policy: IngressPolicy,
        guests: GuestAdmission,
    ) -> Result<Self, ApiError> {
        Self::activate_inner(
            database,
            deployment,
            vault,
            policy,
            guests,
            None,
            LicenseEntitlement::synthetic_for_integration(deployment),
        )
        .await
    }
    #[cfg(feature = "pg-integration")]
    pub async fn activate_with_cache(
        database: &DatabaseConfig,
        deployment: Uuid,
        vault: Arc<WorkspaceVault>,
        policy: IngressPolicy,
        guests: GuestAdmission,
        recording_cache_root: Arc<CacheRoot>,
        recording_cache_options: CacheOptions,
    ) -> Result<Self, ApiError> {
        Self::activate_inner(
            database,
            deployment,
            vault,
            policy,
            guests,
            Some((recording_cache_root, recording_cache_options)),
            LicenseEntitlement::synthetic_for_integration(deployment),
        )
        .await
    }
    pub async fn activate_product_with_cache(
        database: &DatabaseConfig,
        deployment: Uuid,
        vault: Arc<WorkspaceVault>,
        policy: IngressPolicy,
        guests: GuestAdmission,
        recording_cache: (Arc<CacheRoot>, CacheOptions),
        license: LicenseEntitlement,
    ) -> Result<Self, ApiError> {
        Self::activate_inner(
            database,
            deployment,
            vault,
            policy,
            guests,
            Some(recording_cache),
            license,
        )
        .await
    }
    async fn activate_inner(
        database: &DatabaseConfig,
        deployment: Uuid,
        vault: Arc<WorkspaceVault>,
        policy: IngressPolicy,
        guests: GuestAdmission,
        recording_cache: Option<(Arc<CacheRoot>, CacheOptions)>,
        license: LicenseEntitlement,
    ) -> Result<Self, ApiError> {
        if !guests.matches(deployment) {
            return Err(ApiError::Invalid);
        }
        let dummy =
            tokio::task::spawn_blocking(|| px_credentials::hash(&Uuid::new_v4().to_string()))
                .await
                .map_err(|_| ApiError::Internal)?
                .map_err(|_| ApiError::Internal)?;
        let db = ConsoleDatabase::connect(database, deployment, vault).await?;
        let startup = async {
            if !db.initialized().await? {
                return Err(ApiError::Unavailable);
            }
            let lease = ServiceLease::acquire(database, Service::Console, deployment).await?;
            let epoch = db.nodes().begin_runtime().await?;
            lease.status().check()?;
            Ok((lease, epoch))
        }
        .await;
        let (mut lease, epoch) = match startup {
            Ok(pair) => pair,
            Err(error) => {
                db.close().await;
                return Err(error);
            }
        };
        let recording_cache = match recording_cache {
            Some((root, options)) => match db
                .recording_cache()
                .begin_runtime(root, epoch, options)
                .await
            {
                Ok(cache) => Some(cache),
                Err(error) => {
                    db.close().await;
                    return Err(error.into());
                }
            },
            None => None,
        };
        let cancellation = CancellationToken::new();
        let state = Arc::new(StateData {
            db,
            lease: lease.status(),
            cancellation: cancellation.clone(),
            policy,
            slots: Arc::new(Semaphore::new(4)),
            limits: Default::default(),
            node_limits: Default::default(),
            node_slots: Arc::new(Semaphore::new(node_wire::MAX_CONNECTIONS)),
            dummy,
            guests,
            epoch,
            recording_cache,
            uploads: recording_upload_api::UploadRegistry::new(),
            management_events: management_events::ManagementEvents::new(),
            license,
        });
        let supervisor_cancellation = cancellation.clone();
        let supervisor = tokio::spawn(async move {
            let _failure_guard = supervisor_cancellation.clone().drop_guard();
            let mut interval = tokio::time::interval(Duration::from_secs(1));
            interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
            loop {
                tokio::select! {
                    biased;
                    _=supervisor_cancellation.cancelled()=>break,
                    _=interval.tick()=>if lease.renew().await.is_err(){supervisor_cancellation.cancel();break},
                }
            }
        });
        let license_supervisor = state.license.online_refresh_interval().map(|refresh_period| {
            let license_state = state.clone();
            let license_cancellation = cancellation.clone();
            tokio::spawn(async move {
                let mut freshness_check = tokio::time::interval(Duration::from_secs(1));
                freshness_check.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
                let mut refresh = tokio::time::interval_at(
                    tokio::time::Instant::now() + refresh_period,
                    refresh_period,
                );
                refresh.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
                loop {
                    tokio::select! {
                        biased;
                        _=license_cancellation.cancelled()=>break,
                        _=freshness_check.tick()=>if license_state.license.validate_now().is_err(){
                            tracing::error!("official license authority freshness expired");
                            license_cancellation.cancel();
                            break;
                        },
                        _=refresh.tick()=>if let Err(error)=license_state.license.refresh_online().await{
                            tracing::warn!(%error, "official license authority refresh failed");
                        },
                    }
                }
            })
        });
        let telemetry_state = state.clone();
        let telemetry_cancellation = cancellation.clone();
        let telemetry_retention = tokio::spawn(async move {
            let mut interval = tokio::time::interval(Duration::from_secs(60));
            interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
            loop {
                tokio::select! {
                    biased;
                    _=telemetry_cancellation.cancelled()=>break,
                    _=interval.tick()=>{
                        match tokio::time::timeout(
                            Duration::from_secs(10),
                            async {
                                let samples = telemetry_state.db.nodes().prune_telemetry_history().await?;
                                let alerts = telemetry_state.db.telemetry_alerts().prune().await?;
                                Ok::<_, px_console_store::StoreError>((samples, alerts))
                            },
                        ).await {
                            Ok(Ok((samples, alerts))) if samples > 0 || alerts > 0 => {
                                tracing::info!(samples, alerts, "pruned expired node telemetry data");
                            }
                            Ok(Ok(_)) => {}
                            Ok(Err(error)) => {
                                tracing::warn!(%error, "node telemetry retention failed");
                            }
                            Err(_) => {
                                tracing::warn!("node telemetry retention timed out");
                            }
                        }
                    },
                }
            }
        });
        let cache_expiration = state.recording_cache.clone().map(|cache| {
            let cache_state = state.clone();
            let cache_cancellation = cancellation.clone();
            tokio::spawn(async move {
                let mut interval = tokio::time::interval(Duration::from_secs(30));
                interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
                loop {
                    tokio::select! {
                        biased;
                        _=cache_cancellation.cancelled()=>break,
                        _=interval.tick()=>{
                            match tokio::time::timeout(
                                Duration::from_secs(10),
                                cache_state.db.recording_cache().expire(&cache, 100),
                            ).await {
                                Ok(Ok(expired)) if expired > 0 => tracing::info!(expired, "expired abandoned recording cache attempts"),
                                Ok(Ok(_)) => {}
                                Ok(Err(error)) => tracing::warn!(%error, "recording cache expiration failed"),
                                Err(_) => tracing::warn!("recording cache expiration timed out"),
                            }
                        },
                    }
                }
            })
        });
        Ok(Self {
            state,
            supervisor,
            license_supervisor,
            telemetry_retention,
            cache_expiration,
        })
    }
    pub fn router(&self) -> Router {
        Router::new()
            .merge(device_api::routes())
            .merge(application_api::routes())
            .merge(license_api::routes())
            .merge(management_events::routes())
            .merge(node_api::routes())
            .merge(deployment_api::routes())
            .merge(guest_api::routes())
            .merge(resource_api::routes())
            .merge(saved_connection_api::routes())
            .merge(profile_api::routes())
            .merge(update_api::routes())
            .merge(history_api::routes())
            .merge(recording_cache_api::routes())
            .merge(recording_upload_api::routes())
            .merge(telemetry_alert_api::routes())
            .route("/health/ready", get(ready))
            .route("/api/console/accounts", post(identity::register))
            .route("/api/console/sessions", post(identity::login))
            .route(
                "/api/console/session",
                get(identity::profile).delete(identity::logout),
            )
            .route("/api/console/password", patch(identity::change_password))
            .route(
                "/api/console/users/{id}/password",
                patch(management::reset_password),
            )
            .route(
                "/api/console/users",
                get(management::users).post(management::create_user),
            )
            .route(
                "/api/console/users/{id}",
                patch(management::update_user).delete(management::delete_user),
            )
            .route(
                "/api/console/groups",
                get(management::groups).post(management::create_group),
            )
            .route(
                "/api/console/groups/{id}",
                get(management::group).delete(management::delete_group),
            )
            .route(
                "/api/console/groups/{id}/members",
                get(management::members).put(management::replace_members),
            )
            .layer(DefaultBodyLimit::max(32 * 1024))
            .layer(middleware::from_fn_with_state(
                self.state.clone(),
                admission,
            ))
            .with_state(self.state.clone())
            .route("/health/live", get(|| async { StatusCode::NO_CONTENT }))
    }
    pub fn product_router(&self, static_directory: std::path::PathBuf) -> Router {
        let static_files = Arc::new(static_files::StaticFiles::new(static_directory));
        self.router()
            .fallback(move |method, uri| static_files::serve(static_files.clone(), method, uri))
    }
    pub async fn cancelled(&self) {
        self.state.cancellation.cancelled().await;
    }
    pub fn cancellation_token(&self) -> CancellationToken {
        self.state.cancellation.clone()
    }
    /// Caller closes listeners and joins in-flight requests before closing the shared pool.
    pub async fn shutdown(mut self) {
        self.state.cancellation.cancel();
        self.supervisor.abort();
        if let Some(task) = &self.license_supervisor {
            task.abort();
        }
        self.telemetry_retention.abort();
        if let Some(task) = &self.cache_expiration {
            task.abort();
        }
        let _ = (&mut self.supervisor).await;
        if let Some(mut task) = self.license_supervisor.take() {
            let _ = (&mut task).await;
        }
        let _ = (&mut self.telemetry_retention).await;
        if let Some(mut task) = self.cache_expiration.take() {
            let _ = (&mut task).await;
        }
        // Every upgraded node connection owns one permit until its database generation is
        // closed. Listener shutdown must precede this call so no new upgrade can race the join.
        let _connections = self
            .state
            .node_slots
            .clone()
            .acquire_many_owned(node_wire::MAX_CONNECTIONS as u32)
            .await;
        self.state.db.close().await;
    }
}
impl Drop for ConsoleRuntime {
    fn drop(&mut self) {
        self.state.cancellation.cancel();
        self.supervisor.abort();
        if let Some(task) = &self.license_supervisor {
            task.abort();
        }
        self.telemetry_retention.abort();
        if let Some(task) = &self.cache_expiration {
            task.abort();
        }
    }
}
async fn ready(State(state): State<Arc<StateData>>) -> Result<StatusCode, ApiError> {
    state.active()?;
    state.db.ready().await?;
    if !state.db.initialized().await? {
        return Err(ApiError::Unavailable);
    }
    Ok(StatusCode::NO_CONTENT)
}
async fn admission(
    State(state): State<Arc<StateData>>,
    request: axum::extract::Request,
    next: Next,
) -> Result<Response, ApiError> {
    state.active()?;
    let method = request.method().clone();
    let path = request.uri().path().to_owned();
    let mut response = next.run(request).await;
    // Never return a freshly minted capability after this activation lost authority.
    state.active()?;
    response
        .headers_mut()
        .insert(header::CACHE_CONTROL, HeaderValue::from_static("no-store"));
    response.headers_mut().insert(
        header::X_CONTENT_TYPE_OPTIONS,
        HeaderValue::from_static("nosniff"),
    );
    if response.status().is_success() {
        if let Some((category, resource_id)) = management_events::http_mutation(&method, &path) {
            state.management_events.publish(category, resource_id);
        }
    }
    Ok(response)
}
