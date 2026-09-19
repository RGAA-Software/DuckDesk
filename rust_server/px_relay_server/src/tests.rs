use crate::state::{ConnectionHandle, RelayRegistry};
use crate::{config::RelayConfig, server::router};
use futures_util::{SinkExt, StreamExt};
use prost::Message as ProstMessage;
use protocol::px_relay::{
    RelayCreateRoomMessage, RelayErrorCode, RelayHello, RelayMessage, RelayMessageType,
    RelayNotificationMessage, RelayRequestControlMessage, RelayRequestControlRespMessage,
    RelayTargetMessage,
};
use std::{
    net::SocketAddr,
    time::{Duration, SystemTime, UNIX_EPOCH},
};
use tokio::{net::TcpListener, task::JoinHandle};
use tokio_tungstenite::{
    connect_async,
    tungstenite::{self, Message},
    MaybeTlsStream, WebSocketStream,
};

type TestSocket = WebSocketStream<MaybeTlsStream<tokio::net::TcpStream>>;

fn connection_handle(
    generation: uuid::Uuid,
) -> (
    ConnectionHandle,
    tokio::sync::mpsc::Receiver<crate::state::OutboundMessage>,
) {
    let (sender, receiver) = tokio::sync::mpsc::channel(16);
    (
        ConnectionHandle {
            generation,
            sender,
            authorized_remote_device_id: None,
        },
        receiver,
    )
}

fn assert_relay_error(
    result: Result<Vec<crate::state::Delivery>, RelayErrorCode>,
    expected: RelayErrorCode,
) {
    match result {
        Err(actual) => assert_eq!(actual, expected),
        Ok(_) => panic!("relay operation unexpectedly succeeded"),
    }
}

#[test]
fn payload_requires_acceptance_monotonic_sequence_and_completed_delivery_accounting() {
    let mut registry = RelayRegistry::default();
    let (creator, _creator_receiver) = connection_handle(uuid::Uuid::new_v4());
    let (remote, _remote_receiver) = connection_handle(uuid::Uuid::new_v4());
    registry.register("creator".into(), creator, 4).unwrap();
    registry.register("remote".into(), remote, 4).unwrap();
    let (room, _) = registry
        .create_room("creator", "creator", "remote", "Creator", "stream", 4)
        .unwrap();

    assert_relay_error(
        registry.forward_payload(
            "creator",
            "creator",
            std::slice::from_ref(&room.id),
            7,
            4,
            vec![1, 2, 3, 4],
        ),
        RelayErrorCode::KRelayCodeRejectControl,
    );
    registry
        .accept_control_response("remote", &room.id, "creator", "remote", vec![5], true)
        .unwrap();
    let deliveries = registry
        .forward_payload(
            "creator",
            "creator",
            std::slice::from_ref(&room.id),
            7,
            4,
            vec![1, 2, 3, 4],
        )
        .unwrap();
    let before_delivery = registry.snapshot();
    assert_eq!(before_delivery.uploaded_payload_bytes, 4);
    assert_eq!(before_delivery.forwarded_payload_bytes, 0);
    assert_eq!(before_delivery.creator_to_remote_payload_bytes, 0);
    registry.record_forwarded_payload(deliveries[0].outbound.accounting.unwrap());
    let after_delivery = registry.snapshot();
    assert_eq!(after_delivery.forwarded_payload_bytes, 4);
    assert_eq!(after_delivery.creator_to_remote_payload_bytes, 4);

    assert_relay_error(
        registry.forward_payload(
            "creator",
            "creator",
            std::slice::from_ref(&room.id),
            7,
            4,
            vec![1, 2, 3, 4],
        ),
        RelayErrorCode::KRelayCodeRejectControl,
    );
    assert_relay_error(
        registry.forward_payload(
            "creator",
            "creator",
            &[room.id.clone(), room.id.clone()],
            8,
            4,
            vec![1, 2, 3, 4],
        ),
        RelayErrorCode::KRelayCodeRejectControl,
    );

    let reverse = registry
        .forward_payload(
            "remote",
            "remote",
            std::slice::from_ref(&room.id),
            0,
            3,
            vec![6, 7, 8],
        )
        .unwrap();
    registry.record_forwarded_payload(reverse[0].outbound.accounting.unwrap());
    let final_snapshot = registry.snapshot();
    assert_eq!(final_snapshot.uploaded_payload_bytes, 7);
    assert_eq!(final_snapshot.forwarded_payload_bytes, 7);
    assert_eq!(final_snapshot.remote_to_creator_payload_bytes, 3);
}

#[test]
fn replacement_connection_fences_stale_disconnect_and_removes_old_rooms() {
    let mut registry = RelayRegistry::default();
    let render_generation = uuid::Uuid::new_v4();
    let first_client_generation = uuid::Uuid::new_v4();
    let replacement_generation = uuid::Uuid::new_v4();
    let (render, _render_receiver) = connection_handle(render_generation);
    let (first_client, _first_client_receiver) = connection_handle(first_client_generation);
    registry.register("render".into(), render, 4).unwrap();
    registry.register("client".into(), first_client, 4).unwrap();
    registry
        .create_room("client", "client", "render", "Client", "old", 4)
        .unwrap();

    let (replacement, _replacement_receiver) = connection_handle(replacement_generation);
    let replacement_deliveries = registry.register("client".into(), replacement, 4).unwrap();
    assert_eq!(replacement_deliveries.len(), 1);
    assert_eq!(registry.snapshot().rooms, 0);
    registry
        .create_room("client", "client", "render", "Client", "new", 4)
        .unwrap();

    assert!(registry
        .disconnect("client", first_client_generation)
        .is_empty());
    let snapshot = registry.snapshot();
    assert_eq!(snapshot.connections, 2);
    assert_eq!(snapshot.rooms, 1);
}

async fn start_server() -> (SocketAddr, JoinHandle<()>) {
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let address = listener.local_addr().unwrap();
    let config = RelayConfig {
        listen: address,
        app_key: b"test-relay-app-key".to_vec(),
        max_connections: 16,
        max_rooms: 8,
        outbound_queue: 32,
        max_message_bytes: 1024 * 1024,
    };
    let server = tokio::spawn(async move {
        axum::serve(listener, router(config)).await.unwrap();
    });
    (address, server)
}

async fn connect(address: SocketAddr, device_id: &str, remote_device_id: &str) -> TestSocket {
    let url = format!(
        "ws://{address}/relay?device_id={device_id}&remote_device_id={remote_device_id}&device_name=test&stream_id=stream&appkey=test-relay-app-key"
    );
    connect_async(url).await.unwrap().0
}

async fn send(socket: &mut TestSocket, message: RelayMessage) {
    socket
        .send(Message::Binary(message.encode_to_vec().into()))
        .await
        .unwrap();
}

async fn receive(socket: &mut TestSocket) -> RelayMessage {
    let message = tokio::time::timeout(Duration::from_secs(2), socket.next())
        .await
        .unwrap()
        .unwrap()
        .unwrap();
    let Message::Binary(payload) = message else {
        panic!("expected a binary Relay frame");
    };
    RelayMessage::decode(payload).unwrap()
}

#[tokio::test]
async fn rejects_invalid_app_key_before_websocket_upgrade() {
    let (address, server) = start_server().await;
    let result = connect_async(format!(
        "ws://{address}/relay?device_id=client&remote_device_id=render&appkey=wrong-key"
    ))
    .await;
    let tungstenite::Error::Http(response) = result.unwrap_err() else {
        panic!("expected an HTTP rejection");
    };
    assert_eq!(response.status(), http::StatusCode::UNAUTHORIZED);
    server.abort();
}

#[tokio::test]
async fn accepts_only_a_current_ticket_bound_to_the_frontend_route() {
    let (address, server) = start_server().await;
    let session_id = uuid::Uuid::parse_str("10000000-0000-0000-0000-000000000001").unwrap();
    let remote_resource_id = uuid::Uuid::parse_str("20000000-0000-0000-0000-000000000002").unwrap();
    let now_unix_seconds = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap()
        .as_secs();
    let ticket = px_relay_admission::issue(
        b"test-relay-app-key",
        session_id,
        remote_resource_id,
        now_unix_seconds,
        now_unix_seconds + 60,
    )
    .unwrap();
    let accepted = connect_async(format!(
        "ws://{address}/relay?device_id=client_android&remote_device_id=server_{remote_resource_id}&device_name=test&stream_id={session_id}&appkey={}",
        ticket.as_str()
    ))
    .await;
    assert!(accepted.is_ok());

    let mismatched = connect_async(format!(
        "ws://{address}/relay?device_id=client_android&remote_device_id=server_30000000-0000-0000-0000-000000000003&device_name=test&stream_id={session_id}&appkey={}",
        ticket.as_str()
    ))
    .await;
    let tungstenite::Error::Http(response) = mismatched.unwrap_err() else {
        panic!("expected a route-bound ticket rejection");
    };
    assert_eq!(response.status(), http::StatusCode::UNAUTHORIZED);
    server.abort();
}

#[tokio::test]
async fn forwards_control_and_payload_only_inside_an_authorized_room() {
    let (address, server) = start_server().await;
    let mut render = connect(address, "render-one", "").await;
    let mut client = connect(address, "client-one", "render-one").await;

    send(
        &mut render,
        RelayMessage {
            r#type: RelayMessageType::KRelayHello as i32,
            hello: Some(RelayHello::default()),
            ..Default::default()
        },
    )
    .await;
    assert_eq!(
        receive(&mut render).await.r#type,
        RelayMessageType::KRelayHello as i32
    );

    send(
        &mut client,
        RelayMessage {
            r#type: RelayMessageType::KRelayCreateRoom as i32,
            create_room: Some(RelayCreateRoomMessage {
                device_id: "client-one".into(),
                device_name: "test".into(),
                stream_id: "stream".into(),
                remote_device_id: "render-one".into(),
            }),
            ..Default::default()
        },
    )
    .await;
    let room_response = receive(&mut client).await;
    assert_eq!(
        room_response.r#type,
        RelayMessageType::KRelayCreateRoomResp as i32
    );
    let room_id = room_response.create_room_resp.unwrap().room_id;

    send(
        &mut client,
        RelayMessage {
            r#type: RelayMessageType::KRelayRequestControl as i32,
            request_control: Some(RelayRequestControlMessage {
                device_id: "client-one".into(),
                remote_device_id: "render-one".into(),
                room_id: room_id.clone(),
                stream_id: "resource-session".into(),
                safety_pwd_md5: "credential-digest".into(),
                ..Default::default()
            }),
            ..Default::default()
        },
    )
    .await;
    let control_request = receive(&mut render).await;
    assert_eq!(
        control_request.request_control.unwrap().safety_pwd_md5,
        "credential-digest"
    );

    send(
        &mut render,
        RelayMessage {
            r#type: RelayMessageType::KRelayRequestControlResp as i32,
            request_control_resp: Some(RelayRequestControlRespMessage {
                device_id: "client-one".into(),
                remote_device_id: "render-one".into(),
                room_id: room_id.clone(),
                under_control: true,
                message: "accepted".into(),
            }),
            ..Default::default()
        },
    )
    .await;
    assert_eq!(
        receive(&mut client).await.r#type,
        RelayMessageType::KRelayRequestControlResp as i32
    );
    assert_eq!(
        receive(&mut client).await.r#type,
        RelayMessageType::KRelayRoomPrepared as i32
    );
    assert_eq!(
        receive(&mut render).await.r#type,
        RelayMessageType::KRelayRoomPrepared as i32
    );

    send(
        &mut client,
        RelayMessage {
            from_device_id: "client-one".into(),
            r#type: RelayMessageType::KRelayTargetMessage as i32,
            relay: Some(RelayTargetMessage {
                relay_msg_index: 1,
                room_ids: vec![room_id.clone()],
                payload: b"client-to-render".to_vec(),
            }),
            ..Default::default()
        },
    )
    .await;
    assert_eq!(
        receive(&mut render).await.relay.unwrap().payload,
        b"client-to-render"
    );

    send(
        &mut render,
        RelayMessage {
            from_device_id: "render-one".into(),
            r#type: RelayMessageType::KRelayTargetMessage as i32,
            relay: Some(RelayTargetMessage {
                relay_msg_index: 1,
                room_ids: vec![room_id],
                payload: b"render-to-client".to_vec(),
            }),
            ..Default::default()
        },
    )
    .await;
    assert_eq!(
        receive(&mut client).await.relay.unwrap().payload,
        b"render-to-client"
    );
    server.abort();
}

#[tokio::test]
async fn retired_notification_channel_is_rejected() {
    let (address, server) = start_server().await;
    let mut client = connect(address, "client-one", "render-one").await;
    send(
        &mut client,
        RelayMessage {
            r#type: RelayMessageType::KRelayNotification as i32,
            notification: Some(RelayNotificationMessage {
                body: "retired-signaling".into(),
                from_device: "client-one".into(),
            }),
            ..Default::default()
        },
    )
    .await;
    assert_eq!(
        receive(&mut client).await.r#type,
        RelayMessageType::KRelayError as i32
    );
    server.abort();
}
