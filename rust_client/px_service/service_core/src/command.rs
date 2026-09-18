use crate::proto::{decode_service_message, ServiceMessageType, VirtualDisplayOperation};
use crate::state::RenderLaunchSpec;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Command {
    StartDesktop(RenderLaunchSpec),
    StopDesktop,
    RestartDesktop(RenderLaunchSpec),
    HeartBeat {
        index: i64,
        from: String,
        /// Render-owned logical session snapshot, serialized JSON. It is not
        /// interpreted by the privileged Service process.
        logical_sessions_json: String,
    },
    CtrlAltDelete {
        req_device_id: String,
        req_stream_id: String,
    },
    VirtualDisplay {
        request_id: String,
        operation: VirtualDisplayOperation,
        width: u32,
        height: u32,
        refresh_hz: u32,
    },
    AdmitFrontend {
        request_id: String,
        session_id: String,
        revision: i64,
        frontend_token: String,
    },
    OpenResourceChannel {
        request_id: String,
        source_id: String,
        session_id: String,
        channel_kind: crate::proto::ResourceChannelKind,
    },
    ReportResourceChannel {
        request_id: String,
        channel_id: String,
        sequence: u64,
        sent_bytes: u64,
        received_bytes: u64,
        elapsed_ms: u64,
        outcome: crate::proto::ResourceChannelOutcome,
    },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DispatchResult {
    pub command: Command,
}

pub fn dispatch_message(bytes: &[u8]) -> Result<DispatchResult, String> {
    let message = decode_service_message(bytes).map_err(|err| err.to_string())?;
    let command = match message
        .message_type()
        .ok_or("unknown service message type")?
    {
        ServiceMessageType::StartServer => {
            let start = message.start_server.ok_or("missing start_server payload")?;
            Command::StartDesktop(start.into())
        }
        ServiceMessageType::StopServer => Command::StopDesktop,
        ServiceMessageType::RestartServer => {
            let restart = message
                .restart_server
                .ok_or("missing restart_server payload")?;
            Command::RestartDesktop(restart.into())
        }
        ServiceMessageType::HeartBeat => {
            let heart_beat = message.heart_beat.ok_or("missing heart_beat payload")?;
            Command::HeartBeat {
                index: heart_beat.index,
                from: heart_beat.from,
                logical_sessions_json: heart_beat.logical_sessions_json,
            }
        }
        ServiceMessageType::AuthInfo => return Err("panel node authorization is retired".into()),
        ServiceMessageType::ReqCtrlAltDelete => {
            let request = message
                .req_ctrl_alt_delete
                .ok_or("missing req_ctrl_alt_delete payload")?;
            Command::CtrlAltDelete {
                req_device_id: request.req_device_id,
                req_stream_id: request.req_stream_id,
            }
        }
        ServiceMessageType::VirtualDisplayRequest => {
            let request = message
                .virtual_display_request
                .ok_or("missing virtual_display_request payload")?;
            if request.request_id.trim().is_empty() {
                return Err("virtual display request_id is empty".to_string());
            }
            let operation = VirtualDisplayOperation::try_from(request.operation)
                .map_err(|_| "unknown virtual display operation")?;
            Command::VirtualDisplay {
                request_id: request.request_id,
                operation,
                width: request.width,
                height: request.height,
                refresh_hz: request.refresh_hz,
            }
        }
        ServiceMessageType::HeartBeatResp => {
            return Err("heart_beat_resp is outbound only".to_string())
        }
        ServiceMessageType::VirtualDisplayResult => {
            return Err("virtual_display_result is outbound only".to_string())
        }
        ServiceMessageType::AppInstanceReady => {
            return Err("app_instance_ready is handled by the service runtime".to_string())
        }
        ServiceMessageType::FrontendAdmissionRequest => {
            let request = message
                .frontend_admission_request
                .ok_or("missing frontend_admission_request payload")?;
            Command::AdmitFrontend {
                request_id: request.request_id,
                session_id: request.session_id,
                revision: request.revision,
                frontend_token: request.frontend_token,
            }
        }
        ServiceMessageType::FrontendAdmissionResult => {
            return Err("frontend_admission_result is outbound only".to_string())
        }
        ServiceMessageType::ResourceChannelOpenRequest => {
            let request = message
                .resource_channel_open_request
                .ok_or("missing resource_channel_open_request payload")?;
            Command::OpenResourceChannel {
                request_id: request.request_id,
                source_id: request.source_id,
                session_id: request.session_id,
                channel_kind: crate::proto::ResourceChannelKind::try_from(request.channel_kind)
                    .map_err(|_| "unknown resource channel kind")?,
            }
        }
        ServiceMessageType::ResourceChannelOpenResult => {
            return Err("resource_channel_open_result is outbound only".to_string())
        }
        ServiceMessageType::ResourceChannelReportRequest => {
            let request = message
                .resource_channel_report_request
                .ok_or("missing resource_channel_report_request payload")?;
            Command::ReportResourceChannel {
                request_id: request.request_id,
                channel_id: request.channel_id,
                sequence: request.sequence,
                sent_bytes: request.sent_bytes,
                received_bytes: request.received_bytes,
                elapsed_ms: request.elapsed_ms,
                outcome: crate::proto::ResourceChannelOutcome::try_from(request.outcome)
                    .map_err(|_| "unknown resource channel outcome")?,
            }
        }
        ServiceMessageType::ResourceChannelReportResult => {
            return Err("resource_channel_report_result is outbound only".to_string())
        }
    };
    Ok(DispatchResult { command })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::proto::{
        encode_service_message, MsgAuthInfo, MsgFrontendAdmissionRequest, MsgHeartBeat,
        MsgReqCtrlAltDelete, MsgResourceChannelOpenRequest, MsgResourceChannelReportRequest,
        MsgRestartServer, MsgStartServer, MsgVirtualDisplayRequest, ResourceChannelKind,
        ResourceChannelOutcome, ServiceMessage,
    };

    #[test]
    fn dispatch_start_server() {
        let bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::StartServer as i32,
            start_server: Some(MsgStartServer {
                work_dir: "D:/app".to_string(),
                app_path: "D:/app/px_render.exe".to_string(),
                args: vec!["--app_mode=desktop".to_string()],
            }),
            ..Default::default()
        });
        let result = dispatch_message(&bytes).unwrap();
        assert!(matches!(result.command, Command::StartDesktop(_)));
    }

    #[test]
    fn dispatch_restart_server() {
        let bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::RestartServer as i32,
            restart_server: Some(MsgRestartServer {
                work_dir: "D:/app".to_string(),
                app_path: "D:/app/px_render.exe".to_string(),
                args: vec!["--app_mode=desktop".to_string()],
            }),
            ..Default::default()
        });
        let result = dispatch_message(&bytes).unwrap();
        assert!(matches!(result.command, Command::RestartDesktop(_)));
    }

    #[test]
    fn dispatch_heartbeat() {
        let bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::HeartBeat as i32,
            heart_beat: Some(MsgHeartBeat {
                index: 42,
                from: "panel".to_string(),
                ..Default::default()
            }),
            ..Default::default()
        });
        let result = dispatch_message(&bytes).unwrap();
        assert_eq!(
            result.command,
            Command::HeartBeat {
                index: 42,
                from: "panel".to_string(),
                logical_sessions_json: String::new(),
            }
        );
    }

    #[test]
    fn dispatch_heartbeat_ignores_retired_panel_auth_info() {
        let auth_info = MsgAuthInfo {
            device_id: "dev-1".to_string(),
            appkey: "ak-1".to_string(),
            console_host: "console.example.com".to_string(),
            console_port: 443,
            ..Default::default()
        };
        let bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::HeartBeat as i32,
            heart_beat: Some(MsgHeartBeat {
                index: 1,
                from: "panel".to_string(),
                auth_info: Some(auth_info.clone()),
                logical_sessions_json: String::new(),
            }),
            ..Default::default()
        });
        let result = dispatch_message(&bytes).unwrap();
        assert_eq!(
            result.command,
            Command::HeartBeat {
                index: 1,
                from: "panel".to_string(),
                logical_sessions_json: String::new(),
            }
        );
    }

    #[test]
    fn dispatch_auth_info() {
        let auth_info = MsgAuthInfo {
            device_id: "dev-1".to_string(),
            appkey: "ak-1".to_string(),
            console_host: "console.example.com".to_string(),
            console_port: 8443,
            ..Default::default()
        };
        let bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::AuthInfo as i32,
            auth_info: Some(auth_info.clone()),
            ..Default::default()
        });
        assert!(dispatch_message(&bytes).is_err());
    }

    #[test]
    fn dispatch_ctrl_alt_delete() {
        let bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::ReqCtrlAltDelete as i32,
            req_ctrl_alt_delete: Some(MsgReqCtrlAltDelete {
                req_device_id: "dev".to_string(),
                req_stream_id: "stream".to_string(),
            }),
            ..Default::default()
        });
        let result = dispatch_message(&bytes).unwrap();
        assert_eq!(
            result.command,
            Command::CtrlAltDelete {
                req_device_id: "dev".to_string(),
                req_stream_id: "stream".to_string()
            }
        );
    }

    #[test]
    fn dispatch_virtual_display_request() {
        let bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::VirtualDisplayRequest as i32,
            virtual_display_request: Some(MsgVirtualDisplayRequest {
                request_id: "vd-1".to_string(),
                operation: VirtualDisplayOperation::Create as i32,
                width: 1920,
                height: 1080,
                refresh_hz: 60,
            }),
            ..Default::default()
        });
        let result = dispatch_message(&bytes).unwrap();
        assert_eq!(
            result.command,
            Command::VirtualDisplay {
                request_id: "vd-1".to_string(),
                operation: VirtualDisplayOperation::Create,
                width: 1920,
                height: 1080,
                refresh_hz: 60,
            }
        );
    }

    #[test]
    fn dispatch_frontend_admission_request() {
        let bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::FrontendAdmissionRequest as i32,
            frontend_admission_request: Some(MsgFrontendAdmissionRequest {
                request_id: "admission-1".to_string(),
                session_id: "01994ddb-b930-7480-a15d-0a5176d1cc61".to_string(),
                revision: 3,
                frontend_token: "single-use-secret".to_string(),
            }),
            ..Default::default()
        });
        let result = dispatch_message(&bytes).unwrap();
        assert_eq!(
            result.command,
            Command::AdmitFrontend {
                request_id: "admission-1".to_string(),
                session_id: "01994ddb-b930-7480-a15d-0a5176d1cc61".to_string(),
                revision: 3,
                frontend_token: "single-use-secret".to_string(),
            }
        );
    }

    #[test]
    fn frontend_admission_result_is_outbound_only() {
        let bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::FrontendAdmissionResult as i32,
            ..Default::default()
        });
        assert_eq!(
            dispatch_message(&bytes).unwrap_err(),
            "frontend_admission_result is outbound only"
        );
    }

    #[test]
    fn dispatch_resource_channel_operations() {
        let open = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::ResourceChannelOpenRequest as i32,
            resource_channel_open_request: Some(MsgResourceChannelOpenRequest {
                request_id: "open-1".into(),
                source_id: "01994ddb-b930-7480-a15d-0a5176d1cc61".into(),
                session_id: "01994ddb-b930-7480-a15d-0a5176d1cc62".into(),
                channel_kind: ResourceChannelKind::Media as i32,
            }),
            ..Default::default()
        });
        assert_eq!(
            dispatch_message(&open).unwrap().command,
            Command::OpenResourceChannel {
                request_id: "open-1".into(),
                source_id: "01994ddb-b930-7480-a15d-0a5176d1cc61".into(),
                session_id: "01994ddb-b930-7480-a15d-0a5176d1cc62".into(),
                channel_kind: ResourceChannelKind::Media,
            }
        );

        let report = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::ResourceChannelReportRequest as i32,
            resource_channel_report_request: Some(MsgResourceChannelReportRequest {
                request_id: "report-1".into(),
                channel_id: "01994ddb-b930-7480-a15d-0a5176d1cc63".into(),
                sequence: 2,
                sent_bytes: 100,
                received_bytes: 25,
                elapsed_ms: 500,
                outcome: ResourceChannelOutcome::TransportLost as i32,
            }),
            ..Default::default()
        });
        assert_eq!(
            dispatch_message(&report).unwrap().command,
            Command::ReportResourceChannel {
                request_id: "report-1".into(),
                channel_id: "01994ddb-b930-7480-a15d-0a5176d1cc63".into(),
                sequence: 2,
                sent_bytes: 100,
                received_bytes: 25,
                elapsed_ms: 500,
                outcome: ResourceChannelOutcome::TransportLost,
            }
        );
    }
}
