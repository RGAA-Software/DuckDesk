//! Application / Placement / Instance orchestration for CMS.
//! Memory + Mongo (when DB ready); unit tests cover multi-machine scheduling.

use crate::gCmsServiceConnMgr;
use protocol::cms_service::{
    CmsServiceStartAppInstance, CmsServiceStartAppInstanceResult, CmsServiceStopAppInstance,
    CmsServiceStopAppInstanceResult,
};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Duration;
use tokio::sync::{oneshot, Mutex};
use uuid::Uuid;

/// How long HTTP start waits for Service StartAppInstanceResult before failing.
const START_RESULT_TIMEOUT: Duration = Duration::from_secs(25);

fn now_ms() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

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
    /// Legacy: preferred listen port. Superseded by AppNode::listen_port;
    /// kept only to migrate pre-node rows (see load_from_db).
    #[serde(default)]
    pub listen_port: i32,
}

/// 节(node):应用下的一路可运行单元。机器/安装目录/端口都绑在节上;
/// 多开 = 多建节。节本身无持久状态,运行状态由其活跃 Instance 推导。
#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct AppNode {
    pub node_id: String,
    pub app_id: String,
    pub name: String,
    pub device_id: String,
    pub install_root: String,
    pub listen_port: i32,
    /// Last time an instance of this node reached Running (ms epoch; 0 = never).
    /// App-level start picks the stalest free node.
    #[serde(default)]
    pub last_run_at: i64,
    /// Creation order, tie-break for node picking.
    #[serde(default)]
    pub seq_no: u64,
}

/// Flattened row for CMS Web list/edit. 节列表随行下发,实例由 Web 按
/// node_id 从 /instance/list 匹配(只绑活跃态)。
#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct AppRowVo {
    pub app_id: String,
    pub name: String,
    pub game_path: String,
    pub default_game_args: String,
    pub encoder_fps: i32,
    pub encoder_bitrate: i32,
    pub encoder_format: String,
    pub nodes: Vec<AppNode>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SaveAppReq {
    /// Empty/None = create; set = update.
    pub app_id: Option<String>,
    pub name: String,
    /// Absolute path to game exe.
    pub game_path: String,
    pub default_game_args: Option<String>,
    pub encoder_fps: Option<i32>,
    pub encoder_bitrate: Option<i32>,
    pub encoder_format: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SaveNodeReq {
    /// Empty/None = create; set = update.
    pub node_id: Option<String>,
    pub app_id: String,
    pub name: Option<String>,
    pub device_id: String,
    /// None = derive from the app's game_path directory.
    pub install_root: Option<String>,
    /// 0 / None = auto next free port on this device.
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
    /// 节 id;空串 = 节点结构之前的遗留实例(停止后自然消亡,不再展示)。
    #[serde(default)]
    pub node_id: String,
    pub state: InstanceState,
    pub listen_port: i32,
    pub pid: u32,
    pub error: String,
    pub web_client_hint: String,
    /// 发起方身份(launch 链接的客户端 IP);用于启动期同 key 去重,空串=不去重。
    #[serde(default)]
    pub client_key: String,
    /// 预占创建时间(ms);同 client_key 去重的窗口依据(启动回执 ~1s 就返回,
    /// 客户端重发到达时实例往往已 Running,只看 Starting 会漏)。
    #[serde(default)]
    pub created_at_ms: i64,
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
    /// 已废弃(自动选节);保留反序列化兼容旧调用,内容忽略。
    #[serde(default)]
    pub device_id: Option<String>,
    /// 已废弃(端口由节决定);保留反序列化兼容旧调用,内容忽略。
    #[serde(default)]
    pub listen_port: Option<i32>,
    /// 调用方身份(launch 页传浏览器 nonce,直连 GET 传客户端 IP)。非空时,
    /// 同 app 同 key 已有活跃实例则复用,不再新开 —— 防客户端对挂起的
    /// GET 重发(浏览器重试/链接预取/双击)导致一次操作多开。
    #[serde(default)]
    pub client_key: Option<String>,
    /// true: 同 key 的 Running 实例不限时长永久复用(launch 页 nonce,
    /// 「一个浏览器一个实例」);false: Running 仅在 60s 窗口内复用
    /// (IP 兜底,防重发但允许同机稍后多开)。
    #[serde(default)]
    pub client_key_permanent: bool,
}

#[derive(Default)]
struct Inner {
    apps: HashMap<String, Application>,
    nodes: HashMap<String, AppNode>,
    /// 遗留 placement(节点结构前的数据),load 迁移后只读。
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

    pub async fn create_application(
        &self,
        req: CreateApplicationReq,
    ) -> Result<Application, String> {
        if req.name.trim().is_empty() || req.game_exe_rel.trim().is_empty() {
            return Err("name and game_exe_rel required".to_string());
        }
        let game_path = req.game_path.unwrap_or_default().trim().to_string();
        let app = Application {
            app_id: self.next_id("app"),
            name: req.name.trim().to_string(),
            game_path,
            game_exe_rel: req.game_exe_rel.trim().to_string(),
            default_game_args: req.default_game_args.unwrap_or_default(),
            encoder_fps: req.encoder_fps.unwrap_or(60),
            encoder_bitrate: req.encoder_bitrate.unwrap_or(20),
            encoder_format: req.encoder_format.unwrap_or_else(|| "h264".to_string()),
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
        for app in g.apps.values() {
            let game_path = if !app.game_path.trim().is_empty() {
                app.game_path.clone()
            } else {
                // 没有绝对路径时借第一个节的 install_root 展示
                let root = g
                    .nodes
                    .values()
                    .find(|n| n.app_id == app.app_id)
                    .map(|n| n.install_root.clone())
                    .or_else(|| {
                        g.placements
                            .values()
                            .find(|p| p.app_id == app.app_id)
                            .map(|p| p.install_root.clone())
                    })
                    .unwrap_or_default();
                join_game_path(&root, &app.game_exe_rel)
            };
            let mut nodes: Vec<AppNode> = g
                .nodes
                .values()
                .filter(|n| n.app_id == app.app_id)
                .cloned()
                .collect();
            nodes.sort_by_key(|n| n.seq_no);
            rows.push(AppRowVo {
                app_id: app.app_id.clone(),
                name: app.name.clone(),
                game_path,
                default_game_args: app.default_game_args.clone(),
                encoder_fps: app.encoder_fps,
                encoder_bitrate: app.encoder_bitrate,
                encoder_format: app.encoder_format.clone(),
                nodes,
            });
        }
        rows.sort_by(|a, b| a.name.cmp(&b.name));
        rows
    }

    /// 端口占用以「机器」为维度:同机节的端口 + 同机活跃实例的端口。
    fn collect_used_ports_locked(g: &Inner, device_id: &str) -> Vec<i32> {
        let mut used = Vec::new();
        for node in g.nodes.values() {
            if !device_id.is_empty() && node.device_id != device_id {
                continue;
            }
            if node.listen_port > 0 {
                used.push(node.listen_port);
            }
        }
        for inst in g.instances.values() {
            if !device_id.is_empty() && inst.device_id != device_id {
                continue;
            }
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

    /// Suggest the next free listen port on a device for the Web form. Capped
    /// at `DEFAULT_LISTEN_PORT_END`: past the pool tail fall back to the first
    /// free port from the pool start; error when the pool is exhausted.
    pub async fn suggest_next_port(&self, device_id: &str) -> Result<i32, String> {
        let g = self.inner.lock().await;
        Self::suggest_next_port_locked(&g, device_id)
    }

    fn suggest_next_port_locked(g: &Inner, device_id: &str) -> Result<i32, String> {
        let used = Self::collect_used_ports_locked(g, device_id);
        let max = used
            .iter()
            .copied()
            .max()
            .unwrap_or(DEFAULT_LISTEN_PORT_START - 1);
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

    fn ensure_node_port_available_locked(
        g: &Inner,
        device_id: &str,
        port: i32,
        exclude_node_id: Option<&str>,
    ) -> Result<(), String> {
        if !(DEFAULT_LISTEN_PORT_START..=DEFAULT_LISTEN_PORT_END).contains(&port) {
            return Err(format!(
                "端口必须在 {DEFAULT_LISTEN_PORT_START}-{DEFAULT_LISTEN_PORT_END} 之间"
            ));
        }
        for node in g.nodes.values() {
            if exclude_node_id.is_some_and(|id| id == node.node_id) {
                continue;
            }
            if node.device_id == device_id && node.listen_port == port {
                return Err(format!("端口 {port} 已被节点「{}」占用", node.name));
            }
        }
        for inst in g.instances.values() {
            if inst.device_id == device_id
                && matches!(
                    inst.state,
                    InstanceState::Starting | InstanceState::Running | InstanceState::Stopping
                )
                && inst.listen_port == port
            {
                return Err(format!("端口 {port} 正被实例 {} 使用中", inst.instance_id));
            }
        }
        Ok(())
    }

    /// Create or update the application template (no machine/port anymore —
    /// those live on nodes).
    pub async fn save_app(&self, req: SaveAppReq) -> Result<AppRowVo, String> {
        if req.name.trim().is_empty() {
            return Err("请填写应用名称".to_string());
        }
        let (_, game_exe_rel) = split_game_path(&req.game_path)?;
        let game_path = req.game_path.trim().to_string();
        let editing_id = req
            .app_id
            .as_ref()
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty());
        let app_id = editing_id.clone().unwrap_or_else(|| self.next_id("app"));

        let app = {
            let mut g = self.inner.lock().await;
            let existing = editing_id
                .as_ref()
                .map(|id| {
                    g.apps
                        .get(id)
                        .cloned()
                        .ok_or_else(|| format!("应用不存在: {id}"))
                })
                .transpose()?;
            let app = Application {
                app_id: app_id.clone(),
                name: req.name.trim().to_string(),
                game_path,
                game_exe_rel,
                default_game_args: req.default_game_args.unwrap_or_else(|| {
                    existing
                        .as_ref()
                        .map(|e| e.default_game_args.clone())
                        .unwrap_or_default()
                }),
                encoder_fps: req
                    .encoder_fps
                    .unwrap_or_else(|| existing.as_ref().map(|e| e.encoder_fps).unwrap_or(60)),
                encoder_bitrate: req
                    .encoder_bitrate
                    .unwrap_or_else(|| existing.as_ref().map(|e| e.encoder_bitrate).unwrap_or(20)),
                encoder_format: req.encoder_format.unwrap_or_else(|| {
                    existing
                        .as_ref()
                        .map(|e| e.encoder_format.clone())
                        .unwrap_or_else(|| "h264".to_string())
                }),
                webrtc_enabled: true,
                websocket_enabled: true,
                listen_port: existing.as_ref().map(|e| e.listen_port).unwrap_or(0),
            };
            g.apps.insert(app.app_id.clone(), app.clone());
            app
        };
        let _ = crate::app_schedule::store::upsert_application(&app).await;
        let mut row = AppRowVo {
            app_id: app.app_id,
            name: app.name,
            game_path: app.game_path,
            default_game_args: app.default_game_args,
            encoder_fps: app.encoder_fps,
            encoder_bitrate: app.encoder_bitrate,
            encoder_format: app.encoder_format,
            nodes: Vec::new(),
        };
        let g = self.inner.lock().await;
        row.nodes = g
            .nodes
            .values()
            .filter(|n| n.app_id == row.app_id)
            .cloned()
            .collect();
        row.nodes.sort_by_key(|n| n.seq_no);
        Ok(row)
    }

    /// Create or update a node (机器/端口/目录都绑在节上)。
    pub async fn save_node(&self, req: SaveNodeReq) -> Result<AppNode, String> {
        if req.device_id.trim().is_empty() {
            return Err("请选择机器".to_string());
        }
        let device_id = req.device_id.trim().to_string();
        let editing_id = req
            .node_id
            .as_ref()
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty());

        let node = {
            let mut g = self.inner.lock().await;
            let app = g
                .apps
                .get(&req.app_id)
                .cloned()
                .ok_or_else(|| format!("应用不存在: {}", req.app_id))?;
            let existing = editing_id
                .as_ref()
                .map(|id| {
                    g.nodes
                        .get(id)
                        .cloned()
                        .ok_or_else(|| format!("节点不存在: {id}"))
                })
                .transpose()?;

            // 活跃实例存在时不允许换机器/端口(运行身份不可变)
            if let Some(ref old) = existing {
                let has_active = g.instances.values().any(|i| {
                    i.node_id == old.node_id
                        && matches!(
                            i.state,
                            InstanceState::Starting
                                | InstanceState::Running
                                | InstanceState::Stopping
                        )
                });
                if has_active
                    && (old.device_id != device_id
                        || (req.listen_port.unwrap_or(0) > 0
                            && req.listen_port.unwrap_or(0) != old.listen_port))
                {
                    return Err("节点运行中，不能更换机器或端口".to_string());
                }
            }

            let listen_port = match req.listen_port.unwrap_or(0) {
                p if p > 0 => {
                    Self::ensure_node_port_available_locked(
                        &g,
                        &device_id,
                        p,
                        editing_id.as_deref(),
                    )?;
                    p
                }
                _ if existing.as_ref().map(|e| e.listen_port).unwrap_or(0) > 0
                    && existing.as_ref().is_some_and(|e| e.device_id == device_id) =>
                {
                    // 编辑且未指定端口:保留原端口(已校验过)
                    existing.as_ref().unwrap().listen_port
                }
                _ => Self::suggest_next_port_locked(&g, &device_id)?,
            };

            let install_root = match req.install_root.as_ref().map(|s| s.trim()) {
                Some(s) if !s.is_empty() => s.to_string(),
                _ => existing
                    .as_ref()
                    .map(|e| e.install_root.clone())
                    .filter(|s| !s.is_empty())
                    .or_else(|| split_game_path(&app.game_path).ok().map(|(root, _)| root))
                    .ok_or_else(|| "install_root 为空且无法从应用路径推导".to_string())?,
            };

            let node = AppNode {
                node_id: editing_id.clone().unwrap_or_else(|| self.next_id("node")),
                app_id: app.app_id.clone(),
                name: match req.name.as_ref().map(|s| s.trim()) {
                    Some(s) if !s.is_empty() => s.to_string(),
                    _ => {
                        // 默认名:节点N(按该应用现有节点数)
                        let n = g.nodes.values().filter(|x| x.app_id == app.app_id).count() + 1;
                        format!("节点{n}")
                    }
                },
                device_id,
                install_root,
                listen_port,
                last_run_at: existing.as_ref().map(|e| e.last_run_at).unwrap_or(0),
                seq_no: existing
                    .as_ref()
                    .map(|e| e.seq_no)
                    .unwrap_or_else(|| self.seq.fetch_add(1, Ordering::Relaxed)),
            };
            g.nodes.insert(node.node_id.clone(), node.clone());
            node
        };
        let _ = crate::app_schedule::store::upsert_node(&node).await;
        Ok(node)
    }

    pub async fn delete_node(&self, node_id: &str) -> Result<(), String> {
        {
            let mut g = self.inner.lock().await;
            if !g.nodes.contains_key(node_id) {
                return Err(format!("节点不存在: {node_id}"));
            }
            for inst in g.instances.values() {
                if inst.node_id == node_id
                    && matches!(
                        inst.state,
                        InstanceState::Starting | InstanceState::Running | InstanceState::Stopping
                    )
                {
                    return Err("节点运行中，请先停止再删除".to_string());
                }
            }
            g.nodes.remove(node_id);
            // 清掉该节已终结的实例记录,避免重启后重新载入
            let inst_ids: Vec<String> = g
                .instances
                .values()
                .filter(|i| i.node_id == node_id)
                .map(|i| i.instance_id.clone())
                .collect();
            for iid in inst_ids {
                if let Some(i) = g.instances.remove(&iid) {
                    g.request_index.remove(&i.request_id);
                }
            }
        }
        let _ = crate::app_schedule::store::delete_node(node_id).await;
        let _ = crate::app_schedule::store::delete_instances_by_node(node_id).await;
        Ok(())
    }

    pub async fn list_nodes(&self, app_id: Option<&str>) -> Vec<AppNode> {
        let g = self.inner.lock().await;
        let mut nodes: Vec<AppNode> = g
            .nodes
            .values()
            .filter(|n| app_id.is_none_or(|id| n.app_id == id))
            .cloned()
            .collect();
        nodes.sort_by_key(|n| n.seq_no);
        nodes
    }

    pub async fn delete_app(&self, app_id: &str) -> Result<(), String> {
        {
            let mut g = self.inner.lock().await;
            for inst in g.instances.values() {
                if inst.app_id == app_id
                    && matches!(
                        inst.state,
                        InstanceState::Starting | InstanceState::Running | InstanceState::Stopping
                    )
                {
                    return Err("应用下有正在运行的实例，请先停止再删除".to_string());
                }
            }
            if !g.apps.contains_key(app_id) {
                return Err(format!("应用不存在: {app_id}"));
            }
            g.apps.remove(app_id);
            g.nodes.retain(|_, n| n.app_id != app_id);
            // 遗留 placement 一并清
            let plc_ids: Vec<String> = g
                .placements
                .values()
                .filter(|p| p.app_id == app_id)
                .map(|p| p.placement_id.clone())
                .collect();
            for pid in &plc_ids {
                if let Some(p) = g.placements.remove(pid) {
                    g.placement_by_app_device.remove(&(p.app_id, p.device_id));
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
        let _ = crate::app_schedule::store::delete_nodes_by_app(app_id).await;
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
        g.placement_by_app_device.insert(
            (p.app_id.clone(), p.device_id.clone()),
            p.placement_id.clone(),
        );
        g.placements.insert(p.placement_id.clone(), p.clone());
        drop(g);
        if let Err(e) = crate::app_schedule::store::upsert_placement(&p).await {
            tracing::warn!("persist placement failed: {e}");
        }
        Ok(p)
    }

    pub async fn list_placements(&self) -> Vec<AppPlacement> {
        self.inner
            .lock()
            .await
            .placements
            .values()
            .cloned()
            .collect()
    }

    pub async fn list_instances(&self) -> Vec<AppInstance> {
        self.inner
            .lock()
            .await
            .instances
            .values()
            .cloned()
            .collect()
    }

    /// 应用级启动:自动选一个节启动。候选 = 该应用的节中「无活跃实例 ∧ 端口
    /// 未被同机活跃实例占用」者;按 last_run_at 最老(从未运行优先)、seq_no
    /// 最小排序,取第一个 Service 在线的。req.device_id/listen_port 为遗留
    /// 字段,忽略。
    ///
    /// 并发安全:选节与预占(插入 Starting 实例)在同一把锁内原子完成,
    /// 并发请求互斥地各拿一个空闲节 —— 一个请求恰好一个实例,不会撞节。
    ///
    /// 同 client_key 去重:req.client_key 非空且同 app 同 key 已有活跃实例时,
    /// 复用该实例(Starting 则等它脱离,最长 ~30s),不再新开。
    /// client_key_permanent=true(launch 页 nonce):Running 不限时长永久复用;
    /// false(IP 兜底):Running 仅在 60s 窗口内复用 —— 窗口覆盖 Running 是因为
    /// 启动回执 ~1s 就返回,客户端重发(实测 2~4s 后到达)看到的实例往往已是 Running。
    pub async fn start_instance(&self, req: StartInstanceReq) -> Result<AppInstance, String> {
        if let Some(key) = req.client_key.as_deref().filter(|k| !k.is_empty()) {
            let dup = {
                let g = self.inner.lock().await;
                g.instances
                    .values()
                    .find(|i| {
                        i.app_id == req.app_id
                            && i.client_key == key
                            && (i.state == InstanceState::Starting
                                || (i.state == InstanceState::Running
                                    && (req.client_key_permanent
                                        || now_ms() - i.created_at_ms < 60_000)))
                    })
                    .cloned()
            };
            if let Some(inst) = dup {
                tracing::info!(
                    "start_instance dedup: app {} client {} already has instance {} ({:?}), reuse it",
                    req.app_id,
                    key,
                    inst.instance_id,
                    inst.state
                );
                if inst.state != InstanceState::Starting {
                    return Ok(inst);
                }
                if let Some(cur) = self
                    .wait_instance_leave_starting(&inst.instance_id, 30)
                    .await
                {
                    return Ok(cur);
                }
                // 在途实例异常消失,落回正常启动流程
            }
        }
        let client_key = req.client_key.clone().unwrap_or_default();
        let mut offline: Vec<String> = Vec::new();
        let mut tried: Vec<String> = Vec::new();
        loop {
            // 锁内:选节 + 预占。预占后其他并发请求即视为 busy,不会重选。
            let picked = {
                let mut g = self.inner.lock().await;
                if !g.apps.contains_key(&req.app_id) {
                    return Err(format!("unknown app_id {}", req.app_id));
                }
                let app = g.apps.get(&req.app_id).cloned().unwrap();
                let mut nodes: Vec<AppNode> = g
                    .nodes
                    .values()
                    .filter(|n| n.app_id == req.app_id && !tried.contains(&n.node_id))
                    .cloned()
                    .collect();
                if nodes.is_empty() && tried.is_empty() {
                    return Err("应用还没有节点，请先「新建节点」".to_string());
                }
                nodes.sort_by_key(|n| (n.last_run_at, n.seq_no));
                let candidate = nodes.into_iter().find(|n| {
                    let node_busy = g.instances.values().any(|i| {
                        i.node_id == n.node_id
                            && matches!(
                                i.state,
                                InstanceState::Starting
                                    | InstanceState::Running
                                    | InstanceState::Stopping
                            )
                    });
                    let port_busy = g.instances.values().any(|i| {
                        i.device_id == n.device_id
                            && i.listen_port == n.listen_port
                            && matches!(
                                i.state,
                                InstanceState::Starting
                                    | InstanceState::Running
                                    | InstanceState::Stopping
                            )
                    });
                    !node_busy && !port_busy
                });
                match candidate {
                    None => None,
                    Some(node) => {
                        match resolve_start_paths(
                            &app.game_path,
                            &node.install_root,
                            &app.game_exe_rel,
                        ) {
                            Err(e) => return Err(e),
                            Ok((install_root, game_exe_rel)) => {
                                let inst = self.pre_occupy_instance_locked(
                                    &mut g,
                                    &node,
                                    &app,
                                    &client_key,
                                );
                                Some((node, app, inst, install_root, game_exe_rel))
                            }
                        }
                    }
                }
            };
            let Some((node, app, inst, install_root, game_exe_rel)) = picked else {
                return if offline.is_empty() {
                    Err("没有空闲节点（全部在运行中或被占用）".to_string())
                } else {
                    Err(format!("节点的机器均不在线: {}", offline.join(", ")))
                };
            };
            match gCmsServiceConnMgr.get_conn(node.device_id.clone()).await {
                Ok(conn) => {
                    if let Err(e) = crate::app_schedule::store::upsert_instance(&inst).await {
                        tracing::warn!("persist instance failed: {e}");
                    }
                    return self
                        .dispatch_start(conn, &app, inst, install_root, game_exe_rel)
                        .await;
                }
                Err(_) => {
                    // 该节机器不在线:预占实例标记失败释放占用,试下一个候选节
                    offline.push(format!("{}({})", node.name, node.device_id));
                    tried.push(node.node_id.clone());
                    let snapshot = {
                        let mut g = self.inner.lock().await;
                        g.request_index.remove(&inst.request_id);
                        if let Some(i) = g.instances.get_mut(&inst.instance_id) {
                            i.state = InstanceState::Failed;
                            i.error = format!("service offline: {}", node.device_id);
                            Some(i.clone())
                        } else {
                            None
                        }
                    };
                    if let Some(failed) = snapshot {
                        let _ = crate::app_schedule::store::upsert_instance(&failed).await;
                    }
                }
            }
        }
    }

    /// 等待实例脱离 Starting(启动回执最长 ~25s),返回最新快照;实例消失返回 None。
    async fn wait_instance_leave_starting(
        &self,
        instance_id: &str,
        secs: u64,
    ) -> Option<AppInstance> {
        for _ in 0..secs {
            tokio::time::sleep(Duration::from_secs(1)).await;
            let cur = {
                let g = self.inner.lock().await;
                g.instances.get(instance_id).cloned()
            };
            match cur {
                Some(inst) if inst.state != InstanceState::Starting => return Some(inst),
                None => return None,
                _ => {}
            }
        }
        let g = self.inner.lock().await;
        g.instances.get(instance_id).cloned()
    }

    /// 锁内预占:创建并插入 Starting 实例(预占节点端口,并发互斥的关键)。
    /// 调用方必须已持有 inner 锁;持久化在锁外由调用方负责。
    fn pre_occupy_instance_locked(
        &self,
        g: &mut Inner,
        node: &AppNode,
        app: &Application,
        client_key: &str,
    ) -> AppInstance {
        let request_id = self.next_id("req");
        let instance_id = self.next_id("inst");
        let inst = AppInstance {
            instance_id: instance_id.clone(),
            request_id: request_id.clone(),
            app_id: app.app_id.clone(),
            device_id: node.device_id.clone(),
            placement_id: String::new(),
            node_id: node.node_id.clone(),
            state: InstanceState::Starting,
            // Pre-occupy the expected port so a concurrent Start of the same
            // node fails the port check; the Service receipt overwrites it if
            // it actually bound elsewhere.
            listen_port: node.listen_port,
            pid: 0,
            error: String::new(),
            web_client_hint: String::new(),
            client_key: client_key.to_string(),
            created_at_ms: now_ms(),
        };
        g.request_index
            .insert(request_id.clone(), instance_id.clone());
        g.instances.insert(instance_id.clone(), inst.clone());
        inst
    }

    /// 节级启动:在指定节上直接起实例。检查与预占同锁原子完成,并发安全。
    pub async fn start_node(&self, node_id: &str) -> Result<AppInstance, String> {
        let (node, app, inst, install_root, game_exe_rel) = {
            let mut g = self.inner.lock().await;
            let node = g
                .nodes
                .get(node_id)
                .cloned()
                .ok_or_else(|| format!("节点不存在: {node_id}"))?;
            for inst in g.instances.values() {
                if inst.node_id == node_id
                    && matches!(
                        inst.state,
                        InstanceState::Starting | InstanceState::Running | InstanceState::Stopping
                    )
                {
                    return Err(format!("节点「{}」已在运行或启动中", node.name));
                }
            }
            Self::ensure_node_port_available_locked(
                &g,
                &node.device_id,
                node.listen_port,
                Some(node_id),
            )?;
            let app = g
                .apps
                .get(&node.app_id)
                .cloned()
                .ok_or_else(|| format!("unknown app_id {}", node.app_id))?;
            let (install_root, game_exe_rel) =
                resolve_start_paths(&app.game_path, &node.install_root, &app.game_exe_rel)?;
            let inst = self.pre_occupy_instance_locked(&mut g, &node, &app, "");
            (node, app, inst, install_root, game_exe_rel)
        };
        if let Err(e) = crate::app_schedule::store::upsert_instance(&inst).await {
            tracing::warn!("persist instance failed: {e}");
        }
        let conn = match gCmsServiceConnMgr.get_conn(node.device_id.clone()).await {
            Ok(c) => c,
            Err(_) => {
                let snapshot = {
                    let mut g = self.inner.lock().await;
                    g.request_index.remove(&inst.request_id);
                    if let Some(i) = g.instances.get_mut(&inst.instance_id) {
                        i.state = InstanceState::Failed;
                        i.error = format!("service offline: {}", node.device_id);
                        Some(i.clone())
                    } else {
                        None
                    }
                };
                if let Some(failed) = snapshot {
                    let _ = crate::app_schedule::store::upsert_instance(&failed).await;
                }
                return Err(format!("service offline: {}", node.device_id));
            }
        };
        self.dispatch_start(conn, &app, inst, install_root, game_exe_rel)
            .await
    }

    /// 公共下发:waiter 注册 → 下发 → 等回执。实例已由调用方预占(Starting)。
    async fn dispatch_start(
        &self,
        conn: crate::net_service::cms_service_conn::CmsServiceConnPtr,
        app: &Application,
        inst: AppInstance,
        install_root: String,
        game_exe_rel: String,
    ) -> Result<AppInstance, String> {
        let request_id = inst.request_id.clone();
        let instance_id = inst.instance_id.clone();
        let start = CmsServiceStartAppInstance {
            request_id: request_id.clone(),
            instance_id: instance_id.clone(),
            app_id: app.app_id.clone(),
            install_root,
            game_exe_rel,
            game_arguments: app.default_game_args.clone(),
            listen_port: inst.listen_port,
            encoder_fps: app.encoder_fps,
            encoder_bitrate: app.encoder_bitrate,
            encoder_format: app.encoder_format.clone(),
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

        let conn = match gCmsServiceConnMgr.get_conn(device_id.clone()).await {
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
        let stop = CmsServiceStopAppInstance {
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
        result: CmsServiceStartAppInstanceResult,
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
        let mut touched_node: Option<AppNode> = None;
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
                    // 记录节最近运行时间(应用级启动选"最久未运行"的节)
                    if !inst.node_id.is_empty() {
                        if let Some(node) = g.nodes.get_mut(&inst.node_id) {
                            node.last_run_at = now_ms();
                            touched_node = Some(node.clone());
                        }
                    }
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
        if let Some(node) = touched_node {
            let _ = crate::app_schedule::store::upsert_node(&node).await;
        }
        if let (Some(tx), Some(inst)) = (waiter, snapshot) {
            let _ = tx.send(inst);
        }
    }

    pub async fn on_stop_result(&self, device_id: String, result: CmsServiceStopAppInstanceResult) {
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
            Ok((apps, placements, nodes, instances)) => {
                let (healed, migrated) = {
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
                    let mut max_seq = 0u64;
                    for n in nodes {
                        max_seq = max_seq.max(n.seq_no);
                        g.nodes.insert(n.node_id.clone(), n);
                    }
                    // seq 计数器抬高到已持久化的最大 seq_no,避免重启后序号回退
                    let cur = self.seq.load(Ordering::Relaxed);
                    if max_seq >= cur {
                        self.seq.store(max_seq + 1, Ordering::Relaxed);
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
                    // 节点结构迁移:没有节的旧应用,按遗留 placement + app.listen_port
                    // 生成默认节。node_id 取确定性值,重启幂等不重复建。
                    let migrated = self.migrate_legacy_nodes_locked(&mut g);
                    tracing::info!(
                        "app schedule loaded from mongo: apps={} nodes={} instances={} healed={} migrated={}",
                        g.apps.len(),
                        g.nodes.len(),
                        g.instances.len(),
                        healed.len(),
                        migrated.len()
                    );
                    (healed, migrated)
                };
                for i in healed {
                    let _ = crate::app_schedule::store::upsert_instance(&i).await;
                }
                for n in migrated {
                    let _ = crate::app_schedule::store::upsert_node(&n).await;
                }
            }
            Err(e) => tracing::warn!("load app schedule from mongo failed: {e}"),
        }
    }

    /// 节点结构迁移(可从 load_from_db 与单测调用):没有节的旧应用,按遗留
    /// placement + app.listen_port 生成默认节。node_id 取确定性值,幂等。
    fn migrate_legacy_nodes_locked(&self, g: &mut Inner) -> Vec<AppNode> {
        let mut migrated = Vec::new();
        let app_ids: Vec<String> = g.apps.keys().cloned().collect();
        for app_id in app_ids {
            if g.nodes.values().any(|n| n.app_id == app_id) {
                continue;
            }
            let app = g.apps.get(&app_id).cloned().unwrap();
            let legacy_plc = g.placements.values().find(|p| p.app_id == app_id).cloned();
            let install_root = legacy_plc
                .as_ref()
                .map(|p| p.install_root.clone())
                .filter(|s| !s.is_empty())
                .or_else(|| split_game_path(&app.game_path).ok().map(|(root, _)| root))
                .unwrap_or_default();
            let device_id = legacy_plc
                .as_ref()
                .map(|p| p.device_id.clone())
                .unwrap_or_default();
            if device_id.is_empty() || install_root.is_empty() {
                tracing::warn!(
                    "migrate: skip node migration for app {} (no placement/game_path)",
                    app_id
                );
                continue;
            }
            let listen_port = if app.listen_port > 0 {
                app.listen_port
            } else {
                Self::suggest_next_port_locked(g, &device_id).unwrap_or(0)
            };
            if listen_port <= 0 {
                tracing::warn!(
                    "migrate: skip node migration for app {} (no port available)",
                    app_id
                );
                continue;
            }
            let node = AppNode {
                node_id: format!("node-legacy-{app_id}-{device_id}-{listen_port}"),
                app_id: app_id.clone(),
                name: "节点1".to_string(),
                device_id,
                install_root,
                listen_port,
                last_run_at: 0,
                seq_no: self.seq.fetch_add(1, Ordering::Relaxed),
            };
            tracing::info!(
                "migrate: app {} -> default node {} (port {})",
                app_id,
                node.node_id,
                node.listen_port
            );
            g.nodes.insert(node.node_id.clone(), node.clone());
            migrated.push(node);
        }
        migrated
    }

    /// Test helper: inject without network.
    pub async fn inject_for_test(
        &self,
        app: Application,
        placement: AppPlacement,
        inst: AppInstance,
    ) {
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

    /// Test helper: inject a node without network/DB.
    pub async fn inject_node_for_test(&self, node: AppNode) {
        let mut g = self.inner.lock().await;
        g.nodes.insert(node.node_id.clone(), node);
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
    async fn start_requires_node_and_online_service() {
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
        // 没有节:明确报错
        let err = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id.clone(),
                device_id: None,
                listen_port: None,
                client_key: None,
                client_key_permanent: false,
            })
            .await
            .unwrap_err();
        assert!(err.contains("还没有节点"), "{err}");

        // 有节但机器离线
        mgr.save_node(SaveNodeReq {
            node_id: None,
            app_id: app.app_id.clone(),
            name: None,
            device_id: "offline-dev".into(),
            install_root: Some(r"D:\app".into()),
            listen_port: Some(32000),
        })
        .await
        .unwrap();
        let err = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id,
                device_id: None,
                listen_port: None,
                client_key: None,
                client_key_permanent: false,
            })
            .await
            .unwrap_err();
        assert!(err.contains("不在线"), "{err}");
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
            node_id: String::new(),
            state: InstanceState::Starting,
            listen_port: 0,
            pid: 0,
            error: String::new(),
            web_client_hint: String::new(),
            client_key: String::new(),
            created_at_ms: 0,
        };
        mgr.inject_for_test(app, placement, inst).await;
        mgr.on_start_result(
            "dev-1".into(),
            CmsServiceStartAppInstanceResult {
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
            CmsServiceStopAppInstanceResult {
                request_id: "req-1".into(),
                instance_id: "inst-1".into(),
                ok: true,
                error: String::new(),
            },
        )
        .await;
        assert_eq!(mgr.list_instances().await[0].state, InstanceState::Stopped);
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
                node_id: String::new(),
                state: InstanceState::Starting,
                listen_port: 0,
                pid: 0,
                error: String::new(),
                web_client_hint: String::new(),
                client_key: String::new(),
                created_at_ms: 0,
            },
        )
        .await;
        mgr.on_start_result(
            "d".into(),
            CmsServiceStartAppInstanceResult {
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
                node_id: String::new(),
                state: InstanceState::Stopping,
                listen_port: 32000,
                pid: 1,
                error: String::new(),
                web_client_hint: String::new(),
                client_key: String::new(),
                created_at_ms: 0,
            },
        )
        .await;
        mgr.on_stop_result(
            "d".into(),
            CmsServiceStopAppInstanceResult {
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
                node_id: String::new(),
                state: InstanceState::Running,
                listen_port: 32000,
                pid: 99,
                error: String::new(),
                web_client_hint: String::new(),
                client_key: String::new(),
                created_at_ms: 0,
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
                node_id: String::new(),
                state: InstanceState::Running,
                listen_port: 32000,
                pid: 99,
                error: String::new(),
                web_client_hint: String::new(),
                client_key: String::new(),
                created_at_ms: 0,
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
    async fn save_node_assigns_incremental_ports_and_rejects_conflict() {
        let mgr = AppScheduleManager::new();
        let app = mgr
            .save_app(SaveAppReq {
                app_id: None,
                name: "A".into(),
                game_path: r"D:\games\a\game.exe".into(),
                default_game_args: None,
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
            })
            .await
            .unwrap();
        // 同机两个节:端口递增
        let n1 = mgr
            .save_node(SaveNodeReq {
                node_id: None,
                app_id: app.app_id.clone(),
                name: None,
                device_id: "m1".into(),
                install_root: None,
                listen_port: None,
            })
            .await
            .unwrap();
        assert_eq!(n1.listen_port, 32000);
        assert_eq!(n1.name, "节点1");
        // install_root 从应用 game_path 推导
        assert_eq!(n1.install_root, r"D:\games\a");
        let n2 = mgr
            .save_node(SaveNodeReq {
                node_id: None,
                app_id: app.app_id.clone(),
                name: None,
                device_id: "m1".into(),
                install_root: None,
                listen_port: None,
            })
            .await
            .unwrap();
        assert_eq!(n2.listen_port, 32001);
        assert_eq!(n2.name, "节点2");
        // 同机端口冲突:拒绝
        let err = mgr
            .save_node(SaveNodeReq {
                node_id: None,
                app_id: app.app_id.clone(),
                name: None,
                device_id: "m1".into(),
                install_root: None,
                listen_port: Some(32000),
            })
            .await
            .unwrap_err();
        assert!(err.contains("占用"), "{err}");
        // 不同机器允许同端口
        let n3 = mgr
            .save_node(SaveNodeReq {
                node_id: None,
                app_id: app.app_id.clone(),
                name: None,
                device_id: "m2".into(),
                install_root: None,
                listen_port: Some(32000),
            })
            .await
            .unwrap();
        assert_eq!(n3.listen_port, 32000);

        mgr.delete_app(&app.app_id).await.unwrap();
        assert!(mgr.list_app_rows().await.is_empty());
        assert!(mgr.list_nodes(None).await.is_empty());
    }

    fn node_with_port(app_id: &str, device_id: &str, listen_port: i32) -> AppNode {
        AppNode {
            node_id: format!("node-{app_id}-{listen_port}"),
            app_id: app_id.into(),
            name: format!("节-{listen_port}"),
            device_id: device_id.into(),
            install_root: r"D:\x".into(),
            listen_port,
            last_run_at: 0,
            seq_no: 0,
        }
    }

    #[tokio::test]
    async fn suggest_next_port_increments_from_pool_start() {
        let mgr = AppScheduleManager::new();
        assert_eq!(mgr.suggest_next_port("m1").await.unwrap(), 32000);
        {
            let mut g = mgr.inner.lock().await;
            g.nodes
                .insert("n1".into(), node_with_port("a", "m1", 32000));
            g.nodes
                .insert("n2".into(), node_with_port("a", "m1", 32001));
        }
        assert_eq!(mgr.suggest_next_port("m1").await.unwrap(), 32002);
        // 另一台机器不受影响
        assert_eq!(mgr.suggest_next_port("m2").await.unwrap(), 32000);
    }

    #[tokio::test]
    async fn suggest_next_port_wraps_to_first_free_after_pool_end() {
        let mgr = AppScheduleManager::new();
        {
            let mut g = mgr.inner.lock().await;
            g.nodes
                .insert("n1".into(), node_with_port("a", "m1", 32999));
        }
        // max+1 exceeds the pool: suggest the first free port from the start.
        assert_eq!(mgr.suggest_next_port("m1").await.unwrap(), 32000);
        {
            let mut g = mgr.inner.lock().await;
            g.nodes
                .insert("n2".into(), node_with_port("a", "m1", 32000));
        }
        assert_eq!(mgr.suggest_next_port("m1").await.unwrap(), 32001);
    }

    #[tokio::test]
    async fn suggest_next_port_errors_when_pool_full() {
        let mgr = AppScheduleManager::new();
        {
            let mut g = mgr.inner.lock().await;
            for port in 32000..=32999 {
                let n = node_with_port("a", "m1", port);
                g.nodes.insert(n.node_id.clone(), n);
            }
        }
        let err = mgr.suggest_next_port("m1").await.unwrap_err();
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
                node_id: String::new(),
                state,
                listen_port: 32000,
                pid: 0,
                error: String::new(),
                web_client_hint: String::new(),
                client_key: String::new(),
                created_at_ms: 0,
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
        assert_eq!(mgr.list_instances().await[0].state, InstanceState::Running);
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
        mgr2.reconcile_from_service_hb("d".into(), r#"[{"instance_id":"i","state":"running"}]"#)
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
            CmsServiceStartAppInstanceResult {
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
            CmsServiceStartAppInstanceResult {
                request_id: "r".into(),
                instance_id: "i".into(),
                ok: true,
                error: String::new(),
                listen_port: 32055,
                pid: 4242,
            },
        )
        .await;
        assert_eq!(mgr.list_instances().await[0].state, InstanceState::Starting);
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
    async fn save_node_rejects_port_out_of_range() {
        let mgr = AppScheduleManager::new();
        let app = mgr
            .save_app(SaveAppReq {
                app_id: None,
                name: "P".into(),
                game_path: r"D:\games\p\game.exe".into(),
                default_game_args: None,
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
            })
            .await
            .unwrap();
        for port in [31999, 33000] {
            let err = mgr
                .save_node(SaveNodeReq {
                    node_id: None,
                    app_id: app.app_id.clone(),
                    name: None,
                    device_id: "m1".into(),
                    install_root: None,
                    listen_port: Some(port),
                })
                .await
                .unwrap_err();
            assert!(err.contains("32000-32999"), "port {port}: {err}");
        }
    }

    #[tokio::test]
    async fn delete_node_rejected_while_running() {
        let mgr = AppScheduleManager::new();
        let app = mgr
            .save_app(SaveAppReq {
                app_id: None,
                name: "D".into(),
                game_path: r"D:\games\d\game.exe".into(),
                default_game_args: None,
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
            })
            .await
            .unwrap();
        let node = mgr
            .save_node(SaveNodeReq {
                node_id: None,
                app_id: app.app_id.clone(),
                name: None,
                device_id: "m1".into(),
                install_root: None,
                listen_port: None,
            })
            .await
            .unwrap();
        // 注入该节的活跃实例
        {
            let mut g = mgr.inner.lock().await;
            g.instances.insert(
                "i-1".into(),
                AppInstance {
                    instance_id: "i-1".into(),
                    request_id: "r-1".into(),
                    app_id: app.app_id.clone(),
                    device_id: "m1".into(),
                    placement_id: String::new(),
                    node_id: node.node_id.clone(),
                    state: InstanceState::Running,
                    listen_port: node.listen_port,
                    pid: 123,
                    error: String::new(),
                    web_client_hint: String::new(),
                    client_key: String::new(),
                    created_at_ms: 0,
                },
            );
        }
        let err = mgr.delete_node(&node.node_id).await.unwrap_err();
        assert!(err.contains("运行中"), "{err}");
        // 节级启动也应拒绝
        let err = mgr.start_node(&node.node_id).await.unwrap_err();
        assert!(err.contains("已在运行"), "{err}");
        // 应用级启动:唯一节被占 -> 没有空闲节
        let err = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id.clone(),
                device_id: None,
                listen_port: None,
                client_key: None,
                client_key_permanent: false,
            })
            .await
            .unwrap_err();
        assert!(err.contains("没有空闲节点"), "{err}");
        // 编辑运行中的节:换机器/端口拒绝,只改名可以
        let err = mgr
            .save_node(SaveNodeReq {
                node_id: Some(node.node_id.clone()),
                app_id: app.app_id.clone(),
                name: Some("改名".into()),
                device_id: "m2".into(),
                install_root: None,
                listen_port: None,
            })
            .await
            .unwrap_err();
        assert!(err.contains("运行中"), "{err}");
        let renamed = mgr
            .save_node(SaveNodeReq {
                node_id: Some(node.node_id.clone()),
                app_id: app.app_id.clone(),
                name: Some("改名".into()),
                device_id: "m1".into(),
                install_root: None,
                listen_port: None,
            })
            .await
            .unwrap();
        assert_eq!(renamed.name, "改名");
        assert_eq!(renamed.listen_port, node.listen_port);
    }

    #[tokio::test]
    async fn app_start_picks_stalest_free_node_first() {
        let mgr = AppScheduleManager::new();
        let app = mgr
            .save_app(SaveAppReq {
                app_id: None,
                name: "S".into(),
                game_path: r"D:\games\s\game.exe".into(),
                default_game_args: None,
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
            })
            .await
            .unwrap();
        // 两个节:节1 最近跑过,节2 从未跑 -> 应用启动应选节2
        let n1 = mgr
            .save_node(SaveNodeReq {
                node_id: None,
                app_id: app.app_id.clone(),
                name: None,
                device_id: "m1".into(),
                install_root: None,
                listen_port: None,
            })
            .await
            .unwrap();
        let n2 = mgr
            .save_node(SaveNodeReq {
                node_id: None,
                app_id: app.app_id.clone(),
                name: None,
                device_id: "m1".into(),
                install_root: None,
                listen_port: None,
            })
            .await
            .unwrap();
        {
            let mut g = mgr.inner.lock().await;
            g.nodes.get_mut(&n1.node_id).unwrap().last_run_at = now_ms();
        }
        // 无在线 Service:错误信息按选节顺序列出候选(节2 在前)
        let err = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id.clone(),
                device_id: None,
                listen_port: None,
                client_key: None,
                client_key_permanent: false,
            })
            .await
            .unwrap_err();
        let pos2 = err.find(&n2.name).unwrap_or(usize::MAX);
        let pos1 = err.find(&n1.name).unwrap_or(usize::MAX);
        assert!(pos2 < pos1, "stalest node should be picked first: {err}");
    }

    #[tokio::test]
    async fn start_result_updates_node_last_run_at() {
        let mgr = AppScheduleManager::new();
        let app = mgr
            .save_app(SaveAppReq {
                app_id: None,
                name: "L".into(),
                game_path: r"D:\games\l\game.exe".into(),
                default_game_args: None,
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
            })
            .await
            .unwrap();
        let node = mgr
            .save_node(SaveNodeReq {
                node_id: None,
                app_id: app.app_id.clone(),
                name: None,
                device_id: "d".into(),
                install_root: None,
                listen_port: None,
            })
            .await
            .unwrap();
        assert_eq!(node.last_run_at, 0);
        {
            let mut g = mgr.inner.lock().await;
            g.request_index.insert("r-9".into(), "i-9".into());
            g.instances.insert(
                "i-9".into(),
                AppInstance {
                    instance_id: "i-9".into(),
                    request_id: "r-9".into(),
                    app_id: app.app_id.clone(),
                    device_id: "d".into(),
                    placement_id: String::new(),
                    node_id: node.node_id.clone(),
                    state: InstanceState::Starting,
                    listen_port: node.listen_port,
                    pid: 0,
                    error: String::new(),
                    web_client_hint: String::new(),
                    client_key: String::new(),
                    created_at_ms: 0,
                },
            );
        }
        mgr.on_start_result(
            "d".into(),
            CmsServiceStartAppInstanceResult {
                request_id: "r-9".into(),
                instance_id: "i-9".into(),
                ok: true,
                error: String::new(),
                listen_port: node.listen_port,
                pid: 99,
            },
        )
        .await;
        let nodes = mgr.list_nodes(None).await;
        assert!(
            nodes[0].last_run_at > 0,
            "last_run_at should be set on Running"
        );
    }

    #[tokio::test]
    async fn app_start_skips_starting_node_and_picks_free_one() {
        // 并发安全的确定性部分:节2 已有 Starting 实例(视为被并发请求预占),
        // 应用级启动必须跳过它选节1(错误信息只列节1,证明选的是节1)。
        let mgr = AppScheduleManager::new();
        let app = mgr
            .save_app(SaveAppReq {
                app_id: None,
                name: "C".into(),
                game_path: r"D:\games\c\game.exe".into(),
                default_game_args: None,
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
            })
            .await
            .unwrap();
        let n1 = mgr
            .save_node(SaveNodeReq {
                node_id: None,
                app_id: app.app_id.clone(),
                name: None,
                device_id: "m-off".into(),
                install_root: None,
                listen_port: None,
            })
            .await
            .unwrap();
        let n2 = mgr
            .save_node(SaveNodeReq {
                node_id: None,
                app_id: app.app_id.clone(),
                name: None,
                device_id: "m-off".into(),
                install_root: None,
                listen_port: None,
            })
            .await
            .unwrap();
        {
            // 节2 被并发请求预占(Starting)
            let mut g = mgr.inner.lock().await;
            g.request_index.insert("r-c".into(), "i-c".into());
            g.instances.insert(
                "i-c".into(),
                AppInstance {
                    instance_id: "i-c".into(),
                    request_id: "r-c".into(),
                    app_id: app.app_id.clone(),
                    device_id: "m-off".into(),
                    placement_id: String::new(),
                    node_id: n2.node_id.clone(),
                    state: InstanceState::Starting,
                    listen_port: n2.listen_port,
                    pid: 0,
                    error: String::new(),
                    web_client_hint: String::new(),
                    client_key: String::new(),
                    created_at_ms: 0,
                },
            );
        }
        let err = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id.clone(),
                device_id: None,
                listen_port: None,
                client_key: None,
                client_key_permanent: false,
            })
            .await
            .unwrap_err();
        // 机器离线 => 走离线报错;报错只应包含被选中的节1,不含被占的节2
        assert!(err.contains(&n1.name), "should pick free node1: {err}");
        assert!(!err.contains(&n2.name), "busy node2 must be skipped: {err}");
        // 离线失败后预占实例被标记 Failed 释放,不残留 Starting
        let insts = mgr.list_instances().await;
        let pre = insts.iter().find(|i| i.node_id == n1.node_id).unwrap();
        assert!(matches!(pre.state, InstanceState::Failed));
        assert!(pre.error.contains("offline"), "{}", pre.error);
    }

    #[tokio::test]
    async fn start_instance_dedups_same_client_key_while_starting() {
        // 同 app 同 client_key 已有 Starting 实例:等它出结果并返回该实例,
        // 不开新实例(防客户端重发多开);不同 client_key 不命中去重,正常选节。
        let mgr = std::sync::Arc::new(AppScheduleManager::new());
        let app = mgr
            .save_app(SaveAppReq {
                app_id: None,
                name: "D".into(),
                game_path: r"D:\games\d\game.exe".into(),
                default_game_args: None,
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
            })
            .await
            .unwrap();
        let n1 = mgr
            .save_node(SaveNodeReq {
                node_id: None,
                app_id: app.app_id.clone(),
                name: None,
                device_id: "m-off".into(),
                install_root: None,
                listen_port: None,
            })
            .await
            .unwrap();
        {
            // 在途实例:同 app、client_key="ip1",Starting
            let mut g = mgr.inner.lock().await;
            g.request_index.insert("r-d".into(), "i-d".into());
            g.instances.insert(
                "i-d".into(),
                AppInstance {
                    instance_id: "i-d".into(),
                    request_id: "r-d".into(),
                    app_id: app.app_id.clone(),
                    device_id: "m-off".into(),
                    placement_id: String::new(),
                    node_id: n1.node_id.clone(),
                    state: InstanceState::Starting,
                    listen_port: n1.listen_port,
                    pid: 0,
                    error: String::new(),
                    web_client_hint: String::new(),
                    client_key: "ip1".into(),
                    created_at_ms: now_ms(),
                },
            );
        }
        // 200ms 后模拟回执:在途实例变 Running
        let m2 = mgr.clone();
        tokio::spawn(async move {
            tokio::time::sleep(Duration::from_millis(200)).await;
            let mut g = m2.inner.lock().await;
            if let Some(i) = g.instances.get_mut("i-d") {
                i.state = InstanceState::Running;
            }
        });
        // 同 key:命中去重,等到 Running 并返回在途实例,不开新实例
        let inst = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id.clone(),
                device_id: None,
                listen_port: None,
                client_key: Some("ip1".into()),
                client_key_permanent: false,
            })
            .await
            .unwrap();
        assert_eq!(
            inst.instance_id, "i-d",
            "dedup must return in-flight instance"
        );
        assert!(matches!(inst.state, InstanceState::Running));
        assert_eq!(mgr.list_instances().await.len(), 1, "no extra instance");
        // 实例刚转入 Running(60s 窗口内):同 key 再调直接返回它,不多开
        // (启动回执 ~1s 就返回,客户端重发到达时实例多半已 Running)
        let inst = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id.clone(),
                device_id: None,
                listen_port: None,
                client_key: Some("ip1".into()),
                client_key_permanent: false,
            })
            .await
            .unwrap();
        assert_eq!(
            inst.instance_id, "i-d",
            "running-window dedup must reuse instance"
        );
        assert_eq!(mgr.list_instances().await.len(), 1, "no extra instance");
        // 不同 key:不命中去重,走正常选节(唯一节点已被占 -> 报无空闲节点)
        let err = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id.clone(),
                device_id: None,
                listen_port: None,
                client_key: Some("ip2".into()),
                client_key_permanent: false,
            })
            .await
            .unwrap_err();
        assert!(err.contains("没有空闲节点"), "{err}");
        // 空 key:同样不去重
        let err = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id.clone(),
                device_id: None,
                listen_port: None,
                client_key: None,
                client_key_permanent: false,
            })
            .await
            .unwrap_err();
        assert!(err.contains("没有空闲节点"), "{err}");
    }

    #[tokio::test]
    async fn start_instance_permanent_dedup_ignores_age() {
        // client_key_permanent=true(launch 页 nonce):Running 实例无论多久都复用;
        // false(IP 兜底):Running 超过 60s 窗口不再复用。
        let mgr = std::sync::Arc::new(AppScheduleManager::new());
        let app = mgr
            .save_app(SaveAppReq {
                app_id: None,
                name: "E".into(),
                game_path: r"D:\games\e\game.exe".into(),
                default_game_args: None,
                encoder_fps: None,
                encoder_bitrate: None,
                encoder_format: None,
            })
            .await
            .unwrap();
        let n1 = mgr
            .save_node(SaveNodeReq {
                node_id: None,
                app_id: app.app_id.clone(),
                name: None,
                device_id: "m-off".into(),
                install_root: None,
                listen_port: None,
            })
            .await
            .unwrap();
        {
            // 1 小时前启动的 Running 实例(早已超出 60s 窗口)
            let mut g = mgr.inner.lock().await;
            g.request_index.insert("r-e".into(), "i-e".into());
            g.instances.insert(
                "i-e".into(),
                AppInstance {
                    instance_id: "i-e".into(),
                    request_id: "r-e".into(),
                    app_id: app.app_id.clone(),
                    device_id: "m-off".into(),
                    placement_id: String::new(),
                    node_id: n1.node_id.clone(),
                    state: InstanceState::Running,
                    listen_port: n1.listen_port,
                    pid: 0,
                    error: String::new(),
                    web_client_hint: String::new(),
                    client_key: "nonce-e".into(),
                    created_at_ms: now_ms() - 3_600_000,
                },
            );
        }
        // 永久去重:复用旧实例
        let inst = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id.clone(),
                device_id: None,
                listen_port: None,
                client_key: Some("nonce-e".into()),
                client_key_permanent: true,
            })
            .await
            .unwrap();
        assert_eq!(
            inst.instance_id, "i-e",
            "permanent dedup must reuse old running instance"
        );
        assert_eq!(mgr.list_instances().await.len(), 1, "no extra instance");
        // 非永久:超出 60s 窗口,不复用,走正常选节(唯一节点被占 -> 报无空闲节点)
        let err = mgr
            .start_instance(StartInstanceReq {
                app_id: app.app_id.clone(),
                device_id: None,
                listen_port: None,
                client_key: Some("nonce-e".into()),
                client_key_permanent: false,
            })
            .await
            .unwrap_err();
        assert!(err.contains("没有空闲节点"), "{err}");
    }

    #[tokio::test]
    async fn legacy_placement_migrates_to_default_node() {
        let mgr = AppScheduleManager::new();
        // 模拟节点结构之前的数据:app + placement,无 node
        mgr.inject_for_test(
            Application {
                app_id: "a".into(),
                name: "n".into(),
                game_path: r"D:\x\e.exe".into(),
                game_exe_rel: "e.exe".into(),
                default_game_args: String::new(),
                encoder_fps: 60,
                encoder_bitrate: 20,
                encoder_format: "h264".into(),
                webrtc_enabled: true,
                websocket_enabled: true,
                listen_port: 32055,
            },
            AppPlacement {
                placement_id: "p".into(),
                app_id: "a".into(),
                device_id: "d".into(),
                install_root: r"D:\x".into(),
            },
            AppInstance::default(),
        )
        .await;
        let migrated = {
            let mut g = mgr.inner.lock().await;
            mgr.migrate_legacy_nodes_locked(&mut g)
        };
        assert_eq!(migrated.len(), 1);
        let n = &migrated[0];
        assert_eq!(n.device_id, "d");
        assert_eq!(n.install_root, r"D:\x");
        assert_eq!(n.listen_port, 32055);
        assert_eq!(n.name, "节点1");
        // 幂等:再跑一次不重复建
        let again = {
            let mut g = mgr.inner.lock().await;
            mgr.migrate_legacy_nodes_locked(&mut g)
        };
        assert!(again.is_empty());
        assert_eq!(mgr.list_nodes(None).await.len(), 1);
    }
}
