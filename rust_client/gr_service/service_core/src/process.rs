use crate::config::{RENDER_EXE_NAME, USER_PROXY_EXE_NAME};

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum RenderMode {
    Desktop,
    Inner,
    GameHook,
    Unknown,
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum ProcessKind {
    DesktopRender,
    InnerRender,
    GameHookRender,
    Other,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProcessSnapshot {
    pub pid: u32,
    pub exe_path: String,
    pub cmdline: String,
}

impl ProcessSnapshot {
    pub fn new(pid: u32, exe_path: impl Into<String>, cmdline: impl Into<String>) -> Self {
        Self {
            pid,
            exe_path: exe_path.into(),
            cmdline: cmdline.into(),
        }
    }

    pub fn render_mode(&self) -> RenderMode {
        if self.cmdline.contains("--app_mode=desktop") {
            RenderMode::Desktop
        } else if self.cmdline.contains("--app_mode=game-hook") {
            RenderMode::GameHook
        } else if self.cmdline.contains("--app_mode=inner") {
            // Legacy alias; prefer game-hook for CMS-scheduled apps.
            RenderMode::Inner
        } else {
            RenderMode::Unknown
        }
    }

    pub fn is_render_process(&self) -> bool {
        self.exe_path
            .rsplit(['\\', '/'])
            .next()
            .map(|name| name.eq_ignore_ascii_case(RENDER_EXE_NAME))
            .unwrap_or(false)
    }

    pub fn is_user_proxy_process(&self) -> bool {
        self.exe_path
            .rsplit(['\\', '/'])
            .next()
            .map(|name| name.eq_ignore_ascii_case(USER_PROXY_EXE_NAME))
            .unwrap_or(false)
    }

    pub fn is_managed_clipboard_process(&self) -> bool {
        self.is_render_process() || self.is_user_proxy_process()
    }

    pub fn kind(&self) -> ProcessKind {
        if !self.is_render_process() {
            return ProcessKind::Other;
        }
        match self.render_mode() {
            RenderMode::Desktop => ProcessKind::DesktopRender,
            RenderMode::Inner => ProcessKind::InnerRender,
            RenderMode::GameHook => ProcessKind::GameHookRender,
            RenderMode::Unknown => ProcessKind::Other,
        }
    }

    /// Managed by CMS app-instance stop (never desktop).
    pub fn is_game_hook_render_process(&self) -> bool {
        self.kind() == ProcessKind::GameHookRender
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detect_desktop_mode() {
        let process = ProcessSnapshot::new(1, "D:/GammaRayRender.exe", "--app_mode=desktop");
        assert!(process.is_render_process());
        assert_eq!(process.render_mode(), RenderMode::Desktop);
        assert_eq!(process.kind(), ProcessKind::DesktopRender);
    }

    #[test]
    fn detect_inner_mode() {
        let process = ProcessSnapshot::new(1, "D:/GammaRayRender.exe", "--app_mode=inner");
        assert_eq!(process.kind(), ProcessKind::InnerRender);
    }

    #[test]
    fn detect_game_hook_mode() {
        let process = ProcessSnapshot::new(
            1,
            "D:/GammaRayRender.exe",
            "--app_mode=game-hook --network_listen_port=32000",
        );
        assert_eq!(process.render_mode(), RenderMode::GameHook);
        assert_eq!(process.kind(), ProcessKind::GameHookRender);
        assert!(process.is_game_hook_render_process());
        assert!(!ProcessSnapshot::new(2, "D:/GammaRayRender.exe", "--app_mode=desktop")
            .is_game_hook_render_process());
    }

    #[test]
    fn non_render_process_is_other() {
        let process = ProcessSnapshot::new(1, "D:/GammaRay.exe", "");
        assert!(!process.is_render_process());
        assert_eq!(process.kind(), ProcessKind::Other);
    }

    #[test]
    fn detect_user_proxy_process() {
        let process = ProcessSnapshot::new(1, "D:/GammaRayUserProxy.exe", "--render-port=20371");
        assert!(process.is_user_proxy_process());
        assert!(process.is_managed_clipboard_process());
    }
}
