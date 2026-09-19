use axum::extract::ws::Message;
use prost::Message as ProstMessage;
use protocol::px_relay::{
    RelayErrorCode, RelayErrorMessage, RelayMessage, RelayMessageType,
    RelayRemoteDeviceOfflineMessage, RelayRoomDestroyedMessage,
};
use std::collections::{HashMap, HashSet};
use tokio::sync::mpsc;
use uuid::Uuid;

#[derive(Clone)]
pub struct ConnectionHandle {
    pub generation: Uuid,
    pub sender: mpsc::Sender<OutboundMessage>,
    pub authorized_remote_device_id: Option<String>,
}

#[derive(Clone)]
pub struct RelayRoom {
    pub id: String,
    pub creator_device_id: String,
    pub remote_device_id: String,
    pub creator_device_name: String,
    pub creator_stream_id: String,
    pub accepted: bool,
    pub creator_last_message_index: Option<i64>,
    pub remote_last_message_index: Option<i64>,
}

pub struct OutboundMessage {
    pub message: Message,
    pub accounting: Option<PayloadDeliveryAccounting>,
}

#[derive(Clone, Copy)]
pub struct PayloadDeliveryAccounting {
    pub payload_bytes: u64,
    pub creator_to_remote: bool,
}

#[derive(Default)]
pub struct RelayRegistry {
    connections: HashMap<String, ConnectionHandle>,
    rooms: HashMap<String, RelayRoom>,
    device_rooms: HashMap<String, HashSet<String>>,
    uploaded_payload_bytes: u64,
    forwarded_payload_bytes: u64,
    creator_to_remote_payload_bytes: u64,
    remote_to_creator_payload_bytes: u64,
    dropped_messages: u64,
}

pub struct RelaySnapshot {
    pub connections: usize,
    pub rooms: usize,
    pub uploaded_payload_bytes: u64,
    pub forwarded_payload_bytes: u64,
    pub creator_to_remote_payload_bytes: u64,
    pub remote_to_creator_payload_bytes: u64,
    pub dropped_messages: u64,
}

impl RelayRegistry {
    pub fn register(
        &mut self,
        device_id: String,
        handle: ConnectionHandle,
        max_connections: usize,
    ) -> Result<Vec<Delivery>, RelayErrorCode> {
        if !self.connections.contains_key(&device_id) && self.connections.len() >= max_connections {
            return Err(RelayErrorCode::KRelayCodeRejectControl);
        }
        let replaced = self.connections.insert(device_id.clone(), handle);
        Ok(replaced
            .map(|_| self.remove_device_rooms(&device_id))
            .unwrap_or_default())
    }

    pub fn disconnect(&mut self, device_id: &str, generation: Uuid) -> Vec<Delivery> {
        if self
            .connections
            .get(device_id)
            .is_none_or(|connection| connection.generation != generation)
        {
            return Vec::new();
        }
        self.connections.remove(device_id);
        self.remove_device_rooms(device_id)
    }

    fn remove_device_rooms(&mut self, device_id: &str) -> Vec<Delivery> {
        let room_ids = self.device_rooms.remove(device_id).unwrap_or_default();
        let mut deliveries = Vec::new();
        for room_id in room_ids {
            let Some(room) = self.remove_room(&room_id) else {
                continue;
            };
            let peer_device_id = room.peer(device_id).to_string();
            if let Some(peer) = self.connections.get(&peer_device_id) {
                deliveries.push(Delivery::binary(
                    peer.sender.clone(),
                    RelayMessage {
                        r#type: RelayMessageType::KRelayRemoteDeviceOffline as i32,
                        remote_device_offline: Some(RelayRemoteDeviceOfflineMessage {
                            room_id: room.id,
                            device_id: room.creator_device_id,
                            remote_device_id: device_id.to_string(),
                        }),
                        ..Default::default()
                    },
                ));
            }
        }
        deliveries
    }

    pub fn create_room(
        &mut self,
        connection_device_id: &str,
        requested_device_id: &str,
        remote_device_id: &str,
        device_name: &str,
        stream_id: &str,
        max_rooms: usize,
    ) -> Result<(RelayRoom, mpsc::Sender<OutboundMessage>), RelayErrorCode> {
        if connection_device_id != requested_device_id
            || remote_device_id.is_empty()
            || connection_device_id == remote_device_id
        {
            return Err(RelayErrorCode::KRelayCodeRejectControl);
        }
        let connection = self
            .connections
            .get(connection_device_id)
            .ok_or(RelayErrorCode::KRelayCodeClientNotFound)?;
        if connection
            .authorized_remote_device_id
            .as_deref()
            .is_some_and(|expected| expected != remote_device_id)
        {
            return Err(RelayErrorCode::KRelayCodeRejectControl);
        }
        if !self.connections.contains_key(remote_device_id) {
            return Err(RelayErrorCode::KRelayCodeRemoteClientNotFound);
        }
        if self.rooms.len() >= max_rooms {
            return Err(RelayErrorCode::KRelayCodeCreateRoomFailed);
        }

        let room = RelayRoom {
            id: format!("relay-room-{}", Uuid::new_v4()),
            creator_device_id: connection_device_id.to_string(),
            remote_device_id: remote_device_id.to_string(),
            creator_device_name: device_name.to_string(),
            creator_stream_id: stream_id.to_string(),
            accepted: false,
            creator_last_message_index: None,
            remote_last_message_index: None,
        };
        self.rooms.insert(room.id.clone(), room.clone());
        self.device_rooms
            .entry(connection_device_id.to_string())
            .or_default()
            .insert(room.id.clone());
        self.device_rooms
            .entry(remote_device_id.to_string())
            .or_default()
            .insert(room.id.clone());
        Ok((room, connection.sender.clone()))
    }

    pub fn forward_control(
        &self,
        connection_device_id: &str,
        room_id: &str,
        claimed_device_id: &str,
        claimed_remote_device_id: &str,
        payload: Vec<u8>,
    ) -> Result<Delivery, RelayErrorCode> {
        let room = self.authorize_room(connection_device_id, room_id)?;
        if claimed_device_id != connection_device_id
            || claimed_remote_device_id != room.peer(connection_device_id)
        {
            return Err(RelayErrorCode::KRelayCodeRejectControl);
        }
        let target = self
            .connections
            .get(claimed_remote_device_id)
            .ok_or(RelayErrorCode::KRelayCodeRemoteClientNotFound)?;
        Ok(Delivery::raw(target.sender.clone(), payload))
    }

    pub fn accept_control_response(
        &mut self,
        connection_device_id: &str,
        room_id: &str,
        creator_device_id: &str,
        remote_device_id: &str,
        encoded_response: Vec<u8>,
        accepted: bool,
    ) -> Result<Vec<Delivery>, RelayErrorCode> {
        let room = self.authorize_room(connection_device_id, room_id)?.clone();
        if connection_device_id != room.remote_device_id
            || creator_device_id != room.creator_device_id
            || remote_device_id != room.remote_device_id
        {
            return Err(RelayErrorCode::KRelayCodeRejectControl);
        }
        let creator_sender = self
            .connections
            .get(creator_device_id)
            .ok_or(RelayErrorCode::KRelayCodeClientNotFound)?
            .sender
            .clone();
        let mut deliveries = vec![Delivery::raw(creator_sender.clone(), encoded_response)];
        if accepted {
            self.rooms
                .get_mut(room_id)
                .ok_or(RelayErrorCode::KRelayCodeCreateRoomFailed)?
                .accepted = true;
            let prepared = RelayMessage {
                r#type: RelayMessageType::KRelayRoomPrepared as i32,
                room_prepared: Some(protocol::px_relay::RelayRoomPreparedMessage {
                    room_id: room.id.clone(),
                    device_id: room.creator_device_id.clone(),
                    remote_device_id: room.remote_device_id.clone(),
                    creator_device_id: room.creator_device_id.clone(),
                    creator_device_name: room.creator_device_name.clone(),
                    creator_stream_id: room.creator_stream_id.clone(),
                }),
                ..Default::default()
            };
            deliveries.push(Delivery::binary(creator_sender, prepared.clone()));
            let remote = self
                .connections
                .get(remote_device_id)
                .ok_or(RelayErrorCode::KRelayCodeRemoteClientNotFound)?;
            deliveries.push(Delivery::binary(remote.sender.clone(), prepared));
        }
        Ok(deliveries)
    }

    pub fn forward_payload(
        &mut self,
        connection_device_id: &str,
        from_device_id: &str,
        room_ids: &[String],
        message_index: i64,
        payload_bytes: usize,
        encoded_message: Vec<u8>,
    ) -> Result<Vec<Delivery>, RelayErrorCode> {
        if connection_device_id != from_device_id || room_ids.is_empty() {
            return Err(RelayErrorCode::KRelayCodeRejectControl);
        }
        let mut senders = Vec::with_capacity(room_ids.len());
        let mut unique_room_ids = HashSet::with_capacity(room_ids.len());
        for room_id in room_ids {
            if !unique_room_ids.insert(room_id) {
                return Err(RelayErrorCode::KRelayCodeRejectControl);
            }
            let room = self.authorize_room(connection_device_id, room_id)?;
            if !room.accepted || message_index < 0 {
                return Err(RelayErrorCode::KRelayCodeRejectControl);
            }
            let previous_message_index = if room.creator_device_id == connection_device_id {
                room.creator_last_message_index
            } else {
                room.remote_last_message_index
            };
            if previous_message_index
                .is_some_and(|previous| message_index != previous.saturating_add(1))
            {
                return Err(RelayErrorCode::KRelayCodeRejectControl);
            }
            let peer_device_id = room.peer(connection_device_id).to_string();
            let target = self
                .connections
                .get(&peer_device_id)
                .ok_or(RelayErrorCode::KRelayCodeRemoteClientNotFound)?;
            senders.push((
                target.sender.clone(),
                room.creator_device_id == connection_device_id,
            ));
        }
        for room_id in room_ids {
            if let Some(room) = self.rooms.get_mut(room_id) {
                if room.creator_device_id == connection_device_id {
                    room.creator_last_message_index = Some(message_index);
                } else {
                    room.remote_last_message_index = Some(message_index);
                }
            }
        }
        self.uploaded_payload_bytes = self
            .uploaded_payload_bytes
            .saturating_add(payload_bytes as u64);
        Ok(senders
            .into_iter()
            .map(|(sender, creator_to_remote)| {
                Delivery::payload(
                    sender,
                    encoded_message.clone(),
                    PayloadDeliveryAccounting {
                        payload_bytes: payload_bytes as u64,
                        creator_to_remote,
                    },
                )
            })
            .collect())
    }

    pub fn record_forwarded_payload(&mut self, accounting: PayloadDeliveryAccounting) {
        self.forwarded_payload_bytes = self
            .forwarded_payload_bytes
            .saturating_add(accounting.payload_bytes);
        if accounting.creator_to_remote {
            self.creator_to_remote_payload_bytes = self
                .creator_to_remote_payload_bytes
                .saturating_add(accounting.payload_bytes);
        } else {
            self.remote_to_creator_payload_bytes = self
                .remote_to_creator_payload_bytes
                .saturating_add(accounting.payload_bytes);
        }
    }

    pub fn stop_room(
        &mut self,
        connection_device_id: &str,
        room_id: &str,
        claimed_device_id: &str,
        claimed_remote_device_id: &str,
    ) -> Result<Vec<Delivery>, RelayErrorCode> {
        let room = self.authorize_room(connection_device_id, room_id)?.clone();
        if claimed_device_id != connection_device_id
            || claimed_remote_device_id != room.peer(connection_device_id)
        {
            return Err(RelayErrorCode::KRelayCodeRejectControl);
        }
        self.remove_room(room_id);
        let destroyed = RelayMessage {
            r#type: RelayMessageType::KRelayRoomDestroyed as i32,
            room_destroyed: Some(RelayRoomDestroyedMessage {
                room_id: room.id,
                device_id: room.creator_device_id.clone(),
                remote_device_id: room.remote_device_id.clone(),
            }),
            ..Default::default()
        };
        Ok([room.creator_device_id, room.remote_device_id]
            .into_iter()
            .filter_map(|device_id| self.connections.get(&device_id))
            .map(|connection| Delivery::binary(connection.sender.clone(), destroyed.clone()))
            .collect())
    }

    pub fn mark_drop(&mut self) {
        self.dropped_messages = self.dropped_messages.saturating_add(1);
    }

    pub fn snapshot(&self) -> RelaySnapshot {
        RelaySnapshot {
            connections: self.connections.len(),
            rooms: self.rooms.len(),
            uploaded_payload_bytes: self.uploaded_payload_bytes,
            forwarded_payload_bytes: self.forwarded_payload_bytes,
            creator_to_remote_payload_bytes: self.creator_to_remote_payload_bytes,
            remote_to_creator_payload_bytes: self.remote_to_creator_payload_bytes,
            dropped_messages: self.dropped_messages,
        }
    }

    fn authorize_room(&self, device_id: &str, room_id: &str) -> Result<&RelayRoom, RelayErrorCode> {
        let room = self
            .rooms
            .get(room_id)
            .ok_or(RelayErrorCode::KRelayCodeCreateRoomFailed)?;
        if room.creator_device_id != device_id && room.remote_device_id != device_id {
            return Err(RelayErrorCode::KRelayCodeRejectControl);
        }
        Ok(room)
    }

    fn remove_room(&mut self, room_id: &str) -> Option<RelayRoom> {
        let room = self.rooms.remove(room_id)?;
        for device_id in [&room.creator_device_id, &room.remote_device_id] {
            if let Some(room_ids) = self.device_rooms.get_mut(device_id) {
                room_ids.remove(room_id);
                if room_ids.is_empty() {
                    self.device_rooms.remove(device_id);
                }
            }
        }
        Some(room)
    }
}

impl RelayRoom {
    fn peer<'a>(&'a self, device_id: &str) -> &'a str {
        if self.creator_device_id == device_id {
            &self.remote_device_id
        } else {
            &self.creator_device_id
        }
    }
}

pub struct Delivery {
    pub sender: mpsc::Sender<OutboundMessage>,
    pub outbound: OutboundMessage,
}

impl Delivery {
    pub fn binary(sender: mpsc::Sender<OutboundMessage>, message: RelayMessage) -> Self {
        Self::raw(sender, message.encode_to_vec())
    }

    pub fn raw(sender: mpsc::Sender<OutboundMessage>, payload: Vec<u8>) -> Self {
        Self {
            sender,
            outbound: OutboundMessage {
                message: Message::Binary(payload.into()),
                accounting: None,
            },
        }
    }

    pub fn payload(
        sender: mpsc::Sender<OutboundMessage>,
        payload: Vec<u8>,
        accounting: PayloadDeliveryAccounting,
    ) -> Self {
        Self {
            sender,
            outbound: OutboundMessage {
                message: Message::Binary(payload.into()),
                accounting: Some(accounting),
            },
        }
    }
}

pub fn relay_error(code: RelayErrorCode, which_message: i32) -> RelayMessage {
    RelayMessage {
        r#type: RelayMessageType::KRelayError as i32,
        relay_error: Some(RelayErrorMessage {
            code: code as i32,
            message: format!("relay request rejected: {}", code.as_str_name()),
            which_message,
        }),
        ..Default::default()
    }
}
