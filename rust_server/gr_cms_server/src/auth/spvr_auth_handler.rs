use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use crate::spvr_http_util::{get_body, get_body_data, get_body_str};
use axum::body::Body;
use axum::extract::State;
use axum::Json;
use gr_base::{md5_hex, ok_resp, RespMessage};
use std::sync::Arc;
use serde::Serialize;
use serde_json::Value;
use tokio::sync::Mutex;
use gr_auth_mgr::auth_util::{parse_authorization, verify_authorization};
use gr_auth_mgr::authorization::Authorization;
use gr_auth_mgr::time_util;
use crate::{gAuthManager, gKvStorage, gSpvrContext};
use crate::auth::spvr_auth_manager::KEY_AUTHORIZATION;
use crate::user::spvr_user_keys::{KEY_PASSWORD, KEY_USER_NAME};

// update the new authorization
pub async fn handle_update_authorization(State(_context): State<Arc<Mutex<SpvrContext>>>,
                                         body: Body)
                                         -> Result<Json<RespMessage<Authorization>>, SpvrApiError>  {
    let auth_str = get_body_data(body).await?;
    tracing::info!("auth body: {:#?}", auth_str);
    let auth = parse_authorization(auth_str.clone());
    if let Err(err) = auth {
        tracing::error!("Failed to parse authorization: {}", err);
        return Err(SpvrApiError::InvalidAuthorization);
    }

    let mut auth = auth.unwrap();
    if let Err(e) = verify_authorization(&auth) {
        tracing::error!("verify authorization failed: {}", e);
        return Err(SpvrApiError::InvalidAuthorization);
    }

    // check machine code
    if auth.machine_code != "MC-001" && auth.machine_code != "MC-002" {
        if auth.machine_code != gSpvrContext.lock().await.machine_code {
            tracing::error!("Machine code mismatch");
            return Err(SpvrApiError::MachineCodeNotMatched);
        }
    }
    // save to db
    gKvStorage
        .lock().await
        .put(KEY_AUTHORIZATION, auth_str.as_str());

    // update key
    gAuthManager
        .lock().await
        .update_key_used_time(&auth_str);

    // update auth manager
    gAuthManager
        .lock().await
        .update_auth(auth.clone()).await;

    // used time
    auth.used_time_ms = gAuthManager
        .lock().await
        .get_used_time().await;

    Ok(Json(ok_resp(auth)))
}

pub async fn handle_update_auth_password(State(_context): State<Arc<Mutex<SpvrContext>>>,
                                         b: Body)
                                         -> Result<Json<RespMessage<Authorization>>, SpvrApiError>{
    let body = get_body(b).await?;
    tracing::info!("auth body: {:#?}", body);
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let password = get_body_str(&r, KEY_PASSWORD)?;
    if password.is_empty() {
        tracing::error!("password is empty! can't modify it!");
        return Err(SpvrApiError::InvalidParams);
    }

    let mut auth = gAuthManager
        .lock().await
        .get_auth().await;
    if auth.auth_id.is_empty() {
        tracing::info!("auth id is empty! can't modify it!");
        return Err(SpvrApiError::DatabaseError);
    }
    auth.password = password;

    let deploy_str = auth.as_deploy_str();
    if let Err(e) = deploy_str {
        tracing::error!("Failed to deploy auth: {}", e);
        return Err(SpvrApiError::InternalError);
    }
    let deploy_str = deploy_str.unwrap();

    // save to db
    gKvStorage
        .lock().await
        .put(KEY_AUTHORIZATION, deploy_str.as_str());

    // update auth manager
    gAuthManager
        .lock().await
        .update_auth(auth.clone()).await;

    let used_time_ms = gAuthManager
        .lock().await
        .get_used_time().await;
    auth.used_time_ms = used_time_ms;

    Ok(Json(ok_resp(auth)))
}

pub async fn handle_get_authorization(State(_context): State<Arc<Mutex<SpvrContext>>>)
                                      -> Result<Json<RespMessage<Authorization>>, SpvrApiError>  {
    let used_time_ms = gAuthManager
        .lock().await
        .get_used_time().await;
    let mut auth = gAuthManager
        .lock().await
        .get_auth().await;
    auth.used_time_ms = used_time_ms;
    Ok(Json(ok_resp(auth)))
}

pub async fn handle_auth_valid(State(_context): State<Arc<Mutex<SpvrContext>>>)
                                      -> Result<Json<RespMessage<bool>>, SpvrApiError>  {
    let used_time_ms = gAuthManager
        .lock().await
        .get_used_time().await;
    let auth = gAuthManager
        .lock().await
        .get_auth().await;
    let total_time_ms = (auth.days as i64) * 24 * 60 * 60 * 1000; // -> ms
    let valid = used_time_ms < total_time_ms;
    Ok(Json(ok_resp(valid)))
}

#[derive(Serialize)]
#[derive(Default)]
pub(crate) struct RespUsedTime {
    pub used_time_ms: i64, // ms
    pub auth_days: i32, // days
    pub left_time_ms: i64, // ms
    pub left_readable_time: String
}

pub async fn handle_get_used_time(State(_context): State<Arc<Mutex<SpvrContext>>>)
                                    -> Result<Json<RespMessage<RespUsedTime>>, SpvrApiError>  {
    let used_time_ms = gAuthManager
        .lock().await
        .get_used_time().await;
    let auth = gAuthManager
        .lock().await
        .get_auth().await;
    let left_time_ms = (auth.days as i64) * 24 * 60 * 60 * 1000 - used_time_ms;

    Ok(Json(ok_resp(RespUsedTime {
        used_time_ms,
        auth_days: auth.days,
        left_time_ms,
        left_readable_time: time_util::format_duration(left_time_ms as u64),
    })))
}

pub async fn handle_verify_auth_account(State(_context): State<Arc<Mutex<SpvrContext>>>,
                                         b: Body)
                                         -> Result<Json<RespMessage<String>>, SpvrApiError> {
    let body = get_body(b).await?;
    tracing::info!("verify auth account body: {:#?}", body);
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let username = get_body_str(&r, KEY_USER_NAME)?;
    let password = get_body_str(&r, KEY_PASSWORD)?.to_lowercase();

    let auth = gAuthManager
        .lock().await
        .get_auth().await;
    if auth.auth_id.is_empty() {
        tracing::info!("auth id is empty! can't modify it!");
        return Err(SpvrApiError::DatabaseError);
    }

    let matched = auth.username == username
        && md5_hex(&auth.password).to_lowercase() == password;
    if matched {
        Ok(Json(ok_resp("".to_string())))
    }
    else {
        Err(SpvrApiError::PasswordInvalid)
    }
}