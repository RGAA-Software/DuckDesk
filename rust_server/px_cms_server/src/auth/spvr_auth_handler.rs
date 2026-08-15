use crate::auth::spvr_auth_pull::{pull_once, PullOutcome};
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use crate::spvr_http_util::{get_body, get_body_str, get_body_str_or_empty};
use crate::user::spvr_user_keys::{KEY_PASSWORD, KEY_USER_NAME};
use crate::gAuthManager;
use axum::body::Body;
use axum::extract::{ConnectInfo, State};
use axum::Json;
use px_auth_mgr::authorization::Authorization;
use px_auth_mgr::time_util;
use px_base::{get_current_timestamp, md5_hex, ok_resp, RespMessage};
use serde::Serialize;
use serde_json::Value;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::Mutex;

// Manually trigger one authorization pull from the auth server (网络上报授权模式,
// 取代旧的手工上传 license 入口 update/authorization)。
// 该接口在 appkey filter 白名单内（未授权时没有 appkey 可用），因此**只能返回
// 不含凭据的安全状态**（AuthStatus）；登录凭据（username/password）仅对来自
// 服务器本机的请求附带（用于登录页自动填充），远程请求一律为空。
// 服务器已吊销时返回 authorized=false 的状态（拉取失败才返回错误，失败时本地
// 已有授权保持不变）。
pub async fn handle_pull_authorization(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
) -> Result<Json<RespMessage<AuthStatus>>, SpvrApiError> {
    let local = is_local_request(&addr);
    match pull_once().await {
        Ok(PullOutcome::Active(auth)) => {
            tracing::info!(
                "pull/authorization: success, auth_id='{}' mode='{}' days={} max_streams={} local={}",
                auth.auth_id,
                auth.mode,
                auth.days,
                auth.max_streams,
                local
            );
            Ok(Json(ok_resp(build_auth_status(auth, local).await)))
        }
        Ok(PullOutcome::Revoked) => {
            tracing::warn!("pull/authorization: authorization revoked by auth server");
            Ok(Json(ok_resp(build_auth_status(Authorization::default(), local).await)))
        }
        Err(e) => {
            tracing::error!("pull/authorization: pull failed: {}", e);
            Err(SpvrApiError::InternalError)
        }
    }
}

/// 授权状态（安全视图）：供未登录/未授权的登录页展示。appkey/app_secret 一律
/// 不下发；web 登录凭据（username/password）仅对服务器本机请求附带（登录页
/// 自动填充用），远程请求为空串。
#[derive(Serialize, Default)]
pub struct AuthStatus {
    /// 是否已有授权记录（试用或正式）。
    pub authorized: bool,
    /// 授权模式："trial" | "licensed"（未授权时为空串）。
    pub mode: String,
    pub days: i32,
    pub max_streams: i32,
    pub end_timestamp_ms: i64,
    pub used_time_ms: i64,
    /// 授权当前是否有效（未过期）。
    pub valid: bool,
    /// 本机机器码（xxxx-xxxx）。
    pub machine_code: String,
    /// web 登录用户名/密码：仅本机请求非空。
    pub username: String,
    pub password: String,
}

/// 仅当请求来自服务器本机（loopback 或本机任一网卡 IP）时才认为是本地请求。
fn is_local_request(addr: &SocketAddr) -> bool {
    let ip = addr.ip();
    if ip.is_loopback() {
        return true;
    }
    if let Ok(ips) = px_base::ip_util::get_clean_ipv4_addresses() {
        return ips.iter().any(|local| std::net::IpAddr::V4(*local) == ip);
    }
    false
}

async fn build_auth_status(auth: Authorization, include_credentials: bool) -> AuthStatus {
    let used_time_ms = gAuthManager.lock().await.get_used_time().await;
    let now_ms = get_current_timestamp();
    let (left_time_ms, expired) =
        compute_auth_time_status(used_time_ms, auth.days, auth.end_timestamp_ms, now_ms);
    let authorized = !auth.auth_id.is_empty();
    let machine_code = crate::gSpvrContext.lock().await.machine_code.clone();
    AuthStatus {
        authorized,
        mode: if authorized { auth.mode } else { String::new() },
        days: auth.days,
        max_streams: auth.max_streams,
        end_timestamp_ms: auth.end_timestamp_ms,
        used_time_ms,
        valid: authorized && left_time_ms > 0 && !expired,
        machine_code,
        username: if include_credentials { auth.username } else { String::new() },
        password: if include_credentials { auth.password } else { String::new() },
    }
}

/// `GET /get/auth/status` — 登录页展示用授权状态（appkey filter 白名单接口）。
pub async fn handle_get_auth_status(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
) -> Result<Json<RespMessage<AuthStatus>>, SpvrApiError> {
    let local = is_local_request(&addr);
    let auth = gAuthManager.lock().await.get_auth().await;
    Ok(Json(ok_resp(build_auth_status(auth, local).await)))
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
    // 注意（已知行为，与旧模式"重启后恢复 license 密码"语义一致）：
    // 周期 pull 会用服务器 license 里的密码覆盖回内存中的修改。

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
    /// 授权模式："trial"（试用）| "licensed"（正式）。
    pub mode: String,
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
            mode: auth.mode,
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
        // 登录本身就是凭据校验（license 里的 username/password），通过后将
        // appkey 返回给前端保存，供后续受 appkey filter 保护的接口使用。
        Ok(Json(ok_resp(auth.appkey)))
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
        assert_eq!(sanitized.mode, "licensed"); // Authorization::default().mode
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
