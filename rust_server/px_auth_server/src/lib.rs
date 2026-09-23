mod client_address;
pub mod config;
pub mod error;
mod handlers;
use px_credentials as credentials;
use px_private_files::private as key_file;

use axum::{
    extract::DefaultBodyLimit,
    http::{header, HeaderValue, StatusCode},
    routing::{any, delete, get, patch, post},
    Router,
};
use px_auth_store::{LicenseStore, OperatorStore};
use px_license::{LicenseSigner, LicenseTrustStore};
use std::{path::Path, sync::Arc};
use tokio::sync::Semaphore;
use tower_http::{
    services::{ServeDir, ServeFile},
    set_header::SetResponseHeaderLayer,
};
use uuid::Uuid;
use zeroize::Zeroizing;

pub struct AppState {
    store: LicenseStore,
    operators: OperatorStore,
    deployment: Uuid,
    login_slots: Arc<Semaphore>,
    limits: credentials::LoginLimits,
    dummy_password: Zeroizing<String>,
    client_address_resolver: client_address::ClientAddressResolver,
}
impl AppState {
    pub async fn connect(settings: &config::Settings) -> Result<Self, Box<dyn std::error::Error>> {
        let material = key_file::read_private(&settings.signing_key)?;
        let signer = Arc::new(LicenseSigner::from_pkcs8(&material)?);
        let trust_store_bytes = key_file::read_private(&settings.trust_store)?;
        let trust_store = LicenseTrustStore::from_canonical_bytes(&trust_store_bytes)?;
        trust_store.verify_active_signer(&signer)?;
        let mut state = Self::from_signer(&settings.database, settings.deployment, signer).await?;
        state.client_address_resolver =
            client_address::ClientAddressResolver::new(settings.trusted_proxy);
        Ok(state)
    }
    pub async fn from_signer(
        database: &px_pg::DatabaseConfig,
        deployment: Uuid,
        signer: Arc<LicenseSigner>,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        let dummy_password =
            tokio::task::spawn_blocking(|| credentials::hash(&Uuid::new_v4().to_string()))
                .await??;
        let store = LicenseStore::connect(database, deployment, signer).await?;
        let operators = store.operators();
        Ok(Self {
            store,
            operators,
            deployment,
            login_slots: Arc::new(Semaphore::new(4)),
            limits: Default::default(),
            dummy_password,
            client_address_resolver: Default::default(),
        })
    }
    pub async fn close(&self) {
        self.store.close().await;
    }
}

pub fn router(state: Arc<AppState>, static_directory: &Path) -> Router {
    Router::new()
        .route("/health/live", get(|| async { StatusCode::NO_CONTENT }))
        .route("/health/ready", get(handlers::ready))
        .route("/api/auth/sessions", post(handlers::login))
        .route("/api/auth/session", delete(handlers::logout))
        .route("/api/auth/me", get(handlers::me))
        .route(
            "/api/auth/authors",
            get(handlers::authors).post(handlers::create_author),
        )
        .route(
            "/api/auth/authors/{id}/password",
            patch(handlers::set_password),
        )
        .route(
            "/api/auth/customers",
            get(handlers::customers).post(handlers::create_customer),
        )
        .route("/api/auth/licenses", get(handlers::licenses))
        .route("/api/auth/licenses/issue", post(handlers::issue))
        .route("/api/auth/licenses/{id}/revoke", post(handlers::revoke))
        .route("/api", any(|| async { StatusCode::NOT_FOUND }))
        .route("/api/{*path}", any(|| async { StatusCode::NOT_FOUND }))
        .fallback_service(
            ServeDir::new(static_directory)
                .not_found_service(ServeFile::new(static_directory.join("index.html"))),
        )
        .layer(DefaultBodyLimit::max(32 * 1024))
        .layer(SetResponseHeaderLayer::overriding(
            header::CACHE_CONTROL,
            HeaderValue::from_static("no-store"),
        ))
        .layer(SetResponseHeaderLayer::overriding(
            header::X_CONTENT_TYPE_OPTIONS,
            HeaderValue::from_static("nosniff"),
        ))
        .with_state(state)
}
