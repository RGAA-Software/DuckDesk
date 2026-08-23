use crate::console_api_error::ConsoleApiError;
use crate::console_context::ConsoleContext;
use crate::gConsoleUserDeviceMgr;
use crate::identity::model::{page_items, ResourcePage, ResourcePageQuery};
use crate::user::session::AuthenticatedUser;
use crate::user_device::console_user_device::ConsoleUserDeviceSummary;
use axum::extract::{Extension, Query, State};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_query_my_devices(
    State(_context): State<Arc<Mutex<ConsoleContext>>>,
    Extension(subject): Extension<AuthenticatedUser>,
) -> Result<Json<RespMessage<Vec<ConsoleUserDeviceSummary>>>, ConsoleApiError> {
    let devices = gConsoleUserDeviceMgr
        .query_user_device_summaries(subject.uid)
        .await?;
    Ok(Json(ok_resp(devices)))
}

pub async fn handle_query_my_devices_page(
    State(_context): State<Arc<Mutex<ConsoleContext>>>,
    Extension(subject): Extension<AuthenticatedUser>,
    Query(query): Query<ResourcePageQuery>,
) -> Result<Json<RespMessage<ResourcePage<ConsoleUserDeviceSummary>>>, ConsoleApiError> {
    let keyword = query.keyword.trim().to_lowercase();
    let mut devices: Vec<_> = gConsoleUserDeviceMgr
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
