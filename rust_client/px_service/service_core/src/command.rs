use crate::proto::{decode_service_message, MsgAuthInfo, ServiceMessageType};
use crate::state::RenderLaunchSpec;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Command {
    StartDesktop(RenderLaunchSpec),
    StopDesktop,
    RestartDesktop(RenderLaunchSpec),
    HeartBeat {
        index: i64,
        from: String,
        /// Panel piggybacks its latest authorization info on heartbeats.
        auth_info: Option<MsgAuthInfo>,
    },
    AuthInfo(MsgAuthInfo),
    CtrlAltDelete {
        req_device_id: String,
        req_stream_id: String,
    },
    RedeemConnectionTicket {
        request_id: String,
        ticket: String,
        client_nonce: String,
        instance_id: String,
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
                auth_info: heart_beat.auth_info,
            }
        }
        ServiceMessageType::AuthInfo => {
            let auth_info = message.auth_info.ok_or("missing auth_info payload")?;
            Command::AuthInfo(auth_info)
        }
        ServiceMessageType::ReqCtrlAltDelete => {
            let request = message
                .req_ctrl_alt_delete
                .ok_or("missing req_ctrl_alt_delete payload")?;
            Command::CtrlAltDelete {
                req_device_id: request.req_device_id,
                req_stream_id: request.req_stream_id,
            }
        }
        ServiceMessageType::RedeemConnectionTicket => {
            let request = message
                .redeem_connection_ticket
                .ok_or("missing redeem_connection_ticket payload")?;
            Command::RedeemConnectionTicket {
                request_id: request.request_id,
                ticket: request.ticket,
                client_nonce: request.client_nonce,
                instance_id: request.instance_id,
            }
        }
        ServiceMessageType::HeartBeatResp => {
            return Err("heart_beat_resp is outbound only".to_string())
        }
        ServiceMessageType::RedeemConnectionTicketResp => {
            return Err("redeem_connection_ticket_resp is outbound only".to_string())
        }
    };
    Ok(DispatchResult { command })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::proto::{
        encode_service_message, MsgAuthInfo, MsgHeartBeat, MsgReqCtrlAltDelete, MsgRestartServer,
        MsgStartServer, ServiceMessage,
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
                auth_info: None,
            }
        );
    }

    #[test]
    fn dispatch_heartbeat_carries_auth_info() {
        let auth_info = MsgAuthInfo {
            device_id: "dev-1".to_string(),
            appkey: "ak-1".to_string(),
            cms_host: "cms.example.com".to_string(),
            cms_port: 443,
            ..Default::default()
        };
        let bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::HeartBeat as i32,
            heart_beat: Some(MsgHeartBeat {
                index: 1,
                from: "panel".to_string(),
                auth_info: Some(auth_info.clone()),
            }),
            ..Default::default()
        });
        let result = dispatch_message(&bytes).unwrap();
        assert_eq!(
            result.command,
            Command::HeartBeat {
                index: 1,
                from: "panel".to_string(),
                auth_info: Some(auth_info),
            }
        );
    }

    #[test]
    fn dispatch_auth_info() {
        let auth_info = MsgAuthInfo {
            device_id: "dev-1".to_string(),
            appkey: "ak-1".to_string(),
            cms_host: "cms.example.com".to_string(),
            cms_port: 8443,
            ..Default::default()
        };
        let bytes = encode_service_message(&ServiceMessage {
            r#type: ServiceMessageType::AuthInfo as i32,
            auth_info: Some(auth_info.clone()),
            ..Default::default()
        });
        let result = dispatch_message(&bytes).unwrap();
        assert_eq!(result.command, Command::AuthInfo(auth_info));
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
}
