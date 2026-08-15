use std::sync::mpsc;
use std::sync::Arc;
use std::time::Duration;

use px_user_proxy::clipboard::backend::ClipboardBackend;
use px_user_proxy::clipboard::content::ClipboardFileEntry;
use px_user_proxy::clipboard::virtual_file::VirtualFileCoordinator;
use px_user_proxy::clipboard::{ClipboardService, InMemoryClipboard};
use px_user_proxy::config::UserProxyConfig;
use px_user_proxy::engine::UserProxyEngine;
use px_user_proxy::mock_render::{wait_for_event, MockRenderEvent};
use px_user_proxy::proto::{
    build_raw_render_message, build_tc_clipboard_files, parse_tc_message, tc::MessageType,
    StreamRoute,
};
use px_user_proxy::render_client::RenderClient;

fn test_config(port: u16) -> UserProxyConfig {
    UserProxyConfig::default()
        .with_render_port(port)
        .with_ws_path("/user-proxy")
}

async fn setup_engine(
    port: u16,
) -> (
    Arc<UserProxyEngine>,
    Arc<InMemoryClipboard>,
) {
    setup_engine_with_virtual_files(port, false).await
}

async fn setup_engine_with_virtual_files(
    port: u16,
    with_virtual: bool,
) -> (
    Arc<UserProxyEngine>,
    Arc<InMemoryClipboard>,
) {
    let (backend, _notify_rx) = InMemoryClipboard::new_pair();
    let backend = Arc::new(backend);
    let render_client = RenderClient::new(test_config(port));
    let clipboard = if with_virtual {
        let coordinator = VirtualFileCoordinator::new();
        let (outbound_tx, outbound_rx) = mpsc::channel();
        coordinator.set_outbound_sender(outbound_tx);
        spawn_outbound_forwarder(render_client.clone(), outbound_rx);
        Arc::new(ClipboardService::with_virtual_files(
            backend.clone(),
            coordinator,
        ))
    } else {
        Arc::new(ClipboardService::new(backend.clone()))
    };
    let engine = Arc::new(UserProxyEngine::new(clipboard, render_client));
    engine.start_render_loop();
    (engine, backend)
}

fn spawn_outbound_forwarder(client: Arc<RenderClient>, outbound_rx: mpsc::Receiver<Vec<u8>>) {
    std::thread::spawn(move || {
        while let Ok(bytes) = outbound_rx.recv() {
            let client = client.clone();
            if let Err(err) = client.blocking_send_bytes(bytes) {
                tracing::error!("test outbound send failed: {err:#}");
            }
        }
    });
}

#[tokio::test]
async fn integration_connects_and_sends_hello() {
    let server = px_user_proxy::mock_render::start().await.expect("mock render");
    let (engine, _) = setup_engine(server.port()).await;
    assert!(
        engine
            .render_client
            .wait_until_connected(Duration::from_secs(5))
            .await
    );
    assert!(
        wait_for_event(
            &server.handle(),
            |event| matches!(event, MockRenderEvent::Hello),
            Duration::from_secs(3),
        )
        .await
    );
}

#[tokio::test]
async fn integration_remote_clipboard_apply_and_resp() {
    let server = px_user_proxy::mock_render::start().await.expect("mock render");
    let (engine, backend) = setup_engine(server.port()).await;
    assert!(
        engine
            .render_client
            .wait_until_connected(Duration::from_secs(5))
            .await
    );

    server
        .handle()
        .send_raw_render_clipboard("from-client")
        .expect("send");
    tokio::time::sleep(Duration::from_millis(300)).await;

    assert_eq!(
        backend.read_text().expect("read").as_deref(),
        Some("from-client")
    );
    assert!(
        wait_for_event(
            &server.handle(),
            |event| matches!(event, MockRenderEvent::ClipboardResp),
            Duration::from_secs(3),
        )
        .await
    );
}

#[tokio::test]
async fn integration_local_clipboard_to_render() {
    let server = px_user_proxy::mock_render::start().await.expect("mock render");
    let (engine, backend) = setup_engine(server.port()).await;
    assert!(
        engine
            .render_client
            .wait_until_connected(Duration::from_secs(5))
            .await
    );

    backend.set_local_text("host-local-text");
    engine.on_local_clipboard_update().await;

    assert!(
        wait_for_event(
            &server.handle(),
            |event| {
                matches!(
                    event,
                    MockRenderEvent::ClipboardText(text) if text == "host-local-text"
                )
            },
            Duration::from_secs(3),
        )
        .await
    );
}

#[tokio::test]
async fn integration_echo_suppresses_outbound_loop() {
    let server = px_user_proxy::mock_render::start().await.expect("mock render");
    let (engine, backend) = setup_engine(server.port()).await;
    assert!(
        engine
            .render_client
            .wait_until_connected(Duration::from_secs(5))
            .await
    );

    server
        .handle()
        .send_raw_render_clipboard("echo-me")
        .expect("send");
    tokio::time::sleep(Duration::from_millis(300)).await;

    let before = server.handle().events().len();
    backend.set_local_text("echo-me");
    engine.on_local_clipboard_update().await;
    tokio::time::sleep(Duration::from_millis(200)).await;
    let after = server.handle().events().len();
    assert_eq!(before, after);
}

#[tokio::test]
async fn integration_reconnects_after_server_drop() {
    let server = px_user_proxy::mock_render::start().await.expect("mock render");
    let port = server.port();
    let (engine, _) = setup_engine(port).await;
    assert!(
        engine
            .render_client
            .wait_until_connected(Duration::from_secs(5))
            .await
    );

    drop(server);
    tokio::time::sleep(Duration::from_millis(300)).await;
    assert!(!engine.render_client.is_connected());

    let server2 = px_user_proxy::mock_render::start_on_port(port)
        .await
        .expect("mock render restart");
    assert!(
        engine
            .render_client
            .wait_until_connected(Duration::from_secs(6))
            .await
    );
    assert!(
        wait_for_event(
            &server2.handle(),
            |event| matches!(event, MockRenderEvent::Hello),
            Duration::from_secs(3),
        )
        .await
    );
}

#[tokio::test]
async fn integration_virtual_file_apply_installs_session() {
    use prost::Message as ProstMessage;

    let server = px_user_proxy::mock_render::start().await.expect("mock render");
    let (engine, backend) = setup_engine_with_virtual_files(server.port(), true).await;
    assert!(
        engine
            .render_client
            .wait_until_connected(Duration::from_secs(5))
            .await
    );

    let route = StreamRoute {
        stream_id: "stream-vf".to_string(),
        device_id: "device-vf".to_string(),
    };
    let files = vec![ClipboardFileEntry {
        file_name: "missing.bin".to_string(),
        full_path: "Z:/not/local/missing.bin".to_string(),
        ref_path: "missing.bin".to_string(),
        total_size: 11,
    }];
    let inner = build_tc_clipboard_files(&files);
    let mut tc = parse_tc_message(&inner).expect("tc");
    tc.stream_id = route.stream_id.clone();
    tc.device_id = route.device_id.clone();
    let bytes = build_raw_render_message(&tc.encode_to_vec(), false);
    server.handle().drain_events();
    px_user_proxy::render_client::handle_inbound_rp(
        &bytes,
        &engine.clipboard,
        engine.render_client.clone(),
    );
    tokio::time::sleep(Duration::from_millis(200)).await;

    let content = backend.read_content().expect("read");
    assert_eq!(content.files.len(), 1);
    assert_eq!(content.files[0].file_name, "missing.bin");
    assert!(backend.virtual_session().is_some());
    assert!(
        wait_for_event(
            &server.handle(),
            |event| matches!(event, MockRenderEvent::ClipboardResp),
            Duration::from_secs(3),
        )
        .await
    );
}

#[tokio::test]
async fn integration_virtual_file_req_buffer_roundtrip() {
    let server = px_user_proxy::mock_render::start().await.expect("mock render");
    server
        .handle()
        .set_virtual_file("Z:/not/local/missing.bin", b"hello-world".to_vec());

    let (engine, backend) = setup_engine_with_virtual_files(server.port(), true).await;
    assert!(
        engine
            .render_client
            .wait_until_connected(Duration::from_secs(5))
            .await
    );

    let route = StreamRoute {
        stream_id: "stream-vf".to_string(),
        device_id: "device-vf".to_string(),
    };
    engine
        .clipboard
        .apply_remote_files(
            &[ClipboardFileEntry {
                file_name: "missing.bin".to_string(),
                full_path: "Z:/not/local/missing.bin".to_string(),
                ref_path: "missing.bin".to_string(),
                total_size: 11,
            }],
            &route,
        )
        .expect("apply");

    let coordinator = backend.virtual_session().expect("session");
    let stream = coordinator.activate_stream(0).expect("stream");
    let req = stream.begin_read(5).expect("begin");
    coordinator.send_req_buffer(&req).expect("send req");

    assert!(
        wait_for_event(
            &server.handle(),
            |event| matches!(
                event,
                MockRenderEvent::ClipboardReqBuffer {
                    full_name,
                    req_index: 0,
                    req_start: 0,
                    req_size: 5,
                    ..
                } if full_name == "Z:/not/local/missing.bin"
            ),
            Duration::from_secs(3),
        )
        .await
    );

    tokio::time::sleep(Duration::from_millis(300)).await;
    let mut buf = [0u8; 5];
    let n = stream.complete_read(&mut buf).expect("read");
    assert_eq!(n, 5);
    assert_eq!(&buf, b"hello");
}

#[tokio::test]
async fn integration_virtual_file_full_stream_via_coordinator() {
    let coordinator = VirtualFileCoordinator::new();
    coordinator.install_session(px_user_proxy::clipboard::virtual_file::VirtualFileSession {
        route: StreamRoute {
            stream_id: "s".to_string(),
            device_id: "d".to_string(),
        },
        files: vec![ClipboardFileEntry {
            file_name: "f.bin".to_string(),
            full_path: "C:/remote/f.bin".to_string(),
            ref_path: "f.bin".to_string(),
            total_size: 8,
        }],
    });

    let stream = coordinator.activate_stream(0).expect("stream");
    let mut output = Vec::new();
    for (index, chunk) in [b"abcd", b"efgh"].iter().enumerate() {
        let req = stream.begin_read(4).expect("begin");
        assert_eq!(req.req_index, index as i64);
        assert!(coordinator.on_resp_buffer(px_user_proxy::clipboard::virtual_file::RespBufferData {
            full_name: req.full_name.clone(),
            req_index: req.req_index,
            req_start: req.req_start,
            req_size: req.req_size,
            read_size: chunk.len() as i64,
            buffer: chunk.to_vec(),
        }));
        let mut buf = [0u8; 4];
        let n = stream.complete_read(&mut buf).expect("read");
        output.extend_from_slice(&buf[..n]);
    }
    assert_eq!(output, b"abcdefgh");
    assert!(stream.is_transfer_complete());
}

#[tokio::test]
async fn integration_data_channel_resp_buffer_dispatch() {
    let server = px_user_proxy::mock_render::start().await.expect("mock render");
    let (engine, backend) = setup_engine_with_virtual_files(server.port(), true).await;
    assert!(
        engine
            .render_client
            .wait_until_connected(Duration::from_secs(5))
            .await
    );

    let route = StreamRoute {
        stream_id: "stream-r".to_string(),
        device_id: "device-r".to_string(),
    };
    engine
        .clipboard
        .apply_remote_files(
            &[ClipboardFileEntry {
                file_name: "x.dat".to_string(),
                full_path: "Q:/x.dat".to_string(),
                ref_path: "x.dat".to_string(),
                total_size: 3,
            }],
            &route,
        )
        .expect("apply");

    let coordinator = backend.virtual_session().expect("session");
    let stream = coordinator.activate_stream(0).expect("stream");
    let _req = stream.begin_read(3).expect("begin");

    let resp = px_user_proxy::clipboard::virtual_file::RespBufferData {
        full_name: "Q:/x.dat".to_string(),
        req_index: 0,
        req_start: 0,
        req_size: 3,
        read_size: 3,
        buffer: b"xyz".to_vec(),
    };
    server
        .handle()
        .send_raw_render_resp_buffer(&resp, &route)
        .expect("send resp");

    tokio::time::sleep(Duration::from_millis(300)).await;
    let mut buf = [0u8; 3];
    let n = stream.complete_read(&mut buf).expect("read");
    assert_eq!(n, 3);
    assert_eq!(&buf, b"xyz");
}

#[test]
fn integration_proto_resp_buffer_matches_panel_fields() {
    let route = StreamRoute {
        stream_id: "sid".to_string(),
        device_id: "did".to_string(),
    };
    let resp = px_user_proxy::clipboard::virtual_file::RespBufferData {
        full_name: "C:/a".to_string(),
        req_index: 7,
        req_start: 100,
        req_size: 50,
        read_size: 4,
        buffer: b"test".to_vec(),
    };
    let bytes = px_user_proxy::proto::build_tc_resp_buffer(&resp, &route);
    let tc = parse_tc_message(&bytes).expect("tc");
    assert_eq!(tc.r#type, MessageType::KClipboardRespBuffer as i32);
    assert_eq!(tc.stream_id, "sid");
    assert_eq!(tc.device_id, "did");
    let parsed = tc.cp_resp_buffer.expect("resp");
    assert_eq!(parsed.req_index, 7);
    assert_eq!(parsed.buffer, b"test");
}
