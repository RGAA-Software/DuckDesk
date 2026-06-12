use crate::process::{ProcessKind, ProcessSnapshot};
use crate::proto::{
    MsgHeartBeatResp, MsgRestartServer, MsgStartServer, RenderStatus, ServiceMessage,
    ServiceMessageType,
};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RenderLaunchSpec {
    pub work_dir: String,
    pub app_path: String,
    pub args: Vec<String>,
}

impl RenderLaunchSpec {
    pub fn desktop_mode(&self) -> bool {
        self.args.iter().any(|arg| arg == "--app_mode=desktop")
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct ServiceState {
    pub last_desktop_launch: Option<RenderLaunchSpec>,
    pub desktop_alive: bool,
    pub desktop_pid: Option<u32>,
    pub stop_requested: bool,
}

impl ServiceState {
    pub fn update_desktop_launch(&mut self, launch: RenderLaunchSpec) {
        self.last_desktop_launch = Some(launch);
    }

    pub fn update_processes(&mut self, processes: &[ProcessSnapshot]) {
        self.desktop_alive = false;
        self.desktop_pid = None;
        for process in processes {
            if process.kind() == ProcessKind::DesktopRender {
                self.desktop_alive = true;
                self.desktop_pid = Some(process.pid);
                return;
            }
        }
    }

    pub fn should_restart_desktop(&self) -> bool {
        !self.stop_requested && !self.desktop_alive && self.last_desktop_launch.is_some()
    }

    pub fn heartbeat_response(&self, index: i64) -> ServiceMessage {
        ServiceMessage {
            r#type: ServiceMessageType::HeartBeatResp as i32,
            heart_beat_resp: Some(MsgHeartBeatResp {
                index,
                render_status: if self.desktop_alive {
                    RenderStatus::Working as i32
                } else {
                    RenderStatus::Stopped as i32
                },
            }),
            ..Default::default()
        }
    }
}

impl From<MsgStartServer> for RenderLaunchSpec {
    fn from(value: MsgStartServer) -> Self {
        Self {
            work_dir: value.work_dir,
            app_path: value.app_path,
            args: value.args,
        }
    }
}

impl From<MsgRestartServer> for RenderLaunchSpec {
    fn from(value: MsgRestartServer) -> Self {
        Self {
            work_dir: value.work_dir,
            app_path: value.app_path,
            args: value.args,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn state_detects_desktop_render() {
        let mut state = ServiceState::default();
        state.update_processes(&[
            ProcessSnapshot::new(1, "GammaRayRender.exe", "--app_mode=inner"),
            ProcessSnapshot::new(2, "GammaRayRender.exe", "--app_mode=desktop"),
        ]);
        assert!(state.desktop_alive);
        assert_eq!(state.desktop_pid, Some(2));
        assert!(!state.should_restart_desktop());
    }

    #[test]
    fn state_requests_restart_when_saved_launch_missing() {
        let mut state = ServiceState::default();
        state.update_desktop_launch(RenderLaunchSpec {
            work_dir: "D:/app".to_string(),
            app_path: "D:/app/GammaRayRender.exe".to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        state.update_processes(&[]);
        assert!(state.should_restart_desktop());
    }

    #[test]
    fn heartbeat_uses_current_alive_state() {
        let mut state = ServiceState::default();
        state.desktop_alive = true;
        let response = state.heartbeat_response(7);
        assert_eq!(response.message_type(), Some(ServiceMessageType::HeartBeatResp));
        assert_eq!(
            response.heart_beat_resp.unwrap().render_status_enum(),
            Some(RenderStatus::Working)
        );
    }
}
