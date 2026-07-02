use std::sync::Arc;
use std::time::Duration;

use service_core::command::Command;
use service_core::config::ServiceConfig;
use service_core::storage::PersistedRenderLaunchSpec;
use service_core::storage::ServiceStorage;
use service_core::{PersistedServiceState, RenderLaunchSpec, ServiceState};
use tokio::sync::{broadcast, mpsc, Mutex};
use tracing::{error, info, warn};

use crate::user_proxy;
use crate::websocket_server::WebsocketService;
use crate::windows_actions::SystemActions;
use crate::windows_process::ProcessManager;

pub struct ServiceRuntime {
    pub config: ServiceConfig,
    pub storage: ServiceStorage,
    pub process_manager: Arc<dyn ProcessManager>,
    pub windows_actions: Arc<dyn SystemActions>,
    pub state: ServiceState,
    stop_tx: broadcast::Sender<()>,
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
        let (stop_tx, _) = broadcast::channel(4);
        Self {
            config,
            storage,
            process_manager,
            windows_actions,
            state: ServiceState::default(),
            stop_tx,
        }
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
                Ok(None)
            }
            Command::RestartDesktop(spec) => {
                self.restart_desktop(spec)?;
                Ok(None)
            }
            Command::HeartBeat { index, .. } => {
                self.sync_process_state()?;
                Ok(Some(self.state.heartbeat_response(index)))
            }
            Command::CtrlAltDelete { .. } => {
                self.windows_actions.send_ctrl_alt_delete()?;
                Ok(None)
            }
        }
    }

    pub fn start_desktop(&mut self, spec: RenderLaunchSpec) -> Result<(), String> {
        info!(
            "start desktop requested, work_dir={}, app_path={}, args={:?}",
            spec.work_dir, spec.app_path, spec.args
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
        self.process_manager.start_process_as_active_user(
            &spec.work_dir,
            &spec.app_path,
            &spec.args,
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
                self.request_stop();
                let result = self.stop_managed_render();
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

pub async fn run_service(
    runtime: Arc<Mutex<ServiceRuntime>>,
    control_rx: Option<mpsc::UnboundedReceiver<ControlEvent>>,
) -> Result<(), String> {
    {
        let mut guard = runtime.lock().await;
        guard.load_persisted_state()?;
        guard.sync_process_state()?;
        info!("service runtime initialized");
    }

    let service = WebsocketService::new(runtime.clone());
    let service_task = tokio::spawn(async move { service.run_console().await });
    let monitor_task = tokio::spawn(monitor_loop(runtime.clone()));
    let control_task = tokio::spawn(control_loop(runtime.clone(), control_rx));

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
                if guard.state.should_restart_desktop() {
                    if let Some(spec) = guard.state.last_desktop_launch.clone() {
                        warn!("desktop render missing, restarting");
                        if let Err(err) = guard.start_desktop(spec) {
                            error!("restart desktop failed: {err}");
                        }
                    }
                } else if guard.state.should_restart_user_proxy() {
                    if let Some(spec) = guard.state.last_desktop_launch.clone() {
                        warn!("user proxy missing while render alive, restarting");
                        if let Err(err) = guard.start_user_proxy(&spec) {
                            error!("restart user proxy failed: {err}");
                        }
                        let _ = guard.sync_process_state();
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
    use std::sync::Mutex as StdMutex;

    use service_core::process::ProcessSnapshot;

    struct MockProcessManager {
        processes: StdMutex<Vec<ProcessSnapshot>>,
        launches: StdMutex<Vec<RenderLaunchSpec>>,
        session_user_launches: StdMutex<Vec<RenderLaunchSpec>>,
        kills: StdMutex<Vec<u32>>,
    }

    impl MockProcessManager {
        fn new(processes: Vec<ProcessSnapshot>) -> Self {
            Self {
                processes: StdMutex::new(processes),
                launches: StdMutex::new(Vec::new()),
                session_user_launches: StdMutex::new(Vec::new()),
                kills: StdMutex::new(Vec::new()),
            }
        }
    }

    impl ProcessManager for MockProcessManager {
        fn list_processes(&self) -> Result<Vec<ProcessSnapshot>, String> {
            Ok(self.processes.lock().unwrap().clone())
        }

        fn kill_process(&self, pid: u32) -> Result<(), String> {
            self.kills.lock().unwrap().push(pid);
            self.processes
                .lock()
                .unwrap()
                .retain(|process| process.pid != pid);
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
            self.processes
                .lock()
                .unwrap()
                .push(ProcessSnapshot::new(99, app_path, args.join(" ")));
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
            std::env::temp_dir().join("gr_data_test"),
            std::env::temp_dir().join("gr_logs_test"),
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
            app_path: "D:/app/GammaRayRender.exe".to_string(),
            args: vec!["--app_mode=desktop".to_string()],
        };
        runtime.start_desktop(spec.clone()).unwrap();
        assert_eq!(runtime.state.last_desktop_launch, Some(spec));
    }

    #[test]
    fn start_desktop_launches_user_proxy_with_session_user_token() {
        let config = ServiceConfig::new(
            20375,
            std::env::temp_dir().join("gr_data_test_up"),
            std::env::temp_dir().join("gr_logs_test_up"),
        );
        let manager = Arc::new(MockProcessManager::new(Vec::new()));
        let mut runtime = ServiceRuntime::new(
            config,
            manager.clone(),
            Arc::new(MockActions::new()),
        );
        runtime
            .start_desktop(RenderLaunchSpec {
                work_dir: "D:/app".to_string(),
                app_path: "D:/app/GammaRayRender.exe".to_string(),
                args: vec![
                    "--app_mode=desktop".to_string(),
                    "--network_listen_port=20400".to_string(),
                ],
            })
            .unwrap();
        let session_launches = manager.session_user_launches.lock().unwrap();
        assert_eq!(session_launches.len(), 1);
        assert!(session_launches[0].app_path.ends_with("GammaRayUserProxy.exe"));
        assert_eq!(
            session_launches[0].args,
            vec!["--render-port=20400".to_string()]
        );
    }

    #[test]
    fn heartbeat_returns_working_after_sync() {
        let mut runtime = test_runtime(vec![ProcessSnapshot::new(
            1,
            "D:/GammaRayRender.exe",
            "--app_mode=desktop",
        )]);
        let response = runtime
            .handle_command(Command::HeartBeat {
                index: 3,
                from: "panel".to_string(),
            })
            .unwrap()
            .unwrap();
        assert_eq!(
            response.heart_beat_resp.unwrap().render_status_enum(),
            Some(service_core::RenderStatus::Working)
        );
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
            "D:/GammaRayRender.exe",
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
            std::env::temp_dir().join("gr_service_state_load"),
            std::env::temp_dir().join("gr_logs"),
        );
        let mut runtime = ServiceRuntime::new(
            config.clone(),
            Arc::new(MockProcessManager::new(Vec::new())),
            Arc::new(MockActions::new()),
        );
        runtime.state.last_desktop_launch = Some(RenderLaunchSpec {
            work_dir: "D:/persist".to_string(),
            app_path: "D:/persist/GammaRayRender.exe".to_string(),
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
            "D:/persist/GammaRayRender.exe"
        );
    }

    #[test]
    fn stop_managed_render_only_kills_render() {
        let mut runtime = test_runtime(vec![
            ProcessSnapshot::new(1, "D:/GammaRayRender.exe", "--app_mode=desktop"),
            ProcessSnapshot::new(2, "D:/GammaRayGuard.exe", ""),
            ProcessSnapshot::new(3, "D:/GammaRayClientInner.exe", ""),
            ProcessSnapshot::new(4, "D:/GammaRaySysInfo.exe", ""),
            ProcessSnapshot::new(5, "D:/GammaRayUserProxy.exe", "--render-port=20371"),
        ]);
        runtime.stop_managed_render().unwrap();
        assert!(!runtime.state.desktop_alive);
        let processes = runtime.process_manager.list_processes().unwrap();
        assert_eq!(processes.len(), 3);
        assert!(processes.iter().all(|process| !process.is_managed_clipboard_process()));
    }

    #[test]
    fn stop_control_event_only_kills_managed_processes() {
        let mut runtime = test_runtime(vec![
            ProcessSnapshot::new(1, "D:/GammaRayRender.exe", "--app_mode=desktop"),
            ProcessSnapshot::new(2, "D:/GammaRayGuard.exe", ""),
            ProcessSnapshot::new(3, "D:/GammaRayClientInner.exe", ""),
            ProcessSnapshot::new(4, "D:/GammaRaySysInfo.exe", ""),
            ProcessSnapshot::new(5, "D:/GammaRayUserProxy.exe", "--render-port=20371"),
        ]);
        runtime.handle_control_event(ControlEvent::Stop).unwrap();
        runtime.sync_process_state().unwrap();
        assert!(!runtime.state.desktop_alive);
        let processes = runtime.process_manager.list_processes().unwrap();
        assert_eq!(processes.len(), 3);
        assert!(processes.iter().all(|process| !process.is_managed_clipboard_process()));
    }
}
