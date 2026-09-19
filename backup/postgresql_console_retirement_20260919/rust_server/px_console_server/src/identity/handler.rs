use crate::console_api_error::ConsoleApiError;
use crate::event::audit;
use crate::identity::manager::IdentityManager;
use crate::identity::model::GroupView;
use axum::extract::Path;
use axum::Json;
use px_base::{ok_resp, RespMessage};
use serde::Deserialize;

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CreateGroupRequest {
    pub name: String,
    #[serde(default)]
    pub remark: String,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct UpdateGroupRequest {
    pub version: i64,
    pub name: Option<String>,
    pub remark: Option<String>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct VersionRequest {
    pub version: i64,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ReplaceMembersRequest {
    pub version: i64,
    pub user_ids: Vec<String>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ReplaceDevicesRequest {
    pub version: i64,
    pub device_ids: Vec<String>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ReplaceAppsRequest {
    pub version: i64,
    pub app_ids: Vec<String>,
}

pub async fn create_group(
    Json(request): Json<CreateGroupRequest>,
) -> Result<Json<RespMessage<GroupView>>, ConsoleApiError> {
    let group = IdentityManager::create_group(request.name, request.remark).await?;
    audit::record(
        "admin",
        "license_owner",
        "group_create",
        "success",
        "group",
        &group.gid,
        "",
    )
    .await;
    Ok(Json(ok_resp(group)))
}

pub async fn list_groups() -> Result<Json<RespMessage<Vec<GroupView>>>, ConsoleApiError> {
    Ok(Json(ok_resp(IdentityManager::list_groups().await?)))
}

pub async fn update_group(
    Path(gid): Path<String>,
    Json(request): Json<UpdateGroupRequest>,
) -> Result<Json<RespMessage<GroupView>>, ConsoleApiError> {
    let group =
        IdentityManager::update_group(&gid, request.version, request.name, request.remark).await?;
    audit::record(
        "admin",
        "license_owner",
        "group_update",
        "success",
        "group",
        &gid,
        "",
    )
    .await;
    Ok(Json(ok_resp(group)))
}

pub async fn delete_group(
    Path(gid): Path<String>,
    Json(request): Json<VersionRequest>,
) -> Result<Json<RespMessage<bool>>, ConsoleApiError> {
    let deleted = IdentityManager::delete_group(&gid, request.version).await?;
    audit::record(
        "admin",
        "license_owner",
        "group_delete",
        "success",
        "group",
        &gid,
        "soft_delete",
    )
    .await;
    Ok(Json(ok_resp(deleted)))
}

pub async fn replace_members(
    Path(gid): Path<String>,
    Json(request): Json<ReplaceMembersRequest>,
) -> Result<Json<RespMessage<GroupView>>, ConsoleApiError> {
    let group = IdentityManager::replace_members(&gid, request.version, request.user_ids).await?;
    audit::record(
        "admin",
        "license_owner",
        "group_members_replace",
        "success",
        "group",
        &gid,
        "",
    )
    .await;
    Ok(Json(ok_resp(group)))
}

pub async fn replace_devices(
    Path(gid): Path<String>,
    Json(request): Json<ReplaceDevicesRequest>,
) -> Result<Json<RespMessage<GroupView>>, ConsoleApiError> {
    let group = IdentityManager::replace_devices(&gid, request.version, request.device_ids).await?;
    audit::record(
        "admin",
        "license_owner",
        "group_device_grants_replace",
        "success",
        "group",
        &gid,
        "",
    )
    .await;
    Ok(Json(ok_resp(group)))
}

pub async fn replace_apps(
    Path(gid): Path<String>,
    Json(request): Json<ReplaceAppsRequest>,
) -> Result<Json<RespMessage<GroupView>>, ConsoleApiError> {
    let group = IdentityManager::replace_apps(&gid, request.version, request.app_ids).await?;
    audit::record(
        "admin",
        "license_owner",
        "group_app_grants_replace",
        "success",
        "group",
        &gid,
        "",
    )
    .await;
    Ok(Json(ok_resp(group)))
}

pub async fn list_member_ids(
    Path(gid): Path<String>,
) -> Result<Json<RespMessage<Vec<String>>>, ConsoleApiError> {
    Ok(Json(ok_resp(
        IdentityManager::group_member_ids(&gid).await?,
    )))
}

pub async fn list_device_ids(
    Path(gid): Path<String>,
) -> Result<Json<RespMessage<Vec<String>>>, ConsoleApiError> {
    Ok(Json(ok_resp(
        IdentityManager::group_device_ids(&gid).await?,
    )))
}

pub async fn list_app_ids(
    Path(gid): Path<String>,
) -> Result<Json<RespMessage<Vec<String>>>, ConsoleApiError> {
    Ok(Json(ok_resp(IdentityManager::group_app_ids(&gid).await?)))
}
