//! CMS-scheduled game-hook app instances (see docs/cms_app_schedule_plan.md).
//! Desktop render remains a separate single-slot path in ServiceState.

use crate::config::RENDER_EXE_NAME;
use crate::state::RenderLaunchSpec;
use gr_base::crypto_util::base64_encode;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::{Path, PathBuf};

pub const APP_MODE_GAME_HOOK: &str = "game-hook";
pub const DEFAULT_ENCODER_FPS: i32 = 60;
pub const DEFAULT_ENCODER_BITRATE: i32 = 20;
pub const DEFAULT_ENCODER_FORMAT: &str = "h264";
/// Port pool when CMS sends listen_port=0.
pub const DEFAULT_PORT_RANGE_START: u16 = 32000;
pub const DEFAULT_PORT_RANGE_END: u16 = 32999;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AppInstanceState {
    Starting,
    Running,
    Stopping,
    Failed,
    Stopped,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StartAppRequest {
    pub request_id: String,
    pub instance_id: String,
    pub app_id: String,
    pub install_root: String,
    pub game_exe_rel: String,
    pub game_arguments: String,
    pub listen_port: i32,
    pub encoder_fps: i32,
    pub encoder_bitrate: i32,
    pub encoder_format: String,
    pub webrtc_enabled: bool,
    pub websocket_enabled: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AppInstanceRecord {
    pub request_id: String,
    pub instance_id: String,
    pub app_id: String,
    pub install_root: String,
    pub game_exe_rel: String,
    pub listen_port: u16,
    pub pid: Option<u32>,
    pub state: AppInstanceState,
    pub error: String,
    pub launch: RenderLaunchSpec,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct AppInstanceSummary {
    pub instance_id: String,
    pub app_id: String,
    pub listen_port: u16,
    pub pid: u32,
    pub state: String,
}

/// Join install_root + game_exe_rel into an absolute game exe path.
pub fn resolve_game_path(install_root: &str, game_exe_rel: &str) -> Result<PathBuf, String> {
    let root = install_root.trim();
    let rel = game_exe_rel.trim();
    if root.is_empty() {
        return Err("install_root is empty".to_string());
    }
    if rel.is_empty() {
        return Err("game_exe_rel is empty".to_string());
    }
    if Path::new(rel).is_absolute() {
        return Err("game_exe_rel must be relative".to_string());
    }
    // Reject path escape.
    for comp in Path::new(rel).components() {
        if matches!(comp, std::path::Component::ParentDir) {
            return Err("game_exe_rel must not contain '..'".to_string());
        }
    }
    let path = PathBuf::from(root).join(rel);
    Ok(normalize_path_display(&path))
}

fn normalize_path_display(path: &Path) -> PathBuf {
    // Keep OS path; canonicalize is not required for arg building (exe may not exist in unit tests).
    path.to_path_buf()
}

pub fn encode_game_path_b64(game_path: &Path) -> String {
    let s = game_path.to_string_lossy();
    base64_encode(&s)
}

/// Build GammaRayRender launch spec for a game-hook app instance.
pub fn build_game_hook_launch_spec(
    work_dir: impl Into<String>,
    req: &StartAppRequest,
    listen_port: u16,
    game_path: &Path,
) -> RenderLaunchSpec {
    let work_dir = work_dir.into();
    let app_path = PathBuf::from(&work_dir).join(RENDER_EXE_NAME);
    let fps = if req.encoder_fps > 0 {
        req.encoder_fps
    } else {
        DEFAULT_ENCODER_FPS
    };
    let bitrate = if req.encoder_bitrate > 0 {
        req.encoder_bitrate
    } else {
        DEFAULT_ENCODER_BITRATE
    };
    let format = if req.encoder_format.trim().is_empty() {
        DEFAULT_ENCODER_FORMAT
    } else {
        req.encoder_format.trim()
    };
    let game_b64 = encode_game_path_b64(game_path);
    let mut args = vec![
        "--logfile".to_string(),
        format!("--app_mode={APP_MODE_GAME_HOOK}"),
        format!("--app_game_path={game_b64}"),
        "--capture_video=true".to_string(),
        "--capture_video_type=inner".to_string(),
        "--capture_audio=true".to_string(),
        "--capture_audio_type=global".to_string(),
        format!("--webrtc_enabled={}", req.webrtc_enabled),
        format!("--websocket_enabled={}", req.websocket_enabled),
        format!("--encoder_fps={fps}"),
        format!("--encoder_bitrate={bitrate}"),
        format!("--encoder_format={format}"),
        format!("--network_listen_port={listen_port}"),
    ];
    if !req.game_arguments.trim().is_empty() {
        // Passed through settings/CLI if render supports it later; keep as opaque marker for now.
        args.push(format!("--app_game_arguments={}", req.game_arguments.trim()));
    }
    RenderLaunchSpec {
        work_dir,
        app_path: app_path.to_string_lossy().to_string(),
        args,
    }
}

pub fn extract_listen_port(args: &[String]) -> Option<u16> {
    for arg in args {
        if let Some(v) = arg.strip_prefix("--network_listen_port=") {
            if let Ok(p) = v.parse::<u16>() {
                return Some(p);
            }
        }
    }
    None
}

pub fn is_game_hook_launch(spec: &RenderLaunchSpec) -> bool {
    spec.args
        .iter()
        .any(|a| a == "--app_mode=game-hook" || a == &format!("--app_mode={APP_MODE_GAME_HOOK}"))
}

#[derive(Debug, Default)]
pub struct AppInstanceRegistry {
    instances: HashMap<String, AppInstanceRecord>,
    /// Ports reserved by running/starting instances.
    used_ports: HashMap<u16, String>,
    port_range_start: u16,
    port_range_end: u16,
}

impl AppInstanceRegistry {
    pub fn new() -> Self {
        Self {
            instances: HashMap::new(),
            used_ports: HashMap::new(),
            port_range_start: DEFAULT_PORT_RANGE_START,
            port_range_end: DEFAULT_PORT_RANGE_END,
        }
    }

    pub fn with_port_range(mut self, start: u16, end: u16) -> Self {
        self.port_range_start = start;
        self.port_range_end = end.max(start);
        self
    }

    pub fn get(&self, instance_id: &str) -> Option<&AppInstanceRecord> {
        self.instances.get(instance_id)
    }

    pub fn list(&self) -> Vec<&AppInstanceRecord> {
        self.instances.values().collect()
    }

    pub fn summaries(&self) -> Vec<AppInstanceSummary> {
        self.instances
            .values()
            .map(|r| AppInstanceSummary {
                instance_id: r.instance_id.clone(),
                app_id: r.app_id.clone(),
                listen_port: r.listen_port,
                pid: r.pid.unwrap_or(0),
                state: match r.state {
                    AppInstanceState::Starting => "starting",
                    AppInstanceState::Running => "running",
                    AppInstanceState::Stopping => "stopping",
                    AppInstanceState::Failed => "failed",
                    AppInstanceState::Stopped => "stopped",
                }
                .to_string(),
            })
            .collect()
    }

    pub fn instances_json(&self) -> String {
        serde_json::to_string(&self.summaries()).unwrap_or_else(|_| "[]".to_string())
    }

    /// Allocate listen_port: use preferred if >0 (error if taken); else
    /// last_used+1 (default start 32000), wrapping within the pool for free slots.
    pub fn allocate_port(&self, preferred: i32) -> Result<u16, String> {
        if preferred > 0 {
            let p = preferred as u16;
            if self.used_ports.contains_key(&p) {
                return Err(format!("listen_port {p} already in use"));
            }
            return Ok(p);
        }
        let last = self.used_ports.keys().copied().max();
        let mut candidate = match last {
            Some(p) => p.saturating_add(1).max(self.port_range_start),
            None => self.port_range_start,
        };
        if candidate > self.port_range_end {
            candidate = self.port_range_start;
        }
        let mut p = candidate;
        let span = (self.port_range_end - self.port_range_start) as usize + 1;
        for _ in 0..span {
            if !self.used_ports.contains_key(&p) {
                return Ok(p);
            }
            p = if p >= self.port_range_end {
                self.port_range_start
            } else {
                p + 1
            };
        }
        Err("no free listen_port in pool".to_string())
    }

    /// Validate request and register as Starting with a built launch spec.
    pub fn begin_start(
        &mut self,
        work_dir: &str,
        req: StartAppRequest,
    ) -> Result<&AppInstanceRecord, String> {
        if req.request_id.trim().is_empty() {
            return Err("request_id is empty".to_string());
        }
        if req.instance_id.trim().is_empty() {
            return Err("instance_id is empty".to_string());
        }
        if req.app_id.trim().is_empty() {
            return Err("app_id is empty".to_string());
        }
        if self.instances.contains_key(&req.instance_id) {
            let existing = self.instances.get(&req.instance_id).unwrap();
            if matches!(
                existing.state,
                AppInstanceState::Starting | AppInstanceState::Running
            ) {
                return Err(format!(
                    "instance_id {} already {}",
                    req.instance_id,
                    match existing.state {
                        AppInstanceState::Starting => "starting",
                        AppInstanceState::Running => "running",
                        _ => "active",
                    }
                ));
            }
        }
        let game_path = resolve_game_path(&req.install_root, &req.game_exe_rel)?;
        let port = self.allocate_port(req.listen_port)?;
        let launch = build_game_hook_launch_spec(work_dir, &req, port, &game_path);
        self.used_ports.insert(port, req.instance_id.clone());
        let record = AppInstanceRecord {
            request_id: req.request_id.clone(),
            instance_id: req.instance_id.clone(),
            app_id: req.app_id.clone(),
            install_root: req.install_root.clone(),
            game_exe_rel: req.game_exe_rel.clone(),
            listen_port: port,
            pid: None,
            state: AppInstanceState::Starting,
            error: String::new(),
            launch,
        };
        self.instances.insert(req.instance_id.clone(), record);
        Ok(self.instances.get(&req.instance_id).unwrap())
    }

    pub fn mark_running(&mut self, instance_id: &str, pid: u32) -> Result<(), String> {
        let rec = self
            .instances
            .get_mut(instance_id)
            .ok_or_else(|| format!("unknown instance_id {instance_id}"))?;
        rec.pid = Some(pid);
        rec.state = AppInstanceState::Running;
        rec.error.clear();
        Ok(())
    }

    pub fn mark_failed(&mut self, instance_id: &str, error: impl Into<String>) -> Result<(), String> {
        let rec = self
            .instances
            .get_mut(instance_id)
            .ok_or_else(|| format!("unknown instance_id {instance_id}"))?;
        let port = rec.listen_port;
        rec.state = AppInstanceState::Failed;
        rec.error = error.into();
        rec.pid = None;
        self.used_ports.remove(&port);
        Ok(())
    }

    pub fn begin_stop(&mut self, instance_id: &str) -> Result<&AppInstanceRecord, String> {
        let rec = self
            .instances
            .get_mut(instance_id)
            .ok_or_else(|| format!("unknown instance_id {instance_id}"))?;
        if matches!(rec.state, AppInstanceState::Stopped | AppInstanceState::Failed) {
            return Err(format!("instance {instance_id} already stopped/failed"));
        }
        rec.state = AppInstanceState::Stopping;
        Ok(self.instances.get(instance_id).unwrap())
    }

    pub fn mark_stopped(&mut self, instance_id: &str) -> Result<(), String> {
        let rec = self
            .instances
            .get_mut(instance_id)
            .ok_or_else(|| format!("unknown instance_id {instance_id}"))?;
        let port = rec.listen_port;
        rec.state = AppInstanceState::Stopped;
        rec.pid = None;
        self.used_ports.remove(&port);
        Ok(())
    }

    /// True if stop should kill this pid (game-hook instance), never desktop.
    pub fn should_kill_pid_for_instance(&self, instance_id: &str, pid: u32) -> bool {
        match self.instances.get(instance_id) {
            Some(rec) => rec.pid == Some(pid) && is_game_hook_launch(&rec.launch),
            None => false,
        }
    }
}

/// Client URL helper for CMS / tests.
pub fn build_web_client_url(host: &str, listen_port: u16, device_id: &str, instance_id: &str) -> String {
    format!(
        "http://{host}:{listen_port}/web_client/?deviceId={device_id}&instanceId={instance_id}"
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_req(instance_id: &str, port: i32) -> StartAppRequest {
        StartAppRequest {
            request_id: format!("req-{instance_id}"),
            instance_id: instance_id.to_string(),
            app_id: "app-car".to_string(),
            install_root: r"D:\apps\CarGame".to_string(),
            game_exe_rel: r"Binaries\Win64\VehicleGame-Win64-Shipping.exe".to_string(),
            game_arguments: "-dx11".to_string(),
            listen_port: port,
            encoder_fps: 60,
            encoder_bitrate: 20,
            encoder_format: "h264".to_string(),
            webrtc_enabled: true,
            websocket_enabled: true,
        }
    }

    #[test]
    fn resolve_game_path_joins_relative() {
        let p = resolve_game_path(r"D:\apps\CarGame", r"Binaries\Win64\game.exe").unwrap();
        assert!(p.to_string_lossy().contains("CarGame"));
        assert!(p.to_string_lossy().ends_with("game.exe"));
    }

    #[test]
    fn resolve_rejects_absolute_rel_and_parent() {
        assert!(resolve_game_path(r"D:\apps", r"D:\evil\a.exe").is_err());
        assert!(resolve_game_path(r"D:\apps", r"..\evil\a.exe").is_err());
        assert!(resolve_game_path("", "a.exe").is_err());
    }

    #[test]
    fn launch_spec_is_game_hook_with_b64_path_and_port() {
        let req = sample_req("i1", 32010);
        let game = resolve_game_path(&req.install_root, &req.game_exe_rel).unwrap();
        let spec = build_game_hook_launch_spec(r"D:\GoDesk", &req, 32010, &game);
        assert!(is_game_hook_launch(&spec));
        assert!(spec.app_path.ends_with(RENDER_EXE_NAME));
        assert_eq!(extract_listen_port(&spec.args), Some(32010));
        let b64_arg = spec
            .args
            .iter()
            .find(|a| a.starts_with("--app_game_path="))
            .unwrap();
        let b64 = b64_arg.strip_prefix("--app_game_path=").unwrap();
        let decoded = gr_base::crypto_util::base64_decode(b64).unwrap();
        assert!(decoded.contains("VehicleGame"));
        assert!(spec.args.iter().any(|a| a == "--capture_video_type=inner"));
    }

    #[test]
    fn registry_allocates_ports_and_blocks_duplicates() {
        let mut reg = AppInstanceRegistry::new().with_port_range(32000, 32002);
        let r1 = reg
            .begin_start(r"D:\GoDesk", sample_req("a", 0))
            .unwrap()
            .clone();
        assert_eq!(r1.listen_port, 32000);
        let r2 = reg
            .begin_start(r"D:\GoDesk", sample_req("b", 0))
            .unwrap()
            .clone();
        assert_eq!(r2.listen_port, 32001);
        // preferred conflict
        let err = reg
            .begin_start(r"D:\GoDesk", sample_req("c", 32000))
            .unwrap_err();
        assert!(err.contains("already in use"));
        // same instance while running
        reg.mark_running("a", 111).unwrap();
        let err = reg
            .begin_start(r"D:\GoDesk", sample_req("a", 0))
            .unwrap_err();
        assert!(err.contains("already"));
    }

    #[test]
    fn registry_multi_instance_same_app_different_ports() {
        let mut reg = AppInstanceRegistry::new();
        reg.begin_start(r"D:\GoDesk", sample_req("i1", 32100)).unwrap();
        reg.begin_start(r"D:\GoDesk", sample_req("i2", 32101)).unwrap();
        reg.mark_running("i1", 10).unwrap();
        reg.mark_running("i2", 11).unwrap();
        let sums = reg.summaries();
        assert_eq!(sums.len(), 2);
        assert!(sums.iter().all(|s| s.app_id == "app-car"));
        assert!(reg.instances_json().contains("i1"));
        assert!(reg.instances_json().contains("32101"));
    }

    #[test]
    fn stop_releases_port_for_reuse() {
        let mut reg = AppInstanceRegistry::new().with_port_range(32200, 32200);
        reg.begin_start(r"D:\GoDesk", sample_req("x", 0)).unwrap();
        reg.mark_running("x", 99).unwrap();
        reg.begin_stop("x").unwrap();
        reg.mark_stopped("x").unwrap();
        let again = reg.begin_start(r"D:\GoDesk", sample_req("y", 0)).unwrap();
        assert_eq!(again.listen_port, 32200);
    }

    #[test]
    fn failed_start_releases_port() {
        let mut reg = AppInstanceRegistry::new().with_port_range(32300, 32300);
        reg.begin_start(r"D:\GoDesk", sample_req("f", 0)).unwrap();
        reg.mark_failed("f", "spawn failed").unwrap();
        let again = reg.begin_start(r"D:\GoDesk", sample_req("g", 0)).unwrap();
        assert_eq!(again.listen_port, 32300);
    }

    #[test]
    fn should_not_treat_desktop_as_game_hook_kill_target() {
        let mut reg = AppInstanceRegistry::new();
        reg.begin_start(r"D:\GoDesk", sample_req("g1", 32400)).unwrap();
        reg.mark_running("g1", 42).unwrap();
        assert!(reg.should_kill_pid_for_instance("g1", 42));
        assert!(!reg.should_kill_pid_for_instance("g1", 43));
        assert!(!reg.should_kill_pid_for_instance("missing", 42));
    }

    #[test]
    fn web_client_url_includes_device_and_instance() {
        let url = build_web_client_url("10.0.0.2", 32000, "dev-1", "inst-9");
        assert_eq!(
            url,
            "http://10.0.0.2:32000/web_client/?deviceId=dev-1&instanceId=inst-9"
        );
    }

    #[test]
    fn port_pool_exhaustion() {
        let mut reg = AppInstanceRegistry::new().with_port_range(32500, 32500);
        reg.begin_start(r"D:\GoDesk", sample_req("only", 0)).unwrap();
        let err = reg
            .begin_start(r"D:\GoDesk", sample_req("two", 0))
            .unwrap_err();
        assert!(err.contains("no free listen_port"));
    }
}
