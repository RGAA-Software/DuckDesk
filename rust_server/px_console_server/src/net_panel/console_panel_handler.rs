use crate::console_api_error::ConsoleApiError;
use crate::console_context::ConsoleContext;
use crate::console_defs::KEY_DEVICE_ID;
use crate::console_http_util::get_str_param;
use crate::gConsolePanelConnMgr;
use crate::net_panel::console_panel_conn::ConsolePanelConnVo;
use axum::extract::{Query, State};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_query_panel_conn_by_id(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<ConsolePanelConnVo>>, ConsoleApiError> {
    let device_id = get_str_param(&query, KEY_DEVICE_ID)?;
    let conn = gConsolePanelConnMgr.get_conn_info(device_id).await?;
    Ok(Json(ok_resp(conn)))
}

pub async fn handle_query_all_panel_conn(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    _query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<ConsolePanelConnVo>>>, ConsoleApiError> {
    let all_conn = gConsolePanelConnMgr.get_all_conn_info().await?;
    Ok(Json(ok_resp(all_conn)))
}

pub async fn handle_query_online_panel_count(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
) -> Result<Json<RespMessage<usize>>, ConsoleApiError> {
    let count = gConsolePanelConnMgr.get_all_conn_count().await;
    Ok(Json(ok_resp(count)))
}
