use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::event::audit;
use crate::identity::manager::IdentityManager;
use crate::user::cms_user::CmsUserView;
use crate::user::session::{AuthenticatedGuest, AuthenticatedUser};
use crate::{gAuthManager, gUserManager, gUserSessionManager};
use axum::extract::Multipart;
use axum::extract::{ConnectInfo, Extension, State};
use axum::http::{header, HeaderMap, HeaderValue};
use axum::response::{IntoResponse, Response};
use axum::Json;
use px_base::hash_util::{compute_hash, HashAlgo};
use px_base::{ok_resp, RespMessage};
use serde::{Deserialize, Serialize};
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::io::AsyncWriteExt;
use tokio::sync::Mutex;

const MAX_AVATAR_BYTES: usize = 2 * 1024 * 1024;
pub const ADMIN_SESSION_COOKIE: &str = "__Host-px_admin_session";
/// Compatibility cookie for browsers that reject `__Host-` cookies when the
/// CMS is opened through an IP address backed by a locally trusted/self-signed
/// certificate.  It keeps the same host-only, Secure, HttpOnly and SameSite
/// attributes; no session token is exposed to JavaScript.
pub const ADMIN_SESSION_COMPAT_COOKIE: &str = "px_admin_session";
pub const USER_SESSION_COOKIE: &str = "__Host-px_user_session";
pub const GUEST_SESSION_COOKIE: &str = "__Host-px_guest_session";

fn constant_time_equal(left: &str, right: &str) -> bool {
    let key = ring::hmac::Key::new(ring::hmac::HMAC_SHA256, b"pixels-session-compare-v1");
    let expected = ring::hmac::sign(&key, left.as_bytes());
    ring::hmac::verify(&key, right.as_bytes(), expected.as_ref()).is_ok()
}

async fn privacy_hash(value: &str) -> String {
    let settings = crate::gCmsSettings.lock().await;
    // Test configurations may intentionally omit the production-only salt;
    // still namespace the hash per CMS instead of storing a raw unsalted hash.
    let salt = if settings.privacy_hash_salt.is_empty() {
        format!("test:{}", settings.server_name)
    } else {
        settings.privacy_hash_salt.clone()
    };
    compute_hash(HashAlgo::SHA256, format!("{salt}\0{value}").as_bytes())
}

fn avatar_bytes_match_extension(extension: &str, bytes: &[u8]) -> bool {
    match extension {
        "png" => bytes.starts_with(&[0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a]),
        "jpg" | "jpeg" => bytes.starts_with(&[0xff, 0xd8, 0xff]),
        "webp" => bytes.len() >= 12 && bytes.starts_with(b"RIFF") && &bytes[8..12] == b"WEBP",
        _ => false,
    }
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct UserLoginRequest {
    pub username: String,
    pub password: String,
    pub client_type: String,
}

#[derive(Debug, Serialize, Default)]
pub struct UserLoginResponse {
    pub profile: CmsUserView,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub access_token: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub csrf_token: Option<String>,
    pub expires_at: i64,
    pub absolute_expires_at: i64,
}

#[derive(Debug, Serialize, Default)]
pub struct CsrfTokenResponse {
    pub csrf_token: String,
}

async fn profile_for(user: crate::user::cms_user::CmsUser) -> Result<CmsUserView, CmsApiError> {
    let mut profile = CmsUserView::from(user);
    profile.groups = IdentityManager::groups_for_user(&profile.uid).await?;
    Ok(profile)
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct GuestSessionRequest {
    pub client_nonce: String,
    #[serde(default)]
    pub client_type: Option<String>,
}

#[derive(Debug, Serialize, Default)]
pub struct GuestSessionResponse {
    pub csrf_token: String,
    pub expires_at: i64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub access_token: Option<String>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RegisterUserRequest {
    pub username: String,
    pub password: String,
}

pub async fn register_user(
    Extension(subject): Extension<AuthenticatedGuest>,
    Json(request): Json<RegisterUserRequest>,
) -> Result<Json<RespMessage<CmsUserView>>, CmsApiError> {
    let settings = crate::gCmsSettings.lock().await.user.clone();
    crate::user::rate_limit::check(
        format!("register-guest:{}", subject.guest_id),
        settings.rate_limit.login_per_ip_per_minute,
        60 * 1000,
    )?;
    crate::user::rate_limit::check(
        format!(
            "register-account:{}",
            request.username.trim().to_lowercase()
        ),
        settings.rate_limit.login_per_account_per_15_minutes,
        15 * 60 * 1000,
    )?;

    let user = match gUserManager
        .register_user(request.username, request.password)
        .await
    {
        Ok(user) => user,
        Err(error) => {
            audit::record(
                "guest",
                &subject.guest_id,
                "user_register",
                "failure",
                "user",
                "",
                "validation_or_conflict",
            )
            .await;
            return Err(error);
        }
    };
    audit::record(
        "guest",
        &subject.guest_id,
        "user_register",
        "success",
        "user",
        &user.uid,
        "",
    )
    .await;
    Ok(Json(ok_resp(profile_for(user).await?)))
}

pub async fn guest_session(
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Json(request): Json<GuestSessionRequest>,
) -> Result<Response, CmsApiError> {
    let forwarded_https = headers
        .get("x-forwarded-proto")
        .and_then(|value| value.to_str().ok())
        .is_some_and(|value| value.eq_ignore_ascii_case("https"));
    if !addr.ip().is_loopback() && !crate::gCmsSettings.lock().await.ssl_enable && !forwarded_https
    {
        return Err(CmsApiError::Forbidden);
    }
    if request.client_nonce.is_empty()
        || request.client_nonce.len() > 128
        || !request
            .client_nonce
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.'))
    {
        return Err(CmsApiError::InvalidParams);
    }
    let ip_hash = privacy_hash(&addr.ip().to_string()).await;
    if gUserSessionManager.is_guest_blocked(None, &ip_hash).await? {
        return Err(CmsApiError::Forbidden);
    }
    let limit = crate::gCmsSettings
        .lock()
        .await
        .user
        .rate_limit
        .guest_session_per_ip_per_hour;
    crate::user::rate_limit::check(format!("guest-ip:{ip_hash}"), limit, 60 * 60 * 1000)?;
    let user_agent_hash = match headers
        .get(header::USER_AGENT)
        .and_then(|value| value.to_str().ok())
    {
        Some(value) => privacy_hash(value).await,
        None => String::new(),
    };
    let is_panel = request.client_type.as_deref() == Some("panel");
    if request
        .client_type
        .as_deref()
        .is_some_and(|value| value != "panel")
    {
        return Err(CmsApiError::InvalidParams);
    }
    if !is_panel && !crate::user::session_router::same_origin(&headers) {
        return Err(CmsApiError::Forbidden);
    }
    let issued = gUserSessionManager
        .issue_guest(
            format!("guest-{}", uuid::Uuid::new_v4().simple()),
            ip_hash,
            user_agent_hash,
            if is_panel { "guest_panel" } else { "guest_web" }.to_string(),
        )
        .await?;
    let max_age =
        ((issued.session.absolute_expires_at - px_base::get_current_timestamp()) / 1000).max(1);
    let mut response = Json(ok_resp(GuestSessionResponse {
        csrf_token: issued.csrf_token,
        expires_at: issued.session.absolute_expires_at,
        access_token: is_panel.then(|| issued.session_token.clone()),
    }))
    .into_response();
    if !is_panel {
        response.headers_mut().insert(
            header::SET_COOKIE,
            HeaderValue::from_str(&format!(
                "{}={}; Path=/; Max-Age={}; Secure; HttpOnly; SameSite=Lax",
                GUEST_SESSION_COOKIE, issued.session_token, max_age
            ))
            .map_err(|_| CmsApiError::InternalError)?,
        );
    }
    response
        .headers_mut()
        .insert(header::CACHE_CONTROL, HeaderValue::from_static("no-store"));
    Ok(response)
}

pub async fn login(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Json(request): Json<UserLoginRequest>,
) -> Result<Response, CmsApiError> {
    let forwarded_https = headers
        .get("x-forwarded-proto")
        .and_then(|value| value.to_str().ok())
        .is_some_and(|value| value.eq_ignore_ascii_case("https"));
    if !addr.ip().is_loopback() && !crate::gCmsSettings.lock().await.ssl_enable && !forwarded_https
    {
        return Err(CmsApiError::Forbidden);
    }
    if !matches!(request.client_type.as_str(), "panel" | "user_web") {
        return Err(CmsApiError::InvalidParams);
    }
    let ip_hash = privacy_hash(&addr.ip().to_string()).await;
    let policy = crate::gCmsSettings.lock().await.user.rate_limit.clone();
    crate::user::rate_limit::check(
        format!("login-ip:{ip_hash}"),
        policy.login_per_ip_per_minute,
        60 * 1000,
    )?;
    crate::user::rate_limit::check(
        format!("login-account:{}", request.username.trim().to_lowercase()),
        policy.login_per_account_per_15_minutes,
        15 * 60 * 1000,
    )?;
    let login_name = request.username.trim().to_lowercase();
    let user = match gUserManager.query_user_by_username(request.username).await {
        Ok(user) => user,
        Err(_) => {
            audit::record(
                "user",
                &login_name,
                "login",
                "failure",
                "session",
                "",
                "invalid_credentials",
            )
            .await;
            return Err(CmsApiError::InvalidCredentials);
        }
    };
    if user.deleted
        || user.disabled
        || !crate::user::password::verify(&request.password, &user.password_hash)
    {
        audit::record(
            "user",
            &user.uid,
            "login",
            "failure",
            "session",
            "",
            "invalid_credentials",
        )
        .await;
        return Err(CmsApiError::InvalidCredentials);
    }
    if request.client_type == "panel" {
        let issued = gUserSessionManager
            .issue_panel(user.uid.clone(), user.auth_version)
            .await?;
        audit::record(
            "user",
            &user.uid,
            "login",
            "success",
            "session",
            &issued.session.sid,
            "panel",
        )
        .await;
        return Ok(Json(ok_resp(UserLoginResponse {
            profile: profile_for(user).await?,
            access_token: Some(issued.access_token),
            csrf_token: None,
            expires_at: issued.session.expires_at,
            absolute_expires_at: issued.session.absolute_expires_at,
        }))
        .into_response());
    }
    let user_agent_hash = match headers
        .get(header::USER_AGENT)
        .and_then(|value| value.to_str().ok())
    {
        Some(value) => privacy_hash(value).await,
        None => String::new(),
    };
    let issued = gUserSessionManager
        .issue_user_web(
            user.uid.clone(),
            user.auth_version,
            ip_hash,
            user_agent_hash,
        )
        .await?;
    audit::record(
        "user",
        &user.uid,
        "login",
        "success",
        "session",
        &issued.session.sid,
        "user_web",
    )
    .await;
    let mut response = Json(ok_resp(UserLoginResponse {
        profile: profile_for(user).await?,
        access_token: None,
        csrf_token: Some(issued.csrf_token),
        expires_at: issued.session.expires_at,
        absolute_expires_at: issued.session.absolute_expires_at,
    }))
    .into_response();
    response.headers_mut().insert(
        header::SET_COOKIE,
        HeaderValue::from_str(&format!(
            "{}={}; Path=/; Max-Age={}; Secure; HttpOnly; SameSite=Lax",
            USER_SESSION_COOKIE,
            issued.session_token,
            30 * 24 * 60 * 60
        ))
        .map_err(|_| CmsApiError::InternalError)?,
    );
    Ok(response)
}

pub async fn logout(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    headers: HeaderMap,
) -> Result<Response, CmsApiError> {
    let bearer = headers
        .get(header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
        .map(str::trim)
        .unwrap_or("");
    if !bearer.is_empty() {
        let subject = gUserSessionManager.authenticate(bearer).await.ok();
        gUserSessionManager.revoke_token(bearer).await?;
        if let Some(subject) = subject {
            audit::record(
                "user",
                &subject.uid,
                "logout",
                "success",
                "session",
                &subject.sid,
                "panel",
            )
            .await;
        }
    }
    if let Some(token) = cookie_value(&headers, USER_SESSION_COOKIE) {
        let subject = gUserSessionManager.authenticate_user_web(&token).await.ok();
        gUserSessionManager.revoke_token(&token).await?;
        if let Some(subject) = subject {
            audit::record(
                "user",
                &subject.uid,
                "logout",
                "success",
                "session",
                &subject.sid,
                "user_web",
            )
            .await;
        }
    }
    let mut response = Json(ok_resp(true)).into_response();
    response.headers_mut().append(
        header::SET_COOKIE,
        HeaderValue::from_static(
            "__Host-px_user_session=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Lax",
        ),
    );
    Ok(response)
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct AdminLoginRequest {
    pub username: String,
    pub password: String,
}

#[derive(Debug, Serialize, Default)]
pub struct AdminProfile {
    pub auth_id: String,
    pub username: String,
}

#[derive(Debug, Serialize, Default)]
pub struct AdminLoginResponse {
    pub profile: AdminProfile,
    pub csrf_token: String,
    pub expires_at: i64,
    pub absolute_expires_at: i64,
}

pub fn cookie_value(headers: &HeaderMap, name: &str) -> Option<String> {
    headers
        .get(header::COOKIE)
        .and_then(|value| value.to_str().ok())
        .and_then(|cookies| {
            cookies.split(';').find_map(|cookie| {
                let (key, value) = cookie.trim().split_once('=')?;
                (key == name).then(|| value.to_string())
            })
        })
}

pub fn admin_cookie_value(headers: &HeaderMap) -> Option<String> {
    admin_cookie_values(headers).into_iter().next()
}

pub fn admin_cookie_values(headers: &HeaderMap) -> Vec<String> {
    let mut values = Vec::with_capacity(2);
    if let Some(value) = cookie_value(headers, ADMIN_SESSION_COMPAT_COOKIE) {
        values.push(value);
    }
    if let Some(value) = cookie_value(headers, ADMIN_SESSION_COOKIE) {
        if !values.iter().any(|existing| existing == &value) {
            values.push(value);
        }
    }
    values
}

pub async fn admin_login(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Json(request): Json<AdminLoginRequest>,
) -> Result<Response, CmsApiError> {
    let forwarded_https = headers
        .get("x-forwarded-proto")
        .and_then(|value| value.to_str().ok())
        .is_some_and(|value| value.eq_ignore_ascii_case("https"));
    if !addr.ip().is_loopback() && !crate::gCmsSettings.lock().await.ssl_enable && !forwarded_https
    {
        return Err(CmsApiError::Forbidden);
    }
    let ip_hash = privacy_hash(&addr.ip().to_string()).await;
    let policy = crate::gCmsSettings.lock().await.user.rate_limit.clone();
    crate::user::rate_limit::check(
        format!("admin-login-ip:{ip_hash}"),
        policy.login_per_ip_per_minute,
        60 * 1000,
    )?;
    crate::user::rate_limit::check(
        format!(
            "admin-login-account:{}",
            request.username.trim().to_lowercase()
        ),
        policy.login_per_account_per_15_minutes,
        15 * 60 * 1000,
    )?;
    let auth = gAuthManager.lock().await.get_auth().await;
    let username_matches = constant_time_equal(&auth.username, &request.username);
    let password_matches = constant_time_equal(&auth.password, &request.password);
    if auth.auth_id.is_empty() || !username_matches || !password_matches {
        audit::record(
            "admin",
            &request.username.trim().to_lowercase(),
            "login",
            "failure",
            "session",
            "",
            "invalid_credentials",
        )
        .await;
        return Err(CmsApiError::InvalidCredentials);
    }
    let user_agent_hash = match headers
        .get(header::USER_AGENT)
        .and_then(|value| value.to_str().ok())
    {
        Some(value) => privacy_hash(value).await,
        None => String::new(),
    };
    let issued = gUserSessionManager
        .issue_admin(auth.auth_id.clone(), ip_hash, user_agent_hash)
        .await?;
    audit::record(
        "admin",
        &auth.auth_id,
        "login",
        "success",
        "session",
        &issued.session.sid,
        "admin_web",
    )
    .await;
    let body = Json(ok_resp(AdminLoginResponse {
        profile: AdminProfile {
            auth_id: auth.auth_id,
            username: auth.username,
        },
        csrf_token: issued.csrf_token,
        expires_at: issued.session.expires_at,
        absolute_expires_at: issued.session.absolute_expires_at,
    }));
    let mut response = body.into_response();
    response.headers_mut().append(
        header::SET_COOKIE,
        HeaderValue::from_str(&format!(
            "{}={}; Path=/; Max-Age={}; Secure; HttpOnly; SameSite=Lax",
            ADMIN_SESSION_COOKIE,
            issued.session_token,
            8 * 60 * 60
        ))
        .map_err(|_| CmsApiError::InternalError)?,
    );
    response.headers_mut().append(
        header::SET_COOKIE,
        HeaderValue::from_str(&format!(
            "{}={}; Path=/; Max-Age={}; Secure; HttpOnly; SameSite=Lax",
            ADMIN_SESSION_COMPAT_COOKIE,
            issued.session_token,
            8 * 60 * 60
        ))
        .map_err(|_| CmsApiError::InternalError)?,
    );
    Ok(response)
}

pub async fn admin_logout(headers: HeaderMap) -> Result<Response, CmsApiError> {
    if let Some(token) = admin_cookie_value(&headers) {
        let subject = gUserSessionManager.authenticate_admin(&token).await.ok();
        gUserSessionManager.revoke_token(&token).await?;
        if let Some(subject) = subject {
            audit::record(
                "admin",
                &subject.auth_id,
                "logout",
                "success",
                "session",
                &subject.sid,
                "admin_web",
            )
            .await;
        }
    }
    let mut response = Json(ok_resp(true)).into_response();
    response.headers_mut().append(
        header::SET_COOKIE,
        HeaderValue::from_static(
            "__Host-px_admin_session=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Lax",
        ),
    );
    response.headers_mut().append(
        header::SET_COOKIE,
        HeaderValue::from_static(
            "px_admin_session=; Path=/; Max-Age=0; Secure; HttpOnly; SameSite=Lax",
        ),
    );
    Ok(response)
}

pub async fn admin_me(
    Extension(subject): Extension<crate::user::session::AuthenticatedAdmin>,
) -> Result<Json<RespMessage<AdminProfile>>, CmsApiError> {
    let auth = gAuthManager.lock().await.get_auth().await;
    if auth.auth_id != subject.auth_id {
        return Err(CmsApiError::AuthenticationRequired);
    }
    Ok(Json(ok_resp(AdminProfile {
        auth_id: auth.auth_id,
        username: auth.username,
    })))
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ChangePasswordRequest {
    pub current_password: String,
    pub new_password: String,
}

pub async fn change_password(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<ChangePasswordRequest>,
) -> Result<Response, CmsApiError> {
    let user = gUserManager.query_user_by_id(subject.uid.clone()).await?;
    if user.deleted
        || user.disabled
        || !crate::user::password::verify(&request.current_password, &user.password_hash)
    {
        return Err(CmsApiError::InvalidCredentials);
    }
    let updated = gUserManager
        .update_user_password(subject.uid.clone(), request.new_password)
        .await?;
    gUserSessionManager.revoke_all(&subject.uid).await?;
    audit::record(
        "user",
        &subject.uid,
        "password_change",
        "success",
        "user",
        &subject.uid,
        "self_service",
    )
    .await;
    if subject.client_type == "panel" {
        let issued = gUserSessionManager
            .issue_panel(updated.uid.clone(), updated.auth_version)
            .await?;
        return Ok(Json(ok_resp(UserLoginResponse {
            profile: profile_for(updated).await?,
            access_token: Some(issued.access_token),
            csrf_token: None,
            expires_at: issued.session.expires_at,
            absolute_expires_at: issued.session.absolute_expires_at,
        }))
        .into_response());
    }
    let user_agent_hash = match headers
        .get(header::USER_AGENT)
        .and_then(|value| value.to_str().ok())
    {
        Some(value) => privacy_hash(value).await,
        None => String::new(),
    };
    let issued = gUserSessionManager
        .issue_user_web(
            updated.uid.clone(),
            updated.auth_version,
            privacy_hash(&addr.ip().to_string()).await,
            user_agent_hash,
        )
        .await?;
    let mut response = Json(ok_resp(UserLoginResponse {
        profile: profile_for(updated).await?,
        access_token: None,
        csrf_token: Some(issued.csrf_token),
        expires_at: issued.session.expires_at,
        absolute_expires_at: issued.session.absolute_expires_at,
    }))
    .into_response();
    response.headers_mut().insert(
        header::SET_COOKIE,
        HeaderValue::from_str(&format!(
            "{}={}; Path=/; Max-Age={}; Secure; HttpOnly; SameSite=Lax",
            USER_SESSION_COOKIE,
            issued.session_token,
            30 * 24 * 60 * 60
        ))
        .map_err(|_| CmsApiError::InternalError)?,
    );
    Ok(response)
}

pub async fn me(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    Extension(subject): Extension<AuthenticatedUser>,
) -> Result<Json<RespMessage<CmsUserView>>, CmsApiError> {
    let user = gUserManager.query_user_by_id(subject.uid).await?;
    Ok(Json(ok_resp(profile_for(user).await?)))
}

pub async fn refresh_user_csrf(
    Extension(subject): Extension<AuthenticatedUser>,
) -> Result<Response, CmsApiError> {
    if subject.client_type != "user_web" {
        return Err(CmsApiError::Forbidden);
    }
    let csrf_token = gUserSessionManager.refresh_user_csrf(&subject.sid).await?;
    let mut response = Json(ok_resp(CsrfTokenResponse { csrf_token })).into_response();
    response
        .headers_mut()
        .insert(header::CACHE_CONTROL, HeaderValue::from_static("no-store"));
    Ok(response)
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct UpdateProfileRequest {
    pub username: String,
}

pub async fn update_profile(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<UpdateProfileRequest>,
) -> Result<Json<RespMessage<CmsUserView>>, CmsApiError> {
    if subject.must_change_password {
        return Err(CmsApiError::Forbidden);
    }
    let user = gUserManager
        .update_username(subject.uid, request.username)
        .await?;
    Ok(Json(ok_resp(profile_for(user).await?)))
}

pub async fn update_avatar(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    Extension(subject): Extension<AuthenticatedUser>,
    mut multipart: Multipart,
) -> Result<Json<RespMessage<CmsUserView>>, CmsApiError> {
    if subject.must_change_password {
        return Err(CmsApiError::Forbidden);
    }
    let mut avatar: Option<(String, Vec<u8>)> = None;
    while let Some(mut field) = multipart
        .next_field()
        .await
        .map_err(|_| CmsApiError::InvalidParams)?
    {
        if field.name() != Some("file") {
            continue;
        }
        let filename = field.file_name().unwrap_or("");
        let extension = filename
            .rsplit_once('.')
            .map(|(_, extension)| extension.to_ascii_lowercase())
            .filter(|extension| matches!(extension.as_str(), "png" | "jpg" | "jpeg" | "webp"))
            .ok_or(CmsApiError::InvalidParams)?;
        let mut bytes = Vec::new();
        while let Some(chunk) = field
            .chunk()
            .await
            .map_err(|_| CmsApiError::UploadFileFailed)?
        {
            if bytes.len().saturating_add(chunk.len()) > MAX_AVATAR_BYTES {
                return Err(CmsApiError::InvalidParams);
            }
            bytes.extend_from_slice(&chunk);
        }
        if bytes.is_empty() {
            return Err(CmsApiError::InvalidParams);
        }
        if !avatar_bytes_match_extension(&extension, &bytes) {
            return Err(CmsApiError::InvalidParams);
        }
        avatar = Some((extension, bytes));
        break;
    }

    let (extension, bytes) = avatar.ok_or(CmsApiError::UploadFileFailed)?;
    let directory = "./uploads/avatar";
    tokio::fs::create_dir_all(directory)
        .await
        .map_err(|_| CmsApiError::UploadFileFailed)?;
    let upload_id = uuid::Uuid::new_v4().simple();
    let target_name = format!("{}_{}.{}", subject.uid, upload_id, extension);
    let target_path = format!("{}/{}", directory, target_name);
    let temporary_path = format!("{}/.{}_{}.tmp", directory, subject.uid, upload_id);
    let mut file = tokio::fs::File::create(&temporary_path)
        .await
        .map_err(|_| CmsApiError::UploadFileFailed)?;
    if file.write_all(&bytes).await.is_err() || file.flush().await.is_err() {
        let _ = tokio::fs::remove_file(&temporary_path).await;
        return Err(CmsApiError::UploadFileFailed);
    }
    drop(file);
    if tokio::fs::rename(&temporary_path, &target_path)
        .await
        .is_err()
    {
        let _ = tokio::fs::remove_file(&temporary_path).await;
        return Err(CmsApiError::UploadFileFailed);
    }

    let user = gUserManager
        .update_avatar_path(subject.uid, format!("/uploads/avatar/{}", target_name))
        .await?;
    Ok(Json(ok_resp(profile_for(user).await?)))
}

#[cfg(test)]
mod tests {
    use super::{admin_cookie_value, avatar_bytes_match_extension};
    use axum::http::{header, HeaderMap, HeaderValue};

    #[test]
    fn avatar_signature_must_match_the_allowed_extension() {
        assert!(avatar_bytes_match_extension(
            "png",
            &[0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a]
        ));
        assert!(avatar_bytes_match_extension("jpeg", &[0xff, 0xd8, 0xff]));
        assert!(avatar_bytes_match_extension("webp", b"RIFF0000WEBP"));
        assert!(!avatar_bytes_match_extension("png", b"<script>"));
        assert!(!avatar_bytes_match_extension("svg", b"<svg/>"));
    }

    #[test]
    fn admin_cookie_prefers_compatible_name_and_falls_back_to_host_cookie() {
        let mut headers = HeaderMap::new();
        headers.insert(
            header::COOKIE,
            HeaderValue::from_static(
                "__Host-px_admin_session=stale-host; px_admin_session=current-compatible",
            ),
        );
        assert_eq!(
            admin_cookie_value(&headers).as_deref(),
            Some("current-compatible")
        );

        headers.insert(
            header::COOKIE,
            HeaderValue::from_static("__Host-px_admin_session=host-only"),
        );
        assert_eq!(admin_cookie_value(&headers).as_deref(), Some("host-only"));
    }
}
