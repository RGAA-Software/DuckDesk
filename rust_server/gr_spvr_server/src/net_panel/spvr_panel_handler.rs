use std::collections::HashMap;
use std::sync::Arc;
use axum::extract::{Query, State};
use axum::Json;
use tokio::sync::Mutex;
use gr_base::{ok_resp, RespMessage};
use crate::gSpvrPanelConnMgr;
use crate::net_panel::spvr_panel_conn::{SpvrPanelConn, SpvrPanelConnVo};
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use crate::spvr_defs::KEY_DEVICE_ID;
use crate::spvr_http_util::get_str_param;

pub async fn handle_query_panel_conn_by_id(State(_ctx): State<Arc<Mutex<SpvrContext>>>,
                                           query: Query<HashMap<String, String>>)
                                           -> Result<Json<RespMessage<SpvrPanelConnVo>>, SpvrApiError> {
    let device_id = get_str_param(&query, KEY_DEVICE_ID)?;
    let conn = gSpvrPanelConnMgr
        .get_conn_info(device_id).await?;
    Ok(Json(ok_resp(conn)))
}

pub async fn handle_query_all_panel_conn(State(_ctx): State<Arc<Mutex<SpvrContext>>>,
                                      query: Query<HashMap<String, String>>)
                                      -> Result<Json<RespMessage<Vec<SpvrPanelConnVo>>>, SpvrApiError> {
    let all_conn = gSpvrPanelConnMgr
        .get_all_conn_info().await?;
    Ok(Json(ok_resp(all_conn)))
}

pub async fn handle_query_online_panel_count(State(_ctx): State<Arc<Mutex<SpvrContext>>>)
                                             -> Result<Json<RespMessage<usize>>, SpvrApiError> {
    let count = gSpvrPanelConnMgr
        .get_all_conn_count().await;
    Ok(Json(ok_resp(count)))
}

