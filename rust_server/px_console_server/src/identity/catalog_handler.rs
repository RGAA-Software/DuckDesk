use crate::app_schedule::gAppScheduleManager;
use crate::app_schedule::manager::AppAccessMode;
use crate::console_api_error::ConsoleApiError;
use crate::event::audit;
use crate::gDeviceManager;
use crate::identity::manager::IdentityManager;
use axum::extract::Path;
use axum::Json;
use px_base::{ok_resp, RespMessage};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Default, Serialize)]
pub struct DeviceCatalogItem {
    pub device_id: String,
    pub name: String,
    pub online: bool,
}

#[derive(Debug, Clone, Default, Serialize)]
pub struct AppCatalogItem {
    pub app_id: String,
    pub name: String,
    pub access_mode: AppAccessMode,
    pub group_ids: Vec<String>,
    pub version: i64,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct UpdateAppAccessRequest {
    pub version: i64,
    pub access_mode: AppAccessMode,
    pub group_ids: Option<Vec<String>>,
}

fn validate_access_assignment(
    access_mode: &AppAccessMode,
    group_ids: &[String],
) -> Result<(), ConsoleApiError> {
    if access_mode == &AppAccessMode::Acl && group_ids.is_empty() {
        return Err(ConsoleApiError::InvalidParams);
    }
    Ok(())
}

pub async fn list_device_catalog() -> Result<Json<RespMessage<Vec<DeviceCatalogItem>>>, ConsoleApiError>
{
    let mut items: Vec<_> = gDeviceManager
        .query_devices(String::new(), String::new(), String::new(), 1, 10_000)
        .await?
        .into_iter()
        .map(|device| DeviceCatalogItem {
            device_id: device.device_id,
            name: device.device_name,
            online: device.active,
        })
        .collect();
    items.sort_by(|left, right| left.name.cmp(&right.name));
    Ok(Json(ok_resp(items)))
}

pub async fn list_app_catalog() -> Result<Json<RespMessage<Vec<AppCatalogItem>>>, ConsoleApiError> {
    let mut items = Vec::new();
    for app in gAppScheduleManager.list_applications().await {
        let group_ids = IdentityManager::app_group_ids(&app.app_id).await?;
        items.push(AppCatalogItem {
            app_id: app.app_id,
            name: app.name,
            access_mode: app.access_mode,
            group_ids,
            version: app.version,
        });
    }
    items.sort_by(|left, right| left.name.cmp(&right.name));
    Ok(Json(ok_resp(items)))
}

pub async fn update_app_access(
    Path(app_id): Path<String>,
    Json(request): Json<UpdateAppAccessRequest>,
) -> Result<Json<RespMessage<AppCatalogItem>>, ConsoleApiError> {
    let effective_group_ids = match &request.group_ids {
        Some(group_ids) => {
            IdentityManager::validate_group_ids(group_ids).await?;
            group_ids.clone()
        }
        None => IdentityManager::app_group_ids(&app_id).await?,
    };
    validate_access_assignment(&request.access_mode, &effective_group_ids)?;
    let app = gAppScheduleManager
        .update_access_mode(&app_id, request.version, request.access_mode)
        .await
        .map_err(|error| match error.as_str() {
            "RESOURCE_NOT_FOUND" => ConsoleApiError::ResourceNotFound,
            "VERSION_CONFLICT" => ConsoleApiError::VersionConflict,
            _ => ConsoleApiError::DatabaseError,
        })?;
    let group_ids = match request.group_ids {
        Some(_) => IdentityManager::replace_groups_for_app(&app_id, effective_group_ids).await?,
        None => effective_group_ids,
    };
    audit::record(
        "admin",
        "license_owner",
        "app_access_mode_update",
        "success",
        "application",
        &app_id,
        "",
    )
    .await;
    Ok(Json(ok_resp(AppCatalogItem {
        app_id: app.app_id,
        name: app.name,
        access_mode: app.access_mode,
        group_ids,
        version: app.version,
    })))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn acl_access_requires_at_least_one_group() {
        assert_eq!(
            validate_access_assignment(&AppAccessMode::Acl, &[]),
            Err(ConsoleApiError::InvalidParams)
        );
        assert!(validate_access_assignment(&AppAccessMode::Acl, &["group-1".to_string()]).is_ok());
        assert!(validate_access_assignment(&AppAccessMode::Public, &[]).is_ok());
    }
}
