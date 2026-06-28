use crate::relay::relay_api_error::RelayApiError;
use crate::relay::relay_message::{
    KEY_DEVICE_ID, KEY_DEVICE_LOCAL_IPS, KEY_DEVICE_NAME, KEY_DEVICE_W3C_IP, KEY_RELAY_SERVER_IP,
    KEY_RELAY_SERVER_PORT,
};
use crate::spvr_context::SpvrContext;
use crate::{gRelayConnMgr, gSpvrSettings};
use axum::body::Bytes;
use axum::extract::{ConnectInfo, Query, State};
use axum::Json;
use gr_base::{
    ok_resp_vec_str_map, RespStringMap,
    RespVecStringMap, StringMap,
};
use prost::Message;
use protocol::relay::{RelayMessage, RelayMessageType, RelayNotificationMessage};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::default::Default;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::Mutex;

// handler device; query devices
// /query/devices
pub async fn hd_query_devices(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    _query: Query<HashMap<String, String>>,
    ConnectInfo(_addr): ConnectInfo<SocketAddr>,
) -> Result<Json<RespVecStringMap>, RelayApiError> {
    let connections = gRelayConnMgr.get_connections().await;
    let mut r = Vec::new();
    for conn in connections {
        r.push(conn.lock().await.as_str_map());
    }
    Ok(Json(ok_resp_vec_str_map(r)))
}

// handler device; query device
// /query/device
pub async fn hd_query_device(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
    ConnectInfo(_addr): ConnectInfo<SocketAddr>,
) -> Result<Json<RespStringMap>, RelayApiError> {
    let device_id = query.get("device_id").unwrap().clone();
    let conn = gRelayConnMgr.get_conn(device_id).await;
    if conn.is_none() {
        return Err(RelayApiError::DeviceNotFound);
    }

    let conn = conn.unwrap();
    let device_id = conn.lock().await.device_id.clone();
    let client_w3c_ip = conn.lock().await.client_w3c_host.clone();
    let mut client_local_ips = "".to_string();
    for info in conn.lock().await.client_net_info.clone() {
        client_local_ips.push_str(info.ip.as_str());
        client_local_ips.push(';');
    }
    let device_name = conn.lock().await.device_name.clone();
    let _stream_id = conn.lock().await.stream_id.clone();
    let server_w3c_ip = gSpvrSettings.lock().await.server_w3c_ip.clone();
    let relay_port = gSpvrSettings.lock().await.relay_port as i32;

    let mut value = StringMap::new();
    value.insert(KEY_DEVICE_ID.to_string(), device_id.clone());
    value.insert(KEY_DEVICE_W3C_IP.to_string(), client_w3c_ip.clone());
    value.insert(KEY_DEVICE_LOCAL_IPS.to_string(), client_local_ips);
    value.insert(KEY_RELAY_SERVER_IP.to_string(), server_w3c_ip.clone());
    value.insert(KEY_RELAY_SERVER_PORT.to_string(), relay_port.to_string());
    value.insert(KEY_DEVICE_NAME.to_string(), device_name);
    value.insert(KEY_DEVICE_ID.to_string(), device_id);
    Ok(Json(gr_base::ok_resp(value)))
}

#[derive(Serialize, Deserialize, Debug)]
pub struct NotificationEvent {
    from_device: String,
    event: String,
}

// handler device; notify event
// /notify/event
pub async fn hd_notify_event(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
    raw_body: String,
) -> Result<Json<RespStringMap>, RelayApiError> {
    let from_device_id = query
        .get("from_device_id")
        .unwrap_or(&"".to_string())
        .clone();
    let to_device_id = query.get("to_device_id").unwrap_or(&"".to_string()).clone();
    if from_device_id.is_empty() || to_device_id.is_empty() {
        tracing::error!(
            "notify event failed, from: {}, to: {}",
            from_device_id,
            to_device_id
        );
        return Err(RelayApiError::InvalidParams);
    }

    let conn = gRelayConnMgr.get_conn(to_device_id.clone()).await;
    if conn.is_none() {
        tracing::error!("notify event failed, device not found: {}", to_device_id);
        return Err(RelayApiError::DeviceNotFound);
    }
    let conn = conn.unwrap();

    let event = serde_json::from_str::<NotificationEvent>(&raw_body);
    if let Err(e) = event {
        tracing::error!(
            "==> notify event parse body failed: {}, raw_body: {}",
            e,
            raw_body
        );
        return Err(RelayApiError::InvalidParams);
    }
    let event = event.unwrap();

    let mut rl_msg = RelayMessage::default();
    rl_msg.set_type(RelayMessageType::KRelayNotification);
    rl_msg.notification = Some(RelayNotificationMessage {
        body: raw_body,
        from_device: event.from_device,
    });
    let buffer = rl_msg.encode_to_vec();

    // send message to target device
    if conn
        .lock()
        .await
        .send_bin_message(Bytes::from(buffer))
        .await
    {
        return Ok(Json(gr_base::ok_resp(HashMap::default())));
    }
    Err(RelayApiError::NotifyEventFailed)
}
