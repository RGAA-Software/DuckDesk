use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine as _};
use rand::RngCore;
use std::sync::Arc;
use std::time::Duration;

use service_core::app_instance::{cmdline_has_listen_port, pid_belongs_to_instance};
use service_core::command::Command;
use service_core::config::ServiceConfig;
use service_core::storage::PersistedRenderLaunchSpec;
use service_core::storage::ServiceStorage;
use service_core::{
    AppInstanceRegistry, PersistedServiceState, RenderLaunchSpec, ServiceState, StartAppRequest,
    FINISHED_RECORD_TTL,
};
use tokio::sync::{broadcast, mpsc, oneshot, Mutex};
use tracing::{error, info, warn};

use crate::user_proxy;
use crate::virtual_display_manager::VirtualDisplayManager;
use crate::websocket_server::WebsocketService;
use crate::windows_actions::SystemActions;
use crate::windows_process::ProcessManager;

pub struct ServiceRuntime {
    pub config: ServiceConfig,
    pub storage: ServiceStorage,
    pub process_manager: Arc<dyn ProcessManager>,
    pub windows_actions: Arc<dyn SystemActions>,
    pub state: ServiceState,
    pub app_registry: AppInstanceRegistry,
    /// render ws 下发通道: key = "render_{listen_port}"(心跳 from),
    /// 用于 Console 停止实例时主动给 render 推 kSrvStopServer。
    pub render_senders: std::collections::HashMap<String, mpsc::UnboundedSender<Vec<u8>>>,
    /// One-shot Browser/first-frame acknowledgements for WebView starts.
    pub webview_ready_waiters:
        std::collections::HashMap<String, oneshot::Sender<Result<(), String>>>,
    pub ticket_redeem_tx: Option<mpsc::Sender<TicketRedeemRequest>>,
    pub virtual_display_manager: Option<Arc<VirtualDisplayManager>>,
    pub virtual_display_init_error: Option<String>,
    pub virtual_display_results:
        std::collections::HashMap<String, service_core::MsgVirtualDisplayResult>,
    /// Per-service-start credential required by the loopback Render IPC WS.
    /// It is never persisted and is only passed to child Render processes.
    pub ipc_token: String,
    stop_tx: broadcast::Sender<()>,
}

pub struct TicketRedeemRequest {
    pub request_id: String,
    pub ticket: String,
    pub client_nonce: String,
    pub instance_id: String,
    pub response: oneshot::Sender<TicketRedeemResult>,
}

#[derive(Debug, Clone, Default)]
pub struct TicketRedeemResult {
    pub ok: bool,
    pub code: String,
    pub kind: String,
    pub device_id: String,
    pub app_id: String,
    pub instance_id: String,
    pub subject_type: String,
    pub subject_id: String,
    pub permissions: Vec<String>,
    pub expires_at: i64,
    pub rtc_ice_config_json: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ControlEvent {
    Stop,
    ConsoleConnect(u32),
    ConsoleDisconnect(u32),
    SessionLogon(u32),
    SessionLogoff(u32),
    SessionLock(u32),
    SessionUnlock(u32),
}

impl ServiceRuntime {
    pub fn new(
        config: ServiceConfig,
        process_manager: Arc<dyn ProcessManager>,
        windows_actions: Arc<dyn SystemActions>,
    ) -> Self {
        let storage = ServiceStorage::new(config.storage_file());
        let driver_dir = std::env::current_exe()
            .ok()
            .and_then(|path| path.parent().map(|parent| parent.join("parsec_vdd")))
            .unwrap_or_else(|| std::path::PathBuf::from("parsec_vdd"));
        #[cfg(not(test))]
        let manager_result = VirtualDisplayManager::new_windows_session_aware(
            config.data_root.clone(),
            driver_dir,
            process_manager.clone(),
        );
        // Unit-test process managers do not spawn an interactive helper. The
        // manager/back-end behavior itself is covered by dedicated fakes.
        #[cfg(test)]
        let manager_result =
            VirtualDisplayManager::new_windows(config.data_root.clone(), driver_dir);
        let (virtual_display_manager, virtual_display_init_error) = match manager_result {
            Ok(manager) => (Some(Arc::new(manager)), None),
            Err(err) => (None, Some(err.to_string())),
        };
        let (stop_tx, _) = broadcast::channel(4);
        let mut ipc_bytes = [0_u8; 32];
        rand::rng().fill_bytes(&mut ipc_bytes);
        Self {
            config,
            storage,
            process_manager,
            windows_actions,
            state: ServiceState::default(),
            app_registry: AppInstanceRegistry::new(),
            render_senders: std::collections::HashMap::new(),
            webview_ready_waiters: std::collections::HashMap::new(),
            ticket_redeem_tx: None,
            virtual_display_manager,
            virtual_display_init_error,
            virtual_display_results: std::collections::HashMap::new(),
            ipc_token: URL_SAFE_NO_PAD.encode(ipc_bytes),
            stop_tx,
        }
    }

    /// Complete the one-shot WebView readiness wait only when the instance and
    /// port match the process that Service launched.
    pub fn complete_webview_ready(
        &mut self,
        instance_id: &str,
        listen_port: i32,
        result: Result<(), String>,
    ) -> bool {
        let valid = u16::try_from(listen_port).ok().is_some_and(|port| {
            self.app_registry
                .get(instance_id)
                .is_some_and(|record| record.listen_port == port)
        });
        if !valid {
            return false;
        }
        let Some(waiter) = self.webview_ready_waiters.remove(instance_id) else {
            return false;
        };
        waiter.send(result).is_ok()
    }

    pub fn subscribe_stop(&self) -> broadcast::Receiver<()> {
        self.stop_tx.subscribe()
    }

    pub fn request_stop(&mut self) {
        self.state.stop_requested = true;
        let _ = self.stop_tx.send(());
    }

    pub fn load_persisted_state(&mut self) -> Result<(), String> {
        let persisted = self.storage.load().map_err(|err| err.to_string())?;
        self.state.last_desktop_launch = persisted.desktop_launch.map(Into::into);
        info!(
            "loaded persisted state, desktop_launch_present={}",
            self.state.last_desktop_launch.is_some()
        );
        Ok(())
    }

    pub fn persist_state(&self) -> Result<(), String> {
        let persisted = PersistedServiceState {
            desktop_launch: self
                .state
                .last_desktop_launch
                .clone()
                .map(PersistedRenderLaunchSpec::from),
        };
        self.storage.save(&persisted).map_err(|err| err.to_string())
    }

    pub fn sync_process_state(&mut self) -> Result<(), String> {
        let processes = self.process_manager.list_processes()?;
        self.state.update_processes(&processes);
        Ok(())
    }

    pub fn handle_command(
        &mut self,
        command: Command,
    ) -> Result<Option<service_core::ServiceMessage>, String> {
        match command {
            Command::StartDesktop(spec) => {
                self.start_desktop(spec)?;
                Ok(None)
            }
            Command::StopDesktop => {
                self.stop_desktop()?;
                // Explicit stop must also clear the recorded launch, otherwise
                // the monitor loop would pull the render back within 3s.
                self.state.last_desktop_launch = None;
                self.persist_state()?;
                Ok(None)
            }
            Command::RestartDesktop(spec) => {
                self.restart_desktop(spec)?;
                Ok(None)
            }
            Command::HeartBeat {
                index,
                from,
                auth_info,
            } => {
                self.sync_process_state()?;
                // 应用层心跳:render 主循环每秒上报(from = "render_{port}"),
                // 用于 hang 检测——进程活着但消息循环死掉时心跳会中断。
                if from.starts_with("render_") {
                    self.state.note_render_heartbeat();
                }
                if let Some(auth_info) = auth_info {
                    self.state.last_auth_info = Some(auth_info);
                }
                Ok(Some(self.state.heartbeat_response(index)))
            }
            Command::AuthInfo(auth_info) => {
                info!(
                    "received auth info, device_id={}, appkey_configured={}, console={}:{}",
                    auth_info.device_id,
                    !auth_info.appkey.is_empty(),
                    auth_info.console_host,
                    auth_info.console_port
                );
                self.state.last_auth_info = Some(auth_info);
                Ok(None)
            }
            Command::CtrlAltDelete { .. } => {
                self.windows_actions.send_ctrl_alt_delete()?;
                Ok(None)
            }
            Command::RedeemConnectionTicket { .. } => {
                Err("ticket redemption must use the asynchronous service path".to_string())
            }
            Command::VirtualDisplay { .. } => {
                Err("virtual display operations must use the asynchronous service path".to_string())
            }
        }
    }

    pub fn start_desktop(&mut self, spec: RenderLaunchSpec) -> Result<(), String> {
        info!(
            "start desktop requested, work_dir={}, app_path={}",
            spec.work_dir, spec.app_path
        );
        self.sync_process_state()?;
        if self.state.desktop_alive {
            if self.state.last_desktop_launch.as_ref() == Some(&spec) {
                info!("desktop render already running with same launch spec");
                return Ok(());
            }
            warn!("desktop render already alive with different launch spec, stopping first");
            self.stop_desktop()?;
        }
        let mut secure_args = spec.args.clone();
        secure_args.retain(|arg| !arg.starts_with("--service_ipc_token="));
        secure_args.push(format!("--service_ipc_token={}", self.ipc_token));
        self.process_manager.start_process_as_active_user(
            &spec.work_dir,
            &spec.app_path,
            &secure_args,
        )?;
        if let Err(err) = self.start_user_proxy(&spec) {
            warn!("start user proxy failed: {err}");
        }
        self.state.update_desktop_launch(spec);
        self.persist_state()?;
        self.sync_process_state()?;
        info!(
            "start desktop finished, desktop_alive={}, desktop_pid={:?}",
            self.state.desktop_alive, self.state.desktop_pid
        );
        Ok(())
    }

    fn start_user_proxy(&self, spec: &RenderLaunchSpec) -> Result<(), String> {
        let render_port = user_proxy::extract_render_port(&spec.args);
        let app_path = user_proxy::user_proxy_path(&spec.work_dir);
        info!(
            "starting user proxy, path={}, render_port={}",
            app_path, render_port
        );
        self.process_manager.start_process_as_session_user(
            &spec.work_dir,
            &app_path,
            &user_proxy::user_proxy_args(render_port),
        )
    }

    pub fn stop_desktop(&mut self) -> Result<(), String> {
        let processes = self.process_manager.list_processes()?;
        let mut killed = 0usize;
        for process in processes {
            // Never kill Console game-hook instances from desktop stop.
            if process.is_app_instance_render_process() {
                continue;
            }
            if process.is_managed_clipboard_process() {
                info!(
                    "stopping managed desktop process, pid={}, exe_path={}, cmdline={}",
                    process.pid, process.exe_path, process.cmdline
                );
                let _ = self.process_manager.kill_process(process.pid);
                killed += 1;
            }
        }
        self.sync_process_state()?;
        info!("stop desktop finished, killed_managed_count={killed}");
        Ok(())
    }

    /// Console-scheduled game-hook/WebView instance start. Returns (listen_port, pid).
    ///
    /// Async + narrow locking: the registry is only locked while reading or
    /// mutating it; the multi-second process waits run unlocked so heartbeats
    /// and other instances are not blocked.
    pub async fn start_app_instance(
        runtime: &Arc<Mutex<ServiceRuntime>>,
        req: StartAppRequest,
    ) -> Result<(u16, u32), String> {
        let app_mode = service_core::app_instance::normalized_app_mode(&req.app_mode)?;
        let is_webview = app_mode == service_core::app_instance::APP_MODE_WEBVIEW;
        let game_path = if is_webview {
            service_core::app_instance::decode_webview_url(&req.webview_url_b64)?;
            None
        } else {
            let path = service_core::resolve_game_path(&req.install_root, &req.game_exe_rel)?;
            if !path.is_file() {
                return Err(format!(
                    "游戏程序不存在: {}（请核对路径，连续空格也会导致找不到文件）",
                    path.display()
                ));
            }
            Some(path)
        };
        let (record, process_manager, ipc_token, webview_ready_rx) = {
            let mut guard = runtime.lock().await;
            let work_dir = guard.pick_app_work_dir()?;
            let record = guard.app_registry.begin_start(&work_dir, req)?.clone();
            let ready_rx = if is_webview {
                let (ready_tx, ready_rx) = oneshot::channel();
                guard
                    .webview_ready_waiters
                    .insert(record.instance_id.clone(), ready_tx);
                Some(ready_rx)
            } else {
                None
            };
            (
                record,
                guard.process_manager.clone(),
                guard.ipc_token.clone(),
                ready_rx,
            )
        };
        let instance_id = record.instance_id.clone();
        let port = record.listen_port;
        let mut launch = record.launch.clone();
        launch
            .args
            .retain(|arg| !arg.starts_with("--service_ipc_token="));
        launch.args.push(format!("--service_ipc_token={ipc_token}"));
        info!(
            "start app instance {}, mode={}, has_game={}, view={:?}, work_dir={}, port={}",
            instance_id,
            app_mode,
            game_path.is_some(),
            record.view_game_path,
            launch.work_dir,
            port
        );
        if let Err(err) = process_manager.start_process_as_active_user(
            &launch.work_dir,
            &launch.app_path,
            &launch.args,
        ) {
            let mut guard = runtime.lock().await;
            guard.webview_ready_waiters.remove(&instance_id);
            let _ = guard.app_registry.mark_failed(&instance_id, err.clone());
            return Err(format!("启动 Render 失败: {err}"));
        }
        // Resolve the Browser/root render only. CEF children carry --type and
        // are deliberately excluded by find_app_render_pid_by_port.
        let pid = match wait_app_render_pid_by_port(&process_manager, port, 40, 100).await {
            Some(pid) => pid,
            None => {
                let msg = format!("Render 已启动但未找到监听端口 {port} 的进程");
                warn!("app instance {instance_id}: {msg}");
                // spawn 可能已成功但 WMI/参数匹配没跟上:再查一次并杀树兜底,
                // 确认清干净再 mark_failed,避免孤儿进程占用端口。
                if let Some(orphan_pid) = find_app_render_pid_by_port(&process_manager, port) {
                    warn!("killing orphaned render pid={orphan_pid} on port {port}");
                    kill_process_tree(&process_manager, orphan_pid);
                    if !wait_app_render_exit_by_port(&process_manager, port, 10, 100).await {
                        error!("orphan render on port {port} still alive after kill");
                    }
                }
                let mut guard = runtime.lock().await;
                guard.webview_ready_waiters.remove(&instance_id);
                let _ = guard.app_registry.mark_failed(&instance_id, msg.clone());
                return Err(msg);
            }
        };

        // Game-hook must actually launch the game. WebView readiness is
        // reported by its Render integration and does not have a game process.
        let game_path_str = game_path
            .as_ref()
            .map(|p| p.to_string_lossy().to_string())
            .unwrap_or_default();
        if let Some(game_path) = game_path.as_ref() {
            if !wait_game_process(&process_manager, &game_path_str, 50, 200).await {
                let msg = format!(
                    "游戏进程未启动: {}（Render 已退出或启动游戏失败，请核对程序路径）",
                    game_path.display()
                );
                warn!("app instance {instance_id}: {msg}");
                kill_process_tree(&process_manager, pid);
                let mut guard = runtime.lock().await;
                let _ = guard.app_registry.mark_failed(&instance_id, msg.clone());
                return Err(msg);
            }
        }

        if let Some(ready_rx) = webview_ready_rx {
            let ready = tokio::time::timeout(Duration::from_secs(20), ready_rx).await;
            let error = match ready {
                Ok(Ok(Ok(()))) => None,
                Ok(Ok(Err(error))) => Some(error),
                Ok(Err(_)) => Some("WebView Ready 等待通道已关闭".to_string()),
                Err(_) => Some("WebView 在 20 秒内未产生可编码首帧".to_string()),
            };
            if let Some(error) = error {
                warn!("app instance {instance_id}: {error}");
                kill_process_tree(&process_manager, pid);
                let mut guard = runtime.lock().await;
                guard.webview_ready_waiters.remove(&instance_id);
                let _ = guard.app_registry.mark_failed(&instance_id, error.clone());
                return Err(error);
            }
        }

        let mut guard = runtime.lock().await;
        guard.webview_ready_waiters.remove(&instance_id);
        if let Err(err) = guard.app_registry.mark_running(&instance_id, pid) {
            // 等待期间并发 Stop 已终结该实例:杀掉刚拉起的进程树,避免孤儿。
            warn!("start app instance {instance_id}: {err}; killing spawned tree");
            let view_path = guard
                .app_registry
                .get(&instance_id)
                .and_then(|r| r.view_game_path.clone());
            drop(guard);
            kill_process_tree(&process_manager, pid);
            let processes = process_manager.list_processes().unwrap_or_default();
            if !game_path_str.is_empty() {
                for game_pid in
                    service_core::process::find_pids_for_game_exe(&processes, &game_path_str)
                {
                    let _ = process_manager.kill_process(game_pid);
                }
            }
            // UE view 进程同样兜底清理。
            if let Some(view_path) = view_path {
                for view_pid in service_core::process::find_pids_for_game_exe(
                    &processes,
                    &view_path.to_string_lossy(),
                ) {
                    let _ = process_manager.kill_process(view_pid);
                }
            }
            return Err(err);
        }
        Ok((port, pid))
    }

    /// Pick a work_dir containing px_render.exe for game-hook launches.
    /// Prefer last desktop work_dir (Pixels dist) when it still exists; else
    /// service exe directory / current_dir (console local runs).
    fn pick_app_work_dir(&self) -> Result<String, String> {
        let candidate_dirs = [
            self.state
                .last_desktop_launch
                .as_ref()
                .map(|s| s.work_dir.clone())
                .unwrap_or_default(),
            std::env::current_exe()
                .ok()
                .and_then(|p| p.parent().map(|d| d.to_string_lossy().to_string()))
                .unwrap_or_default(),
            std::env::current_dir()
                .ok()
                .map(|d| d.to_string_lossy().to_string())
                .unwrap_or_default(),
        ];
        candidate_dirs
            .into_iter()
            .find(|d| {
                !d.is_empty()
                    && std::path::Path::new(d).is_dir()
                    && std::path::Path::new(d)
                        .join(service_core::config::RENDER_EXE_NAME)
                        .is_file()
            })
            .ok_or_else(|| {
                "no valid work_dir with px_render.exe (desktop launch / service exe dir)"
                    .to_string()
            })
    }

    /// Drop registry entries whose Render process is gone (killed outside Console stop).
    /// Also reaps Failed records: if the render is actually alive (e.g. the pid
    /// lookup timed out during start), kill the tree and remove the record so the
    /// port is really freed. Called before Console heartbeats so HB no longer claims
    /// ghost "running" instances.
    pub fn reap_dead_app_instances(&mut self) {
        use service_core::AppInstanceState;
        self.app_registry.prune_finished(FINISHED_RECORD_TTL);
        let candidates: Vec<(String, u16, Option<u32>, AppInstanceState)> = self
            .app_registry
            .list()
            .into_iter()
            .filter(|r| {
                matches!(
                    r.state,
                    AppInstanceState::Running
                        | AppInstanceState::Stopping
                        | AppInstanceState::Failed
                )
            })
            .map(|r| (r.instance_id.clone(), r.listen_port, r.pid, r.state.clone()))
            .collect();
        if candidates.is_empty() {
            return;
        }
        let process_manager = self.process_manager.clone();
        for (instance_id, listen_port, pid, state) in candidates {
            match find_app_render_pid_by_port(&process_manager, listen_port) {
                Some(live_pid) => {
                    if state == AppInstanceState::Failed {
                        warn!(
                            "reap failed app instance {instance_id}: render pid={live_pid} still alive on port {listen_port}, killing tree"
                        );
                        kill_process_tree(&process_manager, live_pid);
                        self.app_registry.remove(&instance_id);
                    } else if pid != Some(live_pid) {
                        let _ = self.app_registry.mark_running(&instance_id, live_pid);
                    }
                }
                None => {
                    // Failed with no live render: leave the record for prune_finished.
                    if state == AppInstanceState::Failed {
                        continue;
                    }
                    // No game-hook render on the listen port → instance is gone.
                    info!(
                        "reap dead app instance {instance_id}: listen_port={listen_port} pid={pid:?} gone"
                    );
                    let _ = self.app_registry.mark_stopped(&instance_id);
                }
            }
        }
    }

    /// Async + narrow locking, mirroring start_app_instance.
    pub async fn stop_app_instance(
        runtime: &Arc<Mutex<ServiceRuntime>>,
        instance_id: &str,
    ) -> Result<(), String> {
        let (rec, process_manager) = {
            let mut guard = runtime.lock().await;
            let rec = guard.app_registry.begin_stop(instance_id)?.clone();
            // A concurrent stop must immediately cancel a WebView startup that
            // is waiting for the first CEF frame instead of leaving it blocked
            // until the full readiness timeout expires.
            guard.webview_ready_waiters.remove(instance_id);
            (rec, guard.process_manager.clone())
        };
        info!(
            "stop app instance {}, pid={:?}, port={}",
            instance_id, rec.pid, rec.listen_port
        );
        // 优雅停止:先经 ws 给 render 下发 kSrvStopServer,render 会广播
        // kInstanceStopped 通知所有客户端后自行退出;宽限后仍走强杀兜底。
        {
            let guard = runtime.lock().await;
            if let Some(sender) = guard
                .render_senders
                .get(&format!("render_{}", rec.listen_port))
            {
                let stop_msg = service_core::ServiceMessage {
                    r#type: service_core::ServiceMessageType::StopServer as i32,
                    stop_server: Some(service_core::MsgStopServer::default()),
                    ..Default::default()
                };
                if sender
                    .send(service_core::encode_service_message(&stop_msg))
                    .is_ok()
                {
                    info!(
                        "sent kSrvStopServer to render_{} for instance {}",
                        rec.listen_port, instance_id
                    );
                }
            }
        }
        tokio::time::sleep(Duration::from_millis(800)).await;
        let processes = process_manager.list_processes().unwrap_or_default();
        let game_path = service_core::resolve_game_path(&rec.install_root, &rec.game_exe_rel).ok();

        let mut kill_pids: Vec<u32> = Vec::new();
        let mut identity_mismatch = false;
        // Kill 前校验 pid 当前身份:render 崩溃后 Windows 会复用 pid,只比数值
        // 会把占用该 pid 的无辜进程整棵树 TerminateProcess。
        let recorded_render_pid = rec.pid.filter(|_| {
            service_core::app_instance::is_game_hook_launch(&rec.launch)
                || service_core::app_instance::is_webview_launch(&rec.launch)
        });
        let render_pid = recorded_render_pid
            .and_then(|pid| {
                let belongs = game_path
                    .as_ref()
                    .map(|gp| pid_belongs_to_instance(&processes, rec.listen_port, gp, pid))
                    .unwrap_or(false);
                if belongs {
                    Some(pid)
                } else if processes.iter().any(|p| p.pid == pid) {
                    warn!(
                        "stop app instance {instance_id}: recorded pid {pid} is no longer the game-hook render/game, skipping (pid reuse?)"
                    );
                    identity_mismatch = true;
                    None
                } else {
                    // Recorded pid is gone entirely — nothing to kill for it.
                    None
                }
            })
            .or_else(|| find_app_render_pid_by_port(&process_manager, rec.listen_port));

        if let Some(pid) = render_pid {
            kill_pids.extend(service_core::process::collect_process_tree(&processes, pid));
        }
        if let Some(gp) = game_path.as_ref() {
            for pid in
                service_core::process::find_pids_for_game_exe(&processes, &gp.to_string_lossy())
            {
                if !kill_pids.contains(&pid) {
                    kill_pids.push(pid);
                }
            }
        }
        // UE boot/view：外壳可能拉起真游戏后先行退出，boot 树杀不到已成孤儿的
        // view 进程，按 view 路径补杀。
        if let Some(view_path) = rec.view_game_path.as_ref() {
            for pid in service_core::process::find_pids_for_game_exe(
                &processes,
                &view_path.to_string_lossy(),
            ) {
                if !kill_pids.contains(&pid) {
                    kill_pids.push(pid);
                }
            }
        }

        if kill_pids.is_empty() && identity_mismatch {
            return Err(format!(
                "instance {instance_id} recorded pid is now owned by an unrelated process, refusing to kill"
            ));
        }

        info!(
            "stop app instance {} will kill pids={:?} (render+game tree)",
            instance_id, kill_pids
        );
        for pid in kill_pids {
            if let Err(err) = process_manager.kill_process(pid) {
                warn!("kill pid {pid} for instance {instance_id} failed: {err}");
            }
        }
        // kill 后按端口复查:进程仍在监听时不能 mark_stopped,否则端口被释放,
        // 下一个实例分配同端口直接撞车。此时返回 Err 让 Console 标 failed。
        if !wait_app_render_exit_by_port(&process_manager, rec.listen_port, 10, 100).await {
            return Err(format!(
                "instance {instance_id} render still alive on port {} after kill",
                rec.listen_port
            ));
        }
        let mut guard = runtime.lock().await;
        guard.app_registry.mark_stopped(instance_id)?;
        Ok(())
    }

    pub fn restart_desktop(&mut self, spec: RenderLaunchSpec) -> Result<(), String> {
        self.stop_desktop()?;
        self.start_desktop(spec)
    }

    pub fn stop_managed_render(&mut self) -> Result<(), String> {
        info!("stop managed render begin");
        self.stop_desktop()?;
        info!("stop managed render finished");
        Ok(())
    }

    pub fn handle_control_event(&mut self, event: ControlEvent) -> Result<(), String> {
        match event {
            ControlEvent::Stop => {
                warn!("received service stop control event");
                // Keep the persisted launch spec so a later service start (e.g.
                // after a reboot) resumes the desktop render headlessly. Only an
                // explicit StopDesktop command (from the panel) clears it.
                //
                // Finish terminating the old Render before broadcasting stop.
                // Broadcasting first lets another run_service task win the
                // select and cancel this control future while it is yielding in
                // process discovery. The orphan then retains the previous
                // service's one-time IPC token and cannot reconnect after the
                // service restart until the heartbeat watchdog replaces it.
                let result = self.stop_managed_render();
                self.request_stop();
                if result.is_ok() {
                    info!("service stop control event handled successfully");
                }
                result
            }
            ControlEvent::ConsoleConnect(session) => {
                info!("console connect event, session={session}");
                self.stop_desktop()
            }
            ControlEvent::ConsoleDisconnect(session) => {
                info!("console disconnect event, session={session}");
                Ok(())
            }
            ControlEvent::SessionLogon(_session) => {
                info!("session logon");
                Ok(())
            }
            ControlEvent::SessionLogoff(_session) => {
                info!("session logoff");
                Ok(())
            }
            ControlEvent::SessionLock(_session) => {
                info!("session lock");
                Ok(())
            }
            ControlEvent::SessionUnlock(_session) => {
                info!("session unlock");
                Ok(())
            }
        }
    }
}

fn find_app_render_pid_by_port(
    process_manager: &Arc<dyn ProcessManager>,
    port: u16,
) -> Option<u32> {
    let processes = process_manager.list_processes().ok()?;
    for p in processes {
        if !p.is_app_instance_render_process() {
            continue;
        }
        // Exact token match only: substring matching would hit port 3200 on
        // "--network_listen_port=32000".
        if cmdline_has_listen_port(&p.cmdline, port) {
            return Some(p.pid);
        }
    }
    None
}

async fn wait_app_render_pid_by_port(
    process_manager: &Arc<dyn ProcessManager>,
    port: u16,
    attempts: u32,
    sleep_ms: u64,
) -> Option<u32> {
    for _ in 0..attempts {
        if let Some(pid) = find_app_render_pid_by_port(process_manager, port) {
            return Some(pid);
        }
        tokio::time::sleep(Duration::from_millis(sleep_ms)).await;
    }
    None
}

/// Wait for a game-hook render to disappear. WMI can keep a successfully
/// terminated process visible briefly, so checking once immediately after
/// `TerminateProcess` creates false stop failures and leaves Console instances in
/// `failed`. This is intentionally the inverse of `wait_app_render_pid_by_port`.
async fn wait_app_render_exit_by_port(
    process_manager: &Arc<dyn ProcessManager>,
    port: u16,
    attempts: u32,
    sleep_ms: u64,
) -> bool {
    for _ in 0..attempts {
        if find_app_render_pid_by_port(process_manager, port).is_none() {
            return true;
        }
        tokio::time::sleep(Duration::from_millis(sleep_ms)).await;
    }
    find_app_render_pid_by_port(process_manager, port).is_none()
}

async fn wait_game_process(
    process_manager: &Arc<dyn ProcessManager>,
    game_path: &str,
    attempts: u32,
    sleep_ms: u64,
) -> bool {
    for _ in 0..attempts {
        if let Ok(processes) = process_manager.list_processes() {
            if !service_core::process::find_pids_for_game_exe(&processes, game_path).is_empty() {
                return true;
            }
        }
        tokio::time::sleep(Duration::from_millis(sleep_ms)).await;
    }
    false
}

fn kill_process_tree(process_manager: &Arc<dyn ProcessManager>, root_pid: u32) {
    let processes = process_manager.list_processes().unwrap_or_default();
    for pid in service_core::process::collect_process_tree(&processes, root_pid) {
        if let Err(err) = process_manager.kill_process(pid) {
            warn!("kill tree pid {pid} failed: {err}");
        }
    }
}

pub async fn run_service(
    runtime: Arc<Mutex<ServiceRuntime>>,
    control_rx: Option<mpsc::UnboundedReceiver<ControlEvent>>,
) -> Result<(), String> {
    {
        let mut guard = runtime.lock().await;
        guard.load_persisted_state()?;
        guard.sync_process_state()?;
        if let Some(manager) = guard.virtual_display_manager.clone() {
            match manager.query() {
                Ok(result) => info!(
                    "virtual display state reconciled, phase={:?}, owned={}, actual={}, generation={}",
                    result.status.phase,
                    result.status.owned_slots.len(),
                    result.status.monitors.len(),
                    result.status.topology_generation
                ),
                Err(err) => warn!("virtual display startup reconciliation failed: {err}"),
            }
        } else if let Some(err) = &guard.virtual_display_init_error {
            warn!("virtual display manager unavailable: {err}");
        }
        info!("service runtime initialized");
    }

    let service = WebsocketService::new(runtime.clone());
    let service_task = tokio::spawn(async move { service.run_console().await });
    let monitor_task = tokio::spawn(monitor_loop(runtime.clone()));
    let control_task = tokio::spawn(control_loop(runtime.clone(), control_rx));
    let console_task = tokio::spawn(crate::console_client::console_client_loop(runtime.clone()));

    tokio::select! {
        result = service_task => {
            match result {
                Ok(inner) => inner,
                Err(err) => Err(err.to_string()),
            }
        }
        result = monitor_task => {
            match result {
                Ok(inner) => inner,
                Err(err) => Err(err.to_string()),
            }
        }
        result = control_task => {
            match result {
                Ok(inner) => inner,
                Err(err) => Err(err.to_string()),
            }
        }
        result = console_task => {
            match result {
                Ok(inner) => inner,
                Err(err) => Err(err.to_string()),
            }
        }
    }
}

async fn monitor_loop(runtime: Arc<Mutex<ServiceRuntime>>) -> Result<(), String> {
    let mut interval = tokio::time::interval(Duration::from_secs(3));
    let mut stop_rx = {
        let guard = runtime.lock().await;
        guard.subscribe_stop()
    };
    loop {
        tokio::select! {
            _ = interval.tick() => {
                let mut guard = runtime.lock().await;
                if let Err(err) = guard.sync_process_state() {
                    error!("sync_process_state failed: {err}");
                    continue;
                }
                if guard.state.desktop_alive {
                    guard.state.reset_restart_backoff();
                }
                // 进程存活但应用层心跳超时 = render 主循环 hang,进程级检查
                // 发现不了。杀掉后走下方既有重启逻辑(固定间隔重试)。
                if guard.state.render_hung() {
                    let pid = guard.state.desktop_pid;
                    warn!("desktop render heartbeat expired, killing hung render, pid={pid:?}");
                    if let Some(pid) = pid {
                        if let Err(err) = guard.process_manager.kill_process(pid) {
                            error!("kill hung render failed: {err}");
                        }
                    }
                    let _ = guard.sync_process_state();
                    // 复用 restart 冷却,避免 kill/restart 连环抖动
                    guard.state.note_restart_failure();
                }
                if guard.state.should_restart_desktop() {
                    if let Some(spec) = guard.state.last_desktop_launch.clone() {
                        if let Some(remaining) = guard.state.restart_backoff_remaining() {
                            info!("desktop restart cooldown active, remaining={remaining:?}");
                        } else {
                            warn!("desktop render missing, restarting");
                            if let Err(err) = guard.start_desktop(spec) {
                                error!("restart desktop failed: {err}");
                                guard.state.note_restart_failure();
                            }
                        }
                    }
                } else if guard.state.should_restart_user_proxy() {
                    if let Some(spec) = guard.state.last_desktop_launch.clone() {
                        if let Some(remaining) = guard.state.restart_backoff_remaining() {
                            info!("user proxy restart cooldown active, remaining={remaining:?}");
                        } else {
                            warn!("user proxy missing while render alive, restarting");
                            if let Err(err) = guard.start_user_proxy(&spec) {
                                error!("restart user proxy failed: {err}");
                                guard.state.note_restart_failure();
                            }
                            let _ = guard.sync_process_state();
                        }
                    }
                }
            }
            _ = stop_rx.recv() => {
                info!("monitor loop received stop signal");
                return Ok(());
            }
        }
    }
}

async fn control_loop(
    runtime: Arc<Mutex<ServiceRuntime>>,
    mut control_rx: Option<mpsc::UnboundedReceiver<ControlEvent>>,
) -> Result<(), String> {
    let Some(ref mut receiver) = control_rx else {
        return std::future::pending::<Result<(), String>>().await;
    };
    while let Some(event) = receiver.recv().await {
        info!("control loop received event: {:?}", event);
        let mut guard = runtime.lock().await;
        guard.handle_control_event(event)?;
    }
    warn!("control loop channel closed");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{
        atomic::{AtomicBool, Ordering},
        Mutex as StdMutex,
    };

    use service_core::process::ProcessSnapshot;

    struct MockProcessManager {
        processes: StdMutex<Vec<ProcessSnapshot>>,
        launches: StdMutex<Vec<RenderLaunchSpec>>,
        session_user_launches: StdMutex<Vec<RenderLaunchSpec>>,
        kills: StdMutex<Vec<u32>>,
        next_pid: StdMutex<u32>,
        fail_kills: StdMutex<bool>,
        stale_lists_after_kill: StdMutex<u32>,
        stale_snapshot: StdMutex<Option<Vec<ProcessSnapshot>>>,
    }

    impl MockProcessManager {
        fn new(processes: Vec<ProcessSnapshot>) -> Self {
            Self {
                processes: StdMutex::new(processes),
                launches: StdMutex::new(Vec::new()),
                session_user_launches: StdMutex::new(Vec::new()),
                kills: StdMutex::new(Vec::new()),
                next_pid: StdMutex::new(1000),
                fail_kills: StdMutex::new(false),
                stale_lists_after_kill: StdMutex::new(0),
                stale_snapshot: StdMutex::new(None),
            }
        }
    }

    impl ProcessManager for MockProcessManager {
        fn list_processes(&self) -> Result<Vec<ProcessSnapshot>, String> {
            let mut remaining = self.stale_lists_after_kill.lock().unwrap();
            if *remaining > 0 {
                *remaining -= 1;
                if let Some(snapshot) = self.stale_snapshot.lock().unwrap().as_ref() {
                    return Ok(snapshot.clone());
                }
            }
            Ok(self.processes.lock().unwrap().clone())
        }

        fn kill_process(&self, pid: u32) -> Result<(), String> {
            if *self.fail_kills.lock().unwrap() {
                return Err("mock kill denied".to_string());
            }
            self.kills.lock().unwrap().push(pid);
            let mut processes = self.processes.lock().unwrap();
            let snapshot = processes.clone();
            processes.retain(|process| process.pid != pid);
            drop(processes);
            if *self.stale_lists_after_kill.lock().unwrap() > 0 {
                *self.stale_snapshot.lock().unwrap() = Some(snapshot);
            }
            Ok(())
        }

        fn start_process_as_active_user(
            &self,
            work_dir: &str,
            app_path: &str,
            args: &[String],
        ) -> Result<(), String> {
            let spec = RenderLaunchSpec {
                work_dir: work_dir.to_string(),
                app_path: app_path.to_string(),
                args: args.to_vec(),
            };
            self.launches.lock().unwrap().push(spec.clone());
            let pid = {
                let mut next = self.next_pid.lock().unwrap();
                let pid = *next;
                *next += 1;
                pid
            };
            self.processes.lock().unwrap().push(ProcessSnapshot::new(
                pid,
                app_path,
                args.join(" "),
            ));
            // 模拟 game-hook render 拉起游戏子进程,wait_game_process 才能通过。
            if args.iter().any(|a| a == "--app_mode=game-hook") {
                if let Some(b64) = args.iter().find_map(|a| a.strip_prefix("--app_game_path=")) {
                    if let Ok(game_path) = px_base::crypto_util::base64_decode(b64) {
                        let game_pid = {
                            let mut next = self.next_pid.lock().unwrap();
                            let pid = *next;
                            *next += 1;
                            pid
                        };
                        self.processes
                            .lock()
                            .unwrap()
                            .push(ProcessSnapshot::new(game_pid, game_path, "").with_parent(pid));
                    }
                }
            }
            Ok(())
        }

        fn start_process_as_session_user(
            &self,
            work_dir: &str,
            app_path: &str,
            args: &[String],
        ) -> Result<(), String> {
            self.session_user_launches
                .lock()
                .unwrap()
                .push(RenderLaunchSpec {
                    work_dir: work_dir.to_string(),
                    app_path: app_path.to_string(),
                    args: args.to_vec(),
                });
            self.start_process_as_active_user(work_dir, app_path, args)
        }
    }

    struct MockActions {
        count: StdMutex<u32>,
    }

    impl MockActions {
        fn new() -> Self {
            Self {
                count: StdMutex::new(0),
            }
        }
    }

    impl SystemActions for MockActions {
        fn send_ctrl_alt_delete(&self) -> Result<(), String> {
            *self.count.lock().unwrap() += 1;
            Ok(())
        }
    }

    fn test_runtime(processes: Vec<ProcessSnapshot>) -> ServiceRuntime {
        let config = ServiceConfig::new(
            20375,
            std::env::temp_dir().join("px_data_test"),
            std::env::temp_dir().join("px_logs_test"),
        );
        ServiceRuntime::new(
            config,
            Arc::new(MockProcessManager::new(processes)),
            Arc::new(MockActions::new()),
        )
    }

    #[test]
    fn start_desktop_persists_launch_spec() {
        let mut runtime = test_runtime(Vec::new());
        let spec = RenderLaunchSpec {
            work_dir: "D:/app".to_string(),
            app_path: "D:/app/px_render.exe".to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        };
        runtime.start_desktop(spec.clone()).unwrap();
        assert_eq!(runtime.state.last_desktop_launch, Some(spec));
    }

    #[test]
    fn start_desktop_launches_user_proxy_with_session_user_token() {
        let config = ServiceConfig::new(
            20375,
            std::env::temp_dir().join("px_data_test_up"),
            std::env::temp_dir().join("px_logs_test_up"),
        );
        let manager = Arc::new(MockProcessManager::new(Vec::new()));
        let mut runtime =
            ServiceRuntime::new(config, manager.clone(), Arc::new(MockActions::new()));
        runtime
            .start_desktop(RenderLaunchSpec {
                work_dir: "D:/app".to_string(),
                app_path: "D:/app/px_render.exe".to_string(),
                args: vec![
                    "--app_mode=desktop".to_string(),
                    "--network_listen_port=20400".to_string(),
                ],
            })
            .unwrap();
        let session_launches = manager.session_user_launches.lock().unwrap();
        assert_eq!(session_launches.len(), 1);
        assert!(session_launches[0].app_path.ends_with("px_function.exe"));
        assert_eq!(
            session_launches[0].args,
            vec!["--render-port=20400".to_string()]
        );
    }

    #[test]
    fn heartbeat_returns_working_after_sync() {
        let mut runtime = test_runtime(vec![ProcessSnapshot::new(
            1,
            "D:/px_render.exe",
            "--app_mode=desktop",
        )]);
        let response = runtime
            .handle_command(Command::HeartBeat {
                index: 3,
                from: "panel".to_string(),
                auth_info: None,
            })
            .unwrap()
            .unwrap();
        assert_eq!(
            response.heart_beat_resp.unwrap().render_status_enum(),
            Some(service_core::RenderStatus::Working)
        );
    }

    fn test_auth_info() -> service_core::MsgAuthInfo {
        service_core::MsgAuthInfo {
            device_id: "dev-1".to_string(),
            auth_id: "aid-1".to_string(),
            auth_name: "license".to_string(),
            machine_code: "mc".to_string(),
            appkey: "ak-1".to_string(),
            role: 1,
            days: 365,
            max_streams: 4,
            end_timestamp_ms: 1_900_000_000_000,
            console_host: "console.example.com".to_string(),
            console_port: 8443,
            console_ssl: true,
        }
    }

    #[test]
    fn render_heartbeat_updates_hung_detection_baseline() {
        let mut runtime = test_runtime(vec![ProcessSnapshot::new(
            1,
            "D:/px_render.exe",
            "--app_mode=desktop",
        )]);
        assert!(runtime.state.last_render_heartbeat.is_none());
        runtime
            .handle_command(Command::HeartBeat {
                index: 1,
                from: "render_20371".to_string(),
                auth_info: None,
            })
            .unwrap();
        assert!(runtime.state.last_render_heartbeat.is_some());

        // Panel heartbeats must not touch the render hung-detection baseline.
        runtime.state.last_render_heartbeat = None;
        runtime
            .handle_command(Command::HeartBeat {
                index: 2,
                from: "panel".to_string(),
                auth_info: None,
            })
            .unwrap();
        assert!(runtime.state.last_render_heartbeat.is_none());
    }

    #[test]
    fn heartbeat_with_auth_info_updates_state() {
        let mut runtime = test_runtime(Vec::new());
        let auth_info = test_auth_info();
        runtime
            .handle_command(Command::HeartBeat {
                index: 1,
                from: "panel".to_string(),
                auth_info: Some(auth_info.clone()),
            })
            .unwrap();
        assert_eq!(runtime.state.last_auth_info, Some(auth_info.clone()));

        // A heartbeat without auth_info must not clear the recorded one.
        runtime
            .handle_command(Command::HeartBeat {
                index: 2,
                from: "panel".to_string(),
                auth_info: None,
            })
            .unwrap();
        assert_eq!(runtime.state.last_auth_info, Some(auth_info));
    }

    #[test]
    fn auth_info_command_updates_state() {
        let mut runtime = test_runtime(Vec::new());
        assert!(runtime.state.last_auth_info.is_none());
        let auth_info = test_auth_info();
        let response = runtime
            .handle_command(Command::AuthInfo(auth_info.clone()))
            .unwrap();
        assert!(response.is_none());
        assert_eq!(runtime.state.last_auth_info, Some(auth_info));
    }

    #[test]
    fn ctrl_alt_delete_is_forwarded() {
        let mut runtime = test_runtime(Vec::new());
        runtime
            .handle_command(Command::CtrlAltDelete {
                req_device_id: "d".to_string(),
                req_stream_id: "s".to_string(),
            })
            .unwrap();
    }

    #[test]
    fn console_connect_stops_desktop_render() {
        let mut runtime = test_runtime(vec![ProcessSnapshot::new(
            1,
            "D:/px_render.exe",
            "--app_mode=desktop",
        )]);
        runtime
            .handle_control_event(ControlEvent::ConsoleConnect(1))
            .unwrap();
        assert!(!runtime.state.desktop_alive);
    }

    #[test]
    fn persisted_state_is_loaded_back() {
        let config = ServiceConfig::new(
            20375,
            std::env::temp_dir().join("px_service_state_load"),
            std::env::temp_dir().join("px_logs"),
        );
        let mut runtime = ServiceRuntime::new(
            config.clone(),
            Arc::new(MockProcessManager::new(Vec::new())),
            Arc::new(MockActions::new()),
        );
        runtime.state.last_desktop_launch = Some(RenderLaunchSpec {
            work_dir: "D:/persist".to_string(),
            app_path: "D:/persist/px_render.exe".to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        runtime.persist_state().unwrap();

        let mut reloaded = ServiceRuntime::new(
            config,
            Arc::new(MockProcessManager::new(Vec::new())),
            Arc::new(MockActions::new()),
        );
        reloaded.load_persisted_state().unwrap();
        assert_eq!(
            reloaded.state.last_desktop_launch.unwrap().app_path,
            "D:/persist/px_render.exe"
        );
    }

    #[test]
    fn stop_managed_render_only_kills_render() {
        let mut runtime = test_runtime(vec![
            ProcessSnapshot::new(1, "D:/px_render.exe", "--app_mode=desktop"),
            ProcessSnapshot::new(2, "D:/UnrelatedApp.exe", ""),
            ProcessSnapshot::new(3, "D:/px_client.exe", ""),
            ProcessSnapshot::new(4, "D:/px_osinfo.exe", ""),
            ProcessSnapshot::new(5, "D:/px_function.exe", "--render-port=20371"),
        ]);
        runtime.stop_managed_render().unwrap();
        assert!(!runtime.state.desktop_alive);
        let processes = runtime.process_manager.list_processes().unwrap();
        assert_eq!(processes.len(), 3);
        assert!(processes
            .iter()
            .all(|process| !process.is_managed_clipboard_process()));
    }

    #[test]
    fn stop_desktop_command_clears_launch_and_persists_cleared_state() {
        let config = ServiceConfig::new(
            20375,
            std::env::temp_dir().join("px_service_stop_desktop"),
            std::env::temp_dir().join("px_logs_stop_desktop"),
        );
        let mut runtime = ServiceRuntime::new(
            config.clone(),
            Arc::new(MockProcessManager::new(vec![ProcessSnapshot::new(
                1,
                "D:/px_render.exe",
                "--app_mode=desktop",
            )])),
            Arc::new(MockActions::new()),
        );
        runtime.state.last_desktop_launch = Some(RenderLaunchSpec {
            work_dir: "D:/app".to_string(),
            app_path: "D:/app/px_render.exe".to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        runtime.handle_command(Command::StopDesktop).unwrap();
        assert!(runtime.state.last_desktop_launch.is_none());
        assert!(
            !runtime.state.should_restart_desktop(),
            "monitor loop must not pull the render back after an explicit stop"
        );

        // The cleared state must be persisted, or a service restart would
        // resurrect the launch record and the monitor loop would restart it.
        let mut reloaded = ServiceRuntime::new(
            config,
            Arc::new(MockProcessManager::new(Vec::new())),
            Arc::new(MockActions::new()),
        );
        reloaded.load_persisted_state().unwrap();
        assert!(reloaded.state.last_desktop_launch.is_none());
    }

    #[test]
    fn stop_control_event_only_kills_managed_processes() {
        let mut runtime = test_runtime(vec![
            ProcessSnapshot::new(1, "D:/px_render.exe", "--app_mode=desktop"),
            ProcessSnapshot::new(2, "D:/UnrelatedApp.exe", ""),
            ProcessSnapshot::new(3, "D:/px_client.exe", ""),
            ProcessSnapshot::new(4, "D:/px_osinfo.exe", ""),
            ProcessSnapshot::new(5, "D:/px_function.exe", "--render-port=20371"),
        ]);
        runtime.state.last_desktop_launch = Some(RenderLaunchSpec {
            work_dir: "D:/app".to_string(),
            app_path: "D:/app/px_render.exe".to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        runtime.handle_control_event(ControlEvent::Stop).unwrap();
        runtime.sync_process_state().unwrap();
        assert!(!runtime.state.desktop_alive);
        assert!(
            runtime.state.last_desktop_launch.is_some(),
            "SCM stop must preserve the launch spec so a restart resumes the render"
        );
        let processes = runtime.process_manager.list_processes().unwrap();
        assert_eq!(processes.len(), 3);
        assert!(processes
            .iter()
            .all(|process| !process.is_managed_clipboard_process()));
    }

    struct StopOrderingProcessManager {
        processes: StdMutex<Vec<ProcessSnapshot>>,
        stop_rx: StdMutex<broadcast::Receiver<()>>,
        stop_was_broadcast_before_kill: AtomicBool,
    }

    impl ProcessManager for StopOrderingProcessManager {
        fn list_processes(&self) -> Result<Vec<ProcessSnapshot>, String> {
            Ok(self.processes.lock().unwrap().clone())
        }

        fn kill_process(&self, pid: u32) -> Result<(), String> {
            if self.stop_rx.lock().unwrap().try_recv().is_ok() {
                self.stop_was_broadcast_before_kill
                    .store(true, Ordering::SeqCst);
            }
            self.processes
                .lock()
                .unwrap()
                .retain(|process| process.pid != pid);
            Ok(())
        }

        fn start_process_as_active_user(
            &self,
            _work_dir: &str,
            _app_path: &str,
            _args: &[String],
        ) -> Result<(), String> {
            Ok(())
        }
    }

    #[test]
    fn stop_control_broadcasts_only_after_managed_render_is_killed() {
        let mut runtime = test_runtime(Vec::new());
        let manager = Arc::new(StopOrderingProcessManager {
            processes: StdMutex::new(vec![ProcessSnapshot::new(
                1,
                "D:/px_render.exe",
                "--app_mode=desktop",
            )]),
            stop_rx: StdMutex::new(runtime.subscribe_stop()),
            stop_was_broadcast_before_kill: AtomicBool::new(false),
        });
        runtime.process_manager = manager.clone();

        runtime.handle_control_event(ControlEvent::Stop).unwrap();

        assert!(manager.processes.lock().unwrap().is_empty());
        assert!(
            !manager
                .stop_was_broadcast_before_kill
                .load(Ordering::SeqCst),
            "broadcasting stop before cleanup lets run_service cancel cleanup"
        );
    }

    fn sample_start_req(id: &str, port: i32, install_root: &str) -> StartAppRequest {
        StartAppRequest {
            request_id: format!("req-{id}"),
            instance_id: id.to_string(),
            app_id: "app-car".to_string(),
            app_mode: "game-hook".to_string(),
            webview_url_b64: String::new(),
            install_root: install_root.to_string(),
            game_exe_rel: r"Binaries\Win64\game.exe".to_string(),
            game_arguments: String::new(),
            listen_port: port,
            encoder_fps: 60,
            encoder_bitrate: 20,
            encoder_format: "h264".to_string(),
            webrtc_enabled: true,
            websocket_enabled: true,
            live_stream_id: "test-device__app__test-app".to_string(),
            push_rtmp_url: "rtmp://127.0.0.1:1935/live/{live_stream_id}".to_string(),
            device_id: "test-device".to_string(),
            relay_device_id: format!("test-device__instance__{id}"),
            relay_server_host: "console.test".to_string(),
            relay_server_port: 30502,
            relay_appkey: "app-key".to_string(),
        }
    }

    fn sample_webview_req(id: &str, port: i32) -> StartAppRequest {
        let mut request = sample_start_req(id, port, "");
        request.app_id = "app-webview".to_string();
        request.app_mode = "webview".to_string();
        request.webview_url_b64 = URL_SAFE_NO_PAD.encode(b"https://example.com/app");
        request.install_root.clear();
        request.game_exe_rel.clear();
        request
    }

    /// Temp work_dir with a fake px_render.exe + temp game exe; start
    /// validation requires both to exist on disk.
    struct AppTestDirs {
        work_dir_s: String,
        render_path: std::path::PathBuf,
        game_root_s: String,
        game_exe: std::path::PathBuf,
    }

    fn make_app_test_dirs(tag: &str) -> AppTestDirs {
        let base = std::env::temp_dir().join(format!("px_app_test_{tag}_{}", std::process::id()));
        let work_dir = base.join("work");
        let _ = std::fs::create_dir_all(&work_dir);
        let render_path = work_dir.join("px_render.exe");
        std::fs::write(&render_path, b"fake").unwrap();
        let game_exe = base.join(r"game\Binaries\Win64\game.exe");
        let _ = std::fs::create_dir_all(game_exe.parent().unwrap());
        std::fs::write(&game_exe, b"fake").unwrap();
        AppTestDirs {
            work_dir_s: work_dir.to_string_lossy().to_string(),
            render_path,
            game_root_s: base.join("game").to_string_lossy().to_string(),
            game_exe,
        }
    }

    #[tokio::test]
    async fn start_and_stop_app_instance_does_not_touch_desktop() {
        let dirs = make_app_test_dirs("startstop");
        let config = ServiceConfig::new(
            20375,
            std::env::temp_dir().join("px_data_app_inst"),
            std::env::temp_dir().join("px_logs_app_inst"),
        );
        let manager = Arc::new(MockProcessManager::new(vec![ProcessSnapshot::new(
            1,
            "D:/px_render.exe",
            "--app_mode=desktop",
        )]));
        let mut runtime =
            ServiceRuntime::new(config, manager.clone(), Arc::new(MockActions::new()));
        runtime.state.last_desktop_launch = Some(RenderLaunchSpec {
            work_dir: dirs.work_dir_s.clone(),
            app_path: dirs.render_path.to_string_lossy().to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        let runtime = Arc::new(Mutex::new(runtime));

        let (port, pid) = ServiceRuntime::start_app_instance(
            &runtime,
            sample_start_req("inst-1", 32111, &dirs.game_root_s),
        )
        .await
        .unwrap();
        assert_eq!(port, 32111);
        assert!(pid >= 1000);
        assert_eq!(
            runtime
                .lock()
                .await
                .app_registry
                .get("inst-1")
                .unwrap()
                .state,
            service_core::AppInstanceState::Running
        );
        // Mock spawn already launched the game as a child of the render.
        let game_pid = manager
            .processes
            .lock()
            .unwrap()
            .iter()
            .find(|p| p.exe_path_eq(&dirs.game_exe.to_string_lossy()))
            .map(|p| p.pid)
            .expect("game child process");

        // desktop still listed
        assert!(manager
            .list_processes()
            .unwrap()
            .iter()
            .any(|p| p.pid == 1 && p.cmdline.contains("desktop")));

        ServiceRuntime::stop_app_instance(&runtime, "inst-1")
            .await
            .unwrap();
        let kills = manager.kills.lock().unwrap().clone();
        assert!(kills.contains(&pid), "must kill render");
        assert!(kills.contains(&game_pid), "must kill game child");
        // desktop pid not killed
        assert!(!kills.contains(&1));
        assert!(manager.list_processes().unwrap().iter().any(|p| p.pid == 1));
    }

    #[tokio::test]
    async fn webview_start_waits_for_matching_first_frame_ready() {
        let dirs = make_app_test_dirs("webview_ready");
        let config = ServiceConfig::new(
            20375,
            std::env::temp_dir().join("px_data_webview_ready"),
            std::env::temp_dir().join("px_logs_webview_ready"),
        );
        let manager = Arc::new(MockProcessManager::new(Vec::new()));
        let mut service = ServiceRuntime::new(config, manager, Arc::new(MockActions::new()));
        service.state.last_desktop_launch = Some(RenderLaunchSpec {
            work_dir: dirs.work_dir_s,
            app_path: dirs.render_path.to_string_lossy().to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        let runtime = Arc::new(Mutex::new(service));
        let task_runtime = runtime.clone();
        let start = tokio::spawn(async move {
            ServiceRuntime::start_app_instance(
                &task_runtime,
                sample_webview_req("web-ready", 32112),
            )
            .await
        });

        for _ in 0..20 {
            if runtime
                .lock()
                .await
                .webview_ready_waiters
                .contains_key("web-ready")
            {
                break;
            }
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
        assert!(
            !start.is_finished(),
            "start must wait for the browser first frame"
        );
        assert!(!runtime
            .lock()
            .await
            .complete_webview_ready("web-ready", 32113, Ok(())));
        assert!(runtime
            .lock()
            .await
            .complete_webview_ready("web-ready", 32112, Ok(())));
        let (port, _) = start.await.unwrap().unwrap();
        assert_eq!(port, 32112);
        assert_eq!(
            runtime
                .lock()
                .await
                .app_registry
                .get("web-ready")
                .unwrap()
                .state,
            service_core::AppInstanceState::Running
        );
    }

    #[tokio::test]
    async fn stop_app_instance_refuses_to_kill_reused_pid() {
        let dirs = make_app_test_dirs("reuse");
        let config = ServiceConfig::new(
            20375,
            std::env::temp_dir().join("px_data_app_reuse"),
            std::env::temp_dir().join("px_logs_app_reuse"),
        );
        let manager = Arc::new(MockProcessManager::new(Vec::new()));
        let mut runtime =
            ServiceRuntime::new(config, manager.clone(), Arc::new(MockActions::new()));
        runtime.state.last_desktop_launch = Some(RenderLaunchSpec {
            work_dir: dirs.work_dir_s.clone(),
            app_path: dirs.render_path.to_string_lossy().to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        let runtime = Arc::new(Mutex::new(runtime));

        let (_, pid) = ServiceRuntime::start_app_instance(
            &runtime,
            sample_start_req("inst-r", 32220, &dirs.game_root_s),
        )
        .await
        .unwrap();

        // Render crashes and Windows reuses its pid for an innocent process;
        // the game process exits too.
        {
            let mut processes = manager.processes.lock().unwrap();
            processes.retain(|p| p.pid != pid && p.parent_pid != Some(pid));
            processes.push(ProcessSnapshot::new(pid, "C:/Windows/notepad.exe", ""));
        }

        let err = ServiceRuntime::stop_app_instance(&runtime, "inst-r")
            .await
            .unwrap_err();
        assert!(err.contains("refusing to kill"), "unexpected: {err}");
        // The innocent process must survive, and nothing may have been killed.
        assert!(manager
            .list_processes()
            .unwrap()
            .iter()
            .any(|p| p.pid == pid && p.exe_path.contains("notepad")));
        assert!(manager.kills.lock().unwrap().is_empty());
        // Record must not be marked stopped.
        assert_eq!(
            runtime
                .lock()
                .await
                .app_registry
                .get("inst-r")
                .unwrap()
                .state,
            service_core::AppInstanceState::Stopping
        );
    }

    #[tokio::test]
    async fn stop_app_instance_kill_failure_keeps_port_reserved() {
        let dirs = make_app_test_dirs("killfail");
        let config = ServiceConfig::new(
            20375,
            std::env::temp_dir().join("px_data_app_killfail"),
            std::env::temp_dir().join("px_logs_app_killfail"),
        );
        let manager = Arc::new(MockProcessManager::new(Vec::new()));
        let mut runtime =
            ServiceRuntime::new(config, manager.clone(), Arc::new(MockActions::new()));
        runtime.state.last_desktop_launch = Some(RenderLaunchSpec {
            work_dir: dirs.work_dir_s.clone(),
            app_path: dirs.render_path.to_string_lossy().to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        let runtime = Arc::new(Mutex::new(runtime));

        ServiceRuntime::start_app_instance(
            &runtime,
            sample_start_req("inst-k", 32221, &dirs.game_root_s),
        )
        .await
        .unwrap();

        *manager.fail_kills.lock().unwrap() = true;
        let err = ServiceRuntime::stop_app_instance(&runtime, "inst-k")
            .await
            .unwrap_err();
        assert!(err.contains("still alive"), "unexpected: {err}");
        {
            let guard = runtime.lock().await;
            let rec = guard.app_registry.get("inst-k").unwrap();
            // Not stopped: the port stays reserved so a new instance cannot collide.
            assert_eq!(rec.state, service_core::AppInstanceState::Stopping);
            assert!(guard.app_registry.allocate_port(32221).is_err());
        }

        // Once kills work again, stop succeeds.
        *manager.fail_kills.lock().unwrap() = false;
        ServiceRuntime::stop_app_instance(&runtime, "inst-k")
            .await
            .unwrap();
        assert_eq!(
            runtime
                .lock()
                .await
                .app_registry
                .get("inst-k")
                .unwrap()
                .state,
            service_core::AppInstanceState::Stopped
        );
    }

    #[tokio::test]
    async fn render_exit_wait_tolerates_stale_wmi_snapshots() {
        let manager = Arc::new(MockProcessManager::new(vec![ProcessSnapshot::new(
            42,
            "D:/px_render.exe",
            "--app_mode=game-hook --network_listen_port=32222",
        )]));
        *manager.stale_lists_after_kill.lock().unwrap() = 2;
        manager.kill_process(42).unwrap();

        assert!(
            wait_app_render_exit_by_port(&(manager as Arc<dyn ProcessManager>), 32222, 5, 1,).await
        );
    }

    #[test]
    fn stop_desktop_skips_game_hook_processes() {
        let mut runtime = test_runtime(vec![
            ProcessSnapshot::new(1, "D:/px_render.exe", "--app_mode=desktop"),
            ProcessSnapshot::new(
                2,
                "D:/px_render.exe",
                "--app_mode=game-hook --network_listen_port=32000",
            ),
            ProcessSnapshot::new(3, "D:/px_function.exe", "--render-port=20371"),
        ]);
        runtime.state.last_desktop_launch = Some(RenderLaunchSpec {
            work_dir: "D:/app".to_string(),
            app_path: "D:/app/px_render.exe".to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        runtime.stop_desktop().unwrap();
        let left = runtime.process_manager.list_processes().unwrap();
        assert!(
            left.iter().any(|p| p.pid == 2),
            "game-hook must survive desktop stop"
        );
        assert!(!left.iter().any(|p| p.pid == 1));
    }

    #[tokio::test]
    async fn multi_app_instances_same_machine_different_ports() {
        let dirs = make_app_test_dirs("multi");
        let manager = Arc::new(MockProcessManager::new(Vec::new()));
        let mut runtime = ServiceRuntime::new(
            ServiceConfig::new(
                20375,
                std::env::temp_dir().join("px_data_app_multi"),
                std::env::temp_dir().join("px_logs_app_multi"),
            ),
            manager,
            Arc::new(MockActions::new()),
        );
        runtime.state.last_desktop_launch = Some(RenderLaunchSpec {
            work_dir: dirs.work_dir_s.clone(),
            app_path: dirs.render_path.to_string_lossy().to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        });
        let runtime = Arc::new(Mutex::new(runtime));
        let (p1, id1) = ServiceRuntime::start_app_instance(
            &runtime,
            sample_start_req("a", 32201, &dirs.game_root_s),
        )
        .await
        .unwrap();
        let (p2, id2) = ServiceRuntime::start_app_instance(
            &runtime,
            sample_start_req("b", 32202, &dirs.game_root_s),
        )
        .await
        .unwrap();
        assert_eq!(p1, 32201);
        assert_eq!(p2, 32202);
        assert_ne!(id1, id2);
        assert_eq!(runtime.lock().await.app_registry.list().len(), 2);
    }
}
