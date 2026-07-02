use std::sync::Arc;
use std::time::Duration;

use gr_user_proxy::clipboard::backend::ClipboardBackend;
use gr_user_proxy::clipboard::{ClipboardService, InMemoryClipboard};
use gr_user_proxy::config::UserProxyConfig;
use gr_user_proxy::engine::UserProxyEngine;
use gr_user_proxy::mock_render::{wait_for_event, MockRenderEvent};
use gr_user_proxy::render_client::RenderClient;

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
    let (backend, _notify_rx) = InMemoryClipboard::new_pair();
    let backend = Arc::new(backend);
    let clipboard = Arc::new(ClipboardService::new(backend.clone()));
    let render_client = RenderClient::new(test_config(port));
    let engine = Arc::new(UserProxyEngine::new(clipboard, render_client));
    engine.start_render_loop();
    (engine, backend)
}

#[tokio::test]
async fn integration_connects_and_sends_hello() {
    let server = gr_user_proxy::mock_render::start().await.expect("mock render");
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
    let server = gr_user_proxy::mock_render::start().await.expect("mock render");
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
    let server = gr_user_proxy::mock_render::start().await.expect("mock render");
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
    let server = gr_user_proxy::mock_render::start().await.expect("mock render");
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
    let server = gr_user_proxy::mock_render::start().await.expect("mock render");
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

    let server2 = gr_user_proxy::mock_render::start_on_port(port)
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
