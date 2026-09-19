use crate::console_api_error::ConsoleApiError;
use crate::event::audit;
use crate::identity::manager::IdentityManager;
use crate::identity::model::GroupRef;
use crate::user::console_user::ConsoleUser;
use crate::user::session::AuthenticatedUser;
use crate::{gConsoleUserDeviceMgr, gDeviceManager, gUserManager, gUserSessionManager};
use axum::extract::{Extension, Path, Query};
use axum::http::{header, HeaderValue, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Default)]
pub struct UserAdminView {
    pub uid: String,
    pub username: String,
    pub avatar_url: String,
    pub assigned: bool,
    pub disabled: bool,
    pub auth_version: i64,
    pub must_change_password: bool,
    pub groups: Vec<GroupRef>,
    pub created_at: i64,
    pub updated_at: i64,
    pub version: i64,
}

impl UserAdminView {
    async fn from_user(user: ConsoleUser) -> Result<Self, ConsoleApiError> {
        Ok(Self {
            groups: IdentityManager::groups_for_user(&user.uid).await?,
            uid: user.uid,
            username: user.username,
            avatar_url: user.avatar_path,
            assigned: user.assigned,
            disabled: user.disabled,
            auth_version: user.auth_version,
            must_change_password: false,
            created_at: user.created_timestamp,
            updated_at: user.update_timestamp,
            version: user.version,
        })
    }
}

#[derive(Debug, Serialize, Default)]
pub struct UserPage {
    pub items: Vec<UserAdminView>,
    pub page: i32,
    pub page_size: i32,
    pub total: u32,
}

#[derive(Debug, Serialize)]
pub struct SessionAdminView {
    pub sid: String,
    pub client_type: String,
    pub created_at: i64,
    pub last_used_at: i64,
    pub expires_at: i64,
    pub absolute_expires_at: i64,
    pub revoked_at: Option<i64>,
    /// Correlation-only prefixes. Full source and user-agent hashes are not
    /// exposed to the browser.
    pub ip_hash_prefix: String,
    pub user_agent_hash_prefix: String,
}

#[derive(Debug, Serialize)]
pub struct GuestSessionAdminView {
    pub sid: String,
    pub guest_id: String,
    pub client_type: String,
    pub created_at: i64,
    pub last_used_at: i64,
    pub expires_at: i64,
    pub revoked_at: Option<i64>,
    pub ip_hash_prefix: String,
    pub user_agent_hash_prefix: String,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct BlockGuestRequest {
    #[serde(default)]
    pub block_guest_id: bool,
    #[serde(default)]
    pub block_ip_hash: bool,
    #[serde(default)]
    pub reason: String,
}

fn hash_prefix(value: &str) -> String {
    value.chars().take(8).collect()
}

#[derive(Debug, Deserialize, Default)]
#[serde(deny_unknown_fields)]
pub struct UserListQuery {
    pub page: Option<i32>,
    pub page_size: Option<i32>,
    pub keyword: Option<String>,
    pub sort: Option<String>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CreateUserRequest {
    pub username: String,
    pub initial_password: Option<String>,
    #[serde(default)]
    pub group_ids: Vec<String>,
    #[serde(default)]
    pub device_ids: Vec<String>,
}

#[derive(Debug, Serialize, Default)]
pub struct CreateUserResponse {
    pub user: UserAdminView,
    pub initial_password: String,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct BatchCreateUsersRequest {
    pub size: u16,
    #[serde(default = "default_user_prefix")]
    pub username_prefix: String,
    #[serde(default)]
    pub group_ids: Vec<String>,
}

fn default_user_prefix() -> String {
    "user".to_string()
}

#[derive(Serialize)]
struct BatchUserCsvRow {
    username: String,
    initial_password: String,
    uid: String,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct UpdateUserRequest {
    pub version: i64,
    pub username: Option<String>,
    pub disabled: Option<bool>,
    pub avatar_url: Option<String>,
    pub group_ids: Option<Vec<String>>,
    pub device_ids: Option<Vec<String>>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct VersionRequest {
    pub version: i64,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ResetPasswordRequest {
    pub version: i64,
    #[serde(default)]
    pub generated: bool,
    pub supplied_password: Option<String>,
}

#[derive(Debug, Serialize, Default)]
pub struct ResetPasswordResponse {
    pub user: UserAdminView,
    pub initial_password: String,
}

#[derive(Debug, Serialize, Default)]
pub struct PasswordViewResponse {
    /// `None` is returned only for accounts created before recoverable
    /// password storage was introduced. Resetting such an account populates it.
    pub password: Option<String>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct LogoutAllRequest {
    pub current_password: String,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ReplacePersonalDevicesRequest {
    pub device_ids: Vec<String>,
}

pub async fn list_users(
    Query(query): Query<UserListQuery>,
) -> Result<Json<RespMessage<UserPage>>, ConsoleApiError> {
    let page = query.page.unwrap_or(1).clamp(1, 100_000);
    let page_size = query.page_size.unwrap_or(20).clamp(1, 100);
    let keyword = query.keyword.unwrap_or_default();
    if keyword.chars().count() > 128 || keyword.chars().any(char::is_control) {
        return Err(ConsoleApiError::InvalidParams);
    }
    let (sort_field, sort_order) = match query.sort.as_deref().unwrap_or("-created_at") {
        "created_at" => ("created_timestamp", 1),
        "-created_at" => ("created_timestamp", -1),
        "username" => ("username_normalized", 1),
        "-username" => ("username_normalized", -1),
        _ => return Err(ConsoleApiError::InvalidParams),
    };
    let users = gUserManager
        .query_users(
            page,
            page_size,
            keyword.clone(),
            String::new(),
            Some(sort_field.to_string()),
            Some(sort_order),
        )
        .await?;
    let mut items = Vec::with_capacity(users.len());
    for user in users {
        items.push(UserAdminView::from_user(user).await?);
    }
    Ok(Json(ok_resp(UserPage {
        items,
        page,
        page_size,
        total: gUserManager.count_users_matching(&keyword).await?,
    })))
}

pub async fn create_user(
    Json(request): Json<CreateUserRequest>,
) -> Result<Response, ConsoleApiError> {
    validate_group_ids(&request.group_ids).await?;
    validate_device_ids(&request.device_ids).await?;
    let initial_password = request
        .initial_password
        .filter(|value| !value.is_empty())
        .unwrap_or_else(crate::user::password::generate_random);
    let user = gUserManager
        .create_managed_user(request.username, initial_password.clone())
        .await?;
    if let Err(error) = IdentityManager::replace_groups_for_user(&user.uid, request.group_ids).await
    {
        let _ = gUserManager
            .admin_delete_user(user.uid.clone(), user.version)
            .await;
        return Err(error);
    }
    if let Err(error) = gConsoleUserDeviceMgr
        .replace_personal_devices(&user.uid, request.device_ids)
        .await
    {
        let _ = IdentityManager::replace_groups_for_user(&user.uid, Vec::new()).await;
        let _ = gUserManager
            .admin_delete_user(user.uid.clone(), user.version)
            .await;
        return Err(error);
    }
    audit::record(
        "admin",
        "license_owner",
        "user_create",
        "success",
        "user",
        &user.uid,
        "",
    )
    .await;
    let body = Json(ok_resp(CreateUserResponse {
        user: UserAdminView::from_user(user).await?,
        initial_password,
    }));
    let mut response = body.into_response();
    response.headers_mut().insert(
        axum::http::header::CACHE_CONTROL,
        axum::http::HeaderValue::from_static("no-store"),
    );
    Ok(response)
}

async fn validate_group_ids(group_ids: &[String]) -> Result<(), ConsoleApiError> {
    let known: std::collections::HashSet<_> = IdentityManager::list_groups()
        .await?
        .into_iter()
        .map(|group| group.gid)
        .collect();
    if group_ids.iter().any(|gid| !known.contains(gid)) {
        return Err(ConsoleApiError::GroupNotFound);
    }
    Ok(())
}

async fn validate_device_ids(device_ids: &[String]) -> Result<(), ConsoleApiError> {
    let ids: std::collections::BTreeSet<_> = device_ids
        .iter()
        .filter(|id| !id.is_empty())
        .cloned()
        .collect();
    for device_id in ids {
        gDeviceManager.query_device_by_id(device_id).await?;
    }
    Ok(())
}

pub async fn batch_create_users_csv(
    Json(request): Json<BatchCreateUsersRequest>,
) -> Result<Response, ConsoleApiError> {
    if !(1..=500).contains(&request.size) {
        return Err(ConsoleApiError::InvalidParams);
    }
    let prefix = request.username_prefix.trim();
    if prefix.is_empty()
        || prefix.chars().count() > 48
        || prefix.chars().any(char::is_control)
        || prefix.contains('/')
        || prefix.contains('\\')
        || matches!(prefix.chars().next(), Some('=' | '+' | '-' | '@'))
    {
        return Err(ConsoleApiError::InvalidParams);
    }
    validate_group_ids(&request.group_ids).await?;

    // UTF-8 BOM makes non-ASCII usernames open correctly in current Excel.
    // Formula-leading prefixes are rejected above to prevent CSV injection.
    let mut writer = csv::Writer::from_writer(vec![0xef, 0xbb, 0xbf]);
    let mut created_users = Vec::with_capacity(request.size as usize);
    for _ in 0..request.size {
        // The random suffix avoids scanning existing sequential names and also
        // keeps parallel batch requests from selecting the same account name.
        let suffix = &uuid::Uuid::new_v4().simple().to_string()[..10];
        let username = format!("{prefix}-{suffix}");
        let password = crate::user::password::generate_random();
        let user = match gUserManager
            .create_managed_user(username.clone(), password.clone())
            .await
        {
            Ok(user) => user,
            Err(error) => {
                rollback_created_users(&created_users).await;
                return Err(error);
            }
        };
        if let Err(error) =
            IdentityManager::replace_groups_for_user(&user.uid, request.group_ids.clone()).await
        {
            created_users.push((user.uid.clone(), user.version));
            rollback_created_users(&created_users).await;
            return Err(error);
        }
        if writer
            .serialize(BatchUserCsvRow {
                username,
                initial_password: password,
                uid: user.uid.clone(),
            })
            .is_err()
        {
            created_users.push((user.uid.clone(), user.version));
            rollback_created_users(&created_users).await;
            return Err(ConsoleApiError::InternalError);
        }
        created_users.push((user.uid, user.version));
    }
    let bytes = writer
        .into_inner()
        .map_err(|_| ConsoleApiError::InternalError)?;
    audit::record(
        "admin",
        "license_owner",
        "user_batch_create",
        "success",
        "user",
        "",
        &format!("count={}", request.size),
    )
    .await;
    Response::builder()
        .status(StatusCode::OK)
        .header(header::CONTENT_TYPE, "text/csv; charset=utf-8")
        .header(
            header::CONTENT_DISPOSITION,
            HeaderValue::from_static("attachment; filename=px_users.csv"),
        )
        .header(header::CACHE_CONTROL, "no-store")
        .header(header::PRAGMA, "no-cache")
        .body(axum::body::Body::from(bytes))
        .map_err(|_| ConsoleApiError::InternalError)
}

async fn rollback_created_users(users: &[(String, i64)]) {
    for (uid, version) in users {
        let _ = IdentityManager::replace_groups_for_user(uid, Vec::new()).await;
        let _ = gUserManager.admin_delete_user(uid.clone(), *version).await;
    }
}

pub async fn update_user(
    Path(uid): Path<String>,
    Json(request): Json<UpdateUserRequest>,
) -> Result<Json<RespMessage<UserAdminView>>, ConsoleApiError> {
    if let Some(group_ids) = &request.group_ids {
        validate_group_ids(group_ids).await?;
    }
    if let Some(device_ids) = &request.device_ids {
        validate_device_ids(device_ids).await?;
    }
    let user = gUserManager
        .admin_update_user(
            uid.clone(),
            request.version,
            request.username,
            request.disabled,
            request.avatar_url,
        )
        .await?;
    if let Some(group_ids) = request.group_ids {
        IdentityManager::replace_groups_for_user(&uid, group_ids).await?;
    }
    if let Some(device_ids) = request.device_ids {
        gConsoleUserDeviceMgr
            .replace_personal_devices(&uid, device_ids)
            .await?;
    }
    audit::record(
        "admin",
        "license_owner",
        "user_update",
        "success",
        "user",
        &uid,
        "",
    )
    .await;
    Ok(Json(ok_resp(UserAdminView::from_user(user).await?)))
}

pub async fn delete_user(
    Path(uid): Path<String>,
    Json(request): Json<VersionRequest>,
) -> Result<Json<RespMessage<UserAdminView>>, ConsoleApiError> {
    let user = gUserManager.admin_delete_user(uid, request.version).await?;
    IdentityManager::remove_user_from_all_groups(&user.uid).await?;
    gConsoleUserDeviceMgr
        .remove_personal_devices_for_user(&user.uid)
        .await?;
    audit::record(
        "admin",
        "license_owner",
        "user_delete",
        "success",
        "user",
        &user.uid,
        "soft_delete",
    )
    .await;
    Ok(Json(ok_resp(UserAdminView::from_user(user).await?)))
}

pub async fn reset_password(
    Path(uid): Path<String>,
    Json(request): Json<ResetPasswordRequest>,
) -> Result<Response, ConsoleApiError> {
    if !request.generated && request.supplied_password.is_none() {
        return Err(ConsoleApiError::InvalidParams);
    }
    let password = if request.generated {
        crate::user::password::generate_random()
    } else {
        request.supplied_password.unwrap_or_default()
    };
    let user = gUserManager
        .admin_reset_password(uid, request.version, password.clone())
        .await?;
    audit::record(
        "admin",
        "license_owner",
        "password_reset",
        "success",
        "user",
        &user.uid,
        "sessions_revoked",
    )
    .await;
    let mut response = Json(ok_resp(ResetPasswordResponse {
        user: UserAdminView::from_user(user).await?,
        initial_password: password,
    }))
    .into_response();
    response.headers_mut().insert(
        axum::http::header::CACHE_CONTROL,
        axum::http::HeaderValue::from_static("no-store"),
    );
    Ok(response)
}

pub async fn view_password(Path(uid): Path<String>) -> Result<Response, ConsoleApiError> {
    let password = gUserManager.admin_recover_password(uid.clone()).await?;
    audit::record(
        "admin",
        "license_owner",
        "password_view",
        "success",
        "user",
        &uid,
        if password.is_some() {
            "password_recovered"
        } else {
            "legacy_password_unavailable"
        },
    )
    .await;
    let mut response = Json(ok_resp(PasswordViewResponse { password })).into_response();
    response.headers_mut().insert(
        header::CACHE_CONTROL,
        HeaderValue::from_static("no-store, private"),
    );
    response
        .headers_mut()
        .insert(header::PRAGMA, HeaderValue::from_static("no-cache"));
    Ok(response)
}

pub async fn revoke_all_sessions(
    Path(uid): Path<String>,
) -> Result<Json<RespMessage<UserAdminView>>, ConsoleApiError> {
    let user = gUserManager.revoke_all_sessions(uid).await?;
    audit::record(
        "admin",
        "license_owner",
        "session_revoke_all",
        "success",
        "user",
        &user.uid,
        "",
    )
    .await;
    Ok(Json(ok_resp(UserAdminView::from_user(user).await?)))
}

pub async fn list_user_sessions(
    Path(uid): Path<String>,
) -> Result<Json<RespMessage<Vec<SessionAdminView>>>, ConsoleApiError> {
    gUserManager.query_user_by_id(uid.clone()).await?;
    let sessions = gUserSessionManager
        .list_subject_sessions("user", &uid)
        .await?
        .into_iter()
        .map(|session| SessionAdminView {
            sid: session.sid,
            client_type: session.client_type,
            created_at: session.created_at,
            last_used_at: session.last_used_at,
            expires_at: session.expires_at,
            absolute_expires_at: session.absolute_expires_at,
            revoked_at: session.revoked_at,
            ip_hash_prefix: hash_prefix(&session.ip_hash),
            user_agent_hash_prefix: hash_prefix(&session.user_agent_hash),
        })
        .collect();
    Ok(Json(ok_resp(sessions)))
}

pub async fn list_guest_sessions(
) -> Result<Json<RespMessage<Vec<GuestSessionAdminView>>>, ConsoleApiError> {
    let sessions = gUserSessionManager
        .list_guest_sessions()
        .await?
        .into_iter()
        .map(|session| GuestSessionAdminView {
            sid: session.sid,
            guest_id: session.subject_id,
            client_type: session.client_type,
            created_at: session.created_at,
            last_used_at: session.last_used_at,
            expires_at: session.expires_at,
            revoked_at: session.revoked_at,
            ip_hash_prefix: hash_prefix(&session.ip_hash),
            user_agent_hash_prefix: hash_prefix(&session.user_agent_hash),
        })
        .collect();
    Ok(Json(ok_resp(sessions)))
}

pub async fn block_guest_session(
    Path(sid): Path<String>,
    Json(request): Json<BlockGuestRequest>,
) -> Result<Json<RespMessage<bool>>, ConsoleApiError> {
    if request.reason.len() > 256 || request.reason.chars().any(char::is_control) {
        return Err(ConsoleApiError::InvalidParams);
    }
    let session = gUserSessionManager
        .block_guest_session(
            &sid,
            request.block_guest_id,
            request.block_ip_hash,
            request.reason.trim(),
        )
        .await?;
    audit::record(
        "admin",
        "license_owner",
        "guest_block",
        "success",
        "guest",
        &session.subject_id,
        if request.block_ip_hash {
            "ip_and_or_guest"
        } else {
            "guest_id"
        },
    )
    .await;
    Ok(Json(ok_resp(true)))
}

pub async fn list_personal_devices(
    Path(uid): Path<String>,
) -> Result<Json<RespMessage<Vec<String>>>, ConsoleApiError> {
    gUserManager.query_user_by_id(uid.clone()).await?;
    Ok(Json(ok_resp(
        gConsoleUserDeviceMgr.personal_device_ids(&uid).await?,
    )))
}

pub async fn replace_personal_devices(
    Path(uid): Path<String>,
    Json(request): Json<ReplacePersonalDevicesRequest>,
) -> Result<Json<RespMessage<Vec<String>>>, ConsoleApiError> {
    let devices = gConsoleUserDeviceMgr
        .replace_personal_devices(&uid, request.device_ids)
        .await?;
    audit::record(
        "admin",
        "license_owner",
        "personal_device_grant_replace",
        "success",
        "user",
        &uid,
        "",
    )
    .await;
    Ok(Json(ok_resp(devices)))
}

pub async fn logout_all(
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<LogoutAllRequest>,
) -> Result<Json<RespMessage<bool>>, ConsoleApiError> {
    let user = gUserManager.query_user_by_id(subject.uid.clone()).await?;
    if user.deleted
        || user.disabled
        || !crate::user::password::verify(&request.current_password, &user.password_hash)
    {
        return Err(ConsoleApiError::InvalidCredentials);
    }
    gUserManager
        .revoke_all_sessions(subject.uid.clone())
        .await?;
    audit::record(
        "user",
        &subject.uid,
        "session_revoke_all",
        "success",
        "user",
        &subject.uid,
        "self_service",
    )
    .await;
    Ok(Json(ok_resp(true)))
}
