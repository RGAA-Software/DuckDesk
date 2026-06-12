use protocol::spvr_relay::{SpvrRelayHeartBeat, SpvrRelayHello};

pub trait SpvrGrpcWsClientTrait {
    // hello
    // msg: Hello message
    // local_ip: connected client ip
    async fn on_ws_hello(&mut self, local_ip: String, msg: SpvrRelayHello);

    // heart beat
    async fn on_ws_heartbeat(&mut self, msg: SpvrRelayHeartBeat);

    // closed
    async fn on_ws_close(&mut self);
}