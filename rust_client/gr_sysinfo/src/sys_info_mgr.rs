use adlx::helper::AdlxHelper;
use anyhow::Result;
use gr_base::sys_info::{
    SysComponentInfo, SysCpuInfo, SysDiskInfo, SysGpuInfo, SysInfo, SysIpNetwork, SysMemInfo,
    SysNetworkInfo, SysOsInfo, SysSingleCpuInfo,
};
use nvml_wrapper::enum_wrappers::device::TemperatureSensor;
use nvml_wrapper::Nvml;
use sysinfo::{Components, Disks, Networks, System, Users};

#[derive(Debug, Clone)]
struct DefaultEthernet {
    ipv4: String,
    transmit_speed: u64,
    receive_speed: u64,
}

pub struct SysInfoManager {
    system: System,
    networks: Networks,
    disks: Disks,
    components: Components,
    users: Users,
    max_frequency: f32,
    def_ethernet: Option<DefaultEthernet>,
}

impl SysInfoManager {
    pub fn new() -> Self {
        let system = System::new_all();
        let networks = Networks::new_with_refreshed_list();
        let disks = Disks::new_with_refreshed_list();
        let components = Components::new_with_refreshed_list();
        let users = Users::new_with_refreshed_list();

        SysInfoManager {
            system,
            networks,
            disks,
            components,
            users,
            max_frequency: 0.0,
            def_ethernet: None,
        }
    }

    pub fn load_system_info(&mut self) -> SysInfo {
        self.system.refresh_all();
        self.networks.refresh(true);
        self.disks.refresh(true);
        self.components.refresh(true);
        self.users.refresh();

        // CPU
        let usage = self.system.global_cpu_usage();
        let vendor = self.system.cpus()[0].vendor_id();
        let brand = self.system.cpus()[0].brand();
        let base_frequency = self.system.cpus()[0].frequency() as f32 / 1000.0;
        let current_frequency = 0.0;
        if self.max_frequency <= 0.1 {
            self.max_frequency = calcmhz::mhz().unwrap_or(0.0) as f32 / 1000.0;
        }
        let mut cpus = Vec::new();
        for cpu in self.system.cpus() {
            let single_cpu = SysSingleCpuInfo {
                name: cpu.name().to_string(),
                usage: cpu.cpu_usage(),
            };
            cpus.push(single_cpu);
        }
        let cpu = SysCpuInfo {
            usage,
            vendor: vendor.to_string(),
            brand: brand.to_string(),
            base_frequency,
            current_frequency,
            max_frequency: self.max_frequency,
            cpus,
        };

        let gb = 1024 * 1024 * 1024;
        // Memory
        let mem = SysMemInfo {
            total: self.system.total_memory(),
            total_gb: self.system.total_memory() / gb,
            used: self.system.used_memory(),
            used_gb: self.system.used_memory() / gb,
            available: self.system.available_memory(),
            available_gb: self.system.available_memory() / gb,
        };

        // Disks
        let mut disks_info = Vec::new();
        for disk in &mut self.disks {
            disks_info.push(SysDiskInfo {
                disk_type: disk.kind().to_string(),
                mount_on: disk.mount_point().to_str().unwrap_or("").to_string(),
                filesystem: disk.file_system().to_str().unwrap_or("").to_string(),
                available: disk.available_space(),
                available_gb: disk.available_space() / gb,
                total: disk.total_space(),
                total_gb: disk.total_space() / gb,
            });
        }

        // Network
        if self.def_ethernet.is_none() {
            let def_ethernet = match netdev::get_default_interface() {
                Ok(interface) => {
                    println!("\tIPv4: {:?}", interface.ipv4);
                    println!("\tIPv6: {:?}", interface.ipv6);
                    println!("\tTransmit Speed: {:?}", interface.transmit_speed);
                    println!("\tReceive Speed: {:?}", interface.receive_speed);
                    if interface.ipv4.is_empty() {
                        None
                    } else {
                        Some(DefaultEthernet {
                            ipv4: interface.ipv4[0].addr().to_string(),
                            transmit_speed: interface.transmit_speed.unwrap_or(0),
                            receive_speed: interface.receive_speed.unwrap_or(0),
                        })
                    }
                }
                _ => None,
            };
            println!("\tDefault Network: {:?}", def_ethernet);
            self.def_ethernet = def_ethernet;
        }

        let mut networks = Vec::new();
        for (interface_name, data) in self.networks.iter() {
            if interface_name.contains("VMware") {
                continue;
            }

            let mut max_transmit_speed = 0;
            let mut max_receive_speed = 0;
            let mut nts = Vec::new();
            let mut found_def_ethernet = false;
            for nt in data.ip_networks() {
                let addr = nt.addr.to_string();
                // ignore IPV6
                if addr.contains(":") || addr.contains("::") {
                    continue;
                }
                nts.push(SysIpNetwork {
                    addr: addr.clone(),
                    prefix: nt.prefix,
                });

                if let Some(def_ethernet) = self.def_ethernet.clone() {
                    if addr == def_ethernet.ipv4 {
                        max_transmit_speed = def_ethernet.transmit_speed;
                        max_receive_speed = def_ethernet.receive_speed;
                        found_def_ethernet = true;
                    }
                }
            }
            if !found_def_ethernet {
                println!(
                    "this is not default ethernet: {}, {}",
                    interface_name,
                    data.mac_address().to_string()
                );
                continue;
            }

            networks.push(SysNetworkInfo {
                name: interface_name.clone(),
                mac: data.mac_address().to_string(),
                ip_networks: nts,
                received_data: data.total_received(),
                sent_data: data.total_transmitted(),
                max_transmit_speed,
                max_receive_speed,
            });
        }

        // OS info
        let os = SysOsInfo {
            sys_name: System::name()
                .unwrap_or_else(|| "<unknown>".to_owned())
                .to_string(),
            sys_kernel_version: System::kernel_version()
                .unwrap_or_else(|| "<unknown>".to_owned())
                .to_string(),
            sys_os_version: System::os_version()
                .unwrap_or_else(|| "<unknown>".to_owned())
                .to_string(),
            sys_os_long_version: System::long_os_version()
                .unwrap_or_else(|| "<unknown>".to_owned())
                .to_string(),
            sys_host_name: System::host_name()
                .unwrap_or_else(|| "<unknown>".to_owned())
                .to_string(),
            sys_kernel: System::kernel_long_version().to_string(),
        };

        // Components
        let mut cps = Vec::new();
        for component in self.components.iter() {
            cps.push(SysComponentInfo {
                temperature: component.temperature().unwrap_or(0.0),
                max: component.max().unwrap_or(0.0),
                critical: component.critical().unwrap_or(0.0),
                label: component.label().to_string(),
            });
        }

        // Uptime
        let up = System::uptime();
        let mut uptime = up;
        let days = uptime / 86400;
        uptime -= days * 86400;
        let hours = uptime / 3600;
        uptime -= hours * 3600;
        let minutes = uptime / 60;
        let uptime = format!("{days} days {hours} hours {minutes} minutes",);

        // GPU info
        let mut gpus = Vec::new();
        let nvml = Nvml::init();
        if let Ok(nvml) = nvml {
            let device_count = nvml.device_count().unwrap_or(0);
            for i in 0..device_count {
                let mut gpu_info = SysGpuInfo::default();
                let device = nvml.device_by_index(i);
                if let Ok(device) = device {
                    // ID
                    if let Ok(serial) = device.serial() {
                        gpu_info.id = serial;
                    } else {
                        if let Ok(uuid) = device.uuid() {
                            gpu_info.id = uuid;
                        }
                    }

                    // Brand
                    let brand = device.name();
                    if let Ok(brand) = brand {
                        let brand = brand.trim_matches('"').replace("\"", "");
                        gpu_info.brand = brand.to_string();
                    }

                    // Fan speed
                    let fan_speed = device.fan_speed_rpm(0).unwrap_or(0);
                    gpu_info.fan_speed = fan_speed;

                    // Power limit
                    gpu_info.power_limit = device.enforced_power_limit().unwrap_or(0);

                    // Encoder
                    if let Ok(u) = device.encoder_utilization() {
                        gpu_info.encoder_utilization = u.utilization;
                    }

                    if let Ok(u) = device.utilization_rates() {
                        gpu_info.gpu_utilization = u.gpu;
                        gpu_info.mem_utilization = u.memory;
                    }

                    gpu_info.temperature = device.temperature(TemperatureSensor::Gpu).unwrap_or(0);

                    if let Ok(mi) = device.memory_info() {
                        gpu_info.mem_free = mi.free;
                        gpu_info.mem_free_gb = mi.free as f32 * 1.0 / (gb as f32);
                        gpu_info.mem_total = mi.total;
                        gpu_info.mem_total_gb = mi.total as f32 * 1.0 / (gb as f32);
                        gpu_info.mem_used = mi.used;
                        gpu_info.mem_used_gb = mi.used as f32 * 1.0 / (gb as f32);
                    }

                    // let enc_sessions = device.encoder_sessions();
                    // if let Ok(sessions) = enc_sessions {
                    //     sessions.iter().for_each(|info| {
                    //         println!("{:#?}", info);
                    //     });
                    // }

                    gpus.push(gpu_info);
                }
            }
        }

        if let Ok(amd_gpus) = self.load_amd_gpu_info() {
            for amd_gpu_info in amd_gpus {
                gpus.push(amd_gpu_info);
            }
        }

        SysInfo {
            timestamp: gr_base::get_current_timestamp(),
            timestamp_readable: gr_base::get_current_readable_timestamp(),
            cpu,
            mem,
            disks: disks_info,
            networks,
            os,
            components: cps,
            uptime,
            gpus,
        }
    }

    fn load_amd_gpu_info(&self) -> Result<Vec<SysGpuInfo>, anyhow::Error> {
        let helper = AdlxHelper::new()?;
        let system = helper.system();
        let gpu_list = system.gpus()?;
        let performance_monitoring_services = system.performance_monitoring_services()?;

        let mut gpus_info = Vec::new();
        for gpu in 0..gpu_list.size() {
            let gpu = gpu_list.at(gpu)?;

            // id
            let gpu_id = if let Ok(id) = gpu.unique_id() {
                id.to_string()
            } else {
                "".to_string()
            };

            let gpu_name = gpu.name().unwrap_or("<unknown>");
            let gpu_ram = gpu.total_vram().unwrap_or(0);

            let gpu_metrics = performance_monitoring_services.current_gpu_metrics(&gpu)?;
            let supported_metrics = performance_monitoring_services.supported_gpu_metrics(&gpu)?;

            let gpu_usage = if supported_metrics.is_supported_gpu_usage().unwrap_or(false) {
                gpu_metrics.usage().unwrap_or(0.0)
            } else {
                println!("using metrics not supported");
                0.0
            };

            let gpu_used_ram = if supported_metrics.is_supported_gpu_vram().unwrap_or(false) {
                gpu_metrics.vram().unwrap_or(0)
            } else {
                println!("vram metrics not supported");
                0
            };

            let gpu_fan_speed = if supported_metrics
                .is_supported_gpu_fan_speed()
                .unwrap_or(false)
            {
                gpu_metrics.fan_speed().unwrap_or(0)
            } else {
                println!("fan_speed metrics not supported");
                0
            };

            let gpu_temperature = if supported_metrics
                .is_supported_gpu_temperature()
                .unwrap_or(false)
            {
                gpu_metrics.temperature().unwrap_or(0.0)
            } else {
                println!("temperature metrics not supported");
                0.0
            };

            let mut info = SysGpuInfo::default();
            info.id = gpu_id;
            info.brand = gpu_name.to_string();
            info.gpu_utilization = gpu_usage as u32;
            info.mem_total_gb = gpu_ram as f32 * 1.0 / 1024.0;
            info.mem_used_gb = gpu_used_ram as f32 * 1.0 / 1024.0;
            info.fan_speed = gpu_fan_speed as u32;
            info.temperature = gpu_temperature as u32;
            gpus_info.push(info);
        }

        Ok(gpus_info)
    }

    pub fn load_system_info_as_json(&mut self) -> String {
        let info = self.load_system_info();
        serde_json::to_string(&info).unwrap_or("".to_string())
    }

    pub fn load_system_info_as_encrypt_json(&mut self) -> String {
        self.load_system_info_as_json()
    }
}

#[cfg(test)]
mod tests {
    use crate::sys_info_mgr::{SysIpNetwork, SysNetworkInfo};
    use adlx::{gpu::Gpu1, helper::AdlxHelper, interface::Interface, Gpu2};
    use anyhow::Result;
    use sysinfo::{Networks, System};

    #[test]
    pub fn test_cpu_frequency() {
        for i in 0..10 {
            let cpu_frequency_in_mhz = calcmhz::mhz().unwrap();
            println!("{} MHz", cpu_frequency_in_mhz);
            std::thread::sleep(std::time::Duration::from_secs(1));
        }
    }

    #[test]
    pub fn test_amd_gpu_info() {
        let helper = AdlxHelper::new().unwrap();
        let system = helper.system();
        let gpu_list = system.gpus().unwrap();
        let performance_monitoring_services = system.performance_monitoring_services().unwrap();

        for gpu in 0..gpu_list.size() {
            let gpu = gpu_list.at(gpu).unwrap();
            println!("name: {}", gpu.name().unwrap());
            println!("name: {}", gpu.device_id().unwrap());
            println!("name: {}", gpu.driver_path().unwrap());
            println!("name: {}", gpu.asic_family_type().unwrap());
            println!("name: {}", gpu.total_vram().unwrap());

            let gpu_metrics = performance_monitoring_services
                .current_gpu_metrics(&gpu)
                .unwrap();
            let supported_metrics = performance_monitoring_services
                .supported_gpu_metrics(&gpu)
                .unwrap();

            if supported_metrics.is_supported_gpu_usage().unwrap_or(false) {
                dbg!(gpu_metrics.usage().unwrap());
            } else {
                println!("using metrics not supported");
            }

            if supported_metrics.is_supported_gpu_vram().unwrap_or(false) {
                dbg!(gpu_metrics.vram().unwrap());
            } else {
                println!("vram metrics not supported");
            }
            if supported_metrics
                .is_supported_gpu_fan_speed()
                .unwrap_or(false)
            {
                dbg!(gpu_metrics.fan_speed().unwrap());
            } else {
                println!("fan_speed metrics not supported");
            }
            if supported_metrics
                .is_supported_gpu_temperature()
                .unwrap_or(false)
            {
                dbg!(gpu_metrics.temperature().unwrap());
            } else {
                println!("temperature metrics not supported");
            }

            // let gpu1 = gpu.cast::<Gpu1>().unwrap();
            // let gpu2 = gpu.cast::<Gpu2>().unwrap();
            //println!("name: {}", gpu1.name().unwrap());
            //println!("product name: {}", gpu1.product_name().unwrap());
        }
    }

    #[test]
    pub fn test_networks() {
        let system = System::new_all();
        let mut networks = Networks::new_with_refreshed_list();
        networks.refresh(true);

        for (interface_name, data) in networks.iter() {
            if interface_name.contains("VMware") {
                continue;
            }

            println!("interface_name: {}", interface_name);
            println!("data: {:#?}", data);
            println!("networks: {:#?}", data.ip_networks());
            for nt in data.ip_networks() {
                let addr = nt.addr.to_string();
                // ignore IPV6
                if addr.contains(":") || addr.contains("::") {
                    continue;
                }
            }
        }
    }

    #[test]
    pub fn test_networks_2() {
        match netdev::get_default_interface() {
            Ok(interface) => {
                println!("Default Interface:");
                println!("\tIndex: {}", interface.index);
                println!("\tName: {}", interface.name);
                println!("\tFriendly Name: {:?}", interface.friendly_name);
                println!("\tDescription: {:?}", interface.description);
                println!("\tType: {}", interface.if_type.name());
                println!("\tFlags: {:?}", interface.flags);
                println!("\t\tis UP {}", interface.is_up());
                println!("\t\tis LOOPBACK {}", interface.is_loopback());
                println!("\t\tis MULTICAST {}", interface.is_multicast());
                println!("\t\tis BROADCAST {}", interface.is_broadcast());
                println!("\t\tis POINT TO POINT {}", interface.is_point_to_point());
                println!("\t\tis TUN {}", interface.is_tun());
                println!("\t\tis RUNNING {}", interface.is_running());
                println!("\t\tis PHYSICAL {}", interface.is_physical());
                println!("\tOperational state: {:?}", interface.oper_state);
                if let Some(mac_addr) = interface.mac_addr {
                    println!("\tMAC Address: {}", mac_addr);
                } else {
                    println!("\tMAC Address: (Failed to get mac address)");
                }
                println!("\tIPv4: {:?}", interface.ipv4);
                println!("\tIPv6: {:?}", interface.ipv6);
                println!("\tTransmit Speed: {:?}", interface.transmit_speed);
                println!("\tReceive Speed: {:?}", interface.receive_speed);
                println!("\tStats: {:?}", interface.stats);
                if let Some(gateway) = interface.gateway {
                    println!("Default Gateway");
                    println!("\tMAC Address: {}", gateway.mac_addr);
                    println!("\tIPv4: {:?}", gateway.ipv4);
                    println!("\tIPv6: {:?}", gateway.ipv6);
                } else {
                    println!("Default Gateway: (Not found)");
                }
                println!("DNS Servers: {:?}", interface.dns_servers);
                println!("MTU: {:?}", interface.mtu);
                println!("Default: {}", interface.default);
            }
            Err(e) => {
                println!("Error: {}", e);
            }
        }
    }
}
