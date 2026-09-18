use prost::Message;

pub type ProtoError = prost::DecodeError;

mod generated {
    include!(concat!(env!("OUT_DIR"), "/px.rs"));
}

pub use generated::{
    MsgAppInstanceReady, MsgAuthInfo, MsgFrontendAdmissionRequest, MsgFrontendAdmissionResult,
    MsgHeartBeat, MsgHeartBeatResp, MsgReqCtrlAltDelete, MsgResourceChannelOpenRequest,
    MsgResourceChannelOpenResult, MsgResourceChannelReportRequest, MsgResourceChannelReportResult,
    MsgRestartServer, MsgStartServer, MsgStopServer, MsgVirtualDisplayRequest,
    MsgVirtualDisplayResult, RenderStatus, ResourceChannelKind, ResourceChannelOutcome,
    ServiceMessage, ServiceMessageType, VirtualDisplayOperation,
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
    pub const VirtualDisplayRequest: Self = Self::KSrvVirtualDisplayRequest;
    pub const VirtualDisplayResult: Self = Self::KSrvVirtualDisplayResult;
    pub const AppInstanceReady: Self = Self::KSrvAppInstanceReady;
    pub const FrontendAdmissionRequest: Self = Self::KSrvFrontendAdmissionRequest;
    pub const FrontendAdmissionResult: Self = Self::KSrvFrontendAdmissionResult;
    pub const ResourceChannelOpenRequest: Self = Self::KSrvResourceChannelOpenRequest;
    pub const ResourceChannelOpenResult: Self = Self::KSrvResourceChannelOpenResult;
    pub const ResourceChannelReportRequest: Self = Self::KSrvResourceChannelReportRequest;
    pub const ResourceChannelReportResult: Self = Self::KSrvResourceChannelReportResult;
}

#[allow(non_upper_case_globals)]
impl ResourceChannelKind {
    pub const Control: Self = Self::KResourceChannelControl;
    pub const Media: Self = Self::KResourceChannelMedia;
    pub const Audio: Self = Self::KResourceChannelAudio;
    pub const File: Self = Self::KResourceChannelFile;
    pub const Rdp: Self = Self::KResourceChannelRdp;
}

#[allow(non_upper_case_globals)]
impl ResourceChannelOutcome {
    pub const Progress: Self = Self::KResourceChannelProgress;
    pub const PeerClosed: Self = Self::KResourceChannelPeerClosed;
    pub const UserStopped: Self = Self::KResourceChannelUserStopped;
    pub const TransportLost: Self = Self::KResourceChannelTransportLost;
    pub const PolicyRevoked: Self = Self::KResourceChannelPolicyRevoked;
    pub const IoError: Self = Self::KResourceChannelIoError;
}

#[allow(non_upper_case_globals)]
impl VirtualDisplayOperation {
    pub const Create: Self = Self::KVirtualDisplayCreate;
    pub const RemoveLast: Self = Self::KVirtualDisplayRemoveLast;
    pub const Query: Self = Self::KVirtualDisplayQuery;
    pub const ResetOwned: Self = Self::KVirtualDisplayResetOwned;
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
    fn round_trip_frontend_admission() {
        let request = ServiceMessage {
            r#type: ServiceMessageType::FrontendAdmissionRequest as i32,
            frontend_admission_request: Some(MsgFrontendAdmissionRequest {
                request_id: "admission-1".to_string(),
                session_id: "01994ddb-b930-7480-a15d-0a5176d1cc61".to_string(),
                revision: 4,
                frontend_token: "single-use-secret".to_string(),
            }),
            ..Default::default()
        };
        assert_eq!(
            decode_service_message(&encode_service_message(&request)).unwrap(),
            request
        );

        let response = ServiceMessage {
            r#type: ServiceMessageType::FrontendAdmissionResult as i32,
            frontend_admission_result: Some(MsgFrontendAdmissionResult {
                request_id: "admission-1".to_string(),
                accepted: true,
                error_code: String::new(),
                session_id: "01994ddb-b930-7480-a15d-0a5176d1cc61".to_string(),
                revision: 5,
                target_kind: "cloud_application".to_string(),
                device_id: String::new(),
                application_id: "01994ddb-b930-7480-a15d-0a5176d1cc62".to_string(),
                instance_id: "01994ddb-b930-7480-a15d-0a5176d1cc63".to_string(),
                client_type: "android".to_string(),
                access_role: "controller".to_string(),
                valid_for_ms: 30_000,
            }),
            ..Default::default()
        };
        assert_eq!(
            decode_service_message(&encode_service_message(&response)).unwrap(),
            response
        );
    }

    #[test]
    fn round_trip_resource_channel_activity() {
        let open = ServiceMessage {
            r#type: ServiceMessageType::ResourceChannelOpenRequest as i32,
            resource_channel_open_request: Some(MsgResourceChannelOpenRequest {
                request_id: "open-1".into(),
                source_id: "01994ddb-b930-7480-a15d-0a5176d1cc61".into(),
                session_id: "01994ddb-b930-7480-a15d-0a5176d1cc62".into(),
                channel_kind: ResourceChannelKind::Media as i32,
            }),
            ..Default::default()
        };
        assert_eq!(
            decode_service_message(&encode_service_message(&open)).unwrap(),
            open
        );

        let report = ServiceMessage {
            r#type: ServiceMessageType::ResourceChannelReportRequest as i32,
            resource_channel_report_request: Some(MsgResourceChannelReportRequest {
                request_id: "report-1".into(),
                channel_id: "01994ddb-b930-7480-a15d-0a5176d1cc63".into(),
                sequence: 1,
                sent_bytes: 100,
                received_bytes: 25,
                elapsed_ms: 500,
                outcome: ResourceChannelOutcome::PeerClosed as i32,
            }),
            ..Default::default()
        };
        assert_eq!(
            decode_service_message(&encode_service_message(&report)).unwrap(),
            report
        );
    }

    #[test]
    fn unknown_fields_are_ignored() {
        let mut bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::StopServer as i32,
            stop_server: Some(MsgStopServer::default()),
            ..Default::default()
        });
        // Unknown field 99, wire type varint, value 1. Keep this tag outside
        // the live ServiceMessage range as new payloads are appended.
        bytes.extend_from_slice(&[0x98, 0x06, 0x01]);
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
