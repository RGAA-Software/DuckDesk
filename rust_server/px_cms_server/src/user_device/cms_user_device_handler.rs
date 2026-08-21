use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::gCmsUserDeviceMgr;
use crate::identity::model::{page_items, ResourcePage, ResourcePageQuery};
use crate::user::session::AuthenticatedUser;
use crate::user_device::cms_user_device::CmsUserDeviceSummary;
use axum::extract::{Extension, Query, State};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_query_my_devices(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    Extension(subject): Extension<AuthenticatedUser>,
) -> Result<Json<RespMessage<Vec<CmsUserDeviceSummary>>>, CmsApiError> {
    let devices = gCmsUserDeviceMgr
        .query_user_device_summaries(subject.uid)
        .await?;
    Ok(Json(ok_resp(devices)))
}

pub async fn handle_query_my_devices_page(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    Extension(subject): Extension<AuthenticatedUser>,
    Query(query): Query<ResourcePageQuery>,
) -> Result<Json<RespMessage<ResourcePage<CmsUserDeviceSummary>>>, CmsApiError> {
    let keyword = query.keyword.trim().to_lowercase();
    let mut devices: Vec<_> = gCmsUserDeviceMgr
        .query_user_device_summaries(subject.uid)
        .await?
        .into_iter()
        .filter(|device| {
            keyword.is_empty()
                || device.name.to_lowercase().contains(&keyword)
                || device.device_id.to_lowercase().contains(&keyword)
        })
        .collect();
    devices.sort_by(|left, right| {
        right
            .online
            .cmp(&left.online)
            .then_with(|| left.name.cmp(&right.name))
    });
    Ok(Json(ok_resp(page_items(devices, &query))))
}
