use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SysSingleCpuInfo {
    #[serde(default)]
    pub name: String,

    #[serde(default)]
    pub usage: f32,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SysCpuInfo {
    #[serde(default)]
    pub usage: f32,

    #[serde(default)]
    pub vendor: String,

    #[serde(default)]
    pub brand: String,

    #[serde(default)]
    pub base_frequency: f32,

    #[serde(default)]
    pub current_frequency: f32,

    #[serde(default)]
    pub max_frequency: f32,

    #[serde(default)]
    pub cpus: Vec<SysSingleCpuInfo>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SysMemInfo {
    #[serde(default)]
    pub total: u64,

    #[serde(default)]
    pub total_gb: u64,

    #[serde(default)]
    pub used: u64,

    #[serde(default)]
    pub used_gb: u64,

    #[serde(default)]
    pub available: u64,

    #[serde(default)]
    pub available_gb: u64,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SysDiskInfo {
    #[serde(default)]
    pub disk_type: String, // ssd or ...

    #[serde(default)]
    pub mount_on: String, //C: D:

    #[serde(default)]
    pub filesystem: String,

    #[serde(default)]
    pub available: u64,

    #[serde(default)]
    pub available_gb: u64,

    #[serde(default)]
    pub total: u64,

    #[serde(default)]
    pub total_gb: u64,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SysIpNetwork {
    #[serde(default)]
    pub addr: String,

    #[serde(default)]
    pub prefix: u8,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SysNetworkInfo {
    #[serde(default)]
    pub name: String,

    #[serde(default)]
    pub mac: String,

    #[serde(default)]
    pub ip_networks: Vec<SysIpNetwork>,

    #[serde(default)]
    pub received_data: u64,

    #[serde(default)]
    pub sent_data: u64,

    #[serde(default)]
    pub max_transmit_speed: u64,

    #[serde(default)]
    pub max_receive_speed: u64,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SysUserInfo {}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SysOsInfo {
    #[serde(default)]
    pub sys_name: String,

    #[serde(default)]
    pub sys_kernel_version: String,

    #[serde(default)]
    pub sys_os_version: String,

    #[serde(default)]
    pub sys_os_long_version: String,

    #[serde(default)]
    pub sys_host_name: String,

    #[serde(default)]
    pub sys_kernel: String,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SysComponentInfo {
    #[serde(default)]
    pub temperature: f32,

    #[serde(default)]
    pub max: f32,

    #[serde(default)]
    pub critical: f32,

    #[serde(default)]
    pub label: String,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SysGpuInfo {
    #[serde(default)]
    pub id: String,

    #[serde(default)]
    pub brand: String,

    #[serde(default)]
    pub fan_speed: u32,

    #[serde(default)]
    pub power_limit: u32,

    #[serde(default)]
    pub encoder_utilization: u32,

    #[serde(default)]
    pub gpu_utilization: u32,

    #[serde(default)]
    pub mem_utilization: u32,

    #[serde(default)]
    pub temperature: u32,

    #[serde(default)]
    pub mem_free: u64,

    #[serde(default)]
    pub mem_free_gb: f32,

    #[serde(default)]
    pub mem_used: u64,

    #[serde(default)]
    pub mem_used_gb: f32,

    #[serde(default)]
    pub mem_total: u64,

    #[serde(default)]
    pub mem_total_gb: f32,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SysInfo {
    #[serde(default)]
    pub timestamp: i64,

    #[serde(default)]
    pub timestamp_readable: String,

    #[serde(default)]
    pub cpu: SysCpuInfo,

    #[serde(default)]
    pub mem: SysMemInfo,

    #[serde(default)]
    pub disks: Vec<SysDiskInfo>,

    #[serde(default)]
    pub networks: Vec<SysNetworkInfo>,

    #[serde(default)]
    pub os: SysOsInfo,

    #[serde(default)]
    pub components: Vec<SysComponentInfo>,

    #[serde(default)]
    pub uptime: String,

    #[serde(default)]
    pub gpus: Vec<SysGpuInfo>,
}
