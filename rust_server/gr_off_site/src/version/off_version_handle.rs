use std::collections::HashMap;
use std::sync::Arc;
use axum::body::Body;
use axum::extract::{Query, State};
use axum::Json;
use mongodb::bson::{doc, DateTime};
use mongodb::bson::oid::ObjectId;
use serde_json::Value;
use tokio::sync::Mutex;
use gr_base::{ok_resp, RespMessage, RespStringMap};
use crate::version::off_version::{OffUpdateVersion, OffUpdateVersionResponse, OffQueryVersionResponse, OffVersion};
use crate::{gOffVersionManager, gOffDatabase};
use crate::off_api_error::OffApiError;
use crate::off_api_keys::{KEY_CONSULT_TYPE, KEY_CONTENT, KEY_EMAIL, KEY_ITEM_ID, KEY_PROCESSED, KEY_QQ, KEY_TITLE, KEY_WECHAT, KEY_YOUR_NAME};
use crate::off_context::OffContext;
use crate::off_http_utils::{get_body, get_int_param, get_int_param_or, get_str_param};

pub async fn handle_update_product_version(State(_ctx): State<Arc<Mutex<OffContext>>>,
                                      query: Query<HashMap<String, String>>,
                                      Json(mut update_version): Json<OffUpdateVersion>)
                                      -> Result<Json<RespMessage<OffUpdateVersionResponse>>, OffApiError> {

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

    gOffVersionManager
        .lock().await
        .current_version = the_version.clone();

    gOffVersionManager
        .lock().await
        .insert_version(the_version).await?;

    Ok(Json(ok_resp(OffUpdateVersionResponse {
        message: "ok".to_string(),
    })))
}

pub async fn handle_query_product_version(State(_ctx): State<Arc<Mutex<OffContext>>>,
                                           query: Query<HashMap<String, String>>)
                                           -> Result<Json<RespMessage<OffQueryVersionResponse>>, OffApiError> {

    let mut value = String::new();
    let mut cur_version = gOffVersionManager.lock().await.current_version.clone();
    if cur_version.version.is_empty() {
        let opt_version = gOffVersionManager.lock().await.query_latest_version().await?;

        if let Some(version) = opt_version {
            tracing::info!("latest version: {:?}", version);
            value = version.version.clone();
        } else {
            tracing::info!("no version found");
            return Err(OffApiError::VersionNotFound);
        }
    } else{
        value = cur_version.version.clone();
    }

    Ok(Json(ok_resp(OffQueryVersionResponse {
        version: value,
    })))
}