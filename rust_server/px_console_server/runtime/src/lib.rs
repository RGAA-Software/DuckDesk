//! New Console runtime composition. No Mongo or legacy transport adapter.
//! The product executable is switched only after all domain ingress is connected.
mod application_api;
mod config;
mod deployment_api;
mod device_api;
pub mod error;
mod guest_api;
mod guest_source;
mod identity;
mod management;
mod node_api;
mod node_wire;
mod policy;
mod request;
mod resource_api;
mod secrets;
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
pub use policy::IngressPolicy;
use px_console_store::{ConsoleDatabase, RuntimeEpoch, WorkspaceVault};
use px_pg::{DatabaseConfig, LeaseStatus, Service, ServiceLease};
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
}
impl StateData {
    fn active(&self) -> Result<(), ApiError> {
        if self.cancellation.is_cancelled() {
            return Err(ApiError::Unavailable);
        }
        self.lease.check()?;
        Ok(())
    }
}
/// Owner of the lease renewal task and the shared database pool.
pub struct ConsoleRuntime {
    state: Arc<StateData>,
    supervisor: JoinHandle<()>,
}
impl ConsoleRuntime {
    pub async fn activate(
        database: &DatabaseConfig,
        deployment: Uuid,
        vault: Arc<WorkspaceVault>,
        policy: IngressPolicy,
        guests: GuestAdmission,
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
        });
        let supervisor = tokio::spawn(async move {
            let _failure_guard = cancellation.clone().drop_guard();
            let mut interval = tokio::time::interval(Duration::from_secs(1));
            interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
            loop {
                tokio::select! {
                    biased;
                    _=cancellation.cancelled()=>break,
                    _=interval.tick()=>if lease.renew().await.is_err(){cancellation.cancel();break},
                }
            }
        });
        Ok(Self { state, supervisor })
    }
    pub fn router(&self) -> Router {
        Router::new()
            .merge(device_api::routes())
            .merge(application_api::routes())
            .merge(node_api::routes())
            .merge(deployment_api::routes())
            .merge(guest_api::routes())
            .merge(resource_api::routes())
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
    pub async fn cancelled(&self) {
        self.state.cancellation.cancelled().await;
    }
    /// Caller closes listeners and joins in-flight requests before closing the shared pool.
    pub async fn shutdown(mut self) {
        self.state.cancellation.cancel();
        self.supervisor.abort();
        let _ = (&mut self.supervisor).await;
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
    Ok(response)
}
