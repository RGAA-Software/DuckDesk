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
use tokio::sync::Mutex;
use uuid::Uuid;

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct Application {
    pub app_id: String,
    pub name: String,
    pub game_exe_rel: String,
    pub default_game_args: String,
    pub encoder_fps: i32,
    pub encoder_bitrate: i32,
    pub encoder_format: String,
    pub webrtc_enabled: bool,
    pub websocket_enabled: bool,
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
        let app = Application {
            app_id: self.next_id("app"),
            name: req.name.trim().to_string(),
            game_exe_rel: req.game_exe_rel.trim().to_string(),
            default_game_args: req.default_game_args.unwrap_or_default(),
            encoder_fps: req.encoder_fps.unwrap_or(60),
            encoder_bitrate: req.encoder_bitrate.unwrap_or(20),
            encoder_format: req
                .encoder_format
                .unwrap_or_else(|| "h264".to_string()),
            webrtc_enabled: true,
            websocket_enabled: true,
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

        // Service must be online.
        let conn = match gSpvrServiceConnMgr.get_conn(req.device_id.clone()).await {
            Ok(c) => c,
            Err(_) => return Err(format!("service offline: {}", req.device_id)),
        };

        let request_id = self.next_id("req");
        let instance_id = self.next_id("inst");
        let listen_port = req.listen_port.unwrap_or(0);
        let inst = AppInstance {
            instance_id: instance_id.clone(),
            request_id: request_id.clone(),
            app_id: app.app_id.clone(),
            device_id: req.device_id.clone(),
            placement_id: placement.placement_id.clone(),
            state: InstanceState::Starting,
            listen_port: 0,
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
            install_root: placement.install_root,
            game_exe_rel: app.game_exe_rel,
            game_arguments: app.default_game_args,
            listen_port,
            encoder_fps: app.encoder_fps,
            encoder_bitrate: app.encoder_bitrate,
            encoder_format: app.encoder_format,
            webrtc_enabled: app.webrtc_enabled,
            websocket_enabled: app.websocket_enabled,
        };

        let ok = conn.lock().await.send_start_app_instance(start).await;
        if !ok {
            let snapshot = {
                let mut g = self.inner.lock().await;
                if let Some(i) = g.instances.get_mut(&instance_id) {
                    i.state = InstanceState::Failed;
                    i.error = "send to service failed".to_string();
                    Some(i.clone())
                } else {
                    None
                }
            };
            if let Some(failed) = snapshot {
                let _ = crate::app_schedule::store::upsert_instance(&failed).await;
            }
            return Err("send to service failed".to_string());
        }
        Ok(inst)
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

        let conn = gSpvrServiceConnMgr
            .get_conn(device_id.clone())
            .await
            .map_err(|_| format!("service offline: {device_id}"))?;
        let stop = SpvrServiceStopAppInstance {
            request_id,
            instance_id: instance_id.to_string(),
        };
        let ok = conn.lock().await.send_stop_app_instance(stop).await;
        if !ok {
            return Err("send stop failed".to_string());
        }
        let g = self.inner.lock().await;
        Ok(g.instances.get(instance_id).cloned().unwrap())
    }

    pub async fn on_start_result(
        &self,
        device_id: String,
        result: SpvrServiceStartAppInstanceResult,
    ) {
        let mut g = self.inner.lock().await;
        let Some(instance_id) = g.request_index.get(&result.request_id).cloned() else {
            tracing::warn!(
                "start result unknown request_id {} from {}",
                result.request_id,
                device_id
            );
            return;
        };
        let snapshot = if let Some(inst) = g.instances.get_mut(&instance_id) {
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
            }
            Some(inst.clone())
        } else {
            None
        };
        drop(g);
        if let Some(inst) = snapshot {
            let _ = crate::app_schedule::store::upsert_instance(&inst).await;
        }
    }

    pub async fn on_stop_result(
        &self,
        device_id: String,
        result: SpvrServiceStopAppInstanceResult,
    ) {
        let mut g = self.inner.lock().await;
        let Some(instance_id) = g.request_index.get(&result.request_id).cloned() else {
            // Also match by instance_id directly
            if let Some(inst) = g.instances.get_mut(&result.instance_id) {
                if result.ok {
                    inst.state = InstanceState::Stopped;
                } else {
                    inst.error = result.error;
                }
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
            if result.ok {
                inst.state = InstanceState::Stopped;
                inst.pid = 0;
            } else {
                inst.error = result.error;
            }
            Some(inst.clone())
        } else {
            None
        };
        drop(g);
        if let Some(inst) = snapshot {
            let _ = crate::app_schedule::store::upsert_instance(&inst).await;
        }
    }

    pub async fn load_from_db(&self) {
        match crate::app_schedule::store::load_all().await {
            Ok((apps, placements, instances)) => {
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
                for i in instances {
                    g.request_index
                        .insert(i.request_id.clone(), i.instance_id.clone());
                    g.instances.insert(i.instance_id.clone(), i);
                }
                tracing::info!(
                    "app schedule loaded from mongo: apps={} placements={} instances={}",
                    g.apps.len(),
                    g.placements.len(),
                    g.instances.len()
                );
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
            game_exe_rel: "g.exe".into(),
            default_game_args: String::new(),
            encoder_fps: 60,
            encoder_bitrate: 20,
            encoder_format: "h264".into(),
            webrtc_enabled: true,
            websocket_enabled: true,
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
                game_exe_rel: "e".into(),
                default_game_args: String::new(),
                encoder_fps: 60,
                encoder_bitrate: 20,
                encoder_format: "h264".into(),
                webrtc_enabled: true,
                websocket_enabled: true,
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
}
