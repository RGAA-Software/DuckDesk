mod auth;
pub mod config;
pub mod error;
mod handlers;
mod model;
mod store;

use axum::{
    extract::DefaultBodyLimit,
    http::{header, HeaderValue, StatusCode},
    routing::{any, delete, get, patch},
    Router,
};
use config::Settings;
use px_pg::{PgPool, Service};
use std::{path::Path, sync::Arc};
use tokio::sync::Semaphore;
use tower_http::{
    services::{ServeDir, ServeFile},
    set_header::SetResponseHeaderLayer,
};
use uuid::Uuid;

pub static MIGRATIONS: sqlx::migrate::Migrator = sqlx::migrate!("./migrations");

pub struct AppState {
    pool: PgPool,
    deployment: Uuid,
    admin_digest: [u8; 32],
    login_slots: Semaphore,
}
impl AppState {
    pub async fn connect(settings: &Settings) -> Result<Self, px_pg::DatabaseError> {
        let pool = settings
            .database
            .connect_runtime(Service::Desk, settings.deployment, &MIGRATIONS)
            .await?;
        Ok(Self {
            pool,
            deployment: settings.deployment,
            admin_digest: settings.admin_digest,
            login_slots: Semaphore::new(8),
        })
    }
    pub async fn close(&self) {
        self.pool.close().await;
    }
}

pub fn router(state: Arc<AppState>, static_directory: &Path) -> Router {
    Router::new()
        .route("/health/live", get(|| async { StatusCode::NO_CONTENT }))
        .route("/health/ready", get(handlers::ready))
        .route("/api/desk/admin/sessions", axum::routing::post(auth::login))
        .route("/api/desk/admin/session", delete(auth::logout))
        .route(
            "/api/desk/consults",
            get(handlers::consults).post(handlers::create_consult),
        )
        .route(
            "/api/desk/issues",
            get(handlers::issues).post(handlers::create_issue),
        )
        .route("/api/desk/consults/{id}", patch(handlers::mark_consult))
        .route("/api/desk/issues/{id}", patch(handlers::mark_issue))
        .route(
            "/api/desk/versions",
            get(handlers::version).post(handlers::publish),
        )
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
