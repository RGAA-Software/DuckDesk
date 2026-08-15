use crate::gSpvrServiceConnMgr;
use crate::net_service::spvr_service_conn::SpvrServiceConnVo;
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use axum::extract::State;
use axum::Json;
use px_base::{ok_resp, RespMessage};
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_query_all_service_conn(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
) -> Result<Json<RespMessage<Vec<SpvrServiceConnVo>>>, SpvrApiError> {
    let all_conn = gSpvrServiceConnMgr.get_all_conn_info().await?;
    Ok(Json(ok_resp(all_conn)))
}

pub async fn handle_query_online_service_count(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
) -> Result<Json<RespMessage<usize>>, SpvrApiError> {
    let count = gSpvrServiceConnMgr.get_all_conn_count().await;
    Ok(Json(ok_resp(count)))
}
