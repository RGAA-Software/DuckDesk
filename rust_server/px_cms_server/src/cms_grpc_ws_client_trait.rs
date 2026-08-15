use protocol::cms_px_relay::{CmsRelayHeartBeat, CmsRelayHello};

pub trait CmsGrpcWsClientTrait {
    // hello
    // msg: Hello message
    // local_ip: connected client ip
    async fn on_ws_hello(&mut self, local_ip: String, msg: CmsRelayHello);

    // heart beat
    async fn on_ws_heartbeat(&mut self, msg: CmsRelayHeartBeat);

    // closed
    async fn on_ws_close(&mut self);
}
