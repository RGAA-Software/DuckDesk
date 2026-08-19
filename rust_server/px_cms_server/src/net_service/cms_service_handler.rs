use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::gCmsServiceConnMgr;
use crate::net_service::cms_service_conn::CmsServiceConnVo;
use axum::extract::State;
use axum::Json;
use px_base::{ok_resp, RespMessage};
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_query_all_service_conn(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<Vec<CmsServiceConnVo>>>, CmsApiError> {
    let all_conn = gCmsServiceConnMgr.get_all_conn_info().await?;
    Ok(Json(ok_resp(all_conn)))
}

pub async fn handle_query_online_service_count(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<usize>>, CmsApiError> {
    let count = gCmsServiceConnMgr.get_all_conn_count().await;
    Ok(Json(ok_resp(count)))
}
