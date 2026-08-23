use protocol::console_relay::{ConsoleRelayHeartBeat, ConsoleRelayHello};

pub trait ConsoleGrpcWsClientTrait {
    // hello
    // msg: Hello message
    // local_ip: connected client ip
    async fn on_ws_hello(&mut self, local_ip: String, msg: ConsoleRelayHello);

    // heart beat
    async fn on_ws_heartbeat(&mut self, msg: ConsoleRelayHeartBeat);

    // closed
    async fn on_ws_close(&mut self);
}
