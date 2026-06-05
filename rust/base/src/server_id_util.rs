use disk_serial_number::get_all_disks;
use raw_cpuid::CpuId;
use crate::hash_util;
use crate::hash_util::HashAlgo::MD5;
use serde::{Deserialize, Serialize};
use crate::hash_util::compute_hash;

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct CpuInfo {
    pub brand: String,
    pub family: u8,
    pub model: u8,
    pub stepping: u8,
}

impl CpuInfo {
    pub fn as_id(&self) -> String {
        let value = format!("{}-{}-{}-{}", self.brand, self.family, self.model, self.stepping);
        tracing::info!("CpuInfo: {}", value);
        compute_hash(MD5, value.as_bytes())
    }
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct HardDiskInfo {
    pub name: String,
    pub model: String,
    pub serial_number: String,
}

impl HardDiskInfo {
    pub fn as_id(&self) -> String {
        let value = format!("{}-{}-{}", self.name, self.model, self.serial_number);
        tracing::info!("HardDiskInfo: {}", value);
        hash_util::compute_hash(MD5, value.as_bytes())
    }
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SystemInfo {
    pub cpu_info: CpuInfo,
    pub hard_disks_info: Vec<HardDiskInfo>,
}

impl SystemInfo {
    pub fn new() -> Self {
        let mut info = Self::default();
        let cpuid = CpuId::new();
        let brand = if let Some(brand) = cpuid.get_processor_brand_string() {
            brand.as_str().to_string()
        }
        else {
            "".to_string()
        };

        let mut cpu_info = CpuInfo::default();
        cpu_info.brand = brand;

        if let Some(fi) = cpuid.get_feature_info() {
            cpu_info.family = fi.family_id();
            cpu_info.model = fi.model_id();
            cpu_info.stepping = fi.stepping_id();
        }

        let disks = get_all_disks();
        let mut disks_info = Vec::new();
        match disks {
            Ok(disk_list) => {
                for disk in disk_list {
                    let mut disk_info = HardDiskInfo::default();
                    disk_info.name = disk.name.clone();
                    if let Some(model) = &disk.model {
                        disk_info.model = model.clone();
                    }
                    if let Some(serial) = &disk.serial_number {
                        disk_info.serial_number = serial.clone();
                    }
                    disks_info.push(disk_info);
                }
            }
            Err(e) => {
                eprintln!("Error retrieving disk information: {}", e);
            }
        }

        info.cpu_info = cpu_info;
        info.hard_disks_info = disks_info;
        info
    }

    pub fn gen_unique_id(&self) -> String {
        // cpuid
        let cpuid = self.cpu_info.as_id();

        // disks id
        let mut hds_id = Vec::new();
        self.hard_disks_info.iter().for_each(|hd| {
            hds_id.push(hd.as_id());
        });
        hds_id.sort();

        let mut disk_id = String::new();
        hds_id.iter().for_each(|hd| {
            disk_id += hd;
        });

        compute_hash(MD5, format!("{}{}", cpuid, disk_id).as_bytes())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_system_info() {
        let sysinfo = SystemInfo::new();
        let id = sysinfo.gen_unique_id();
        println!("gr_sysinfo: {:#?}", sysinfo);
        println!("id: {}", id)
    }

}
