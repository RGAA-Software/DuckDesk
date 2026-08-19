use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::cms_defs::KEY_DEVICE_ID;
use crate::cms_http_util::{get_int_param, get_str_param};
use crate::gCmsClientConnMgr;
use crate::net_client::cms_client_conn::CmsClientConnVo;
use crate::user::cms_user_keys::{KEY_PAGE, KEY_PAGE_SIZE};
use axum::extract::{Query, State};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_query_client_conns(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<CmsClientConnVo>>>, CmsApiError> {
    let device_id = get_str_param(&query, KEY_DEVICE_ID)?;
    let page = get_int_param(&query, KEY_PAGE)?;
    let page_size = get_int_param(&query, KEY_PAGE_SIZE)?;

    let clients_conn = gCmsClientConnMgr
        .query_client_conns(device_id, page, page_size)
        .await?;
    Ok(Json(ok_resp(clients_conn)))
}

pub async fn handle_query_conns(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<CmsClientConnVo>>>, CmsApiError> {
    let page = get_int_param(&query, KEY_PAGE)?;
    let page_size = get_int_param(&query, KEY_PAGE_SIZE)?;

    let clients_conn = gCmsClientConnMgr.query_conns(page, page_size).await?;
    Ok(Json(ok_resp(clients_conn)))
}

pub async fn handle_query_alive_conns(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    _query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<CmsClientConnVo>>>, CmsApiError> {
    let clients_conn = gCmsClientConnMgr.get_alive_connections().await;
    Ok(Json(ok_resp(clients_conn)))
}

pub async fn handle_count_alive_conns(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    _query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<u32>>, CmsApiError> {
    let clients_conn = gCmsClientConnMgr.count_alive_connections().await;
    Ok(Json(ok_resp(clients_conn)))
}
