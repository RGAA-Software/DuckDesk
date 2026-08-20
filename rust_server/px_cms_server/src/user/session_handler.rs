use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::user::cms_user::CmsUserView;
use crate::user::session::AuthenticatedUser;
use crate::{gUserManager, gUserSessionManager};
use axum::extract::{Extension, State};
use axum::http::{header, HeaderMap};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::sync::Mutex;

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
    pub access_token: String,
    pub expires_at: i64,
    pub absolute_expires_at: i64,
}

pub async fn login(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    Json(request): Json<UserLoginRequest>,
) -> Result<Json<RespMessage<UserLoginResponse>>, CmsApiError> {
    if request.client_type != "panel" {
        return Err(CmsApiError::InvalidParams);
    }
    let user = gUserManager
        .query_user_by_username(request.username)
        .await
        .map_err(|_| CmsApiError::InvalidCredentials)?;
    if user.deleted || !crate::user::password::verify(&request.password, &user.password_hash) {
        return Err(CmsApiError::InvalidCredentials);
    }
    let issued = gUserSessionManager
        .issue_panel(user.uid.clone(), user.auth_version)
        .await?;
    Ok(Json(ok_resp(UserLoginResponse {
        profile: CmsUserView::from(user),
        access_token: issued.access_token,
        expires_at: issued.session.expires_at,
        absolute_expires_at: issued.session.absolute_expires_at,
    })))
}

pub async fn logout(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    headers: HeaderMap,
) -> Result<Json<RespMessage<bool>>, CmsApiError> {
    let token = headers
        .get(header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
        .map(str::trim)
        .unwrap_or("");
    gUserSessionManager.revoke_token(token).await?;
    Ok(Json(ok_resp(true)))
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ChangePasswordRequest {
    pub current_password: String,
    pub new_password: String,
}

pub async fn change_password(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<ChangePasswordRequest>,
) -> Result<Json<RespMessage<CmsUserView>>, CmsApiError> {
    let user = gUserManager.query_user_by_id(subject.uid.clone()).await?;
    if user.deleted
        || !crate::user::password::verify(&request.current_password, &user.password_hash)
    {
        return Err(CmsApiError::InvalidCredentials);
    }
    let updated = gUserManager
        .update_user_password(subject.uid, request.new_password)
        .await?;
    Ok(Json(ok_resp(CmsUserView::from(updated))))
}

pub async fn me(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    Extension(subject): Extension<AuthenticatedUser>,
) -> Result<Json<RespMessage<CmsUserView>>, CmsApiError> {
    let user = gUserManager.query_user_by_id(subject.uid).await?;
    Ok(Json(ok_resp(CmsUserView::from(user))))
}
