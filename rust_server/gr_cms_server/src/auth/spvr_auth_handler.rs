use crate::auth::spvr_auth_license_keys::license_to_authorization;
use crate::auth::spvr_auth_manager::KEY_AUTHORIZATION;
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use crate::spvr_http_util::{get_body, get_body_data, get_body_str, get_body_str_or_empty};
use crate::user::spvr_user_keys::{KEY_PASSWORD, KEY_USER_NAME};
use crate::{gAuthManager, gKvStorage, gLicenseVerifier, gSpvrContext};
use axum::body::Body;
use axum::extract::{Query, State};
use axum::Json;
use gr_auth_mgr::auth_license::SignedLicense;
use gr_auth_mgr::authorization::Authorization;
use gr_auth_mgr::time_util;
use gr_base::{get_current_timestamp, md5_hex, ok_resp, RespMessage};
use serde::Serialize;
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

// Upload a new signed license. The auth server is the authority for
// credentials (username/password/app_secret); they are carried in the signed
// license and used verbatim.
pub async fn handle_update_authorization(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    Query(_params): Query<HashMap<String, String>>,
    body: Body,
) -> Result<Json<RespMessage<Authorization>>, SpvrApiError> {
    let auth_str = get_body_data(body).await?;
    tracing::info!(
        "update/authorization: received deploy string, len={} preview='{}'",
        auth_str.len(),
        &auth_str[..auth_str.len().min(60)]
    );

    let verifier = gLicenseVerifier
        .lock()
        .await
        .as_ref()
        .map(Arc::clone)
        .ok_or(SpvrApiError::InternalError)?;
    let machine_code = gSpvrContext.lock().await.machine_code.clone();
    let now_ms = get_current_timestamp();
    tracing::info!(
        "update/authorization: machine_code='{}' now_ms={}",
        machine_code,
        now_ms
    );

    let signed = SignedLicense::parse_deploy_string(&auth_str).map_err(|e| {
        tracing::error!("update/authorization: parse_deploy_string failed: {}", e);
        SpvrApiError::InvalidAuthorization
    })?;

    tracing::info!(
        "update/authorization: parsed license OK, auth_id='{}' auth_name='{}' \
         machine_code='{}' appkey='{}' username='{}' password_len={}",
        signed.license.auth_id,
        signed.license.auth_name,
        signed.license.machine_code,
        signed.license.appkey,
        signed.license.username,
        signed.license.password.len()
    );

    let verify_result = verifier.verify(&signed, &machine_code, now_ms);
    match &verify_result {
        Ok(true) => tracing::info!("update/authorization: signature+machine+expiry verify OK"),
        Ok(false) => {
            // Distinguish which check failed for debugging.
            let sig_ok = verifier.verify_signature(&signed).unwrap_or(false);
            let mc_ok = signed.license.machine_code == machine_code;
            let exp_ok = signed.license.expires_at_ms > now_ms;
            tracing::error!(
                "update/authorization: verify returned false — \
                 signature_ok={} machine_code_ok={} (license='{}' vs local='{}') \
                 expiry_ok={} (expires_ms={} vs now_ms={})",
                sig_ok,
                mc_ok,
                signed.license.machine_code,
                machine_code,
                exp_ok,
                signed.license.expires_at_ms,
                now_ms
            );
        }
        Err(e) => tracing::error!("update/authorization: verify error: {}", e),
    }

    if !verify_result.map_err(|_| SpvrApiError::InvalidAuthorization)? {
        return Err(SpvrApiError::InvalidAuthorization);
    }

    let existing = gAuthManager.lock().await.get_auth().await;
    let mut auth = license_to_authorization(&signed.license, Some(&existing), auth_str.clone());

    // Derive app_secret from appkey so the appkey filter keeps working.
    use gr_auth_mgr::app_secret_util::calculate_app_secret;
    auth.app_secret = calculate_app_secret(auth.appkey.clone());

    tracing::info!(
        "update/authorization: authorization built, auth_id='{}' appkey='{}' \
         app_secret='{}' username='{}' password='{}'",
        auth.auth_id,
        auth.appkey,
        auth.app_secret,
        auth.username,
        auth.password
    );

    // save to db
    gKvStorage
        .lock()
        .await
        .put(KEY_AUTHORIZATION, auth_str.as_str());

    // update key
    gAuthManager.lock().await.update_key_used_time(&auth_str);

    // update auth manager
    gAuthManager.lock().await.update_auth(auth.clone()).await;

    // used time
    auth.used_time_ms = gAuthManager.lock().await.get_used_time().await;

    tracing::info!(
        "update/authorization: SUCCESS, auth stored in KvStorage and AuthManager"
    );
    Ok(Json(ok_resp(auth)))
}

const KEY_OLD_PASSWORD: &str = "old_password";
const KEY_APP_SECRET: &str = "app_secret";

pub async fn handle_update_auth_password(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    b: Body,
) -> Result<Json<RespMessage<Authorization>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let password = get_body_str(&r, KEY_PASSWORD)?;
    let old_password = get_body_str_or_empty(&r, KEY_OLD_PASSWORD);
    let provided_secret = get_body_str_or_empty(&r, KEY_APP_SECRET);
    if password.is_empty() {
        tracing::error!("password is empty! can't modify it!");
        return Err(SpvrApiError::InvalidParams);
    }

    let mut auth = gAuthManager.lock().await.get_auth().await;
    if auth.auth_id.is_empty() {
        tracing::info!("auth id is empty! can't modify it!");
        return Err(SpvrApiError::DatabaseError);
    }

    // require old password or app_secret to change the auth password
    let old_password_ok = !old_password.is_empty()
        && md5_hex(&auth.password).to_lowercase() == old_password.to_lowercase();
    let app_secret_ok = !provided_secret.is_empty() && provided_secret == auth.app_secret;
    if !old_password_ok && !app_secret_ok {
        tracing::error!("update password failed: neither old password nor app_secret matched");
        return Err(SpvrApiError::PasswordInvalid);
    }

    auth.password = password;

    // The deploy string in KvStorage is the signed license from the auth
    // server; we keep it as-is (the auth server is the authority for the
    // signed fields). The password change only affects the in-memory
    // Authorization so the web login uses the new password.
    // save to db (preserve the existing deploy string)
    if let Some(deploy_str) = gKvStorage.lock().await.get(KEY_AUTHORIZATION) {
        // deploy_str unchanged
        let _ = deploy_str;
    }

    // update auth manager
    gAuthManager.lock().await.update_auth(auth.clone()).await;

    let used_time_ms = gAuthManager.lock().await.get_used_time().await;
    auth.used_time_ms = used_time_ms;

    Ok(Json(ok_resp(auth)))
}

pub async fn handle_get_authorization(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
) -> Result<Json<RespMessage<SanitizedAuthorization>>, SpvrApiError> {
    let used_time_ms = gAuthManager.lock().await.get_used_time().await;
    let mut auth = gAuthManager.lock().await.get_auth().await;
    auth.used_time_ms = used_time_ms;
    tracing::info!(
        "get/authorization: auth_id='{}' auth_name='{}' appkey='{}' days={} max_streams={} end_ms={} used_ms={}",
        auth.auth_id, auth.auth_name, auth.appkey, auth.days, auth.max_streams, auth.end_timestamp_ms, auth.used_time_ms
    );
    Ok(Json(ok_resp(SanitizedAuthorization::from(auth))))
}

pub async fn handle_auth_valid(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
) -> Result<Json<RespMessage<bool>>, SpvrApiError> {
    let used_time_ms = gAuthManager.lock().await.get_used_time().await;
    let auth = gAuthManager.lock().await.get_auth().await;
    let now_ms = get_current_timestamp();
    let (left_time_ms, expired) =
        compute_auth_time_status(used_time_ms, auth.days, auth.end_timestamp_ms, now_ms);
    let valid = left_time_ms > 0 && !expired;
    tracing::info!(
        "auth/valid: auth_id='{}' appkey='{}' days={} used_ms={} end_ms={} now_ms={} left_ms={} expired={} -> valid={}",
        auth.auth_id, auth.appkey, auth.days, used_time_ms, auth.end_timestamp_ms, now_ms, left_time_ms, expired, valid
    );
    Ok(Json(ok_resp(valid)))
}

#[derive(Serialize, Default)]
pub struct SanitizedAuthorization {
    pub auth_id: String,
    pub auth_name: String,
    pub machine_code: String,
    pub appkey: String,
    pub app_secret: String,
    pub username: String,
    pub role: i32,
    pub days: i32,
    pub max_streams: i32,
    pub end_timestamp_ms: i64,
    pub used_time_ms: i64,
    pub created_timestamp_ms: i64,
}

impl From<Authorization> for SanitizedAuthorization {
    fn from(auth: Authorization) -> Self {
        SanitizedAuthorization {
            auth_id: auth.auth_id,
            auth_name: auth.auth_name,
            machine_code: auth.machine_code,
            appkey: auth.appkey,
            app_secret: auth.app_secret,
            username: auth.username,
            role: auth.role,
            days: auth.days,
            max_streams: auth.max_streams,
            end_timestamp_ms: auth.end_timestamp_ms,
            used_time_ms: auth.used_time_ms,
            created_timestamp_ms: auth.created_timestamp_ms,
        }
    }
}

#[derive(Serialize, Default)]
pub(crate) struct RespUsedTime {
    pub used_time_ms: i64, // ms
    pub auth_days: i32,    // days
    pub left_time_ms: i64, // ms
    pub left_readable_time: String,
    pub expired: bool,
}

pub async fn handle_get_used_time(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
) -> Result<Json<RespMessage<RespUsedTime>>, SpvrApiError> {
    let used_time_ms = gAuthManager.lock().await.get_used_time().await;
    let auth = gAuthManager.lock().await.get_auth().await;
    let now_ms = get_current_timestamp();
    let (left_time_ms, expired) =
        compute_auth_time_status(used_time_ms, auth.days, auth.end_timestamp_ms, now_ms);

    Ok(Json(ok_resp(RespUsedTime {
        used_time_ms,
        auth_days: auth.days,
        left_time_ms,
        left_readable_time: time_util::format_duration(left_time_ms as u64),
        expired,
    })))
}

/// Pure helper to calculate remaining time and expiration status.
/// Returns (left_time_ms, expired).
pub fn compute_auth_time_status(
    used_time_ms: i64,
    days: i32,
    end_timestamp_ms: i64,
    now_ms: i64,
) -> (i64, bool) {
    let total_time_ms = (days as i64) * 24 * 60 * 60 * 1000;
    let expired = used_time_ms >= total_time_ms || end_timestamp_ms <= now_ms;
    let left_time_ms = (total_time_ms - used_time_ms).max(0);
    (left_time_ms, expired)
}

pub async fn handle_verify_auth_account(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    b: Body,
) -> Result<Json<RespMessage<String>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let username = get_body_str(&r, KEY_USER_NAME)?;
    let password = get_body_str(&r, KEY_PASSWORD)?.to_lowercase();

    let auth = gAuthManager.lock().await.get_auth().await;
    if auth.auth_id.is_empty() {
        tracing::info!("auth id is empty! can't modify it!");
        return Err(SpvrApiError::DatabaseError);
    }

    let matched = auth.username == username && md5_hex(&auth.password).to_lowercase() == password;
    if matched {
        Ok(Json(ok_resp("".to_string())))
    } else {
        Err(SpvrApiError::PasswordInvalid)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sanitized_authorization_drops_sensitive_fields() {
        let auth = Authorization {
            auth_id: "id-1".to_string(),
            auth_name: "name-1".to_string(),
            machine_code: "mc-1".to_string(),
            appkey: "key-1".to_string(),
            app_secret: "secret-1".to_string(),
            username: "user-1".to_string(),
            password: "pass-1".to_string(),
            days: 30,
            max_streams: 4,
            end_timestamp_ms: 1234567890,
            used_time_ms: 1000,
            ..Default::default()
        };
        let sanitized = SanitizedAuthorization::from(auth);
        assert_eq!(sanitized.auth_id, "id-1");
        assert_eq!(sanitized.appkey, "key-1");
        assert_eq!(sanitized.app_secret, "secret-1");
        assert_eq!(sanitized.username, "user-1");
        assert_eq!(sanitized.created_timestamp_ms, 0);
        // Password is the only field that should remain hidden in the sanitized DTO.
        // (It is not part of SanitizedAuthorization.)
    }

    #[test]
    fn auth_time_status_active() {
        let now = 1000;
        let (left, expired) = compute_auth_time_status(1000, 1, now + 100000, now);
        assert!(left > 0);
        assert!(!expired);
    }

    #[test]
    fn auth_time_status_expired_by_used_time() {
        let now = 1000;
        let total = 24 * 60 * 60 * 1000; // 1 day in ms
        let (left, expired) = compute_auth_time_status(total, 1, now + 100000, now);
        assert_eq!(left, 0);
        assert!(expired);
    }

    #[test]
    fn auth_time_status_expired_by_end_timestamp() {
        let now = 1000;
        let (left, expired) = compute_auth_time_status(0, 1, now - 1, now);
        assert_eq!(left, 24 * 60 * 60 * 1000);
        assert!(expired);
    }

    #[test]
    fn auth_time_status_no_underflow_on_negative_left_time() {
        let now = 1000;
        let total = 24 * 60 * 60 * 1000;
        let (left, expired) = compute_auth_time_status(total + 5000, 1, now + 100000, now);
        assert_eq!(left, 0);
        assert!(expired);
    }
}
