use crate::app_schedule::gAppScheduleManager;
use crate::app_schedule::manager::AppAccessMode;
use crate::cms_api_error::CmsApiError;
use crate::event::audit;
use crate::gDeviceManager;
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
    pub version: i64,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct UpdateAppAccessRequest {
    pub version: i64,
    pub access_mode: AppAccessMode,
}

pub async fn list_device_catalog() -> Result<Json<RespMessage<Vec<DeviceCatalogItem>>>, CmsApiError>
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

pub async fn list_app_catalog() -> Result<Json<RespMessage<Vec<AppCatalogItem>>>, CmsApiError> {
    let mut items: Vec<_> = gAppScheduleManager
        .list_applications()
        .await
        .into_iter()
        .map(|app| AppCatalogItem {
            app_id: app.app_id,
            name: app.name,
            access_mode: app.access_mode,
            version: app.version,
        })
        .collect();
    items.sort_by(|left, right| left.name.cmp(&right.name));
    Ok(Json(ok_resp(items)))
}

pub async fn update_app_access(
    Path(app_id): Path<String>,
    Json(request): Json<UpdateAppAccessRequest>,
) -> Result<Json<RespMessage<AppCatalogItem>>, CmsApiError> {
    let app = gAppScheduleManager
        .update_access_mode(&app_id, request.version, request.access_mode)
        .await
        .map_err(|error| match error.as_str() {
            "RESOURCE_NOT_FOUND" => CmsApiError::ResourceNotFound,
            "VERSION_CONFLICT" => CmsApiError::VersionConflict,
            _ => CmsApiError::DatabaseError,
        })?;
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
        version: app.version,
    })))
}
