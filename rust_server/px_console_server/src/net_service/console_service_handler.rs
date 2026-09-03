use crate::console_api_error::ConsoleApiError;
use crate::console_context::ConsoleContext;
use crate::gConsoleServiceConnMgr;
use crate::gConsoleDatabase;
use crate::net_service::console_service_conn::ConsoleServiceConnVo;
use crate::record::console_remote_session::{ConsoleRemoteSession, ConsoleRemoteSessionEvent};
use axum::extract::{Query, State};
use axum::Json;
use futures_util::StreamExt;
use mongodb::bson::doc;
use px_base::{ok_resp, RespMessage};
use std::collections::HashMap;
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

pub async fn handle_query_remote_sessions(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    Query(query): Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<ConsoleRemoteSession>>>, ConsoleApiError> {
    let device_id = query.get("device_id").filter(|value| !value.is_empty())
        .ok_or(ConsoleApiError::InvalidParams)?;
    let collection = gConsoleDatabase.lock().await.remote_session();
    let mut cursor = collection.lock().await.find(doc! { "device_id": device_id })
        .sort(doc! { "active": -1, "updated_timestamp": -1 }).await
        .map_err(|_| ConsoleApiError::DatabaseError)?;
    let mut result = Vec::new();
    while let Some(item) = cursor.next().await { result.push(item.map_err(|_| ConsoleApiError::DatabaseError)?); }
    Ok(Json(ok_resp(result)))
}

pub async fn handle_query_remote_session_events(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    Query(query): Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<ConsoleRemoteSessionEvent>>>, ConsoleApiError> {
    let device_id = query.get("device_id").filter(|value| !value.is_empty())
        .ok_or(ConsoleApiError::InvalidParams)?;
    let collection = gConsoleDatabase.lock().await.remote_session_event();
    let mut cursor = collection.lock().await.find(doc! { "device_id": device_id })
        .sort(doc! { "timestamp": -1 }).limit(500).await
        .map_err(|_| ConsoleApiError::DatabaseError)?;
    let mut result = Vec::new();
    while let Some(item) = cursor.next().await { result.push(item.map_err(|_| ConsoleApiError::DatabaseError)?); }
    Ok(Json(ok_resp(result)))
}
