use crate::off_api_error::OffApiError;
use crate::off_context::OffContext;
use crate::version::off_version::{
    OffQueryVersionResponse, OffUpdateVersion, OffUpdateVersionResponse, OffVersion,
};
use crate::gOffVersionManager;
use axum::extract::{Query, State};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use mongodb::bson::DateTime;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_update_product_version(
    State(_ctx): State<Arc<Mutex<OffContext>>>,
    _query: Query<HashMap<String, String>>,
    Json(update_version): Json<OffUpdateVersion>,
) -> Result<Json<RespMessage<OffUpdateVersionResponse>>, OffApiError> {
    if update_version.verify_code != "e37a4d7256e51250da04fbcc7454a83b" {
        return Err(OffApiError::InvalidVersionVerifyCode);
    }

    if update_version.version.is_empty() {
        return Err(OffApiError::InvalidParams);
    }
    tracing::info!("new version: {}", update_version.version);

    let the_version = OffVersion {
        version: update_version.version,
        created_at: DateTime::now().timestamp_millis(),
    };

    gOffVersionManager.lock().await.current_version = the_version.clone();

    gOffVersionManager
        .lock()
        .await
        .insert_version(the_version)
        .await?;

    Ok(Json(ok_resp(OffUpdateVersionResponse {
        message: "ok".to_string(),
    })))
}

pub async fn handle_query_product_version(
    State(_ctx): State<Arc<Mutex<OffContext>>>,
    _query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<OffQueryVersionResponse>>, OffApiError> {
    let value;
    let cur_version = gOffVersionManager.lock().await.current_version.clone();
    if cur_version.version.is_empty() {
        let opt_version = gOffVersionManager
            .lock()
            .await
            .query_latest_version()
            .await?;

        if let Some(version) = opt_version {
            tracing::info!("latest version: {:?}", version);
            value = version.version.clone();
        } else {
            tracing::info!("no version found");
            return Err(OffApiError::VersionNotFound);
        }
    } else {
        value = cur_version.version.clone();
    }

    Ok(Json(ok_resp(OffQueryVersionResponse { version: value })))
}
