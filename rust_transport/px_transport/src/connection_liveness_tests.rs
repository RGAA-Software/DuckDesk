use super::*;

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn healthy_idle_connection_retains_streams_and_datagrams() {
    tokio::time::timeout(Duration::from_secs(25), async {
        let config = EndpointConfig {
            bind_address: Some("127.0.0.1:0".parse().unwrap()),
            ..EndpointConfig::default()
        };
        let server = TransportEndpoint::bind(config.clone()).await.unwrap();
        let client = TransportEndpoint::bind(config).await.unwrap();
        let (client_connection, server_connection) =
            tokio::try_join!(client.connect(server.address()), server.accept()).unwrap();

        // No application traffic for twice the idle timeout. QUIC liveness, rather
        // than video delivery or user input, must keep a static desktop connected.
        tokio::select! {
            _ = tokio::time::sleep(CONNECTION_IDLE_TIMEOUT * 2) => {}
            reason = client_connection.closed() => panic!("idle client closed: {reason}"),
            reason = server_connection.closed() => panic!("idle server closed: {reason}"),
        }

        let (mut request_stream, mut response_stream) = client_connection.open_bi().await.unwrap();
        request_stream
            .write_all(b"idle-session-request")
            .await
            .unwrap();
        request_stream.finish().unwrap();
        let (mut reply_stream, mut inbound_stream) = server_connection.accept_bi().await.unwrap();
        assert_eq!(
            inbound_stream.read_to_end(64).await.unwrap(),
            b"idle-session-request"
        );
        reply_stream.write_all(b"idle-session-reply").await.unwrap();
        reply_stream.finish().unwrap();
        assert_eq!(
            response_stream.read_to_end(64).await.unwrap(),
            b"idle-session-reply"
        );
        client_connection
            .send_datagram(Bytes::from_static(b"idle-media"))
            .unwrap();
        assert_eq!(
            server_connection.read_datagram().await.unwrap(),
            b"idle-media"[..]
        );

        tokio::join!(client.close(), server.close());
    })
    .await
    .expect("idle connection validation timed out");
}
