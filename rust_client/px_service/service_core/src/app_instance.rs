//! CMS-scheduled game-hook app instances (see docs/cms_app_schedule_plan.md).
//! Desktop render remains a separate single-slot path in ServiceState.

use crate::config::RENDER_EXE_NAME;
use crate::process::ProcessSnapshot;
use crate::state::RenderLaunchSpec;
use crate::ue_bootstrap::UeViewInfo;
use px_base::crypto_util::base64_encode;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

pub const APP_MODE_GAME_HOOK: &str = "game-hook";
pub const DEFAULT_ENCODER_FPS: i32 = 60;
pub const DEFAULT_ENCODER_BITRATE: i32 = 20;
pub const DEFAULT_ENCODER_FORMAT: &str = "h264";
/// Port pool when CMS sends listen_port=0.
pub const DEFAULT_PORT_RANGE_START: u16 = 32000;
pub const DEFAULT_PORT_RANGE_END: u16 = 32999;
/// How long finished (stopped/failed) records are kept before prune removes them.
pub const FINISHED_RECORD_TTL: Duration = Duration::from_secs(600);

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
    /// UE bootstrap 解析出的真游戏(view)进程路径；None 表示非 UE 外壳，
    /// boot 即 view（注入目标就是启动的游戏进程本身）。
    pub view_game_path: Option<PathBuf>,
    /// Set when the instance reaches stopped/failed; used by prune_finished.
    pub finished_at: Option<Instant>,
}

impl AppInstanceRecord {
    /// Active states are reported in heartbeat summaries and keep the port reserved.
    pub fn is_active(&self) -> bool {
        matches!(
            self.state,
            AppInstanceState::Starting | AppInstanceState::Running | AppInstanceState::Stopping
        )
    }
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
/// `game_path` is the boot exe (launched normally); when `view` is set the
/// render discovers and injects that real game process instead of the boot.
pub fn build_game_hook_launch_spec(
    work_dir: impl Into<String>,
    req: &StartAppRequest,
    listen_port: u16,
    game_path: &Path,
    view: Option<&UeViewInfo>,
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
    if let Some(v) = view {
        args.push(format!(
            "--app_game_view_path={}",
            encode_game_path_b64(&v.view_path)
        ));
    }
    if !req.game_arguments.trim().is_empty() {
        args.push(format!("--app_game_args={}", req.game_arguments.trim()));
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

/// True if the cmdline carries an exact `--network_listen_port={port}` token.
/// Token-boundary safe: port 3200 must not match 32000 (substring pitfall).
pub fn cmdline_has_listen_port(cmdline: &str, port: u16) -> bool {
    let args: Vec<String> = cmdline.split_whitespace().map(|s| s.to_string()).collect();
    extract_listen_port(&args) == Some(port)
}

/// Identity check before killing: the pid must currently be this instance's
/// game-hook render (exact listen-port match) or its game exe (path match).
/// Guards against Windows pid reuse killing an innocent process tree.
pub fn pid_belongs_to_instance(
    processes: &[ProcessSnapshot],
    listen_port: u16,
    game_path: &Path,
    pid: u32,
) -> bool {
    let Some(p) = processes.iter().find(|p| p.pid == pid) else {
        return false;
    };
    if p.is_game_hook_render_process() {
        return cmdline_has_listen_port(&p.cmdline, listen_port);
    }
    p.exe_path_eq(&game_path.to_string_lossy())
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

    /// Only active states (starting/running/stopping) are reported in the
    /// heartbeat; CMS reconcile treats absence as stopped, and stop/start
    /// failures are already delivered via explicit result messages.
    pub fn summaries(&self) -> Vec<AppInstanceSummary> {
        self.instances
            .values()
            .filter(|r| r.is_active())
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

    /// Allocate listen_port: use preferred if >0 (error if out of range, taken,
    /// or occupied on the OS); else last_used+1 (default start 32000), wrapping
    /// within the pool for free slots.
    pub fn allocate_port(&self, preferred: i32) -> Result<u16, String> {
        if preferred > 0 {
            if preferred > i32::from(u16::MAX) {
                return Err(format!("listen_port {preferred} out of u16 range"));
            }
            let p = preferred as u16;
            if p < self.port_range_start || p > self.port_range_end {
                return Err(format!(
                    "listen_port {p} out of range [{}-{}]",
                    self.port_range_start, self.port_range_end
                ));
            }
            if self.used_ports.contains_key(&p) {
                return Err(format!("listen_port {p} already in use"));
            }
            if !port_bindable(p) {
                return Err(format!("listen_port {p} is occupied on the OS"));
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
            // OS probe catches orphan renders / foreign processes the registry
            // does not track (e.g. after a failed start left a live render).
            if !self.used_ports.contains_key(&p) && port_bindable(p) {
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
        // UE bootstrap 外壳：解析真游戏(view)进程路径，render 注入它以代替外壳。
        // 注意：boot 外壳自己也会读 201/202 并把命令行透传给 view 子进程，
        // 所以 202 的 base_args 不并入我们传给 boot 的参数（避免项目名重复）。
        let view = crate::ue_bootstrap::resolve_ue_bootstrap(&game_path);
        let port = self.allocate_port(req.listen_port)?;
        let launch = build_game_hook_launch_spec(work_dir, &req, port, &game_path, view.as_ref());
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
            view_game_path: view.map(|v| v.view_path),
            finished_at: None,
        };
        self.instances.insert(req.instance_id.clone(), record);
        Ok(self.instances.get(&req.instance_id).unwrap())
    }

    pub fn mark_running(&mut self, instance_id: &str, pid: u32) -> Result<(), String> {
        let rec = self
            .instances
            .get_mut(instance_id)
            .ok_or_else(|| format!("unknown instance_id {instance_id}"))?;
        // A concurrent Stop may have finished the record while the start task
        // was still waiting; never resurrect a finished instance.
        if matches!(rec.state, AppInstanceState::Stopped | AppInstanceState::Failed) {
            return Err(format!(
                "instance {instance_id} already stopped/failed, refusing mark_running"
            ));
        }
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
        rec.finished_at = Some(Instant::now());
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
        // A stale stop task must not clobber a record a concurrent Start
        // has just re-registered as Starting.
        if matches!(rec.state, AppInstanceState::Starting) {
            return Err(format!(
                "instance {instance_id} is starting, refusing stale mark_stopped"
            ));
        }
        let port = rec.listen_port;
        rec.state = AppInstanceState::Stopped;
        rec.pid = None;
        rec.finished_at = Some(Instant::now());
        self.used_ports.remove(&port);
        Ok(())
    }

    /// Drop finished (stopped/failed) records older than max_age so the
    /// registry does not grow unboundedly. Active records are always kept.
    pub fn prune_finished(&mut self, max_age: Duration) {
        let now = Instant::now();
        self.instances.retain(|_, rec| {
            if rec.is_active() {
                return true;
            }
            match rec.finished_at {
                Some(finished) => now.duration_since(finished) < max_age,
                None => true,
            }
        });
    }

    /// Remove a record entirely (reap of a failed instance whose orphan
    /// render has been cleaned up).
    pub fn remove(&mut self, instance_id: &str) -> bool {
        match self.instances.remove(instance_id) {
            Some(rec) => {
                self.used_ports.remove(&rec.listen_port);
                true
            }
            None => false,
        }
    }

    /// True if stop should kill this pid (game-hook instance), never desktop.
    pub fn should_kill_pid_for_instance(&self, instance_id: &str, pid: u32) -> bool {
        match self.instances.get(instance_id) {
            Some(rec) => rec.pid == Some(pid) && is_game_hook_launch(&rec.launch),
            None => false,
        }
    }
}

/// OS-level occupancy probe: true if the port can be bound right now.
/// Catches orphan renders / foreign processes the registry does not track.
/// Probes both IPv4 and IPv6 wildcards; either stack occupied counts as
/// unavailable. A host without an IPv6 stack (probe fails for a reason
/// other than address-in-use) is NOT treated as occupied.
pub fn port_bindable(port: u16) -> bool {
    if std::net::TcpListener::bind(("0.0.0.0", port)).is_err() {
        return false;
    }
    match std::net::TcpListener::bind(("::", port)) {
        Ok(_) => true,
        Err(e) => e.kind() != std::io::ErrorKind::AddrInUse,
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
        let spec = build_game_hook_launch_spec(r"D:\GoDesk", &req, 32010, &game, None);
        assert!(is_game_hook_launch(&spec));
        assert!(spec.app_path.ends_with(RENDER_EXE_NAME));
        assert_eq!(extract_listen_port(&spec.args), Some(32010));
        let b64_arg = spec
            .args
            .iter()
            .find(|a| a.starts_with("--app_game_path="))
            .unwrap();
        let b64 = b64_arg.strip_prefix("--app_game_path=").unwrap();
        let decoded = px_base::crypto_util::base64_decode(b64).unwrap();
        assert!(decoded.contains("VehicleGame"));
        assert!(spec.args.iter().any(|a| a == "--capture_video_type=inner"));
    }

    #[test]
    fn launch_spec_carries_view_path_and_game_args() {
        let req = sample_req("i1", 32010);
        let game = resolve_game_path(&req.install_root, &req.game_exe_rel).unwrap();
        let view = UeViewInfo {
            view_path: game.clone(),
            base_args: None,
        };
        let spec = build_game_hook_launch_spec(r"D:\GoDesk", &req, 32010, &game, Some(&view));
        // view path is passed base64-encoded like the boot path.
        let view_arg = spec
            .args
            .iter()
            .find(|a| a.starts_with("--app_game_view_path="))
            .expect("view path arg");
        let b64 = view_arg.strip_prefix("--app_game_view_path=").unwrap();
        let decoded = px_base::crypto_util::base64_decode(b64).unwrap();
        assert!(decoded.contains("VehicleGame"));
        // game args flag must match render's gflags name (app_game_args).
        assert!(spec.args.iter().any(|a| a == "--app_game_args=-dx11"));
        assert!(!spec.args.iter().any(|a| a.starts_with("--app_game_arguments")));
        // Without a view, no view arg is emitted.
        let spec_no_view = build_game_hook_launch_spec(r"D:\GoDesk", &req, 32010, &game, None);
        assert!(!spec_no_view
            .args
            .iter()
            .any(|a| a.starts_with("--app_game_view_path=")));
    }

    #[test]
    fn non_ue_launch_args_pass_through_unchanged() {
        // sample_req 的 exe 不存在，resolve_ue_bootstrap 返回 None：
        // 验证 begin_start 在非 UE 路径下参数原样传递、无 view 路径。
        let mut reg = AppInstanceRegistry::new();
        let req = sample_req("ue", 32800);
        let rec = reg.begin_start(r"D:\GoDesk", req).unwrap();
        assert!(rec
            .launch
            .args
            .iter()
            .any(|a| a == "--app_game_args=-dx11"));
        assert!(rec.view_game_path.is_none());
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

    #[test]
    fn port_bindable_detects_v4_and_v6_occupancy() {
        // IPv4 occupancy must fail the probe, and free again on release.
        // (Wildcard bind: on Windows a specific-address bind does not always
        // block a later wildcard bind.)
        let v4 = std::net::TcpListener::bind(("0.0.0.0", 0)).unwrap();
        let port = v4.local_addr().unwrap().port();
        assert!(!port_bindable(port), "v4-occupied port must be unbindable");
        drop(v4);
        assert!(port_bindable(port), "released port must be bindable");

        // IPv6 wildcard occupancy must also fail the probe (skipped on hosts
        // without an IPv6 stack). Note: on Windows a specific-address bind
        // (e.g. ::1) does not block a later wildcard bind, so use [::].
        if let Ok(v6) = std::net::TcpListener::bind(("::", 0)) {
            let port = v6.local_addr().unwrap().port();
            assert!(!port_bindable(port), "v6-occupied port must be unbindable");
        }
    }

    #[test]
    fn preferred_port_out_of_range_rejected() {
        let mut reg = AppInstanceRegistry::new();
        for port in [80, 31999, 33000, 70000] {
            let err = reg
                .begin_start(r"D:\GoDesk", sample_req("x", port))
                .unwrap_err();
            assert!(err.contains("out of range") || err.contains("out of u16 range"));
        }
        reg.begin_start(r"D:\GoDesk", sample_req("ok", 32600)).unwrap();
    }

    #[test]
    fn listen_port_token_boundary() {
        // Substring pitfall: port 3200 must not match "--network_listen_port=32000".
        assert!(!cmdline_has_listen_port(
            "GammaRayRender.exe --app_mode=game-hook --network_listen_port=32000",
            3200
        ));
        assert!(cmdline_has_listen_port(
            "GammaRayRender.exe --app_mode=game-hook --network_listen_port=32000 --capture_video=true",
            32000
        ));
        // Token at end of cmdline also matches.
        assert!(cmdline_has_listen_port(
            "GammaRayRender.exe --network_listen_port=32001",
            32001
        ));
        assert!(!cmdline_has_listen_port("GammaRayRender.exe", 32000));
    }

    #[test]
    fn pid_identity_check_before_kill() {
        use crate::process::ProcessSnapshot;
        let game_path = Path::new(r"D:\apps\CarGame\Binaries\Win64\game.exe");
        let processes = vec![
            ProcessSnapshot::new(
                100,
                "D:/GoDesk/GammaRayRender.exe",
                "--app_mode=game-hook --network_listen_port=32000",
            ),
            ProcessSnapshot::new(
                101,
                "D:/GoDesk/GammaRayRender.exe",
                "--app_mode=game-hook --network_listen_port=32005",
            ),
            ProcessSnapshot::new(102, r"D:\apps\CarGame\Binaries\Win64\game.exe", ""),
            ProcessSnapshot::new(103, "C:/Windows/notepad.exe", ""),
        ];
        // Own render with matching port.
        assert!(pid_belongs_to_instance(&processes, 32000, game_path, 100));
        // Render of another instance (different port).
        assert!(!pid_belongs_to_instance(&processes, 32000, game_path, 101));
        // Own game exe (path match, case/separator-insensitive).
        assert!(pid_belongs_to_instance(&processes, 32000, game_path, 102));
        // Pid reuse: an innocent process now owns the recorded pid.
        assert!(!pid_belongs_to_instance(&processes, 32000, game_path, 103));
        // Pid no longer exists at all.
        assert!(!pid_belongs_to_instance(&processes, 32000, game_path, 999));
    }

    #[test]
    fn summaries_only_report_active_states() {
        let mut reg = AppInstanceRegistry::new();
        reg.begin_start(r"D:\GoDesk", sample_req("s1", 32700)).unwrap();
        reg.mark_running("s1", 10).unwrap();
        reg.begin_stop("s1").unwrap();
        reg.mark_stopped("s1").unwrap();
        reg.begin_start(r"D:\GoDesk", sample_req("f1", 32701)).unwrap();
        reg.mark_failed("f1", "boom").unwrap();
        reg.begin_start(r"D:\GoDesk", sample_req("r1", 32702)).unwrap();
        let sums = reg.summaries();
        assert_eq!(sums.len(), 1);
        assert_eq!(sums[0].instance_id, "r1");
        assert_eq!(sums[0].state, "starting");
    }

    #[test]
    fn prune_finished_removes_aged_records() {
        let mut reg = AppInstanceRegistry::new();
        reg.begin_start(r"D:\GoDesk", sample_req("old", 32710)).unwrap();
        reg.mark_failed("old", "boom").unwrap();
        reg.begin_start(r"D:\GoDesk", sample_req("new", 32711)).unwrap();
        reg.begin_stop("new").unwrap();
        reg.mark_stopped("new").unwrap();
        reg.begin_start(r"D:\GoDesk", sample_req("act", 32712)).unwrap();
        // Age the "old" record beyond the TTL; "new" stays fresh.
        reg.instances.get_mut("old").unwrap().finished_at =
            Some(Instant::now() - FINISHED_RECORD_TTL - Duration::from_secs(1));
        reg.prune_finished(FINISHED_RECORD_TTL);
        assert!(reg.get("old").is_none());
        assert!(reg.get("new").is_some(), "fresh finished record kept");
        assert!(reg.get("act").is_some(), "active record kept");
    }

    #[test]
    fn finished_record_rejects_stale_state_transitions() {
        let mut reg = AppInstanceRegistry::new();
        reg.begin_start(r"D:\GoDesk", sample_req("g", 32720)).unwrap();
        reg.begin_stop("g").unwrap();
        reg.mark_stopped("g").unwrap();
        // A start task that was still waiting must not resurrect the record.
        assert!(reg.mark_running("g", 55).is_err());
        // A stale stop task must not clobber a re-started record.
        reg.begin_start(r"D:\GoDesk", sample_req("g", 32721)).unwrap();
        assert!(reg.mark_stopped("g").is_err());
        assert_eq!(reg.get("g").unwrap().state, AppInstanceState::Starting);
    }
}
