use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::gCmsUserDeviceMgr;
use crate::user::session::AuthenticatedUser;
use crate::user_device::cms_user_device::CmsUserDeviceSummary;
use axum::extract::{Extension, State};
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
