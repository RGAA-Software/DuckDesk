use crate::console_api_error::ConsoleApiError;
use crate::console_context::ConsoleContext;
use crate::gConsoleServiceConnMgr;
use crate::net_service::console_service_conn::ConsoleServiceConnVo;
use axum::extract::State;
use axum::Json;
use px_base::{ok_resp, RespMessage};
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_query_all_service_conn(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
) -> Result<Json<RespMessage<Vec<ConsoleServiceConnVo>>>, ConsoleApiError> {
    let all_conn = gConsoleServiceConnMgr.get_all_conn_info().await?;
    Ok(Json(ok_resp(all_conn)))
}

pub async fn handle_query_online_service_count(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
) -> Result<Json<RespMessage<usize>>, ConsoleApiError> {
    let count = gConsoleServiceConnMgr.get_all_conn_count().await;
    Ok(Json(ok_resp(count)))
}
