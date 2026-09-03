use crate::process::{ProcessKind, ProcessSnapshot};
use crate::proto::{
    MsgAuthInfo, MsgHeartBeatResp, MsgRestartServer, MsgStartServer, RenderStatus, ServiceMessage,
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
    /// Consecutive failed restart attempts (informational; the retry
    /// interval is fixed, retries never stop).
    pub consecutive_restart_failures: u32,
    /// Latest authorization info pushed by the panel (via heartbeat or a
    /// standalone AuthInfo message); drives the Console client connection.
    pub last_auth_info: Option<MsgAuthInfo>,
    /// Last time an application-level heartbeat (`from = "render_*"`) was
    /// received from the desktop render. Drives hung-render detection: the
    /// process may be alive while its message loop is dead.
    pub last_render_heartbeat: Option<std::time::Instant>,
    /// Latest reliable snapshot received from the desktop Render. Console
    /// owns interpretation/auditing; Service only forwards this value.
    pub logical_sessions_json: String,
    /// When the current desktop render was launched (or first observed).
    /// Used to grant a startup grace period before hung detection kicks in.
    pub desktop_started_at: Option<std::time::Instant>,
}

/// Grace period after a (re)launch during which a missing render heartbeat
/// is tolerated (process start, plugin load, first connection take time).
pub const RENDER_STARTUP_GRACE: std::time::Duration = std::time::Duration::from_secs(60);
/// Max silence from a running render before it is considered hung. The render
/// heartbeats every 1s via RenderServiceClient, so 30s of silence means the
/// main loop is dead even though the process still exists.
pub const RENDER_HEARTBEAT_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(30);

impl ServiceState {
    pub fn update_desktop_launch(&mut self, launch: RenderLaunchSpec) {
        self.last_desktop_launch = Some(launch);
        // A fresh launch resets the hung-detection baseline: new process gets
        // a full startup grace period and its heartbeat starts from scratch.
        self.desktop_started_at = Some(std::time::Instant::now());
        self.last_render_heartbeat = None;
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
        // The render may already be running when the service (re)starts (or
        // the launch record was restored from disk); treat first observation
        // as its start time so it still gets a startup grace period.
        if self.desktop_alive && self.desktop_started_at.is_none() {
            self.desktop_started_at = Some(std::time::Instant::now());
        }
        if !self.desktop_alive {
            self.desktop_started_at = None;
            self.last_render_heartbeat = None;
        }
    }

    /// Record an application-level heartbeat arriving from the render process.
    pub fn note_render_heartbeat(&mut self) {
        self.last_render_heartbeat = Some(std::time::Instant::now());
    }

    /// True when the render process exists but its application-level heartbeat
    /// has stopped (main loop hung) past the startup grace period.
    pub fn render_hung(&self) -> bool {
        if self.stop_requested || !self.desktop_alive || self.last_desktop_launch.is_none() {
            return false;
        }
        if let Some(started_at) = self.desktop_started_at {
            if started_at.elapsed() < RENDER_STARTUP_GRACE {
                return false;
            }
        }
        match self.last_render_heartbeat {
            Some(ts) => ts.elapsed() > RENDER_HEARTBEAT_TIMEOUT,
            // Past the grace period and still no heartbeat at all.
            None => true,
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

    /// Record a failed monitor-loop restart attempt. The retry interval is
    /// fixed (no exponential backoff); the failure count is informational.
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
    /// Fixed 3s interval, never grows and never gives up (2026-08-08:
    /// exponential backoff removed per product requirement).
    /// `None` means a restart may be attempted immediately.
    pub fn restart_backoff_remaining(&self) -> Option<std::time::Duration> {
        let last_attempt = self.last_restart_attempt?;
        let delay = std::time::Duration::from_secs(3);
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
            ProcessSnapshot::new(1, "px_render.exe", "--app_mode=inner"),
            ProcessSnapshot::new(2, "px_render.exe", "--app_mode=desktop"),
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
            app_path: "D:/app/px_render.exe".to_string(),
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
        assert_eq!(
            response.message_type(),
            Some(ServiceMessageType::HeartBeatResp)
        );
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
            app_path: "D:/app/px_render.exe".to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        state.update_processes(&[ProcessSnapshot::new(
            1,
            "px_render.exe",
            "--app_mode=desktop",
        )]);
        assert!(state.should_restart_user_proxy());
    }

    #[test]
    fn restart_backoff_is_fixed_regardless_of_failures() {
        let mut state = ServiceState::default();
        assert!(state.restart_backoff_remaining().is_none());
        for _ in 0..5 {
            state.note_restart_failure();
        }
        let remaining = state
            .restart_backoff_remaining()
            .expect("cooldown after failures");
        // Fixed 3s interval: never grows with the failure count.
        assert!(
            remaining <= std::time::Duration::from_secs(3),
            "retry interval must stay fixed: {remaining:?}"
        );
        assert!(!remaining.is_zero());
    }

    #[test]
    fn restart_backoff_never_caps_or_gives_up() {
        let mut state = ServiceState::default();
        state.consecutive_restart_failures = 30;
        state.last_restart_attempt = Some(std::time::Instant::now());
        let remaining = state.restart_backoff_remaining().expect("cooldown");
        assert!(remaining <= std::time::Duration::from_secs(3));
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

    fn hung_test_state() -> ServiceState {
        let mut state = ServiceState::default();
        state.update_desktop_launch(RenderLaunchSpec {
            work_dir: "D:/app".to_string(),
            app_path: "D:/app/px_render.exe".to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        state.update_processes(&[ProcessSnapshot::new(
            1,
            "px_render.exe",
            "--app_mode=desktop",
        )]);
        state
    }

    #[test]
    fn render_not_hung_within_startup_grace() {
        let state = hung_test_state();
        // No heartbeat yet, but still inside the startup grace period.
        assert!(!state.render_hung());
    }

    #[test]
    fn render_hung_when_no_heartbeat_after_grace() {
        let mut state = hung_test_state();
        state.desktop_started_at = Some(
            std::time::Instant::now() - RENDER_STARTUP_GRACE - std::time::Duration::from_secs(1),
        );
        assert!(state.render_hung());
    }

    #[test]
    fn render_not_hung_with_fresh_heartbeat() {
        let mut state = hung_test_state();
        state.desktop_started_at = Some(
            std::time::Instant::now() - RENDER_STARTUP_GRACE - std::time::Duration::from_secs(1),
        );
        state.note_render_heartbeat();
        assert!(!state.render_hung());
    }

    #[test]
    fn render_hung_with_stale_heartbeat() {
        let mut state = hung_test_state();
        state.desktop_started_at = Some(
            std::time::Instant::now() - RENDER_STARTUP_GRACE - std::time::Duration::from_secs(1),
        );
        state.last_render_heartbeat = Some(
            std::time::Instant::now()
                - RENDER_HEARTBEAT_TIMEOUT
                - std::time::Duration::from_secs(1),
        );
        assert!(state.render_hung());
    }

    #[test]
    fn render_hung_requires_alive_launch_and_no_stop() {
        let mut state = hung_test_state();
        state.desktop_started_at = Some(
            std::time::Instant::now() - RENDER_STARTUP_GRACE - std::time::Duration::from_secs(1),
        );
        // Explicit stop must suppress hung detection.
        state.stop_requested = true;
        assert!(!state.render_hung());
        state.stop_requested = false;
        // Process gone -> not "hung", the normal restart path handles it.
        state.update_processes(&[]);
        assert!(!state.render_hung());
        // Launch record cleared (explicit stop) -> no hung detection either.
        let mut state = hung_test_state();
        state.desktop_started_at = Some(
            std::time::Instant::now() - RENDER_STARTUP_GRACE - std::time::Duration::from_secs(1),
        );
        state.last_desktop_launch = None;
        assert!(!state.render_hung());
    }

    #[test]
    fn new_launch_resets_hung_baseline() {
        let mut state = hung_test_state();
        state.last_render_heartbeat = Some(
            std::time::Instant::now()
                - RENDER_HEARTBEAT_TIMEOUT
                - std::time::Duration::from_secs(1),
        );
        state.update_desktop_launch(RenderLaunchSpec {
            work_dir: "D:/app".to_string(),
            app_path: "D:/app/px_render.exe".to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        assert!(state.last_render_heartbeat.is_none());
        assert!(!state.render_hung());
    }
}
