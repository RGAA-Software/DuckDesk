use std::collections::{BTreeMap, VecDeque};

use gpui::Hsla;
use gpui_component::Theme;
use gr_base::sys_info::{SysComponentInfo, SysDiskInfo, SysGpuInfo, SysInfo, SysNetworkInfo};

pub const MAX_POINTS: usize = 180;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum MonitorTab {
    #[default]
    Overview = 0,
    CpuMemory = 1,
    Gpu = 2,
    Storage = 3,
    Network = 4,
    Sensors = 5,
}

impl MonitorTab {
    pub fn from_index(index: usize) -> Self {
        match index {
            0 => Self::Overview,
            1 => Self::CpuMemory,
            2 => Self::Gpu,
            3 => Self::Storage,
            4 => Self::Network,
            5 => Self::Sensors,
            _ => Self::Overview,
        }
    }
}

#[derive(Clone, Default)]
pub struct HistoryPoint {
    pub time: String,
    pub cpu_usage: f64,
    pub mem_used_gb: f64,
    pub mem_usage_percent: f64,
    pub net_send_mb: f64,
    pub net_recv_mb: f64,
}

#[derive(Clone, Default)]
pub struct GpuHistoryPoint {
    pub time: String,
    pub usage: f64,
    pub temperature: f64,
    pub memory_used_gb: f64,
}

#[derive(Clone, Default)]
pub struct MachineTelemetry {
    pub current: SysInfo,
    pub history: VecDeque<HistoryPoint>,
    pub gpu_history: BTreeMap<String, VecDeque<GpuHistoryPoint>>,
    last_net_received: u64,
    last_net_sent: u64,
    last_sample_ready: bool,
}

impl MachineTelemetry {
    pub fn new(initial: SysInfo) -> Self {
        let mut this = Self {
            current: initial,
            history: VecDeque::with_capacity(MAX_POINTS),
            gpu_history: BTreeMap::new(),
            last_net_received: 0,
            last_net_sent: 0,
            last_sample_ready: false,
        };
        this.record_sample();
        this
    }

    pub fn apply_snapshot(&mut self, snapshot: SysInfo) {
        self.current = snapshot;
        self.record_sample();
    }

    pub fn latest_cpu_percent(&self) -> f32 {
        self.current.cpu.usage
    }

    pub fn latest_mem_percent(&self) -> f32 {
        let total = self.current.mem.total as f64;
        let used = self.current.mem.used as f64;
        if total <= 0.0 {
            0.0
        } else {
            (used / total * 100.0) as f32
        }
    }

    pub fn highest_disk_percent(&self) -> f32 {
        self.current
            .disks
            .iter()
            .map(disk_usage_percent)
            .fold(0.0, f32::max)
    }

    pub fn primary_gpu_percent(&self) -> f32 {
        self.current
            .gpus
            .first()
            .map(|gpu| gpu.gpu_utilization as f32)
            .unwrap_or(0.0)
    }

    pub fn latest_send_rate(&self) -> f64 {
        self.history.back().map(|point| point.net_send_mb).unwrap_or(0.0)
    }

    pub fn latest_recv_rate(&self) -> f64 {
        self.history.back().map(|point| point.net_recv_mb).unwrap_or(0.0)
    }

    fn record_sample(&mut self) {
        let total_mem = self.current.mem.total as f64;
        let used_mem = self.current.mem.used as f64;
        let mem_usage_percent = if total_mem > 0.0 {
            (used_mem / total_mem * 100.0).clamp(0.0, 100.0)
        } else {
            0.0
        };

        let primary_network = self.current.networks.first();
        let (mut net_send_mb, mut net_recv_mb) = (0.0, 0.0);
        if let Some(network) = primary_network {
            if self.last_sample_ready {
                let sent_diff = network.sent_data.saturating_sub(self.last_net_sent);
                let recv_diff = network.received_data.saturating_sub(self.last_net_received);
                net_send_mb = bytes_to_mb(sent_diff);
                net_recv_mb = bytes_to_mb(recv_diff);
            }
            self.last_net_sent = network.sent_data;
            self.last_net_received = network.received_data;
            self.last_sample_ready = true;
        }

        let point = HistoryPoint {
            time: short_time_label(&self.current.timestamp_readable),
            cpu_usage: self.current.cpu.usage as f64,
            mem_used_gb: bytes_to_gb(self.current.mem.used),
            mem_usage_percent,
            net_send_mb,
            net_recv_mb,
        };
        push_point(&mut self.history, point);

        for gpu in &self.current.gpus {
            let id = gpu_key(gpu);
            let point = GpuHistoryPoint {
                time: short_time_label(&self.current.timestamp_readable),
                usage: gpu.gpu_utilization as f64,
                temperature: gpu.temperature as f64,
                memory_used_gb: gpu.mem_used_gb as f64,
            };
            let history = self.gpu_history.entry(id).or_default();
            push_point(history, point);
        }
    }
}

#[derive(Clone, Default)]
pub struct RemoteMachineState {
    pub machine_id: String,
    pub display_name: String,
    pub peer_addr: String,
    pub last_seen: String,
    pub connected: bool,
    pub telemetry: MachineTelemetry,
}

pub fn push_point<T>(queue: &mut VecDeque<T>, point: T) {
    if queue.len() >= MAX_POINTS {
        queue.pop_front();
    }
    queue.push_back(point);
}

pub fn bytes_to_gb(value: u64) -> f64 {
    value as f64 / 1024.0 / 1024.0 / 1024.0
}

pub fn bytes_to_mb(value: u64) -> f64 {
    value as f64 / 1024.0 / 1024.0
}

pub fn short_time_label(raw: &str) -> String {
    raw.rsplit_once(' ')
        .map(|(_, time)| time.to_string())
        .unwrap_or_else(|| raw.to_string())
}

pub fn disk_usage_percent(disk: &SysDiskInfo) -> f32 {
    if disk.total == 0 {
        0.0
    } else {
        ((disk.total.saturating_sub(disk.available)) as f64 / disk.total as f64 * 100.0) as f32
    }
}

pub fn gpu_key(gpu: &SysGpuInfo) -> String {
    if gpu.id.is_empty() {
        gpu.brand.clone()
    } else {
        gpu.id.clone()
    }
}

pub fn gpu_memory_percent(gpu: &SysGpuInfo) -> f32 {
    if gpu.mem_total == 0 {
        0.0
    } else {
        (gpu.mem_used as f64 / gpu.mem_total as f64 * 100.0) as f32
    }
}

pub fn network_ipv4(network: &SysNetworkInfo) -> String {
    network
        .ip_networks
        .iter()
        .map(|ip| format!("{}/{}", ip.addr, ip.prefix))
        .collect::<Vec<_>>()
        .join(", ")
}

pub fn sensor_status(component: &SysComponentInfo) -> String {
    if component.critical > 0.0 && component.temperature >= component.critical {
        "Critical".to_string()
    } else if component.max > 0.0 && component.temperature >= component.max {
        "Warning".to_string()
    } else {
        "Normal".to_string()
    }
}

pub fn format_optional_frequency(freq: f32) -> String {
    if freq <= 0.0 {
        "暂不可用".to_string()
    } else {
        format!("{freq:.2} GHz")
    }
}

pub fn cpu_color(cpu: f32, theme: &Theme) -> Hsla {
    if cpu >= 80.0 {
        theme.red
    } else if cpu >= 45.0 {
        theme.yellow
    } else {
        theme.green
    }
}

pub fn chart_ticks(max_value: f64) -> [f64; 5] {
    let top = if max_value <= 0.0 { 1.0 } else { max_value };
    [top, top * 0.75, top * 0.50, top * 0.25, 0.0]
}

pub fn format_tick(value: f64, unit: &str) -> String {
    match unit {
        "%" => format!("{value:.0}%"),
        "MB/s" => format!("{value:.1}"),
        "GB" => format!("{value:.1}"),
        "°C" => format!("{value:.0}"),
        _ => format!("{value:.1}"),
    }
}
