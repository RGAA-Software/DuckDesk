use crate::gRelayRoomMgr;
use crate::relay::relay_api_error::RelayApiError;
use crate::relay::relay_message::KEY_ROOM_ID;
use crate::relay::relay_room::RelayRoomAdapter;
use crate::cms_context::CmsContext;
use axum::extract::{ConnectInfo, Query, State};
use axum::Json;
use px_base::{get_query_param, ok_resp, RespMessage, RespStringMap};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::Mutex;

// handler room; query single room
pub async fn hr_query_room(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    query: Query<HashMap<String, String>>,
    ConnectInfo(_addr): ConnectInfo<SocketAddr>,
) -> Result<Json<RespStringMap>, RelayApiError> {
    let room_id = get_query_param(&query.0, KEY_ROOM_ID);
    if room_id.is_none() {
        return Err(RelayApiError::InvalidParams);
    }

    if let Some(room) = gRelayRoomMgr.find_room(room_id.unwrap(), false).await {
        Ok(Json(px_base::ok_resp_str_map(room.as_str_map())))
    } else {
        Err(RelayApiError::RoomNotFound)
    }
}

// handler room; query rooms
pub async fn hr_query_total_rooms(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    ConnectInfo(_addr): ConnectInfo<SocketAddr>,
) -> Result<Json<RespMessage<Vec<RelayRoomAdapter>>>, RelayApiError> {
    let r = gRelayRoomMgr.find_total_rooms().await;
    Ok(Json(ok_resp(r)))
}

// handler room; query rooms
pub async fn hr_query_total_alive_rooms(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    ConnectInfo(_addr): ConnectInfo<SocketAddr>,
) -> Result<Json<RespMessage<Vec<RelayRoomAdapter>>>, RelayApiError> {
    let r = gRelayRoomMgr.find_total_alive_rooms().await;
    Ok(Json(ok_resp(r)))
}
