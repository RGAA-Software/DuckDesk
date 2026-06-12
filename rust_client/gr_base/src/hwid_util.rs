use crate::md5_hex;
use sysinfo::{Disks, System};

pub struct HardwareIdUtil {}

impl HardwareIdUtil {
    pub fn generate_hardware_id() -> String {
        let mut system = System::new_all();
        system.refresh_all();
        let mut disks = Disks::new_with_refreshed_list();
        disks.refresh(true);

        let vendor = system.cpus()[0].vendor_id();
        let brand = system.cpus()[0].brand();
        let cpu_size = system.cpus().len();

        let mac_addr = match netdev::get_default_interface() {
            Ok(interface) => {
                if let Some(addr) = interface.mac_addr {
                    addr.to_string()
                } else {
                    "".to_string()
                }
            }
            Err(_) => "".to_string(),
        };

        let mut disk_mt = "".to_string();
        for disk in &mut disks {
            let mt = disk
                .mount_point()
                .to_string_lossy()
                .to_string()
                .to_lowercase();
            if mt.starts_with("c:") {
                disk_mt = mt + format!("{}", disk.total_space() / 1024 / 1024 / 1024).as_str();
            }
        }

        let info = format!("{}-{}-{}-{}-{}", vendor, brand, cpu_size, mac_addr, disk_mt);
        println!("mac: {}", info);
        md5_hex(&info)
    }
}

#[cfg(test)]
mod tests {
    use crate::hwid_util::HardwareIdUtil;

    #[test]
    fn test_hardware_id() {
        let id = HardwareIdUtil::generate_hardware_id();
        println!("id: {}", id);
    }
}
