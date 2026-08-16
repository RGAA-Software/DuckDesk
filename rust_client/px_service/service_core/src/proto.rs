use prost::Message;

pub type ProtoError = prost::DecodeError;

mod generated {
    include!(concat!(env!("OUT_DIR"), "/px.rs"));
}

pub use generated::{
    MsgAuthInfo, MsgHeartBeat, MsgHeartBeatResp, MsgReqCtrlAltDelete, MsgRestartServer,
    MsgStartServer, MsgStopServer, RenderStatus, ServiceMessage, ServiceMessageType,
};

// prost only derives PartialEq; all MsgAuthInfo fields are scalar so Eq is sound
// and keeps Command/ServiceState's Eq derive working.
impl Eq for MsgAuthInfo {}

#[allow(non_upper_case_globals)]
impl ServiceMessageType {
    pub const StartServer: Self = Self::KSrvStartServer;
    pub const StopServer: Self = Self::KSrvStopServer;
    pub const RestartServer: Self = Self::KSrvRestartServer;
    pub const HeartBeat: Self = Self::KSrvHeartBeat;
    pub const HeartBeatResp: Self = Self::KSrvHeartBeatResp;
    pub const ReqCtrlAltDelete: Self = Self::KSrvReqCtrlAltDelete;
    pub const AuthInfo: Self = Self::KSrvAuthInfo;
}

#[allow(non_upper_case_globals)]
impl RenderStatus {
    pub const Stopped: Self = Self::KStopped;
    pub const Working: Self = Self::KWorking;
}

impl ServiceMessage {
    pub fn message_type(&self) -> Option<ServiceMessageType> {
        ServiceMessageType::try_from(self.r#type).ok()
    }

    pub fn set_message_type(&mut self, value: ServiceMessageType) {
        self.r#type = value as i32;
    }
}

impl MsgHeartBeatResp {
    pub fn render_status_enum(&self) -> Option<RenderStatus> {
        RenderStatus::try_from(self.render_status).ok()
    }
}

pub fn encode_service_message(message: &ServiceMessage) -> Vec<u8> {
    message.encode_to_vec()
}

pub fn decode_service_message(input: &[u8]) -> Result<ServiceMessage, ProtoError> {
    ServiceMessage::decode(input)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_start_server() {
        let msg = ServiceMessage {
            r#type: ServiceMessageType::StartServer as i32,
            start_server: Some(MsgStartServer {
                work_dir: "D:/app".to_string(),
                app_path: "D:/app/px_render.exe".to_string(),
                args: vec![
                    "--app_mode=desktop".to_string(),
                    "--panel_server_port=1".to_string(),
                ],
            }),
            ..Default::default()
        };
        let bytes = encode_service_message(&msg);
        let decoded = decode_service_message(&bytes).unwrap();
        assert_eq!(decoded, msg);
    }

    #[test]
    fn round_trip_heartbeat_response() {
        let msg = ServiceMessage {
            r#type: ServiceMessageType::HeartBeatResp as i32,
            heart_beat_resp: Some(MsgHeartBeatResp {
                index: 99,
                render_status: RenderStatus::Working as i32,
            }),
            ..Default::default()
        };
        let decoded = decode_service_message(&encode_service_message(&msg)).unwrap();
        assert_eq!(decoded, msg);
    }

    #[test]
    fn round_trip_ctrl_alt_delete() {
        let msg = ServiceMessage {
            r#type: ServiceMessageType::ReqCtrlAltDelete as i32,
            req_ctrl_alt_delete: Some(MsgReqCtrlAltDelete {
                req_device_id: "device".to_string(),
                req_stream_id: "stream".to_string(),
            }),
            ..Default::default()
        };
        assert_eq!(
            decode_service_message(&encode_service_message(&msg)).unwrap(),
            msg
        );
    }

    #[test]
    fn unknown_fields_are_ignored() {
        let mut bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::StopServer as i32,
            stop_server: Some(MsgStopServer::default()),
            ..Default::default()
        });
        bytes.extend_from_slice(&[0x50, 0x01]);
        let decoded = decode_service_message(&bytes).unwrap();
        assert_eq!(decoded.message_type(), Some(ServiceMessageType::StopServer));
        assert!(decoded.stop_server.is_some());
    }

    #[test]
    fn invalid_wire_type_is_rejected() {
        let err = decode_service_message(&[0x0a, 0x01, 0x01]).unwrap_err();
        assert!(!err.to_string().is_empty());
    }
}
