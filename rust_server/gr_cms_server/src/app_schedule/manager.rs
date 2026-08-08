//! Application / Placement / Instance orchestration for CMS.
//! Memory + Mongo (when DB ready); unit tests cover multi-machine scheduling.

use crate::gSpvrServiceConnMgr;
use protocol::spvr_service::{
    SpvrServiceStartAppInstance, SpvrServiceStartAppInstanceResult, SpvrServiceStopAppInstance,
    SpvrServiceStopAppInstanceResult,
};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Duration;
use tokio::sync::{oneshot, Mutex};
use uuid::Uuid;

/// How long HTTP start waits for Service StartAppInstanceResult before failing.
const START_RESULT_TIMEOUT: Duration = Duration::from_secs(25);

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct Application {
    pub app_id: String,
    pub name: String,
    /// Absolute game exe path shown/edited in CMS Web.
    #[serde(default)]
    pub game_path: String,
    /// Relative/file name sent to Service (derived from game_path).
    pub game_exe_rel: String,
    pub default_game_args: String,
    pub encoder_fps: i32,
    pub encoder_bitrate: i32,
    pub encoder_format: String,
    pub webrtc_enabled: bool,
    pub websocket_enabled: bool,
    /// Preferred listen port (default assigned from 32000 upward).
    #[serde(default)]
    pub listen_port: i32,
}

/// Flattened row for CMS Web list/edit.
#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct AppRowVo {
    pub app_id: String,
    pub placement_id: String,
    pub name: String,
    pub device_id: String,
    pub game_path: String,
    pub listen_port: i32,
    pub default_game_args: String,
    pub encoder_fps: i32,
    pub encoder_bitrate: i32,
    pub encoder_format: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SaveAppReq {
    /// Empty/None = create; set = update.
    pub app_id: Option<String>,
    pub name: String,
    pub device_id: String,
    /// Absolute path to game exe.
    pub game_path: String,
    pub default_game_args: Option<String>,
    pub encoder_fps: Option<i32>,
    pub encoder_bitrate: Option<i32>,
    pub encoder_format: Option<String>,
    /// 0 / None = auto next free port from 32000.
    pub listen_port: Option<i32>,
}

const DEFAULT_LISTEN_PORT_START: i32 = 32000;
const DEFAULT_LISTEN_PORT_END: i32 = 32999;

/// Split absolute game path into (install_root, game_exe_rel=file_name).
pub fn split_game_path(game_path: &str) -> Result<(String, String), String> {
    let raw = game_path.trim();
    if raw.is_empty() {
        return Err("程序路径不能为空".to_string());
    }
    let path = std::path::Path::new(raw);
    if !path.is_absolute() {
        return Err("程序路径必须是绝对路径，例如 D:\\games\\app.exe".to_string());
    }
    let file = path
        .file_name()
        .and_then(|s| s.to_str())
        .filter(|s| !s.is_empty())
        .ok_or_else(|| "程序路径无效".to_string())?;
    let parent = path
        .parent()
        .and_then(|p| p.to_str())
        .filter(|s| !s.is_empty())
        .ok_or_else(|| "程序路径缺少目录".to_string())?;
    Ok((parent.to_string(), file.to_string()))
}

fn join_game_path(install_root: &str, game_exe_rel: &str) -> String {
    if game_exe_rel.trim().is_empty() {
        return install_root.to_string();
    }
    let root = install_root.trim_end_matches(['\\', '/']);
    format!("{root}\\{game_exe_rel}")
}

/// Prefer absolute `game_path` when present; heal legacy rows where `game_exe_rel`
/// was mistakenly stored as an absolute path (Service rejects those).
fn resolve_start_paths(
    game_path: &str,
    install_root: &str,
    game_exe_rel: &str,
) -> Result<(String, String), String> {
    let path = game_path.trim();
    if !path.is_empty() {
        return split_game_path(path);
    }
    let rel = game_exe_rel.trim();
    if rel.is_empty() {
        return Err("程序路径无效".to_string());
    }
    if std::path::Path::new(rel).is_absolute() {
        return split_game_path(rel);
    }
    let root = install_root.trim();
    if root.is_empty() {
        return Err("install_root is empty".to_string());
    }
    Ok((root.to_string(), rel.to_string()))
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct AppPlacement {
    pub placement_id: String,
    pub app_id: String,
    pub device_id: String,
    pub install_root: String,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum InstanceState {
    Starting,
    Running,
    Failed,
    Stopping,
    #[default]
    Stopped,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct AppInstance {
    pub instance_id: String,
    pub request_id: String,
    pub app_id: String,
    pub device_id: String,
    pub placement_id: String,
    pub state: InstanceState,
    pub listen_port: i32,
    pub pid: u32,
    pub error: String,
    pub web_client_hint: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CreateApplicationReq {
    pub name: String,
    pub game_exe_rel: String,
    pub default_game_args: Option<String>,
    pub encoder_fps: Option<i32>,
    pub encoder_bitrate: Option<i32>,
    pub encoder_format: Option<String>,
    #[serde(default)]
    pub game_path: Option<String>,
    #[serde(default)]
    pub listen_port: Option<i32>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CreatePlacementReq {
    pub app_id: String,
    pub device_id: String,
    pub install_root: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StartInstanceReq {
    pub app_id: String,
    pub device_id: String,
    /// Optional preferred port; 0 = service allocates.
    pub listen_port: Option<i32>,
}

#[derive(Default)]
struct Inner {
    apps: HashMap<String, Application>,
    placements: HashMap<String, AppPlacement>,
    /// placement key app_id+device_id -> placement_id
    placement_by_app_device: HashMap<(String, String), String>,
    instances: HashMap<String, AppInstance>,
    /// request_id -> instance_id
    request_index: HashMap<String, String>,
    /// HTTP start waits here until Service posts StartAppInstanceResult.
    start_waiters: HashMap<String, oneshot::Sender<AppInstance>>,
}

pub struct AppScheduleManager {
    inner: Mutex<Inner>,
    seq: AtomicU64,
}

impl AppScheduleManager {
    pub fn new() -> Self {
        Self {
            inner: Mutex::new(Inner::default()),
            seq: AtomicU64::new(1),
        }
    }

    fn next_id(&self, prefix: &str) -> String {
        let n = self.seq.fetch_add(1, Ordering::Relaxed);
        format!("{prefix}-{n}-{}", &Uuid::new_v4().to_string()[..8])
    }

    pub async fn create_application(&self, req: CreateApplicationReq) -> Result<Application, String> {
        if req.name.trim().is_empty() || req.game_exe_rel.trim().is_empty() {
            return Err("name and game_exe_rel required".to_string());
        }
        let game_path = req
            .game_path
            .unwrap_or_default()
            .trim()
            .to_string();
        let app = Application {
            app_id: self.next_id("app"),
            name: req.name.trim().to_string(),
            game_path,
            game_exe_rel: req.game_exe_rel.trim().to_string(),
            default_game_args: req.default_game_args.unwrap_or_default(),
            encoder_fps: req.encoder_fps.unwrap_or(60),
            encoder_bitrate: req.encoder_bitrate.unwrap_or(20),
            encoder_format: req
                .encoder_format
                .unwrap_or_else(|| "h264".to_string()),
            webrtc_enabled: true,
            websocket_enabled: true,
            listen_port: req.listen_port.unwrap_or(0),
        };
        {
            let mut g = self.inner.lock().await;
            g.apps.insert(app.app_id.clone(), app.clone());
        }
        if let Err(e) = crate::app_schedule::store::upsert_application(&app).await {
            tracing::warn!("persist application failed: {e}");
        }
        Ok(app)
    }

    pub async fn list_applications(&self) -> Vec<Application> {
        self.inner.lock().await.apps.values().cloned().collect()
    }

    pub async fn list_app_rows(&self) -> Vec<AppRowVo> {
        let g = self.inner.lock().await;
        let mut rows = Vec::new();
        for p in g.placements.values() {
            let Some(app) = g.apps.get(&p.app_id) else {
                continue;
            };
            let game_path = if !app.game_path.trim().is_empty() {
                app.game_path.clone()
            } else {
                join_game_path(&p.install_root, &app.game_exe_rel)
            };
            rows.push(AppRowVo {
                app_id: app.app_id.clone(),
                placement_id: p.placement_id.clone(),
                name: app.name.clone(),
                device_id: p.device_id.clone(),
                game_path,
                listen_port: app.listen_port,
                default_game_args: app.default_game_args.clone(),
                encoder_fps: app.encoder_fps,
                encoder_bitrate: app.encoder_bitrate,
                encoder_format: app.encoder_format.clone(),
            });
        }
        rows.sort_by(|a, b| a.name.cmp(&b.name));
        rows
    }

    fn collect_used_ports_locked(g: &Inner, exclude_app_id: Option<&str>) -> Vec<i32> {
        let mut used = Vec::new();
        for app in g.apps.values() {
            if exclude_app_id.is_some_and(|id| id == app.app_id) {
                continue;
            }
            if app.listen_port > 0 {
                used.push(app.listen_port);
            }
        }
        for inst in g.instances.values() {
            if matches!(
                inst.state,
                InstanceState::Starting | InstanceState::Running | InstanceState::Stopping
            ) && inst.listen_port > 0
            {
                used.push(inst.listen_port);
            }
        }
        used
    }

    /// Suggest the next free listen port for the Web form. Capped at
    /// `DEFAULT_LISTEN_PORT_END`: past the pool tail fall back to the first
    /// free port from the pool start; error when the pool is exhausted
    /// (same wording as save_app auto-assign).
    pub async fn suggest_next_port(&self) -> Result<i32, String> {
        let g = self.inner.lock().await;
        let used = Self::collect_used_ports_locked(&g, None);
        let max = used.iter().copied().max().unwrap_or(DEFAULT_LISTEN_PORT_START - 1);
        let next = (max + 1).max(DEFAULT_LISTEN_PORT_START);
        if next <= DEFAULT_LISTEN_PORT_END {
            return Ok(next);
        }
        for port in DEFAULT_LISTEN_PORT_START..=DEFAULT_LISTEN_PORT_END {
            if !used.contains(&port) {
                return Ok(port);
            }
        }
        Err(format!(
            "端口已用完（{DEFAULT_LISTEN_PORT_START}-{DEFAULT_LISTEN_PORT_END}）"
        ))
    }

    fn ensure_port_available_locked(
        g: &Inner,
        port: i32,
        exclude_app_id: Option<&str>,
    ) -> Result<(), String> {
        if !(DEFAULT_LISTEN_PORT_START..=DEFAULT_LISTEN_PORT_END).contains(&port) {
            return Err(format!(
                "端口必须在 {DEFAULT_LISTEN_PORT_START}-{DEFAULT_LISTEN_PORT_END} 之间"
            ));
        }
        for app in g.apps.values() {
            if exclude_app_id.is_some_and(|id| id == app.app_id) {
                continue;
            }
            if app.listen_port == port {
                return Err(format!("端口 {port} 已被应用「{}」占用", app.name));
            }
        }
        for inst in g.instances.values() {
            if matches!(
                inst.state,
                InstanceState::Starting | InstanceState::Running | InstanceState::Stopping
            ) && inst.listen_port == port
            {
                return Err(format!(
                    "端口 {port} 正被实例 {} 使用中",
                    inst.instance_id
                ));
            }
        }
        Ok(())
    }

    /// Create or update application + machine placement from a single Web form.
    pub async fn save_app(&self, req: SaveAppReq) -> Result<AppRowVo, String> {
        if req.name.trim().is_empty() {
            return Err("请填写应用名称".to_string());
        }
        if req.device_id.trim().is_empty() {
            return Err("请选择机器".to_string());
        }
        let (install_root, game_exe_rel) = split_game_path(&req.game_path)?;
        let game_path = req.game_path.trim().to_string();
        let device_id = req.device_id.trim().to_string();
        let editing_id = req
            .app_id
            .as_ref()
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty());

        let app_id = editing_id
            .clone()
            .unwrap_or_else(|| self.next_id("app"));

        let (app, placement, stale_placement_ids) = {
            let mut g = self.inner.lock().await;
            let listen_port = match req.listen_port.unwrap_or(0) {
                p if p > 0 => {
                    Self::ensure_port_available_locked(&g, p, editing_id.as_deref())?;
                    p
                }
                _ => {
                    let used = Self::collect_used_ports_locked(&g, editing_id.as_deref());
                    let max = used
                        .into_iter()
                        .max()
                        .unwrap_or(DEFAULT_LISTEN_PORT_START - 1);
                    let next = (max + 1).max(DEFAULT_LISTEN_PORT_START);
                    if next > DEFAULT_LISTEN_PORT_END {
                        return Err(format!(
                            "端口已用完（{DEFAULT_LISTEN_PORT_START}-{DEFAULT_LISTEN_PORT_END}）"
                        ));
                    }
                    next
                }
            };

            let existing = editing_id
                .as_ref()
                .map(|id| {
                    g.apps
                        .get(id)
                        .cloned()
                        .ok_or_else(|| format!("应用不存在: {id}"))
                })
                .transpose()?;

            if let Some(ref app_id) = editing_id {
                for inst in g.instances.values() {
                    if inst.app_id == *app_id
                        && matches!(
                            inst.state,
                            InstanceState::Starting
                                | InstanceState::Running
                                | InstanceState::Stopping
                        )
                        && inst.device_id != device_id
                    {
                        return Err("应用运行中，不能更换机器".to_string());
                    }
                }
            }

            let app = Application {
                app_id: app_id.clone(),
                name: req.name.trim().to_string(),
                game_path: game_path.clone(),
                game_exe_rel: game_exe_rel.clone(),
                default_game_args: req
                    .default_game_args
                    .unwrap_or_else(|| {
                        existing
                            .as_ref()
                            .map(|e| e.default_game_args.clone())
                            .unwrap_or_default()
                    }),
                encoder_fps: req
                    .encoder_fps
                    .unwrap_or_else(|| existing.as_ref().map(|e| e.encoder_fps).unwrap_or(60)),
                encoder_bitrate: req.encoder_bitrate.unwrap_or_else(|| {
                    existing.as_ref().map(|e| e.encoder_bitrate).unwrap_or(20)
                }),
                encoder_format: req.encoder_format.unwrap_or_else(|| {
                    existing
                        .as_ref()
                        .map(|e| e.encoder_format.clone())
                        .unwrap_or_else(|| "h264".to_string())
                }),
                webrtc_enabled: true,
                websocket_enabled: true,
                listen_port,
            };

            let mut keep_placement_id: Option<String> = None;
            let mut stale = Vec::new();
            let old_keys: Vec<(String, String)> = g
                .placement_by_app_device
                .keys()
                .filter(|(aid, _)| aid == &app.app_id)
                .cloned()
                .collect();
            for key in old_keys {
                if let Some(pid) = g.placement_by_app_device.remove(&key) {
                    if key.1 == device_id {
                        keep_placement_id = Some(pid);
                    } else {
                        g.placements.remove(&pid);
                        stale.push(pid);
                    }
                }
            }

            let placement = if let Some(pid) = keep_placement_id {
                let mut p = g.placements.remove(&pid).unwrap_or(AppPlacement {
                    placement_id: pid.clone(),
                    app_id: app.app_id.clone(),
                    device_id: device_id.clone(),
                    install_root: install_root.clone(),
                });
                p.app_id = app.app_id.clone();
                p.device_id = device_id.clone();
                p.install_root = install_root.clone();
                p
            } else {
                AppPlacement {
                    placement_id: format!("plc-{}", &Uuid::new_v4().to_string()[..8]),
                    app_id: app.app_id.clone(),
                    device_id: device_id.clone(),
                    install_root: install_root.clone(),
                }
            };

            g.apps.insert(app.app_id.clone(), app.clone());
            g.placement_by_app_device.insert(
                (placement.app_id.clone(), placement.device_id.clone()),
                placement.placement_id.clone(),
            );
            g.placements
                .insert(placement.placement_id.clone(), placement.clone());
            (app, placement, stale)
        };

        for pid in stale_placement_ids {
            let _ = crate::app_schedule::store::delete_placement(&pid).await;
        }
        let _ = crate::app_schedule::store::upsert_application(&app).await;
        let _ = crate::app_schedule::store::upsert_placement(&placement).await;

        Ok(AppRowVo {
            app_id: app.app_id,
            placement_id: placement.placement_id,
            name: app.name,
            device_id: placement.device_id,
            game_path: app.game_path,
            listen_port: app.listen_port,
            default_game_args: app.default_game_args,
            encoder_fps: app.encoder_fps,
            encoder_bitrate: app.encoder_bitrate,
            encoder_format: app.encoder_format,
        })
    }

    pub async fn delete_app(&self, app_id: &str) -> Result<(), String> {
        let to_delete = {
            let g = self.inner.lock().await;
            for inst in g.instances.values() {
                if inst.app_id == app_id
                    && matches!(
                        inst.state,
                        InstanceState::Starting | InstanceState::Running | InstanceState::Stopping
                    )
                {
                    return Err("应用运行中，请先停止再删除".to_string());
                }
            }
            if !g.apps.contains_key(app_id) {
                return Err(format!("应用不存在: {app_id}"));
            }
            let plc_ids: Vec<String> = g
                .placements
                .values()
                .filter(|p| p.app_id == app_id)
                .map(|p| p.placement_id.clone())
                .collect();
            plc_ids
        };

        {
            let mut g = self.inner.lock().await;
            g.apps.remove(app_id);
            for pid in &to_delete {
                if let Some(p) = g.placements.remove(pid) {
                    g.placement_by_app_device
                        .remove(&(p.app_id, p.device_id));
                }
            }
            // Drop stopped instances of this app from memory.
            let inst_ids: Vec<String> = g
                .instances
                .values()
                .filter(|i| i.app_id == app_id)
                .map(|i| i.instance_id.clone())
                .collect();
            for iid in inst_ids {
                if let Some(i) = g.instances.remove(&iid) {
                    g.request_index.remove(&i.request_id);
                }
            }
        }
        let _ = crate::app_schedule::store::delete_application(app_id).await;
        for pid in to_delete {
            let _ = crate::app_schedule::store::delete_placement(&pid).await;
        }
        // Drop this app's instances from DB too, otherwise a CMS restart would
        // reload them as orphans.
        let _ = crate::app_schedule::store::delete_instances_by_app(app_id).await;
        Ok(())
    }

    pub async fn create_placement(&self, req: CreatePlacementReq) -> Result<AppPlacement, String> {
        if req.install_root.trim().is_empty() {
            return Err("install_root required".to_string());
        }
        let mut g = self.inner.lock().await;
        if !g.apps.contains_key(&req.app_id) {
            return Err(format!("unknown app_id {}", req.app_id));
        }
        let key = (req.app_id.clone(), req.device_id.clone());
        if let Some(existing) = g.placement_by_app_device.get(&key) {
            return Err(format!("placement already exists: {existing}"));
        }
        let p = AppPlacement {
            placement_id: format!("plc-{}", &Uuid::new_v4().to_string()[..8]),
            app_id: req.app_id,
            device_id: req.device_id,
            install_root: req.install_root.trim().to_string(),
        };
        g.placement_by_app_device
            .insert((p.app_id.clone(), p.device_id.clone()), p.placement_id.clone());
        g.placements.insert(p.placement_id.clone(), p.clone());
        drop(g);
        if let Err(e) = crate::app_schedule::store::upsert_placement(&p).await {
            tracing::warn!("persist placement failed: {e}");
        }
        Ok(p)
    }

    pub async fn list_placements(&self) -> Vec<AppPlacement> {
        self.inner.lock().await.placements.values().cloned().collect()
    }

    pub async fn list_instances(&self) -> Vec<AppInstance> {
        self.inner.lock().await.instances.values().cloned().collect()
    }

    /// Schedule start on a machine that has Placement for the app.
    pub async fn start_instance(&self, req: StartInstanceReq) -> Result<AppInstance, String> {
        let (app, placement) = {
            let g = self.inner.lock().await;
            let app = g
                .apps
                .get(&req.app_id)
                .cloned()
                .ok_or_else(|| format!("unknown app_id {}", req.app_id))?;
            let key = (req.app_id.clone(), req.device_id.clone());
            let placement_id = g
                .placement_by_app_device
                .get(&key)
                .cloned()
                .ok_or_else(|| {
                    format!(
                        "no placement for app {} on device {}",
                        req.app_id, req.device_id
                    )
                })?;
            let placement = g.placements.get(&placement_id).cloned().unwrap();
            (app, placement)
        };

        let (install_root, game_exe_rel) =
            resolve_start_paths(&app.game_path, &placement.install_root, &app.game_exe_rel)?;

        // Service must be online.
        let conn = match gSpvrServiceConnMgr.get_conn(req.device_id.clone()).await {
            Ok(c) => c,
            Err(_) => return Err(format!("service offline: {}", req.device_id)),
        };

        let request_id = self.next_id("req");
        let instance_id = self.next_id("inst");
        // Prefer explicit start override; else the app's saved port; else Service auto.
        let listen_port = match req.listen_port.unwrap_or(0) {
            p if p > 0 => p,
            _ if app.listen_port > 0 => app.listen_port,
            _ => 0,
        };
        if listen_port > 0 {
            let g = self.inner.lock().await;
            Self::ensure_port_available_locked(&g, listen_port, Some(app.app_id.as_str()))?;
        }
        let inst = AppInstance {
            instance_id: instance_id.clone(),
            request_id: request_id.clone(),
            app_id: app.app_id.clone(),
            device_id: req.device_id.clone(),
            placement_id: placement.placement_id.clone(),
            state: InstanceState::Starting,
            // Pre-occupy the expected port so a concurrent Start of the same
            // app fails the port check; the Service receipt overwrites it if
            // it actually bound elsewhere.
            listen_port,
            pid: 0,
            error: String::new(),
            web_client_hint: String::new(),
        };
        {
            let mut g = self.inner.lock().await;
            g.request_index
                .insert(request_id.clone(), instance_id.clone());
            g.instances.insert(instance_id.clone(), inst.clone());
        }
        if let Err(e) = crate::app_schedule::store::upsert_instance(&inst).await {
            tracing::warn!("persist instance failed: {e}");
        }

        let start = SpvrServiceStartAppInstance {
            request_id: request_id.clone(),
            instance_id: instance_id.clone(),
            app_id: app.app_id,
            install_root,
            game_exe_rel,
            game_arguments: app.default_game_args,
            listen_port,
            encoder_fps: app.encoder_fps,
            encoder_bitrate: app.encoder_bitrate,
            encoder_format: app.encoder_format,
            webrtc_enabled: app.webrtc_enabled,
            websocket_enabled: app.websocket_enabled,
        };

        let (wait_tx, wait_rx) = oneshot::channel();
        {
            let mut g = self.inner.lock().await;
            g.start_waiters.insert(request_id.clone(), wait_tx);
        }

        let ok = conn.lock().await.send_start_app_instance(start).await;
        if !ok {
            let snapshot = {
                let mut g = self.inner.lock().await;
                g.start_waiters.remove(&request_id);
                g.request_index.remove(&request_id);
                if let Some(i) = g.instances.get_mut(&instance_id) {
                    i.state = InstanceState::Failed;
                    i.error = "下发到 Service 失败".to_string();
                    Some(i.clone())
                } else {
                    None
                }
            };
            if let Some(failed) = snapshot {
                let _ = crate::app_schedule::store::upsert_instance(&failed).await;
            }
            return Err("下发到 Service 失败".to_string());
        }

        // Block HTTP until Service reports success/failure so Web can toast the error.
        match tokio::time::timeout(START_RESULT_TIMEOUT, wait_rx).await {
            Ok(Ok(final_inst)) => {
                if matches!(final_inst.state, InstanceState::Failed) {
                    let msg = if final_inst.error.is_empty() {
                        "启动失败".to_string()
                    } else {
                        final_inst.error.clone()
                    };
                    Err(msg)
                } else {
                    Ok(final_inst)
                }
            }
            Ok(Err(_)) => Err("启动结果通道已关闭".to_string()),
            Err(_) => {
                let snapshot = {
                    let mut g = self.inner.lock().await;
                    g.start_waiters.remove(&request_id);
                    // Drop the request mapping so a late receipt cannot find
                    // this instance and resurrect it after we report Failed.
                    g.request_index.remove(&request_id);
                    if let Some(i) = g.instances.get_mut(&instance_id) {
                        if matches!(i.state, InstanceState::Starting) {
                            i.state = InstanceState::Failed;
                            i.error = "等待 Service 启动结果超时".to_string();
                        }
                        Some(i.clone())
                    } else {
                        None
                    }
                };
                if let Some(failed) = snapshot {
                    let _ = crate::app_schedule::store::upsert_instance(&failed).await;
                    return Err(if failed.error.is_empty() {
                        "等待 Service 启动结果超时".to_string()
                    } else {
                        failed.error
                    });
                }
                Err("等待 Service 启动结果超时".to_string())
            }
        }
    }

    pub async fn stop_instance(&self, instance_id: &str) -> Result<AppInstance, String> {
        let request_id = self.next_id("req");
        let (device_id, stopping) = {
            let mut g = self.inner.lock().await;
            let inst = g
                .instances
                .get_mut(instance_id)
                .ok_or_else(|| format!("unknown instance {instance_id}"))?;
            if matches!(inst.state, InstanceState::Stopped | InstanceState::Failed) {
                return Err(format!("instance already {:?}", inst.state));
            }
            inst.state = InstanceState::Stopping;
            inst.request_id = request_id.clone();
            let device_id = inst.device_id.clone();
            let snapshot = inst.clone();
            g.request_index
                .insert(request_id.clone(), instance_id.to_string());
            (device_id, snapshot)
        };
        let _ = crate::app_schedule::store::upsert_instance(&stopping).await;

        let conn = match gSpvrServiceConnMgr.get_conn(device_id.clone()).await {
            Ok(c) => c,
            Err(_) => {
                // Service gone: nothing left to stop — clear sticky Stopping.
                let snapshot = {
                    let mut g = self.inner.lock().await;
                    g.request_index.remove(&request_id);
                    if let Some(i) = g.instances.get_mut(instance_id) {
                        i.state = InstanceState::Stopped;
                        i.pid = 0;
                        i.error.clear();
                        Some(i.clone())
                    } else {
                        None
                    }
                };
                if let Some(s) = snapshot {
                    let _ = crate::app_schedule::store::upsert_instance(&s).await;
                }
                return Err(format!("service offline: {device_id}（已标记为停止）"));
            }
        };
        let stop = SpvrServiceStopAppInstance {
            request_id,
            instance_id: instance_id.to_string(),
        };
        let ok = conn.lock().await.send_stop_app_instance(stop).await;
        if !ok {
            let snapshot = {
                let mut g = self.inner.lock().await;
                let g = &mut *g;
                if let Some(i) = g.instances.get_mut(instance_id) {
                    i.state = InstanceState::Failed;
                    i.error = "下发停止失败".to_string();
                    g.request_index.remove(&i.request_id);
                    Some(i.clone())
                } else {
                    None
                }
            };
            if let Some(s) = snapshot {
                let _ = crate::app_schedule::store::upsert_instance(&s).await;
            }
            return Err("下发停止失败".to_string());
        }
        let g = self.inner.lock().await;
        Ok(g.instances.get(instance_id).cloned().unwrap())
    }

    pub async fn on_start_result(
        &self,
        device_id: String,
        result: SpvrServiceStartAppInstanceResult,
    ) {
        let mut guard = self.inner.lock().await;
        let g = &mut *guard;
        let Some(instance_id) = g.request_index.get(&result.request_id).cloned() else {
            tracing::warn!(
                "start result unknown request_id {} from {}",
                result.request_id,
                device_id
            );
            return;
        };
        let waiter = g.start_waiters.remove(&result.request_id);
        let snapshot = if let Some(inst) = g.instances.get_mut(&instance_id) {
            if inst.device_id != device_id {
                tracing::warn!(
                    "start result device mismatch: receipt from {} but instance {} belongs to {} — ignored",
                    device_id,
                    instance_id,
                    inst.device_id
                );
                None
            } else if result.ok && matches!(inst.state, InstanceState::Failed) {
                // Late receipt after the wait already timed out: the user saw
                // the failure and may have retried — do not resurrect. If the
                // process really came up, the next HB reconcile restores it.
                tracing::warn!(
                    "late ok start result for failed instance {} from {} — ignored",
                    instance_id,
                    device_id
                );
                None
            } else {
                if result.ok {
                    inst.state = InstanceState::Running;
                    inst.listen_port = result.listen_port;
                    inst.pid = result.pid;
                    inst.error.clear();
                    inst.web_client_hint = format!(
                        "/web_client/?deviceId={}&instanceId={}",
                        inst.device_id, inst.instance_id
                    );
                } else {
                    inst.state = InstanceState::Failed;
                    inst.error = result.error;
                    // Terminal state: drop the request mapping.
                    g.request_index.remove(&result.request_id);
                }
                Some(inst.clone())
            }
        } else {
            None
        };
        drop(guard);
        if let Some(inst) = snapshot.clone() {
            let _ = crate::app_schedule::store::upsert_instance(&inst).await;
        }
        if let (Some(tx), Some(inst)) = (waiter, snapshot) {
            let _ = tx.send(inst);
        }
    }

    pub async fn on_stop_result(
        &self,
        device_id: String,
        result: SpvrServiceStopAppInstanceResult,
    ) {
        let already_gone = !result.ok
            && (result.error.contains("unknown instance_id")
                || result.error.contains("unknown instance"));
        let treat_stopped = result.ok || already_gone;
        let mut guard = self.inner.lock().await;
        let g = &mut *guard;
        let Some(instance_id) = g.request_index.get(&result.request_id).cloned() else {
            // Also match by instance_id directly
            if let Some(inst) = g.instances.get_mut(&result.instance_id) {
                if inst.device_id != device_id {
                    tracing::warn!(
                        "stop result device mismatch: receipt from {} but instance {} belongs to {} — ignored",
                        device_id,
                        result.instance_id,
                        inst.device_id
                    );
                    return;
                }
                if treat_stopped {
                    inst.state = InstanceState::Stopped;
                    inst.pid = 0;
                    inst.error.clear();
                } else {
                    // Do not leave Stopping forever on stop failure.
                    inst.state = InstanceState::Failed;
                    inst.error = result.error;
                }
                g.request_index.remove(&inst.request_id);
                let snap = inst.clone();
                drop(guard);
                let _ = crate::app_schedule::store::upsert_instance(&snap).await;
            } else {
                tracing::warn!(
                    "stop result unknown request {} from {}",
                    result.request_id,
                    device_id
                );
            }
            return;
        };
        let snapshot = if let Some(inst) = g.instances.get_mut(&instance_id) {
            if inst.device_id != device_id {
                tracing::warn!(
                    "stop result device mismatch: receipt from {} but instance {} belongs to {} — ignored",
                    device_id,
                    instance_id,
                    inst.device_id
                );
                None
            } else {
                if treat_stopped {
                    inst.state = InstanceState::Stopped;
                    inst.pid = 0;
                    inst.error.clear();
                } else {
                    inst.state = InstanceState::Failed;
                    inst.error = result.error;
                }
                // Terminal state: drop the request mapping.
                g.request_index.remove(&result.request_id);
                Some(inst.clone())
            }
        } else {
            None
        };
        drop(guard);
        if let Some(inst) = snapshot {
            let _ = crate::app_schedule::store::upsert_instance(&inst).await;
        }
    }

    /// Align CMS state with what Service reports in HB — in both directions:
    /// active in CMS but missing from HB => stopped; active in HB but
    /// stopped/failed in CMS => revived to Running (pid/port backfilled).
    /// Only `running`/`starting`/`stopping` rows in HB count as alive — a ghost
    /// `stopped` entry with the same instance_id must not keep CMS in Running.
    pub async fn reconcile_from_service_hb(&self, device_id: String, instances_json: &str) {
        #[derive(Deserialize)]
        struct Reported {
            instance_id: String,
            #[serde(default)]
            state: String,
            #[serde(default)]
            pid: u32,
            #[serde(default)]
            listen_port: i32,
        }
        let reported: Vec<Reported> = if instances_json.trim().is_empty() {
            Vec::new()
        } else {
            match serde_json::from_str(instances_json) {
                Ok(v) => v,
                Err(e) => {
                    // A malformed packet is not "no instances" — skip this
                    // round instead of wiping every Running instance.
                    tracing::warn!(
                        "reconcile: bad instances_json from device {}: {} — reconcile skipped",
                        device_id,
                        e
                    );
                    return;
                }
            }
        };
        // Empty state: legacy Service builds omitted the field — treat the row
        // as present/active. Newer proto builds always carry an explicit state.
        let is_active = |state: &str| {
            let s = state.to_ascii_lowercase();
            s.is_empty() || s == "running" || s == "starting" || s == "stopping"
        };
        let active: HashMap<String, &Reported> = reported
            .iter()
            .filter(|r| is_active(&r.state))
            .map(|r| (r.instance_id.clone(), r))
            .collect();

        let (snapshots, known_ids) = {
            let mut g = self.inner.lock().await;
            let g = &mut *g;
            let mut out = Vec::new();
            for inst in g.instances.values_mut() {
                if inst.device_id != device_id {
                    continue;
                }
                match inst.state {
                    InstanceState::Running | InstanceState::Stopping => {
                        if !active.contains_key(&inst.instance_id) {
                            tracing::info!(
                                "reconcile: device {} instance {} was {:?} but not active in service HB — mark stopped",
                                device_id,
                                inst.instance_id,
                                inst.state
                            );
                            inst.state = InstanceState::Stopped;
                            inst.pid = 0;
                            inst.error.clear();
                            g.request_index.remove(&inst.request_id);
                            out.push(inst.clone());
                        }
                    }
                    InstanceState::Stopped | InstanceState::Failed => {
                        if let Some(rep) = active.get(&inst.instance_id) {
                            tracing::info!(
                                "reconcile: device {} instance {} was {:?} but active in service HB — restore running (pid={} port={})",
                                device_id,
                                inst.instance_id,
                                inst.state,
                                rep.pid,
                                rep.listen_port
                            );
                            inst.state = InstanceState::Running;
                            inst.pid = rep.pid;
                            if rep.listen_port > 0 {
                                inst.listen_port = rep.listen_port;
                            }
                            inst.error.clear();
                            out.push(inst.clone());
                        }
                    }
                    // Starting: still waiting for the start receipt; HB alone
                    // must not flip it either way.
                    InstanceState::Starting => {}
                }
            }
            let known: std::collections::HashSet<String> = g
                .instances
                .values()
                .filter(|i| i.device_id == device_id)
                .map(|i| i.instance_id.clone())
                .collect();
            (out, known)
        };
        for r in &reported {
            if !known_ids.contains(&r.instance_id) {
                tracing::debug!(
                    "reconcile: service HB reports unknown instance {} on device {}",
                    r.instance_id,
                    device_id
                );
            }
        }
        for inst in snapshots {
            let _ = crate::app_schedule::store::upsert_instance(&inst).await;
        }
    }

    /// CMS restarted while waiting for a receipt: transitional states would
    /// otherwise live forever. starting => failed, stopping => stopped.
    /// Returns true when the instance was healed (caller must persist it).
    fn heal_instance_after_restart(inst: &mut AppInstance) -> bool {
        match inst.state {
            InstanceState::Starting => {
                inst.state = InstanceState::Failed;
                inst.error = "CMS restarted".to_string();
                true
            }
            InstanceState::Stopping => {
                inst.state = InstanceState::Stopped;
                inst.pid = 0;
                inst.error.clear();
                true
            }
            _ => false,
        }
    }

    pub async fn load_from_db(&self) {
        match crate::app_schedule::store::load_all().await {
            Ok((apps, placements, instances)) => {
                let healed = {
                    let mut g = self.inner.lock().await;
                    for app in apps {
                        g.apps.insert(app.app_id.clone(), app);
                    }
                    for p in placements {
                        g.placement_by_app_device.insert(
                            (p.app_id.clone(), p.device_id.clone()),
                            p.placement_id.clone(),
                        );
                        g.placements.insert(p.placement_id.clone(), p);
                    }
                    let mut healed = Vec::new();
                    for mut i in instances {
                        if Self::heal_instance_after_restart(&mut i) {
                            tracing::info!(
                                "load: heal stale transitional instance {} -> {:?} (CMS restarted)",
                                i.instance_id,
                                i.state
                            );
                            healed.push(i.clone());
                        } else {
                            g.request_index
                                .insert(i.request_id.clone(), i.instance_id.clone());
                        }
                        g.instances.insert(i.instance_id.clone(), i);
                    }
                    tracing::info!(
                        "app schedule loaded from mongo: apps={} placements={} instances={} healed={}",
                        g.apps.len(),
                        g.placements.len(),
                        g.instances.len(),
                        healed.len()
                    );
                    healed
                };
                for i in healed {
                    let _ = crate::app_schedule::store::upsert_instance(&i).await;
                }
            }
            Err(e) => tracing::warn!("load app schedule from mongo failed: {e}"),
        }
    }

    /// Test helper: inject without network.
    pub async fn inject_for_test(&self, app: Application, placement: AppPlacement, inst: AppInstance) {
        let mut g = self.inner.lock().await;
        g.placement_by_app_device.insert(
            (placement.app_id.clone(), placement.device_id.clone()),
            placement.placement_id.clone(),
        );
        g.apps.insert(app.app_id.clone(), app);
        g.placements
            .insert(placement.placement_id.clone(), placement);
        g.request_index
            .insert(inst.request_id.clone(), inst.instance_id.clone());
        g.instances.insert(inst.instance_id.clone(), inst);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn create_app_and_placements_on_two_machines() {
        let mgr = AppScheduleManager::new();
        let app = mgr
            .create_application(CreateApplicationReq {
                name: "CarGame".into(),
                game_exe_rel: r"Binaries\Win64\game.exe".into(),
                default_game_args: Some("-dx11".into()),
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
                game_path: None,
                listen_port: None,
            })
            .await
            .unwrap();
        let p1 = mgr
            .create_placement(CreatePlacementReq {
                app_id: app.app_id.clone(),
                device_id: "machine-a".into(),
                install_root: r"D:\apps\CarGame".into(),
            })
            .await
            .unwrap();
        let p2 = mgr
            .create_placement(CreatePlacementReq {
                app_id: app.app_id.clone(),
                device_id: "machine-b".into(),
                install_root: r"E:\games\CarGame".into(),
            })
            .await
            .unwrap();
        assert_ne!(p1.install_root, p2.install_root);
        assert_eq!(mgr.list_placements().await.len(), 2);
        // duplicate placement rejected
        let err = mgr
            .create_placement(CreatePlacementReq {
                app_id: app.app_id.clone(),
                device_id: "machine-a".into(),
                install_root: r"D:\other".into(),
            })
            .await
            .unwrap_err();
        assert!(err.contains("already exists"));
    }

    #[tokio::test]
    async fn start_requires_placement_and_online_service() {
        let mgr = AppScheduleManager::new();
        let app = mgr
            .create_application(CreateApplicationReq {
                name: "X".into(),
                game_exe_rel: "game.exe".into(),
                default_game_args: None,
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
                game_path: None,
                listen_port: None,
            })
            .await
            .unwrap();
        let err = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id.clone(),
                device_id: "no-place".into(),
                listen_port: None,
            })
            .await
            .unwrap_err();
        assert!(err.contains("no placement"));

        mgr.create_placement(CreatePlacementReq {
            app_id: app.app_id.clone(),
            device_id: "offline-dev".into(),
            install_root: r"D:\app".into(),
        })
        .await
        .unwrap();
        let err = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id,
                device_id: "offline-dev".into(),
                listen_port: Some(32000),
            })
            .await
            .unwrap_err();
        assert!(err.contains("offline"));
    }

    #[tokio::test]
    async fn start_and_stop_result_updates_state() {
        let mgr = AppScheduleManager::new();
        let app = Application {
            app_id: "app-1".into(),
            name: "N".into(),
            game_path: r"D:\a\g.exe".into(),
            game_exe_rel: "g.exe".into(),
            default_game_args: String::new(),
            encoder_fps: 60,
            encoder_bitrate: 20,
            encoder_format: "h264".into(),
            webrtc_enabled: true,
            websocket_enabled: true,
            listen_port: 32000,
        };
        let placement = AppPlacement {
            placement_id: "plc-1".into(),
            app_id: "app-1".into(),
            device_id: "dev-1".into(),
            install_root: r"D:\a".into(),
        };
        let inst = AppInstance {
            instance_id: "inst-1".into(),
            request_id: "req-1".into(),
            app_id: "app-1".into(),
            device_id: "dev-1".into(),
            placement_id: "plc-1".into(),
            state: InstanceState::Starting,
            listen_port: 0,
            pid: 0,
            error: String::new(),
            web_client_hint: String::new(),
        };
        mgr.inject_for_test(app, placement, inst).await;
        mgr.on_start_result(
            "dev-1".into(),
            SpvrServiceStartAppInstanceResult {
                request_id: "req-1".into(),
                instance_id: "inst-1".into(),
                ok: true,
                error: String::new(),
                listen_port: 32055,
                pid: 4242,
            },
        )
        .await;
        let running = mgr.list_instances().await;
        assert_eq!(running[0].state, InstanceState::Running);
        assert_eq!(running[0].listen_port, 32055);
        assert!(running[0].web_client_hint.contains("instanceId=inst-1"));

        mgr.on_stop_result(
            "dev-1".into(),
            SpvrServiceStopAppInstanceResult {
                request_id: "req-1".into(),
                instance_id: "inst-1".into(),
                ok: true,
                error: String::new(),
            },
        )
        .await;
        assert_eq!(
            mgr.list_instances().await[0].state,
            InstanceState::Stopped
        );
    }

    #[tokio::test]
    async fn failed_start_result() {
        let mgr = AppScheduleManager::new();
        mgr.inject_for_test(
            Application {
                app_id: "a".into(),
                name: "n".into(),
                game_path: r"D:\x\e.exe".into(),
                game_exe_rel: "e".into(),
                default_game_args: String::new(),
                encoder_fps: 60,
                encoder_bitrate: 20,
                encoder_format: "h264".into(),
                webrtc_enabled: true,
                websocket_enabled: true,
                listen_port: 32001,
            },
            AppPlacement {
                placement_id: "p".into(),
                app_id: "a".into(),
                device_id: "d".into(),
                install_root: r"D:\x".into(),
            },
            AppInstance {
                instance_id: "i".into(),
                request_id: "r".into(),
                app_id: "a".into(),
                device_id: "d".into(),
                placement_id: "p".into(),
                state: InstanceState::Starting,
                listen_port: 0,
                pid: 0,
                error: String::new(),
                web_client_hint: String::new(),
            },
        )
        .await;
        mgr.on_start_result(
            "d".into(),
            SpvrServiceStartAppInstanceResult {
                request_id: "r".into(),
                instance_id: "i".into(),
                ok: false,
                error: "exe not found".into(),
                listen_port: 0,
                pid: 0,
            },
        )
        .await;
        let i = &mgr.list_instances().await[0];
        assert_eq!(i.state, InstanceState::Failed);
        assert_eq!(i.error, "exe not found");
    }

    #[tokio::test]
    async fn stop_unknown_instance_marks_stopped() {
        let mgr = AppScheduleManager::new();
        mgr.inject_for_test(
            Application {
                app_id: "a".into(),
                name: "n".into(),
                game_path: r"D:\x\e.exe".into(),
                game_exe_rel: "e".into(),
                default_game_args: String::new(),
                encoder_fps: 60,
                encoder_bitrate: 20,
                encoder_format: "h264".into(),
                webrtc_enabled: true,
                websocket_enabled: true,
                listen_port: 32000,
            },
            AppPlacement {
                placement_id: "p".into(),
                app_id: "a".into(),
                device_id: "d".into(),
                install_root: r"D:\x".into(),
            },
            AppInstance {
                instance_id: "i".into(),
                request_id: "r-stop".into(),
                app_id: "a".into(),
                device_id: "d".into(),
                placement_id: "p".into(),
                state: InstanceState::Stopping,
                listen_port: 32000,
                pid: 1,
                error: String::new(),
                web_client_hint: String::new(),
            },
        )
        .await;
        mgr.on_stop_result(
            "d".into(),
            SpvrServiceStopAppInstanceResult {
                request_id: "r-stop".into(),
                instance_id: "i".into(),
                ok: false,
                error: "unknown instance_id i".into(),
            },
        )
        .await;
        let i = &mgr.list_instances().await[0];
        assert_eq!(i.state, InstanceState::Stopped);
        assert!(i.error.is_empty());
    }

    #[tokio::test]
    async fn reconcile_clears_stale_running() {
        let mgr = AppScheduleManager::new();
        mgr.inject_for_test(
            Application {
                app_id: "a".into(),
                name: "n".into(),
                game_path: r"D:\x\e.exe".into(),
                game_exe_rel: "e".into(),
                default_game_args: String::new(),
                encoder_fps: 60,
                encoder_bitrate: 20,
                encoder_format: "h264".into(),
                webrtc_enabled: true,
                websocket_enabled: true,
                listen_port: 32000,
            },
            AppPlacement {
                placement_id: "p".into(),
                app_id: "a".into(),
                device_id: "dev-1".into(),
                install_root: r"D:\x".into(),
            },
            AppInstance {
                instance_id: "ghost".into(),
                request_id: "r".into(),
                app_id: "a".into(),
                device_id: "dev-1".into(),
                placement_id: "p".into(),
                state: InstanceState::Running,
                listen_port: 32000,
                pid: 99,
                error: String::new(),
                web_client_hint: String::new(),
            },
        )
        .await;
        mgr.reconcile_from_service_hb("dev-1".into(), "[]").await;
        let i = &mgr.list_instances().await[0];
        assert_eq!(i.state, InstanceState::Stopped);
        assert_eq!(i.pid, 0);
    }

    #[tokio::test]
    async fn reconcile_ignores_stopped_hb_entries() {
        let mgr = AppScheduleManager::new();
        mgr.inject_for_test(
            Application {
                app_id: "a".into(),
                name: "n".into(),
                game_path: r"D:\x\e.exe".into(),
                game_exe_rel: "e".into(),
                default_game_args: String::new(),
                encoder_fps: 60,
                encoder_bitrate: 20,
                encoder_format: "h264".into(),
                webrtc_enabled: true,
                websocket_enabled: true,
                listen_port: 32000,
            },
            AppPlacement {
                placement_id: "p".into(),
                app_id: "a".into(),
                device_id: "dev-1".into(),
                install_root: r"D:\x".into(),
            },
            AppInstance {
                instance_id: "ghost".into(),
                request_id: "r".into(),
                app_id: "a".into(),
                device_id: "dev-1".into(),
                placement_id: "p".into(),
                state: InstanceState::Running,
                listen_port: 32000,
                pid: 99,
                error: String::new(),
                web_client_hint: String::new(),
            },
        )
        .await;
        // Same id still listed, but Service says stopped → CMS must clear Running.
        mgr.reconcile_from_service_hb(
            "dev-1".into(),
            r#"[{"instance_id":"ghost","state":"stopped"}]"#,
        )
        .await;
        let i = &mgr.list_instances().await[0];
        assert_eq!(i.state, InstanceState::Stopped);
    }

    #[test]
    fn split_absolute_game_path() {
        let (root, rel) =
            split_game_path(r"D:\1_test_games\CarGame\Binaries\Win64\VehicleGame.exe").unwrap();
        assert_eq!(root, r"D:\1_test_games\CarGame\Binaries\Win64");
        assert_eq!(rel, "VehicleGame.exe");
        assert!(split_game_path("relative\\a.exe").is_err());
    }

    #[test]
    fn resolve_start_paths_heals_absolute_exe_rel() {
        let abs = r"D:\games\Binaries\Win64\game.exe";
        let (root, rel) = resolve_start_paths("", r"D:\stale", abs).unwrap();
        assert_eq!(root, r"D:\games\Binaries\Win64");
        assert_eq!(rel, "game.exe");
        let (root2, rel2) = resolve_start_paths(abs, r"D:\ignored", "also-ignored.exe").unwrap();
        assert_eq!((root2, rel2), (root, rel));
    }

    #[tokio::test]
    async fn save_app_assigns_incremental_ports_and_rejects_conflict() {
        let mgr = AppScheduleManager::new();
        let a1 = mgr
            .save_app(SaveAppReq {
                app_id: None,
                name: "A".into(),
                device_id: "m1".into(),
                game_path: r"D:\games\a\game.exe".into(),
                default_game_args: None,
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
                listen_port: None,
            })
            .await
            .unwrap();
        assert_eq!(a1.listen_port, 32000);
        let a2 = mgr
            .save_app(SaveAppReq {
                app_id: None,
                name: "B".into(),
                device_id: "m1".into(),
                game_path: r"D:\games\b\game.exe".into(),
                default_game_args: None,
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
                listen_port: None,
            })
            .await
            .unwrap();
        assert_eq!(a2.listen_port, 32001);
        let err = mgr
            .save_app(SaveAppReq {
                app_id: None,
                name: "C".into(),
                device_id: "m2".into(),
                game_path: r"D:\games\c\game.exe".into(),
                default_game_args: None,
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
                listen_port: Some(32000),
            })
            .await
            .unwrap_err();
        assert!(err.contains("占用"));

        mgr.delete_app(&a1.app_id).await.unwrap();
        assert_eq!(mgr.list_app_rows().await.len(), 1);
    }

    fn app_with_port(app_id: &str, listen_port: i32) -> Application {
        Application {
            app_id: app_id.into(),
            name: format!("n-{app_id}"),
            game_path: r"D:\x\e.exe".into(),
            game_exe_rel: "e.exe".into(),
            default_game_args: String::new(),
            encoder_fps: 60,
            encoder_bitrate: 20,
            encoder_format: "h264".into(),
            webrtc_enabled: true,
            websocket_enabled: true,
            listen_port,
        }
    }

    #[tokio::test]
    async fn suggest_next_port_increments_from_pool_start() {
        let mgr = AppScheduleManager::new();
        assert_eq!(mgr.suggest_next_port().await.unwrap(), 32000);
        {
            let mut g = mgr.inner.lock().await;
            g.apps.insert("a".into(), app_with_port("a", 32000));
            g.apps.insert("b".into(), app_with_port("b", 32001));
        }
        assert_eq!(mgr.suggest_next_port().await.unwrap(), 32002);
    }

    #[tokio::test]
    async fn suggest_next_port_wraps_to_first_free_after_pool_end() {
        let mgr = AppScheduleManager::new();
        {
            let mut g = mgr.inner.lock().await;
            g.apps.insert("a".into(), app_with_port("a", 32999));
        }
        // max+1 exceeds the pool: suggest the first free port from the start.
        assert_eq!(mgr.suggest_next_port().await.unwrap(), 32000);
        {
            let mut g = mgr.inner.lock().await;
            g.apps.insert("b".into(), app_with_port("b", 32000));
        }
        assert_eq!(mgr.suggest_next_port().await.unwrap(), 32001);
    }

    #[tokio::test]
    async fn suggest_next_port_errors_when_pool_full() {
        let mgr = AppScheduleManager::new();
        {
            let mut g = mgr.inner.lock().await;
            for port in 32000..=32999 {
                let id = format!("a-{port}");
                g.apps.insert(id.clone(), app_with_port(&id, port));
            }
        }
        let err = mgr.suggest_next_port().await.unwrap_err();
        assert!(err.contains("端口已用完"), "{err}");
        assert!(err.contains("32000-32999"), "{err}");
    }

    fn fixture(state: InstanceState) -> (Application, AppPlacement, AppInstance) {
        (
            Application {
                app_id: "a".into(),
                name: "n".into(),
                game_path: r"D:\x\e.exe".into(),
                game_exe_rel: "e".into(),
                default_game_args: String::new(),
                encoder_fps: 60,
                encoder_bitrate: 20,
                encoder_format: "h264".into(),
                webrtc_enabled: true,
                websocket_enabled: true,
                listen_port: 32000,
            },
            AppPlacement {
                placement_id: "p".into(),
                app_id: "a".into(),
                device_id: "d".into(),
                install_root: r"D:\x".into(),
            },
            AppInstance {
                instance_id: "i".into(),
                request_id: "r".into(),
                app_id: "a".into(),
                device_id: "d".into(),
                placement_id: "p".into(),
                state,
                listen_port: 32000,
                pid: 0,
                error: String::new(),
                web_client_hint: String::new(),
            },
        )
    }

    #[tokio::test]
    async fn reconcile_skips_on_bad_json() {
        let mgr = AppScheduleManager::new();
        let (app, plc, inst) = fixture(InstanceState::Running);
        mgr.inject_for_test(app, plc, inst).await;
        // Malformed packet must not be treated as "no instances".
        mgr.reconcile_from_service_hb("d".into(), "{not-json").await;
        assert_eq!(
            mgr.list_instances().await[0].state,
            InstanceState::Running
        );
    }

    #[tokio::test]
    async fn reconcile_revives_stopped_and_failed_from_hb() {
        let mgr = AppScheduleManager::new();
        let (app, plc, mut inst) = fixture(InstanceState::Stopped);
        inst.pid = 0;
        inst.error = "boom".into();
        mgr.inject_for_test(app, plc, inst).await;
        mgr.reconcile_from_service_hb(
            "d".into(),
            r#"[{"instance_id":"i","state":"running","pid":777,"listen_port":32010}]"#,
        )
        .await;
        let i = &mgr.list_instances().await[0];
        assert_eq!(i.state, InstanceState::Running);
        assert_eq!(i.pid, 777);
        assert_eq!(i.listen_port, 32010);
        assert!(i.error.is_empty());

        // Failed instances are revived too.
        let mgr2 = AppScheduleManager::new();
        let (app, plc, mut inst) = fixture(InstanceState::Failed);
        inst.error = "timeout".into();
        mgr2.inject_for_test(app, plc, inst).await;
        mgr2
            .reconcile_from_service_hb("d".into(), r#"[{"instance_id":"i","state":"running"}]"#)
            .await;
        assert_eq!(mgr2.list_instances().await[0].state, InstanceState::Running);
    }

    #[tokio::test]
    async fn late_start_result_does_not_revive_failed() {
        let mgr = AppScheduleManager::new();
        let (app, plc, mut inst) = fixture(InstanceState::Failed);
        inst.error = "等待 Service 启动结果超时".into();
        mgr.inject_for_test(app, plc, inst).await;
        mgr.on_start_result(
            "d".into(),
            SpvrServiceStartAppInstanceResult {
                request_id: "r".into(),
                instance_id: "i".into(),
                ok: true,
                error: String::new(),
                listen_port: 32055,
                pid: 4242,
            },
        )
        .await;
        assert_eq!(mgr.list_instances().await[0].state, InstanceState::Failed);
    }

    #[tokio::test]
    async fn start_result_device_mismatch_ignored() {
        let mgr = AppScheduleManager::new();
        let (app, plc, inst) = fixture(InstanceState::Starting);
        mgr.inject_for_test(app, plc, inst).await;
        mgr.on_start_result(
            "other-dev".into(),
            SpvrServiceStartAppInstanceResult {
                request_id: "r".into(),
                instance_id: "i".into(),
                ok: true,
                error: String::new(),
                listen_port: 32055,
                pid: 4242,
            },
        )
        .await;
        assert_eq!(
            mgr.list_instances().await[0].state,
            InstanceState::Starting
        );
    }

    #[tokio::test]
    async fn heal_instance_after_restart_fixes_transitional_states() {
        let (_, _, mut inst) = fixture(InstanceState::Starting);
        assert!(AppScheduleManager::heal_instance_after_restart(&mut inst));
        assert_eq!(inst.state, InstanceState::Failed);
        assert_eq!(inst.error, "CMS restarted");

        let (_, _, mut inst) = fixture(InstanceState::Stopping);
        inst.pid = 5;
        assert!(AppScheduleManager::heal_instance_after_restart(&mut inst));
        assert_eq!(inst.state, InstanceState::Stopped);
        assert_eq!(inst.pid, 0);

        let (_, _, mut inst) = fixture(InstanceState::Running);
        assert!(!AppScheduleManager::heal_instance_after_restart(&mut inst));
        assert_eq!(inst.state, InstanceState::Running);
    }

    #[tokio::test]
    async fn save_app_rejects_port_out_of_range() {
        let mgr = AppScheduleManager::new();
        for port in [31999, 33000] {
            let err = mgr
                .save_app(SaveAppReq {
                    app_id: None,
                    name: "P".into(),
                    device_id: "m1".into(),
                    game_path: r"D:\games\p\game.exe".into(),
                    default_game_args: None,
                    encoder_fps: None,
                    encoder_bitrate: None,
                    encoder_format: None,
                    listen_port: Some(port),
                })
                .await
                .unwrap_err();
            assert!(err.contains("32000-32999"), "port {port}: {err}");
        }
    }
}
