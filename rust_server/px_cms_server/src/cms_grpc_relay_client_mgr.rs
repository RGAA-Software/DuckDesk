use crate::cms_grpc_relay_client::CmsGrpcRelayClient;
use crate::cms_grpc_ws_client_trait::CmsGrpcWsClientTrait;
use protocol::cms_relay::{CmsRelayHeartBeat, CmsRelayHello};
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct CmsGrpcRelayClientManager {
    relay_client: Option<Arc<Mutex<CmsGrpcRelayClient>>>,
}

impl CmsGrpcRelayClientManager {
    pub fn new() -> Self {
        Self { relay_client: None }
    }

    pub async fn get_relay_client(&self) -> Option<Arc<Mutex<CmsGrpcRelayClient>>> {
        self.relay_client.clone()
    }

    pub async fn query_alive_rooms_count(&self) -> Result<u32, ()> {
        if let Some(relay_client) = self.relay_client.clone() {
            return relay_client.lock().await.query_alive_rooms_count().await;
        }
        Err(())
    }
}

impl CmsGrpcWsClientTrait for CmsGrpcRelayClientManager {
    async fn on_ws_hello(&mut self, local_ip: String, msg: CmsRelayHello) {
        let relay_client = Arc::new(Mutex::new(CmsGrpcRelayClient::new()));
        relay_client
            .lock()
            .await
            .connect(local_ip, msg.srv_grpc_port as u16)
            .await;

        // guard it
        CmsGrpcRelayClient::guard(relay_client.clone()).await;

        self.relay_client = Some(relay_client);
        tracing::info!("RelayServer OnHello: {:#?}", msg);
    }

    async fn on_ws_heartbeat(&mut self, _msg: CmsRelayHeartBeat) {
        //tracing::info!("HeartBeat: {:?}", msg);
    }

    async fn on_ws_close(&mut self) {
        self.relay_client.take();
    }
}
