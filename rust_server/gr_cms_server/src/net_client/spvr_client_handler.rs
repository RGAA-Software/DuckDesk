use std::collections::HashMap;
use std::sync::Arc;
use axum::extract::{Query, State};
use axum::Json;
use tokio::sync::Mutex;
use gr_base::{ok_resp, RespMessage};
use crate::gSpvrClientConnMgr;
use crate::net_client::spvr_client_conn::SpvrClientConnVo;
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use crate::spvr_defs::KEY_DEVICE_ID;
use crate::spvr_http_util::{get_int_param, get_str_param};
use crate::stream::spvr_stream::SpvrStream;
use crate::user::spvr_user_keys::{KEY_PAGE, KEY_PAGE_SIZE};

pub async fn handle_query_client_conns(State(_ctx): State<Arc<Mutex<SpvrContext>>>,
                                       query: Query<HashMap<String, String>>)
                                       -> Result<Json<RespMessage<Vec<SpvrClientConnVo>>>, SpvrApiError> {
    let device_id = get_str_param(&query, KEY_DEVICE_ID)?;
    let page = get_int_param(&query, KEY_PAGE)?;
    let page_size = get_int_param(&query, KEY_PAGE_SIZE)?;
    
    let clients_conn = gSpvrClientConnMgr
        .query_client_conns(device_id, page, page_size).await?;
    Ok(Json(ok_resp(clients_conn)))
}

pub async fn handle_query_conns(State(_ctx): State<Arc<Mutex<SpvrContext>>>,
                                       query: Query<HashMap<String, String>>)
                                       -> Result<Json<RespMessage<Vec<SpvrClientConnVo>>>, SpvrApiError> {
    let page = get_int_param(&query, KEY_PAGE)?;
    let page_size = get_int_param(&query, KEY_PAGE_SIZE)?;

    let clients_conn = gSpvrClientConnMgr
        .query_conns(page, page_size).await?;
    Ok(Json(ok_resp(clients_conn)))
}

pub async fn handle_query_alive_conns(State(_ctx): State<Arc<Mutex<SpvrContext>>>,
                                       query: Query<HashMap<String, String>>)
                                       -> Result<Json<RespMessage<Vec<SpvrClientConnVo>>>, SpvrApiError> {
    let clients_conn = gSpvrClientConnMgr
        .get_alive_connections().await;
    Ok(Json(ok_resp(clients_conn)))
}

pub async fn handle_count_alive_conns(State(_ctx): State<Arc<Mutex<SpvrContext>>>,
                                      query: Query<HashMap<String, String>>)
                                      -> Result<Json<RespMessage<u32>>, SpvrApiError> {
    let clients_conn = gSpvrClientConnMgr
        .count_alive_connections().await;
    Ok(Json(ok_resp(clients_conn)))
}