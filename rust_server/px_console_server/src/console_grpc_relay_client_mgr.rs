use crate::console_grpc_relay_client::ConsoleGrpcRelayClient;
use crate::console_grpc_ws_client_trait::ConsoleGrpcWsClientTrait;
use protocol::console_relay::{ConsoleRelayHeartBeat, ConsoleRelayHello};
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct ConsoleGrpcRelayClientManager {
    relay_client: Option<Arc<Mutex<ConsoleGrpcRelayClient>>>,
}

impl ConsoleGrpcRelayClientManager {
    pub fn new() -> Self {
        Self { relay_client: None }
    }

    pub async fn get_relay_client(&self) -> Option<Arc<Mutex<ConsoleGrpcRelayClient>>> {
        self.relay_client.clone()
    }

    pub async fn query_alive_rooms_count(&self) -> Result<u32, ()> {
        if let Some(relay_client) = self.relay_client.clone() {
            return relay_client.lock().await.query_alive_rooms_count().await;
        }
        Err(())
    }
}

impl ConsoleGrpcWsClientTrait for ConsoleGrpcRelayClientManager {
    async fn on_ws_hello(&mut self, local_ip: String, msg: ConsoleRelayHello) {
        let relay_client = Arc::new(Mutex::new(ConsoleGrpcRelayClient::new()));
        relay_client
            .lock()
            .await
            .connect(local_ip, msg.srv_grpc_port as u16)
            .await;

        // guard it
        ConsoleGrpcRelayClient::guard(relay_client.clone()).await;

        self.relay_client = Some(relay_client);
        tracing::info!("RelayServer OnHello: {:#?}", msg);
    }

    async fn on_ws_heartbeat(&mut self, _msg: ConsoleRelayHeartBeat) {
        //tracing::info!("HeartBeat: {:?}", msg);
    }

    async fn on_ws_close(&mut self) {
        self.relay_client.take();
    }
}
