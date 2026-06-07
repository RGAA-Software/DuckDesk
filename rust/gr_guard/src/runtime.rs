use std::path::PathBuf;
use std::sync::mpsc::{self, Sender};
use std::sync::Arc;
use std::thread::{self, JoinHandle};

use tracing::{error, info};

use crate::config::GUARD_POLL_INTERVAL;
use crate::process_monitor::{run_guard_tick, run_initial_sysinfo_check, ProcessLister};
use crate::process_spawn::ProcessSpawner;

pub struct GuardRuntime {
    stop_tx: Sender<()>,
    handle: Option<JoinHandle<()>>,
}

impl GuardRuntime {
    pub fn start(
        app_dir: PathBuf,
        lister: Arc<dyn ProcessLister>,
        spawner: Arc<dyn ProcessSpawner>,
    ) -> Self {
        let (stop_tx, stop_rx) = mpsc::channel();
        let handle = thread::spawn(move || {
            info!("guard runtime started, app_dir={}", app_dir.display());
            if let Err(err) = run_initial_sysinfo_check(lister.as_ref(), spawner.as_ref(), &app_dir) {
                error!("initial sysinfo check failed: {err}");
            }
            loop {
                if stop_rx.try_recv().is_ok() {
                    info!("guard runtime stop requested");
                    break;
                }

                if let Err(err) = run_guard_tick(lister.as_ref(), spawner.as_ref(), &app_dir) {
                    error!("guard tick failed: {err}");
                }

                if stop_rx.recv_timeout(GUARD_POLL_INTERVAL).is_ok() {
                    info!("guard runtime stop requested during wait");
                    break;
                }
            }
            info!("guard runtime exited");
        });
        Self {
            stop_tx,
            handle: Some(handle),
        }
    }

    pub fn stop(&mut self) {
        let _ = self.stop_tx.send(());
        if let Some(handle) = self.handle.take() {
            let _ = handle.join();
        }
    }
}

impl Drop for GuardRuntime {
    fn drop(&mut self) {
        self.stop();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::process_monitor::{ProcessEntry, ProcessLister};
    use crate::process_spawn::ProcessSpawner;
    use std::path::Path;
    use std::sync::Mutex;
    use std::time::Duration;

    struct StaticLister;

    impl ProcessLister for StaticLister {
        fn list_processes(&self) -> Result<Vec<ProcessEntry>, String> {
            Ok(vec![
                ProcessEntry {
                    exe_name: "GammaRay.exe".to_string(),
                },
                ProcessEntry {
                    exe_name: "GammaRaySysInfo.exe".to_string(),
                },
            ])
        }
    }

    #[derive(Default)]
    struct CountingSpawner {
        count: Mutex<usize>,
    }

    impl ProcessSpawner for CountingSpawner {
        fn spawn_path(&self, _exe_path: &Path) -> Result<(), String> {
            *self.count.lock().expect("lock") += 1;
            Ok(())
        }
    }

    #[test]
    fn runtime_can_start_and_stop_without_spawning() {
        let lister = Arc::new(StaticLister);
        let spawner = Arc::new(CountingSpawner::default());
        let mut runtime = GuardRuntime::start(PathBuf::from("D:/GammaRay"), lister, spawner.clone());
        std::thread::sleep(Duration::from_millis(50));
        runtime.stop();
        assert_eq!(*spawner.count.lock().expect("lock"), 0);
    }

    struct MissingSysinfoLister;

    impl ProcessLister for MissingSysinfoLister {
        fn list_processes(&self) -> Result<Vec<ProcessEntry>, String> {
            Ok(vec![])
        }
    }

    #[test]
    fn runtime_performs_initial_sysinfo_bootstrap() {
        let lister = Arc::new(MissingSysinfoLister);
        let spawner = Arc::new(CountingSpawner::default());
        let mut runtime = GuardRuntime::start(PathBuf::from("D:/GammaRay"), lister, spawner.clone());
        std::thread::sleep(Duration::from_millis(50));
        runtime.stop();
        assert!(*spawner.count.lock().expect("lock") >= 1);
    }
}
