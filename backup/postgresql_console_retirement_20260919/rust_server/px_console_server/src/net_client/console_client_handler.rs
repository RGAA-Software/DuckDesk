use crate::console_api_error::ConsoleApiError;
use crate::console_context::ConsoleContext;
use crate::console_defs::KEY_DEVICE_ID;
use crate::console_http_util::{get_int_param, get_str_param};
use crate::gConsoleClientConnMgr;
use crate::net_client::console_client_conn::ConsoleClientConnVo;
use crate::user::console_user_keys::{KEY_PAGE, KEY_PAGE_SIZE};
use axum::extract::{Query, State};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_query_client_conns(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<ConsoleClientConnVo>>>, ConsoleApiError> {
    let device_id = get_str_param(&query, KEY_DEVICE_ID)?;
    let page = get_int_param(&query, KEY_PAGE)?;
    let page_size = get_int_param(&query, KEY_PAGE_SIZE)?;

    let clients_conn = gConsoleClientConnMgr
        .query_client_conns(device_id, page, page_size)
        .await?;
    Ok(Json(ok_resp(clients_conn)))
}

pub async fn handle_query_conns(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<ConsoleClientConnVo>>>, ConsoleApiError> {
    let page = get_int_param(&query, KEY_PAGE)?;
    let page_size = get_int_param(&query, KEY_PAGE_SIZE)?;

    let clients_conn = gConsoleClientConnMgr.query_conns(page, page_size).await?;
    Ok(Json(ok_resp(clients_conn)))
}

pub async fn handle_query_alive_conns(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    _query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<ConsoleClientConnVo>>>, ConsoleApiError> {
    let clients_conn = gConsoleClientConnMgr.get_alive_connections().await;
    Ok(Json(ok_resp(clients_conn)))
}

pub async fn handle_count_alive_conns(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    _query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<u32>>, ConsoleApiError> {
    let clients_conn = gConsoleClientConnMgr.count_alive_connections().await;
    Ok(Json(ok_resp(clients_conn)))
}
