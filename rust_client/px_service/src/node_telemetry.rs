//! Independent sampling lanes: GPU admission never waits for machine hardware.
use crate::hardware_probe::{GpuSnapshot, ProbeKind, ProbeSnapshot};
use crate::hardware_probe_process::{NativeProbeRunner, ProbeRunner};
use px_node_protocol::{NodeTelemetry, TelemetryProbeState};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::task::JoinHandle;

const GPU_CACHE_AGE: Duration = Duration::from_secs(1);
const HARDWARE_REFRESH_AGE: Duration = Duration::from_secs(5);
const HARDWARE_MAX_AGE: Duration = Duration::from_secs(15);

struct CachedSnapshot {
    completed_at: Instant,
    snapshot: ProbeSnapshot,
}

#[derive(Default)]
struct HardwareLane {
    cached: Option<CachedSnapshot>,
    worker: Option<JoinHandle<()>>,
}

impl Drop for HardwareLane {
    fn drop(&mut self) {
        if let Some(worker) = self.worker.take() {
            worker.abort();
        }
    }
}

#[derive(Clone)]
pub(crate) struct NodeTelemetrySampler {
    runner: Arc<dyn ProbeRunner>,
    gpu: Arc<tokio::sync::Mutex<Option<CachedSnapshot>>>,
    cpu: Arc<Mutex<HardwareLane>>,
    memory: Arc<Mutex<HardwareLane>>,
    disk: Arc<Mutex<HardwareLane>>,
}

impl Default for NodeTelemetrySampler {
    fn default() -> Self {
        Self::new(Arc::new(NativeProbeRunner))
    }
}

impl NodeTelemetrySampler {
    fn new(runner: Arc<dyn ProbeRunner>) -> Self {
        Self {
            runner,
            gpu: Arc::new(tokio::sync::Mutex::new(None)),
            cpu: Arc::new(Mutex::new(HardwareLane::default())),
            memory: Arc::new(Mutex::new(HardwareLane::default())),
            disk: Arc::new(Mutex::new(HardwareLane::default())),
        }
    }

    /// Dedicated business path. No CPU, memory or disk query is scheduled or awaited here.
    pub(crate) async fn sample_gpu(&self) -> GpuSnapshot {
        let mut cache = self.gpu.lock().await;
        if let Some(cached) = cache
            .as_ref()
            .filter(|cached| cached.completed_at.elapsed() < GPU_CACHE_AGE)
        {
            if let ProbeSnapshot::Gpu(snapshot) = &cached.snapshot {
                return snapshot.clone();
            }
        }
        let snapshot = match self.runner.sample(ProbeKind::Gpu).await {
            Ok(ProbeSnapshot::Gpu(snapshot)) => snapshot,
            Ok(_) => {
                tracing::warn!("GPU worker returned a different hardware kind");
                GpuSnapshot::unavailable()
            }
            Err(error) => {
                tracing::warn!(%error, probe = "gpu", "independent GPU sampling failed");
                GpuSnapshot::unavailable()
            }
        };
        *cache = Some(CachedSnapshot {
            completed_at: Instant::now(),
            snapshot: ProbeSnapshot::Gpu(snapshot.clone()),
        });
        snapshot
    }

    pub(crate) async fn sample(&self) -> NodeTelemetry {
        // Complete the priority lane first. Slow machine probes run in separate, lower-priority workers.
        let gpu = self.sample_gpu().await;
        self.refresh_hardware(ProbeKind::Cpu, &self.cpu);
        self.refresh_hardware(ProbeKind::Memory, &self.memory);
        self.refresh_hardware(ProbeKind::Disk, &self.disk);
        let cpu = match cached_hardware(&self.cpu) {
            Some(ProbeSnapshot::Cpu(snapshot)) => Some(snapshot),
            _ => None,
        };
        let memory = match cached_hardware(&self.memory) {
            Some(ProbeSnapshot::Memory(snapshot)) => Some(snapshot),
            _ => None,
        };
        let disk = match cached_hardware(&self.disk) {
            Some(ProbeSnapshot::Disk(snapshot)) => Some(snapshot),
            _ => None,
        };
        let gpu_available = gpu.gpu_inventory_revision.is_some();
        let probe_state = if cpu.is_some() && memory.is_some() && disk.is_some() && gpu_available {
            TelemetryProbeState::Ready
        } else if cpu.is_some() || memory.is_some() || disk.is_some() || gpu_available {
            TelemetryProbeState::Partial
        } else {
            TelemetryProbeState::Unavailable
        };
        NodeTelemetry {
            // Preserve the GPU's actual timestamp; a cached report must not freshen admission data.
            sampled_at: gpu.sampled_at,
            probe_state,
            logical_processors: cpu.as_ref().map(|snapshot| snapshot.logical_processors),
            cpu_utilization_per_mille: cpu.and_then(|snapshot| snapshot.utilization_per_mille),
            memory_total_bytes: memory.as_ref().map(|snapshot| snapshot.total_bytes),
            memory_available_bytes: memory.map(|snapshot| snapshot.available_bytes),
            disk_total_bytes: disk.as_ref().map(|snapshot| snapshot.total_bytes),
            disk_free_bytes: disk.map(|snapshot| snapshot.free_bytes),
            gpu_inventory_revision: gpu.gpu_inventory_revision,
            gpus: gpu.gpus,
        }
    }

    fn refresh_hardware(&self, kind: ProbeKind, lane: &Arc<Mutex<HardwareLane>>) {
        let mut lane_state = lane.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
        if lane_state
            .worker
            .as_ref()
            .is_some_and(|worker| !worker.is_finished())
        {
            return;
        }
        let weak_lane = Arc::downgrade(lane);
        let runner = Arc::clone(&self.runner);
        lane_state.worker = Some(tokio::spawn(async move {
            loop {
                let result = runner.sample(kind).await;
                let cached = match result {
                    Ok(snapshot) if snapshot.kind() == kind => Some(CachedSnapshot {
                        completed_at: Instant::now(),
                        snapshot,
                    }),
                    Ok(_) => {
                        tracing::warn!(
                            probe = kind.name(),
                            "hardware worker returned a different kind"
                        );
                        None
                    }
                    Err(error) => {
                        tracing::warn!(%error, probe = kind.name(), "independent hardware sampling failed");
                        None
                    }
                };
                if let Some(lane) = weak_lane.upgrade() {
                    let mut lane_state =
                        lane.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
                    // Failure clears only this lane; no other hardware or GPU result is touched.
                    lane_state.cached = cached;
                } else {
                    return;
                }
                tokio::time::sleep(HARDWARE_REFRESH_AGE).await;
            }
        }));
    }
}

fn cached_hardware(lane: &Mutex<HardwareLane>) -> Option<ProbeSnapshot> {
    let lane_state = lane.lock().unwrap_or_else(|poisoned| poisoned.into_inner());
    lane_state
        .cached
        .as_ref()
        .filter(|cached| cached.completed_at.elapsed() < HARDWARE_MAX_AGE)
        .map(|cached| cached.snapshot.clone())
}

#[cfg(test)]
pub(crate) fn unavailable() -> NodeTelemetry {
    NodeTelemetry {
        sampled_at: chrono::Utc::now(),
        probe_state: TelemetryProbeState::Unavailable,
        logical_processors: None,
        cpu_utilization_per_mille: None,
        memory_total_bytes: None,
        memory_available_bytes: None,
        disk_total_bytes: None,
        disk_free_bytes: None,
        gpu_inventory_revision: None,
        gpus: Vec::new(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::hardware_probe::{CpuSnapshot, DiskSnapshot, MemorySnapshot};
    use crate::hardware_probe_process::ProbeFuture;
    use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

    #[derive(Default)]
    struct ScriptedRunner {
        gpu_failed: AtomicBool,
        hardware_calls: AtomicUsize,
        live_cpu_workers: Arc<AtomicUsize>,
    }

    struct CpuLifetime(Arc<AtomicUsize>);
    impl Drop for CpuLifetime {
        fn drop(&mut self) {
            self.0.fetch_sub(1, Ordering::SeqCst);
        }
    }

    impl ProbeRunner for ScriptedRunner {
        fn sample(&self, kind: ProbeKind) -> ProbeFuture {
            if kind == ProbeKind::Gpu {
                let failed = self.gpu_failed.load(Ordering::SeqCst);
                return Box::pin(async move {
                    if failed {
                        return Err("GPU driver failed".into());
                    }
                    Ok(ProbeSnapshot::Gpu(GpuSnapshot {
                        sampled_at: chrono::Utc::now(),
                        gpu_inventory_revision: Some(11),
                        gpus: vec![],
                    }))
                });
            }
            self.hardware_calls.fetch_add(1, Ordering::SeqCst);
            let live_cpu_workers = Arc::clone(&self.live_cpu_workers);
            Box::pin(async move {
                match kind {
                    ProbeKind::Cpu => {
                        live_cpu_workers.fetch_add(1, Ordering::SeqCst);
                        let _lifetime = CpuLifetime(live_cpu_workers);
                        std::future::pending::<Result<ProbeSnapshot, String>>().await
                    }
                    ProbeKind::Memory => Ok(ProbeSnapshot::Memory(MemorySnapshot {
                        total_bytes: 100,
                        available_bytes: 50,
                    })),
                    ProbeKind::Disk => Ok(ProbeSnapshot::Disk(DiskSnapshot {
                        total_bytes: 200,
                        free_bytes: 80,
                    })),
                    ProbeKind::Gpu => unreachable!(),
                }
            })
        }
    }

    async fn allow_workers_to_complete() {
        for _worker_turn in 0..10 {
            tokio::task::yield_now().await;
        }
    }

    #[tokio::test]
    async fn business_gpu_sampling_never_queries_machine_hardware() {
        let runner = Arc::new(ScriptedRunner::default());
        let sampler = NodeTelemetrySampler::new(runner.clone());
        let snapshot = sampler.sample_gpu().await;
        assert_eq!(snapshot.gpu_inventory_revision, Some(11));
        assert_eq!(runner.hardware_calls.load(Ordering::SeqCst), 0);
    }

    #[tokio::test]
    async fn blocked_cpu_does_not_delay_gpu_memory_or_disk_and_drop_cancels_worker() {
        let runner = Arc::new(ScriptedRunner::default());
        let sampler = NodeTelemetrySampler::new(runner.clone());
        let initial = tokio::time::timeout(Duration::from_millis(100), sampler.sample())
            .await
            .unwrap();
        assert_eq!(initial.gpu_inventory_revision, Some(11));
        assert_eq!(initial.probe_state, TelemetryProbeState::Partial);
        allow_workers_to_complete().await;
        let report = sampler.sample().await;
        assert!(report.logical_processors.is_none());
        assert_eq!(report.memory_total_bytes, Some(100));
        assert_eq!(report.disk_total_bytes, Some(200));
        assert_eq!(report.gpu_inventory_revision, Some(11));
        assert_eq!(runner.live_cpu_workers.load(Ordering::SeqCst), 1);
        let cloned_sampler = sampler.clone();
        drop(sampler);
        allow_workers_to_complete().await;
        assert_eq!(runner.live_cpu_workers.load(Ordering::SeqCst), 1);
        drop(cloned_sampler);
        allow_workers_to_complete().await;
        assert_eq!(runner.live_cpu_workers.load(Ordering::SeqCst), 0);
    }

    #[tokio::test]
    async fn gpu_failure_invalidates_only_gpu_and_never_fabricates_readiness() {
        let runner = Arc::new(ScriptedRunner::default());
        let sampler = NodeTelemetrySampler::new(runner.clone());
        sampler.sample().await;
        allow_workers_to_complete().await;
        runner.gpu_failed.store(true, Ordering::SeqCst);
        *sampler.gpu.lock().await = None;
        let report = sampler.sample().await;
        assert!(report.gpu_inventory_revision.is_none());
        assert!(report.gpus.is_empty());
        assert_eq!(report.memory_total_bytes, Some(100));
        assert_eq!(report.disk_total_bytes, Some(200));
        assert_eq!(report.probe_state, TelemetryProbeState::Partial);
    }

    #[tokio::test]
    async fn stale_hardware_is_omitted_without_invalidating_fresh_gpu() {
        let sampler = NodeTelemetrySampler::default();
        sampler.cpu.lock().unwrap().cached = Some(CachedSnapshot {
            completed_at: Instant::now() - HARDWARE_MAX_AGE,
            snapshot: ProbeSnapshot::Cpu(CpuSnapshot {
                logical_processors: 8,
                utilization_per_mille: Some(100),
            }),
        });
        assert!(cached_hardware(&sampler.cpu).is_none());
    }

    #[tokio::test]
    async fn repeated_sampler_start_stop_does_not_leave_background_workers() {
        let runner = Arc::new(ScriptedRunner::default());
        for _lifecycle in 0..3 {
            let sampler = NodeTelemetrySampler::new(runner.clone());
            sampler.sample().await;
            allow_workers_to_complete().await;
            drop(sampler);
            allow_workers_to_complete().await;
            assert_eq!(runner.live_cpu_workers.load(Ordering::SeqCst), 0);
        }
    }
}
