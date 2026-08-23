use crate::config::{RENDER_EXE_NAME, USER_PROXY_EXE_NAME};

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum RenderMode {
    Desktop,
    Inner,
    GameHook,
    Webview,
    Unknown,
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum ProcessKind {
    DesktopRender,
    InnerRender,
    GameHookRender,
    WebviewRender,
    Other,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProcessSnapshot {
    pub pid: u32,
    pub exe_path: String,
    pub cmdline: String,
    /// WMI ParentProcessId when available.
    pub parent_pid: Option<u32>,
}

impl ProcessSnapshot {
    pub fn new(pid: u32, exe_path: impl Into<String>, cmdline: impl Into<String>) -> Self {
        Self {
            pid,
            exe_path: exe_path.into(),
            cmdline: cmdline.into(),
            parent_pid: None,
        }
    }

    pub fn with_parent(mut self, parent_pid: u32) -> Self {
        self.parent_pid = Some(parent_pid);
        self
    }

    pub fn exe_name(&self) -> &str {
        self.exe_path
            .rsplit(['\\', '/'])
            .next()
            .unwrap_or(self.exe_path.as_str())
    }

    pub fn render_mode(&self) -> RenderMode {
        if self.cmdline.contains("--app_mode=desktop") {
            RenderMode::Desktop
        } else if self.cmdline.contains("--app_mode=game-hook") {
            RenderMode::GameHook
        } else if self.cmdline.contains("--app_mode=webview") {
            RenderMode::Webview
        } else if self.cmdline.contains("--app_mode=inner") {
            // Legacy alias; prefer game-hook for Console-scheduled apps.
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
            RenderMode::Webview => ProcessKind::WebviewRender,
            RenderMode::Unknown => ProcessKind::Other,
        }
    }

    /// Managed by Console app-instance stop (never desktop).
    pub fn is_game_hook_render_process(&self) -> bool {
        self.kind() == ProcessKind::GameHookRender
    }

    /// Console application root render. CEF children also use px_render.exe but
    /// carry `--type=...`; never treat them as independently managed roots.
    pub fn is_app_instance_render_process(&self) -> bool {
        !self.cmdline.contains("--type=")
            && matches!(
                self.kind(),
                ProcessKind::GameHookRender | ProcessKind::WebviewRender
            )
    }

    pub fn exe_path_eq(&self, other: &str) -> bool {
        normalize_exe_key(&self.exe_path) == normalize_exe_key(other)
    }
}

fn normalize_exe_key(path: &str) -> String {
    path.replace('/', "\\").to_ascii_lowercase()
}

/// Root pid + all descendants (by parent_pid), children first then root.
pub fn collect_process_tree(processes: &[ProcessSnapshot], root_pid: u32) -> Vec<u32> {
    let mut children: Vec<u32> = Vec::new();
    let mut stack = vec![root_pid];
    let mut seen = std::collections::HashSet::from([root_pid]);
    while let Some(pid) = stack.pop() {
        for p in processes {
            if p.parent_pid == Some(pid) && seen.insert(p.pid) {
                children.push(p.pid);
                stack.push(p.pid);
            }
        }
    }
    // Kill descendants before the root process.
    children.push(root_pid);
    children
}

/// Match running processes to the absolute game exe path for this instance.
pub fn find_pids_for_game_exe(processes: &[ProcessSnapshot], game_path: &str) -> Vec<u32> {
    let key = normalize_exe_key(game_path);
    processes
        .iter()
        .filter(|p| normalize_exe_key(&p.exe_path) == key)
        .map(|p| p.pid)
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detect_desktop_mode() {
        let process = ProcessSnapshot::new(1, "D:/px_render.exe", "--app_mode=desktop");
        assert!(process.is_render_process());
        assert_eq!(process.render_mode(), RenderMode::Desktop);
        assert_eq!(process.kind(), ProcessKind::DesktopRender);
    }

    #[test]
    fn detect_inner_mode() {
        let process = ProcessSnapshot::new(1, "D:/px_render.exe", "--app_mode=inner");
        assert_eq!(process.kind(), ProcessKind::InnerRender);
    }

    #[test]
    fn detect_game_hook_mode() {
        let process = ProcessSnapshot::new(
            1,
            "D:/px_render.exe",
            "--app_mode=game-hook --network_listen_port=32000",
        );
        assert_eq!(process.render_mode(), RenderMode::GameHook);
        assert_eq!(process.kind(), ProcessKind::GameHookRender);
        assert!(process.is_game_hook_render_process());
        assert!(
            !ProcessSnapshot::new(2, "D:/px_render.exe", "--app_mode=desktop")
                .is_game_hook_render_process()
        );
    }

    #[test]
    fn webview_root_is_managed_but_cef_children_are_not_roots() {
        let root = ProcessSnapshot::new(
            10,
            "D:/px_render.exe",
            "--app_mode=webview --network_listen_port=32002",
        );
        let renderer = ProcessSnapshot::new(
            11,
            "D:/px_render.exe",
            "--type=renderer --app_mode=webview --network_listen_port=32002",
        )
        .with_parent(10);
        assert_eq!(root.kind(), ProcessKind::WebviewRender);
        assert!(root.is_app_instance_render_process());
        assert!(!renderer.is_app_instance_render_process());
        assert_eq!(collect_process_tree(&[root, renderer], 10), vec![11, 10]);
    }

    #[test]
    fn non_render_process_is_other() {
        let process = ProcessSnapshot::new(1, "D:/px_panel.exe", "");
        assert!(!process.is_render_process());
        assert_eq!(process.kind(), ProcessKind::Other);
    }

    #[test]
    fn detect_user_proxy_process() {
        let process = ProcessSnapshot::new(1, "D:/px_function.exe", "--render-port=20371");
        assert!(process.is_user_proxy_process());
        assert!(process.is_managed_clipboard_process());
    }

    #[test]
    fn collect_process_tree_children_before_root() {
        let procs = vec![
            ProcessSnapshot::new(10, "D:/px_render.exe", "--app_mode=game-hook"),
            ProcessSnapshot::new(20, r"D:\games\game.exe", "").with_parent(10),
            ProcessSnapshot::new(21, r"D:\games\helper.exe", "").with_parent(20),
            ProcessSnapshot::new(99, r"D:\other.exe", "").with_parent(1),
        ];
        assert_eq!(collect_process_tree(&procs, 10), vec![20, 21, 10]);
    }

    #[test]
    fn find_game_exe_by_path_or_name() {
        let procs = vec![
            ProcessSnapshot::new(
                1,
                r"D:\1_test_games\CarGame\Binaries\Win64\VehicleGame-Win64-Shipping.exe",
                "",
            ),
            ProcessSnapshot::new(2, r"C:\Windows\notepad.exe", ""),
        ];
        let hits = find_pids_for_game_exe(
            &procs,
            r"D:/1_test_games/CarGame/Binaries/Win64/VehicleGame-Win64-Shipping.exe",
        );
        assert_eq!(hits, vec![1]);
    }
}
