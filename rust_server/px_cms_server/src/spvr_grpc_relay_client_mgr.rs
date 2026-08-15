use crate::spvr_grpc_relay_client::SpvrGrpcRelayClient;
use crate::spvr_grpc_ws_client_trait::SpvrGrpcWsClientTrait;
use protocol::spvr_relay::{SpvrRelayHeartBeat, SpvrRelayHello};
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct SpvrGrpcRelayClientManager {
    relay_client: Option<Arc<Mutex<SpvrGrpcRelayClient>>>,
}

impl SpvrGrpcRelayClientManager {
    pub fn new() -> Self {
        Self { relay_client: None }
    }

    pub async fn get_relay_client(&self) -> Option<Arc<Mutex<SpvrGrpcRelayClient>>> {
        self.relay_client.clone()
    }

    pub async fn query_alive_rooms_count(&self) -> Result<u32, ()> {
        if let Some(relay_client) = self.relay_client.clone() {
            return relay_client.lock().await.query_alive_rooms_count().await;
        }
        Err(())
    }
}

impl SpvrGrpcWsClientTrait for SpvrGrpcRelayClientManager {
    async fn on_ws_hello(&mut self, local_ip: String, msg: SpvrRelayHello) {
        let relay_client = Arc::new(Mutex::new(SpvrGrpcRelayClient::new()));
        relay_client
            .lock()
            .await
            .connect(local_ip, msg.srv_grpc_port as u16)
            .await;

        // guard it
        SpvrGrpcRelayClient::guard(relay_client.clone()).await;

        self.relay_client = Some(relay_client);
        tracing::info!("RelayServer OnHello: {:#?}", msg);
    }

    async fn on_ws_heartbeat(&mut self, _msg: SpvrRelayHeartBeat) {
        //tracing::info!("HeartBeat: {:?}", msg);
    }

    async fn on_ws_close(&mut self) {
        self.relay_client.take();
    }
}
