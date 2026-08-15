use crate::relay::relay_message::{
    KEY_CREATE_TIMESTAMP, KEY_DEVICE_ID, KEY_DEVICE_NAME, KEY_LAST_UPDATE_TIMESTAMP,
    KEY_REMOTE_DEVICE_ID, KEY_ROOM_ID, KEY_STREAM_ID,
};
use crate::relay::relay_proto_maker::make_error_message;
use crate::relay::relay_queue::{RelayPacket, RelayQueue};
use crate::relay::relay_room::{RelayRoom, RelayRoomAdapter};
use crate::{gRelayConnMgr, gRelayRedisConn};
use axum::body::Bytes;
use prost::Message;
use protocol::px_relay::{
    RelayCreateRoomRespMessage, RelayErrorCode, RelayMessage, RelayMessageType,
    RelayRemoteDeviceOfflineMessage, RelayRoomDestroyedMessage, RelayRoomPreparedMessage,
};
use redis::{AsyncCommands, RedisResult};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct RelayRoomManager {
    pub relay_queue: Arc<Mutex<HashMap<String, RelayQueue>>>,
    pub relay_msg_indices: Mutex<HashMap<String, i64>>,
}

impl RelayRoomManager {
    pub fn new() -> Self {
        Self {
            relay_queue: Arc::new(Mutex::new(HashMap::new())),
            relay_msg_indices: Mutex::new(HashMap::new()),
        }
    }

    pub async fn create_room(
        &self,
        device_id: String,
        remote_device_id: String,
        device_name: String,
        stream_id: String,
    ) -> Option<RelayRoom> {
        let conn_device = if let Some(device) = gRelayConnMgr.get_conn(device_id.clone()).await {
            device
        } else {
            tracing::error!("Could not find device {}", device_id);
            return None;
        };

        let conn_remote_device =
            if let Some(remote_device) = gRelayConnMgr.get_conn(remote_device_id.clone()).await {
                remote_device
            } else {
                tracing::error!("Could not find remote device {}", remote_device_id);
                return None;
            };

        let room_id = format!("relay-room:{}-{}", device_id, remote_device_id);
        let mut devices = HashMap::new();
        devices.insert(device_id.clone(), conn_device);
        devices.insert(remote_device_id.clone(), conn_remote_device);

        let relay_room = RelayRoom {
            device_id: device_id.clone(),
            remote_device_id: remote_device_id.clone(),
            room_id: room_id.clone(),
            create_timestamp: px_base::get_current_timestamp(),
            last_update_timestamp: px_base::get_current_timestamp(),
            relay_conns: devices,
            device_name: device_name.clone(),
            stream_id: stream_id.clone(),
        };

        // to redis
        let relay_room_info = [
            (KEY_DEVICE_ID, device_id),
            (KEY_REMOTE_DEVICE_ID, remote_device_id),
            (KEY_ROOM_ID, room_id.clone()),
            (
                KEY_CREATE_TIMESTAMP,
                relay_room.create_timestamp.to_string(),
            ),
            (
                KEY_LAST_UPDATE_TIMESTAMP,
                relay_room.last_update_timestamp.to_string(),
            ),
            (KEY_DEVICE_NAME, device_name),
            (KEY_STREAM_ID, stream_id),
        ];

        let result = gRelayRedisConn
            .lock()
            .await
            .clone_conn()
            .hset_multiple::<String, &str, String, ()>(room_id.clone(), &relay_room_info)
            .await;
        if let Err(err) = result {
            tracing::error!("insert to redis failed {:?}, room id: {}", err, room_id);
            return None;
        }

        // px_relay_server queue
        let mut queue = RelayQueue::new(room_id.clone());
        queue.run().await;
        self.relay_queue.lock().await.insert(room_id.clone(), queue);

        Some(relay_room)
    }

    pub async fn find_room(&self, room_id: String, must_valid: bool) -> Option<RelayRoom> {
        let result = gRelayRedisConn
            .lock()
            .await
            .clone_conn()
            .hgetall::<String, Vec<(String, String)>>(room_id.clone())
            .await;
        if let Err(err) = result {
            tracing::error!("Could not find room: {} in redis, err: {}", room_id, err);
            return None;
        }

        let mut relay_room = RelayRoom::default();
        let room_info = result.unwrap();
        for (key, val) in room_info.iter() {
            if key == KEY_DEVICE_ID {
                relay_room.device_id = val.clone();
            } else if key == KEY_REMOTE_DEVICE_ID {
                relay_room.remote_device_id = val.clone();
            } else if key == KEY_ROOM_ID {
                relay_room.room_id = val.clone();
            } else if key == KEY_CREATE_TIMESTAMP {
                relay_room.create_timestamp = val.parse::<i64>().unwrap_or(0);
            } else if key == KEY_LAST_UPDATE_TIMESTAMP {
                relay_room.last_update_timestamp = val.parse::<i64>().unwrap_or(0);
            } else if key == KEY_DEVICE_NAME {
                relay_room.device_name = val.clone();
            } else if key == KEY_STREAM_ID {
                relay_room.stream_id = val.clone();
            }
        }

        if let Some(device) = gRelayConnMgr.get_conn(relay_room.device_id.clone()).await {
            relay_room
                .relay_conns
                .insert(relay_room.device_id.clone(), device.clone());
            //tracing::info!("found device {:?}", relay_room.device_id);
        }
        if let Some(remote_device) = gRelayConnMgr
            .get_conn(relay_room.remote_device_id.clone())
            .await
        {
            relay_room
                .relay_conns
                .insert(relay_room.remote_device_id.clone(), remote_device.clone());
            //tracing::info!("found remote device {:?}", relay_room.remote_device_id);
        }

        if must_valid {
            if relay_room.is_valid() {
                Some(relay_room)
            } else {
                None
            }
        } else {
            Some(relay_room)
        }
    }

    pub async fn find_room_ids(&self) -> Vec<String> {
        let begin = 0;
        let pattern = "relay-room:client_*";
        let cursor = begin as u64;
        let mut redis_conn = gRelayRedisConn.lock().await.clone_conn();
        let r: RedisResult<(u64, Vec<String>)> = redis::cmd("SCAN")
            .cursor_arg(cursor)
            .arg("MATCH")
            .arg(pattern)
            .arg("COUNT")
            .arg(5000)
            .query_async(&mut redis_conn)
            .await;
        if let Err(err) = r {
            tracing::error!("Could not find rooms in redis, err: {}", err);
            return Vec::new();
        }
        let room_ids = r.unwrap();
        tracing::info!("room ids: {:#?}", room_ids);
        room_ids.1
    }

    pub async fn find_total_rooms(&self) -> Vec<RelayRoomAdapter> {
        let mut r = Vec::new();
        let room_ids = self.find_room_ids().await;
        for room_id in room_ids {
            let room = self.find_room(room_id, false).await;
            if let Some(room) = room {
                r.push(room.adapter());
            }
        }
        r
    }

    pub async fn find_total_alive_rooms(&self) -> Vec<RelayRoomAdapter> {
        let mut rooms = self.find_total_rooms().await;
        rooms.retain(|e| {
            let current_ts = px_base::get_current_timestamp();
            !e.remote_device_id.is_empty()
                && !e.device_id.is_empty()
                && current_ts - e.last_update_timestamp < 10_000
        });
        rooms
    }

    // remote offline/exit
    // > remote device | *** this device offline -->
    // > device        |  notify this device    <--|
    pub async fn notify_remote_device_offline(&self, remote_device_id: String) {
        let r = gRelayRedisConn
            .lock()
            .await
            .clone_conn()
            .keys::<String, Vec<String>>(format!("relay-room:*{}", remote_device_id))
            .await;
        if let Err(err) = r {
            tracing::error!(
                "Could not find rooms contain {}, err: {}",
                remote_device_id,
                err
            );
            return;
        }

        let room_ids = r.unwrap();
        tracing::info!("found room: {:#?}", room_ids);
        for room_id in room_ids.iter() {
            // ignore file transfer room
            if room_id.contains("ft_client") || room_id.contains("ft_server") {
                tracing::info!("ignore room: {}", room_id);
                continue;
            }

            // get room info from redis
            let room_info = gRelayRedisConn
                .lock()
                .await
                .clone_conn()
                .hgetall::<&String, Vec<(String, String)>>(room_id)
                .await;
            if let Err(err) = room_info {
                tracing::error!(
                    "Could not find room info: {} in redis, err: {}",
                    room_id,
                    err
                );
                continue;
            }

            let room_info = room_info.unwrap();
            for (_key, val) in room_info.iter() {
                if val == &remote_device_id {
                    tracing::info!("found remote device id is myself.");
                    break;
                }
            }

            // find creator device id
            let mut target_device_id = "".to_string();
            for (key, val) in room_info.iter() {
                if key == KEY_DEVICE_ID {
                    target_device_id = val.clone();
                    tracing::info!("found device id: {}", target_device_id);
                    break;
                }
            }

            if !target_device_id.is_empty() {
                let mut rl_msg = RelayMessage::default();
                rl_msg.set_type(RelayMessageType::KRelayRemoteDeviceOffline);
                rl_msg.remote_device_offline = Some(RelayRemoteDeviceOfflineMessage {
                    room_id: room_id.clone(),
                    device_id: target_device_id.clone(),
                    remote_device_id: remote_device_id.clone(),
                });

                let r = rl_msg.encode_to_vec();
                if let Some(device) = gRelayConnMgr.get_conn(target_device_id.clone()).await {
                    tracing::info!(
                        "sent to device : {}, offline device is : {}",
                        target_device_id,
                        remote_device_id
                    );
                    tokio::spawn(async move {
                        _ = device.lock().await.send_bin_message(Bytes::from(r)).await;
                    });
                }
            }
        }
    }

    // creator offline/exit
    // > remote device |
    // > device        | *** this device offline, destroy the rooms[media/file transfer]
    pub async fn destroy_room_i_created(&self, device_id: String) {
        let r = gRelayRedisConn
            .lock()
            .await
            .clone_conn()
            .keys::<String, Vec<String>>(format!("relay-room:{}*", device_id))
            .await;
        if let Err(err) = r {
            tracing::error!(
                "Could not find rooms created by: {}, err: {}",
                device_id,
                err
            );
            return;
        }

        let room_ids = r.unwrap();
        for room_id in room_ids.iter() {
            let room_info = gRelayRedisConn
                .lock()
                .await
                .clone_conn()
                .hgetall::<&String, Vec<(String, String)>>(room_id)
                .await;
            if let Err(err) = room_info {
                tracing::error!(
                    "Could not find room info: {} in redis, err: {}",
                    room_id,
                    err
                );
                continue;
            }

            let mut target_remote_device_id = "".to_string();
            let room_info = room_info.unwrap();
            for (key, val) in room_info.iter() {
                if key == KEY_REMOTE_DEVICE_ID {
                    target_remote_device_id = val.clone();
                    break;
                }
            }

            if !target_remote_device_id.is_empty() {
                let mut rl_msg = RelayMessage::default();
                rl_msg.set_type(RelayMessageType::KRelayRoomDestroyed);
                rl_msg.room_destroyed = Some(RelayRoomDestroyedMessage {
                    room_id: room_id.clone(),
                    device_id: device_id.clone(),
                    remote_device_id: target_remote_device_id.clone(),
                });
                let r = rl_msg.encode_to_vec();
                if let Some(remote_device) = gRelayConnMgr.get_conn(target_remote_device_id).await {
                    tokio::spawn(async move {
                        _ = remote_device
                            .lock()
                            .await
                            .send_bin_message(Bytes::from(r))
                            .await;
                    });
                }
            }

            // delete info in redis
            _ = gRelayRedisConn
                .lock()
                .await
                .clone_conn()
                .del::<&String, ()>(room_id)
                .await;

            // exit px_relay_server queue
            let relay_queue = self.relay_queue.clone();
            let rid = room_id.clone();
            tokio::spawn(async move {
                if let Some(queue) = relay_queue.lock().await.get(rid.as_str()) {
                    queue.exit().await;
                }
            });
        }
    }

    pub async fn on_heartbeat_for_my_room(&self, device_id: String) {
        let mut conn = gRelayRedisConn.lock().await.clone_conn();
        let r = conn
            .keys::<String, Vec<String>>(format!("relay-room:{}*", device_id))
            .await;
        let room_ids = r.unwrap();
        for room_id in room_ids.iter() {
            _ = conn
                .hset::<&String, &str, String, ()>(
                    room_id,
                    KEY_LAST_UPDATE_TIMESTAMP,
                    px_base::get_current_timestamp().to_string(),
                )
                .await;
        }
    }

    pub async fn clear_info_in_rooms_i_was_invited(&self, device_id: String) {
        let mut conn = gRelayRedisConn.lock().await.clone_conn();
        let room_id_pattern = format!("relay-room:*{}", device_id);
        tracing::warn!("will find rooms like: {}", room_id_pattern);

        let r = conn.keys::<String, Vec<String>>(room_id_pattern).await;
        let room_ids = r.unwrap();
        for room_id in room_ids.iter() {
            _ = conn
                .hset::<&String, &str, String, ()>(room_id, KEY_REMOTE_DEVICE_ID, "".to_string())
                .await;
            tracing::warn!(
                "I({}) was offline, clear info in room: {}",
                device_id,
                room_id
            );
        }
    }

    pub async fn on_relay(&self, m: RelayMessage, om: Bytes) {
        // append received data size
        //self.append_received_data_size(om.len()).await;

        let sub = m.relay.unwrap();
        let from_device_id = m.from_device_id;
        let relay_msg_index = sub.relay_msg_index;
        for room_id in sub.room_ids.iter() {
            let room = self.find_room(room_id.clone(), true).await;
            if let Some(room) = room {
                if self
                    .relay_msg_indices
                    .lock()
                    .await
                    .contains_key(from_device_id.as_str())
                {
                    let last_relay_msg_index = self
                        .relay_msg_indices
                        .lock()
                        .await
                        .get(from_device_id.as_str())
                        .cloned();
                    if let Some(last_relay_msg_index) = last_relay_msg_index {
                        let diff = relay_msg_index - last_relay_msg_index;

                        if diff != 1 {
                            // tracing::error!("Relay index diff error: {}, now: {}, last: {}, device id: {}",
                            //     diff, relay_msg_index, last_relay_msg_index, from_device_id);
                        }

                        self.relay_msg_indices
                            .lock()
                            .await
                            .insert(from_device_id.clone(), relay_msg_index);
                    } else {
                        //
                    }
                } else {
                    self.relay_msg_indices
                        .lock()
                        .await
                        .insert(from_device_id.clone(), relay_msg_index);
                }

                let from_device_id = from_device_id.clone();
                let om = om.clone();

                let id = room_id.clone();
                if let Some(queue) = self.relay_queue.lock().await.get(&id) {
                    queue
                        .send(RelayPacket {
                            except_id: from_device_id,
                            room,
                            payload: om,
                            relay_msg_index,
                        })
                        .await;
                }
            }
        }
    }

    pub async fn on_create_room(&self, m: RelayMessage, _om: Bytes) {
        let sub = m.create_room.unwrap();
        let room = self
            .create_room(
                sub.device_id.clone(),
                sub.remote_device_id.clone(),
                sub.device_name.clone(),
                sub.stream_id.clone(),
            )
            .await;
        let resp_msg;
        if let Some(room) = room {
            tracing::info!("created room: {}", room.room_id);
            let mut rl_msg = RelayMessage::default();
            rl_msg.set_type(RelayMessageType::KRelayCreateRoomResp);
            rl_msg.create_room_resp = Some(RelayCreateRoomRespMessage {
                device_id: sub.device_id.clone(),
                remote_device_id: sub.remote_device_id,
                room_id: room.room_id.clone(),
            });

            resp_msg = rl_msg.encode_to_vec();
        } else {
            resp_msg = make_error_message(RelayErrorCode::KRelayCodeCreateRoomFailed);
        }

        if let Some(device) = gRelayConnMgr.get_conn(sub.device_id.clone()).await {
            _ = device
                .lock()
                .await
                .send_bin_message(Bytes::from(resp_msg))
                .await;
        }
    }

    pub async fn on_request_control(&self, m: RelayMessage, om: Bytes) {
        let from_device_id = m.from_device_id;
        let sub = m.request_control.unwrap();
        let remote_device_id = sub.remote_device_id;
        let _device_name = sub.device_name;
        let _stream_id = sub.stream_id;
        let remote_conn = gRelayConnMgr.get_conn(remote_device_id.clone()).await;

        if let Some(remote_conn) = remote_conn {
            remote_conn.lock().await.send_bin_message(om).await;
            tracing::info!("request control message to: {}", remote_device_id);
        } else {
            if let Some(conn) = gRelayConnMgr.get_conn(from_device_id).await {
                let r = make_error_message(RelayErrorCode::KRelayCodeRemoteClientNotFound);
                _ = conn.lock().await.send_bin_message(Bytes::from(r)).await;
            }
        }
    }

    pub async fn on_request_control_resp(&self, m: RelayMessage, om: Bytes) {
        let sub = m.request_control_resp.unwrap();
        let creator_device_id = sub.device_id.clone();
        //let remote_device_id = sub.remote_device_id.clone();
        let req_device = gRelayConnMgr.get_conn(creator_device_id.clone()).await;
        if req_device.is_none() {
            tracing::error!("can't find device: {}", creator_device_id);
            return;
        }
        let req_device = req_device.unwrap();
        req_device.lock().await.send_bin_message(om.clone()).await;

        if sub.under_control {
            tracing::info!("{} is under control", sub.remote_device_id);
            let room_id = sub.room_id;
            let room = self.find_room(room_id.clone(), true).await;
            if room.is_none() {
                tracing::error!("can't find room: {}", room_id);
                return;
            }
            let room = room.unwrap();

            let resp_device = gRelayConnMgr.get_conn(sub.remote_device_id.clone()).await;
            if resp_device.is_none() {
                tracing::error!("can't find remote device: {}", sub.remote_device_id);
                return;
            }
            let resp_device = resp_device.unwrap();

            let mut rl_msg = RelayMessage::default();
            rl_msg.set_type(RelayMessageType::KRelayRoomPrepared);
            rl_msg.room_prepared = Some(RelayRoomPreparedMessage {
                room_id,
                device_id: creator_device_id.clone(),
                remote_device_id: sub.remote_device_id,
                creator_device_id,
                creator_device_name: room.device_name.clone(),
                creator_stream_id: room.stream_id.clone(),
            });
            let r = rl_msg.encode_to_vec();
            let rr = r.clone();

            // 1. to requester
            tokio::spawn(async move {
                req_device
                    .lock()
                    .await
                    .send_bin_message(Bytes::from(r))
                    .await;
            });

            // 2. to remote
            tokio::spawn(async move {
                resp_device
                    .lock()
                    .await
                    .send_bin_message(Bytes::from(rr))
                    .await;
            });
        }
    }

    pub async fn on_request_resume_pause_stream(&self, m: RelayMessage, om: Bytes) {
        let from_device_id = m.from_device_id;
        let mut remote_device_id = "".to_string();
        if m.r#type == RelayMessageType::KRelayRequestResumeStream {
            let sub = m.request_resume.unwrap();
            remote_device_id = sub.remote_device_id;
            tracing::info!("request resume stream message to: {}", remote_device_id);
        } else if m.r#type == RelayMessageType::KRelayRequestPausedStream {
            let sub = m.request_pause.unwrap();
            remote_device_id = sub.remote_device_id;
            tracing::info!("request pause stream message to: {}", remote_device_id);
        }

        let remote_conn = gRelayConnMgr.get_conn(remote_device_id.clone()).await;

        if let Some(remote_conn) = remote_conn {
            remote_conn.lock().await.send_bin_message(om).await;
        } else {
            if let Some(conn) = gRelayConnMgr.get_conn(from_device_id).await {
                let r = make_error_message(RelayErrorCode::KRelayCodeRemoteClientNotFound);
                _ = conn.lock().await.send_bin_message(Bytes::from(r)).await;
            }
        }
    }
}
