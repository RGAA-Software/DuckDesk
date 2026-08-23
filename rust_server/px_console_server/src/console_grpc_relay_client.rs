use protocol::grpc_relay::RelayRoomsCountRequest;
use protocol::{
    grpc_relay::grpc_relay_client::GrpcRelayClient, grpc_relay::HeartBeatRequest,
    grpc_relay::RelayStreamRequest,
};
use serde::Serialize;
use std::sync::Arc;
use tokio::sync::Mutex;
use tokio::time::Duration;
use tokio_stream::Stream;
use tonic::codegen::tokio_stream::StreamExt;
use tonic::transport::Channel;

#[derive(Serialize)]
pub struct ConsoleGrpcRelayClient {
    #[serde(skip_serializing)]
    pub client: Arc<Mutex<Option<GrpcRelayClient<Channel>>>>,
    pub hb_index: i64,
    pub grpc_ip: String,
    pub grpc_port: u16,
}

async fn echo_requests_iter() -> impl Stream<Item = RelayStreamRequest> {
    let server_id = "".to_string();
    tokio_stream::iter(1..usize::MAX).map(move |i| RelayStreamRequest {
        server_id: server_id.clone(),
        message: format!("msg {:02}", i),
    })
}

impl ConsoleGrpcRelayClient {
    pub fn new() -> Self {
        Self {
            client: Arc::new(Mutex::new(None)),
            hb_index: 0,
            grpc_ip: "".to_string(),
            grpc_port: 0,
        }
    }

    pub async fn connect(&mut self, grpc_ip: String, grpc_port: u16) -> bool {
        self.grpc_ip = grpc_ip.clone();
        self.grpc_port = grpc_port;
        let addr = format!("http://{}:{}", grpc_ip, grpc_port);
        tracing::info!("relay grpc is connecting to {}", addr);
        let conn = GrpcRelayClient::connect(addr).await;
        if let Err(e) = conn {
            tracing::error!("connect grpc remote error: {}", e);
            return false;
        }
        let conn = conn.unwrap();
        self.client = Arc::new(Mutex::new(Some(conn)));
        true
    }

    pub async fn heartbeat(&mut self) -> bool {
        let server_id = "".to_string();
        if let Some(mut client) = self.client.lock().await.clone() {
            let r = client
                .heart_beat(tonic::Request::new(HeartBeatRequest {
                    server_id,
                    hb_index: self.hb_index,
                }))
                .await;

            if let Ok(_r) = r {
                self.hb_index += 1;
                return true;
            }
        }
        self.client = Arc::new(Mutex::new(None));
        false
    }

    pub async fn query_alive_rooms_count(&mut self) -> Result<u32, ()> {
        if let Some(mut client) = self.client.lock().await.clone() {
            let r = client
                .query_relay_rooms_count(tonic::Request::new(RelayRoomsCountRequest {}))
                .await;

            if let Ok(resp) = r {
                let reply = resp.into_inner();
                return Ok(reply.count);
            }
        }
        Err(())
    }

    pub async fn guard(relay_client: Arc<Mutex<ConsoleGrpcRelayClient>>) {
        tokio::spawn(async move {
            let mut interval = tokio::time::interval(Duration::from_secs(5));
            loop {
                interval.tick().await;
                let relay_client = relay_client.clone();
                let grpc_ip = relay_client.lock().await.grpc_ip.clone();
                let grpc_port = relay_client.lock().await.grpc_port;
                if relay_client.lock().await.heartbeat().await {
                    //tracing::info!("guard is ok: {:?}", Instant::now());
                    continue;
                } else {
                    tracing::error!("Relay Grpc heartbeat failed. It's closed, will reconnect it.");
                    relay_client.lock().await.connect(grpc_ip, grpc_port).await;
                }
            }
        });
    }

    pub async fn start_streaming_request(&mut self, num: usize) {
        let in_stream = echo_requests_iter().await.take(num);

        if let Some(client) = &mut *self.client.lock().await {
            let response = client.stream_request(in_stream).await;
            if let Err(e) = response {
                tracing::error!("streaming request error: {}", e);
                return;
            }
            let response = response.unwrap();

            tokio::spawn(async move {
                let mut resp_stream = response.into_inner();
                while let Some(received) = resp_stream.next().await {
                    let received = received.unwrap();
                    println!("\treceived message: `{}`", received.message);
                }
            });
        }
    }
}
