use crate::gCmsPanelConnMgr;
use crate::net_panel::cms_panel_conn::CmsPanelConnVo;
use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::cms_defs::KEY_DEVICE_ID;
use crate::cms_http_util::get_str_param;
use axum::extract::{Query, State};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_query_panel_conn_by_id(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<CmsPanelConnVo>>, CmsApiError> {
    let device_id = get_str_param(&query, KEY_DEVICE_ID)?;
    let conn = gCmsPanelConnMgr.get_conn_info(device_id).await?;
    Ok(Json(ok_resp(conn)))
}

pub async fn handle_query_all_panel_conn(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    _query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<CmsPanelConnVo>>>, CmsApiError> {
    let all_conn = gCmsPanelConnMgr.get_all_conn_info().await?;
    Ok(Json(ok_resp(all_conn)))
}

pub async fn handle_query_online_panel_count(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<usize>>, CmsApiError> {
    let count = gCmsPanelConnMgr.get_all_conn_count().await;
    Ok(Json(ok_resp(count)))
}
