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
    pub user_proxy_alive: bool,
    pub stop_requested: bool,
    /// Last time a monitor-loop restart was attempted (shared by the desktop
    /// render and the user proxy restarts).
    pub last_restart_attempt: Option<std::time::Instant>,
    /// Consecutive failed restart attempts, drives the exponential backoff.
    pub consecutive_restart_failures: u32,
}

impl ServiceState {
    pub fn update_desktop_launch(&mut self, launch: RenderLaunchSpec) {
        self.last_desktop_launch = Some(launch);
    }

    pub fn update_processes(&mut self, processes: &[ProcessSnapshot]) {
        self.desktop_alive = false;
        self.desktop_pid = None;
        self.user_proxy_alive = false;
        for process in processes {
            if process.kind() == ProcessKind::DesktopRender {
                self.desktop_alive = true;
                self.desktop_pid = Some(process.pid);
            }
            if process.is_user_proxy_process() {
                self.user_proxy_alive = true;
            }
        }
    }

    pub fn should_restart_user_proxy(&self) -> bool {
        !self.stop_requested
            && self.desktop_alive
            && !self.user_proxy_alive
            && self.last_desktop_launch.is_some()
    }

    pub fn should_restart_desktop(&self) -> bool {
        !self.stop_requested && !self.desktop_alive && self.last_desktop_launch.is_some()
    }

    /// Record a failed monitor-loop restart attempt; failures drive the
    /// exponential backoff in `restart_backoff_remaining`.
    pub fn note_restart_failure(&mut self) {
        self.last_restart_attempt = Some(std::time::Instant::now());
        self.consecutive_restart_failures = self.consecutive_restart_failures.saturating_add(1);
    }

    /// Clear the restart backoff once the desktop render is observed alive.
    pub fn reset_restart_backoff(&mut self) {
        self.last_restart_attempt = None;
        self.consecutive_restart_failures = 0;
    }

    /// Remaining cooldown before the next restart attempt is allowed.
    /// Base 3s, doubled per consecutive failure, capped at 5 minutes.
    /// `None` means a restart may be attempted immediately.
    pub fn restart_backoff_remaining(&self) -> Option<std::time::Duration> {
        let last_attempt = self.last_restart_attempt?;
        let shift = self.consecutive_restart_failures.min(7);
        let delay = (std::time::Duration::from_secs(3) * 2u32.pow(shift))
            .min(std::time::Duration::from_secs(300));
        let remaining = delay.saturating_sub(last_attempt.elapsed());
        if remaining.is_zero() {
            None
        } else {
            Some(remaining)
        }
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

    #[test]
    fn state_requests_user_proxy_restart_when_render_alive() {
        let mut state = ServiceState::default();
        state.update_desktop_launch(RenderLaunchSpec {
            work_dir: "D:/app".to_string(),
            app_path: "D:/app/GammaRayRender.exe".to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        state.update_processes(&[ProcessSnapshot::new(
            1,
            "GammaRayRender.exe",
            "--app_mode=desktop",
        )]);
        assert!(state.should_restart_user_proxy());
    }

    #[test]
    fn restart_backoff_grows_with_consecutive_failures() {
        let mut state = ServiceState::default();
        assert!(state.restart_backoff_remaining().is_none());
        state.note_restart_failure();
        let first = state
            .restart_backoff_remaining()
            .expect("backoff after first failure");
        state.note_restart_failure();
        let second = state
            .restart_backoff_remaining()
            .expect("backoff after second failure");
        assert!(
            second > first,
            "backoff must grow with failures: {first:?} -> {second:?}"
        );
    }

    #[test]
    fn restart_backoff_is_capped_at_five_minutes() {
        let mut state = ServiceState::default();
        state.consecutive_restart_failures = 30;
        state.last_restart_attempt = Some(std::time::Instant::now());
        let remaining = state.restart_backoff_remaining().expect("backoff");
        assert!(remaining <= std::time::Duration::from_secs(300));
    }

    #[test]
    fn restart_backoff_resets_after_render_observed_alive() {
        let mut state = ServiceState::default();
        state.note_restart_failure();
        state.note_restart_failure();
        assert!(state.restart_backoff_remaining().is_some());
        state.reset_restart_backoff();
        assert!(state.restart_backoff_remaining().is_none());
        assert_eq!(state.consecutive_restart_failures, 0);
    }
}
