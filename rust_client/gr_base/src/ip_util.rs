use std::net::Ipv4Addr;

/// 获取干净的、真实的IPv4地址列表
///
/// 排除以下网络接口：
/// 1. 虚拟网络接口 (veth, docker, br-, virbr, tun, tap, wg)
/// 2. 虚拟机网络接口 (vmnet, vboxnet, vnic)
/// 3. WSL/WSL2接口 (wsl, wsl2)
/// 4. 容器网络接口
/// 5. VPN接口
pub fn get_clean_ipv4_addresses() -> Result<Vec<Ipv4Addr>, Box<dyn std::error::Error>> {
    use local_ip_address::list_afinet_netifas;

    // 获取所有网络接口和IP地址
    let network_interfaces = list_afinet_netifas()?;

    let mut clean_ips = Vec::new();

    for (interface_name, ip_addr) in network_interfaces {
        // 只处理IPv4地址
        if let std::net::IpAddr::V4(ipv4) = ip_addr {
            // 检查是否为需要排除的虚拟网络接口
            if is_virtual_interface(&interface_name) {
                continue;
            }
            if is_apipa(&ipv4) {
                continue;
            }
            println!("interface is {}, addr : {}", interface_name, ip_addr);
            // 通过所有检查，添加到干净列表
            clean_ips.push(ipv4);
        }
    }

    Ok(clean_ips)
}

fn is_apipa(ip: &std::net::Ipv4Addr) -> bool {
    let octets = ip.octets();
    octets[0] == 169 && octets[1] == 254
}

/// 检查是否为虚拟网络接口
fn is_virtual_interface(interface_name: &str) -> bool {
    let interface_lower = interface_name.to_lowercase();

    // Docker 和容器相关
    if interface_lower.contains("docker")
        || interface_lower.contains("br-")
        || interface_lower.contains("cni")
        || interface_lower.contains("flannel")
        || interface_lower.contains("calico")
        || interface_lower.contains("weave")
        || interface_lower.contains("cilium")
        || interface_lower.contains("container")
        || interface_lower.contains("pod")
        || interface_lower.starts_with("veth")
    {
        //tracing::info!("1 IGNORE: {}", interface_name);
        return true;
    }

    // 虚拟机和虚拟网络
    if interface_lower.contains("vmnet") ||
        interface_lower.contains("vboxnet") ||
        interface_lower.contains("vnic") ||
        interface_lower.contains("virbr") ||
        interface_lower.contains("vmware") ||
        interface_lower.contains("virtual") ||
        interface_lower.contains("hyper-v") ||
        interface_lower.contains("vswitch") ||
        interface_lower.contains("vpn") ||
        interface_lower.contains("tun") ||
        interface_lower.contains("tap") ||
        interface_lower.contains("wg") ||  // WireGuard
        interface_lower.contains("zerotier") ||
        interface_lower.contains("tailscale") ||
        interface_lower.contains("openvpn")
    {
        //tracing::info!("2 IGNORE: {}", interface_name);
        return true;
    }

    // WSL/WSL2
    if interface_lower.contains("wsl") || interface_lower.contains("wsl2") {
        //tracing::info!("3 IGNORE: {}", interface_name);
        return true;
    }

    // 其他虚拟接口
    if interface_lower.contains("lo") ||  // 回环接口
        interface_lower.contains("sit") || // IPv6 over IPv4隧道
        interface_lower.contains("isatap") || // ISATAP隧道
        interface_lower.contains("teredo") || // Teredo隧道
        interface_lower.contains("gif") || // 通用隧道接口
        interface_lower.contains("stf") || // 6to4隧道
        interface_lower.contains("ppp") || // PPP拨号
        interface_lower.contains("slip") || // SLIP
        interface_lower.contains("plip")
    {
        // PLIP
        //tracing::info!("4 IGNORE: {}", interface_name);
        return true;
    }

    false
}

/// 获取首选的真实IPv4地址（通常是最可能用于外网通信的地址）
pub fn get_preferred_real_ipv4() -> Result<Option<Ipv4Addr>, Box<dyn std::error::Error>> {
    let clean_ips = get_clean_ipv4_addresses()?;

    if clean_ips.is_empty() {
        return Ok(None);
    }

    // 优先级排序策略：
    // 1. 首先尝试非私有地址（公网IP）
    // 2. 然后尝试常见的家庭/办公网络私有地址
    // 3. 最后返回其他私有地址

    // 查找公网IP
    for ip in &clean_ips {
        if !is_private_address(ip) {
            return Ok(Some(*ip));
        }
    }

    // 查找常见的家庭/办公网络地址
    for ip in &clean_ips {
        let octets = ip.octets();
        // 优先选择 192.168.1.x，这是最常见的家庭网络
        if octets[0] == 192 && octets[1] == 168 && octets[2] == 1 {
            return Ok(Some(*ip));
        }
        // 然后选择 192.168.0.x
        if octets[0] == 192 && octets[1] == 168 && octets[2] == 0 {
            return Ok(Some(*ip));
        }
        // 然后选择 10.0.0.x
        if octets[0] == 10 && octets[1] == 0 && octets[2] == 0 {
            return Ok(Some(*ip));
        }
    }

    // 返回第一个可用的IP
    Ok(clean_ips.first().copied())
}

/// 判断是否为私有地址
fn is_private_address(ip: &Ipv4Addr) -> bool {
    let octets = ip.octets();

    // RFC 1918 私有地址范围
    // 10.0.0.0/8
    if octets[0] == 10 {
        return true;
    }

    // 172.16.0.0/12
    if octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31 {
        return true;
    }

    // 192.168.0.0/16
    if octets[0] == 192 && octets[1] == 168 {
        return true;
    }

    false
}

/// 获取IPv4地址的详细信息
pub fn get_ipv4_details(ip: Ipv4Addr) -> IPDetails {
    let octets = ip.octets();

    // 判断IP类型
    let ip_type = if is_private_address(&ip) {
        IPType::Private
    } else if octets[0] == 127 {
        IPType::Loopback
    } else if octets[0] == 169 && octets[1] == 254 {
        IPType::LinkLocal
    } else if octets[0] >= 224 && octets[0] <= 239 {
        IPType::Multicast
    } else if octets[0] >= 240 {
        IPType::Reserved
    } else {
        IPType::Public
    };

    // 判断可能的用途
    let possible_use = if octets[0] == 192 && octets[1] == 168 && octets[2] == 1 {
        "家庭/小型办公网络".to_string()
    } else if octets[0] == 10 {
        "企业网络".to_string()
    } else if !is_private_address(&ip) {
        "公网地址".to_string()
    } else {
        "私有网络".to_string()
    };

    IPDetails {
        ip,
        ip_type,
        possible_use,
    }
}

/// IP地址类型
#[derive(Debug, Clone, PartialEq)]
pub enum IPType {
    Public,
    Private,
    Loopback,
    LinkLocal,
    Multicast,
    Reserved,
    Unknown,
}

/// IP地址详细信息
#[derive(Debug, Clone)]
pub struct IPDetails {
    pub ip: Ipv4Addr,
    pub ip_type: IPType,
    pub possible_use: String,
}

/// 网络接口信息
#[derive(Debug, Clone)]
pub struct NetworkInterfaceInfo {
    pub name: String,
    pub ipv4: Option<Ipv4Addr>,
    pub is_virtual: bool,
    pub is_up: bool,
}

/// 获取网络接口的详细信息
pub fn get_network_interfaces_info() -> Result<Vec<NetworkInterfaceInfo>, Box<dyn std::error::Error>>
{
    use local_ip_address::list_afinet_netifas;

    let network_interfaces = list_afinet_netifas()?;
    let mut interfaces_info = Vec::new();

    for (name, ip_addr) in network_interfaces {
        let is_virtual = is_virtual_interface(&name);
        let ipv4 = if let std::net::IpAddr::V4(ipv4) = ip_addr {
            Some(ipv4)
        } else {
            None
        };

        // 这里假设接口是活跃的（简化处理）
        // 实际应用中可能需要更复杂的检查
        let is_up = true;

        interfaces_info.push(NetworkInterfaceInfo {
            name,
            ipv4,
            is_virtual,
            is_up,
        });
    }

    Ok(interfaces_info)
}

/// 获取所有网络接口的IPv4地址（不进行过滤）
pub fn get_all_ipv4_addresses_raw() -> Result<Vec<(String, Ipv4Addr)>, Box<dyn std::error::Error>> {
    use local_ip_address::list_afinet_netifas;

    let network_interfaces = list_afinet_netifas()?;
    let mut result = Vec::new();

    for (interface_name, ip_addr) in network_interfaces {
        if let std::net::IpAddr::V4(ipv4) = ip_addr {
            result.push((interface_name, ipv4));
        }
    }

    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::net::Ipv4Addr;

    #[test]
    fn test_is_virtual_interface() {
        // 测试虚拟接口
        assert!(is_virtual_interface("docker0"));
        assert!(is_virtual_interface("br-12345"));
        assert!(is_virtual_interface("vethabc123"));
        assert!(is_virtual_interface("vmnet1"));
        assert!(is_virtual_interface("vboxnet0"));
        assert!(is_virtual_interface("virbr0"));
        assert!(is_virtual_interface("tun0"));
        assert!(is_virtual_interface("tap0"));
        assert!(is_virtual_interface("wg0"));

        // 测试真实接口（应该返回false）
        assert!(!is_virtual_interface("eth0"));
        assert!(!is_virtual_interface("en0"));
        assert!(!is_virtual_interface("wlan0"));
        assert!(!is_virtual_interface("Wi-Fi"));
        assert!(!is_virtual_interface("Ethernet"));
    }

    #[test]
    fn test_is_private_address() {
        // RFC 1918 私有地址
        assert!(is_private_address(&Ipv4Addr::new(10, 0, 0, 1)));
        assert!(is_private_address(&Ipv4Addr::new(172, 16, 0, 1)));
        assert!(is_private_address(&Ipv4Addr::new(172, 31, 255, 254)));
        assert!(is_private_address(&Ipv4Addr::new(192, 168, 1, 1)));

        // 公网地址
        assert!(!is_private_address(&Ipv4Addr::new(8, 8, 8, 8)));
        assert!(!is_private_address(&Ipv4Addr::new(1, 1, 1, 1)));
        assert!(!is_private_address(&Ipv4Addr::new(203, 0, 113, 1)));
    }

    #[test]
    fn test_get_ipv4_details() {
        let ip = Ipv4Addr::new(192, 168, 1, 100);
        let details = get_ipv4_details(ip);

        assert_eq!(details.ip, ip);
        assert_eq!(details.ip_type, IPType::Private);
        assert_eq!(details.possible_use, "家庭/小型办公网络");

        let ip = Ipv4Addr::new(8, 8, 8, 8);
        let details = get_ipv4_details(ip);
        assert_eq!(details.ip_type, IPType::Public);
    }

    #[test]
    fn test_get_clean_ipv4_addresses() {
        // 注意：这个测试在实际运行时会返回实际系统的IP地址
        // 所以不能断言具体结果，只能检查是否没有panic
        let result = get_clean_ipv4_addresses();
        match result {
            Ok(ips) => {
                println!("找到 {} 个干净IP地址:", ips.len());
                for ip in &ips {
                    println!("  {}", ip);
                    let details = get_ipv4_details(*ip);
                    println!(
                        "    类型: {:?}, 可能用途: {}",
                        details.ip_type, details.possible_use
                    );
                }
            }
            Err(e) => {
                // 在某些环境下可能无法获取网络接口信息
                println!("无法获取网络接口信息: {}", e);
            }
        }
    }
}

/// 使用示例
pub fn test_ip_main() -> Result<(), Box<dyn std::error::Error>> {
    println!("=== 获取干净的真实IPv4地址 ===\n");

    // 1. 获取所有原始IP地址
    println!("所有网络接口的IPv4地址:");
    let all_ips = get_all_ipv4_addresses_raw()?;
    for (iface, ip) in &all_ips {
        let virtual_marker = if is_virtual_interface(iface) {
            " [虚拟]"
        } else {
            ""
        };
        println!("  {}: {}{}", iface, ip, virtual_marker);
    }

    // 2. 获取干净的IP地址
    println!("\n干净的IPv4地址列表（排除虚拟网络）:");
    let clean_ips = get_clean_ipv4_addresses()?;

    if clean_ips.is_empty() {
        println!("  未找到干净的IPv4地址");
    } else {
        for (i, ip) in clean_ips.iter().enumerate() {
            let details = get_ipv4_details(*ip);
            println!(
                "  {}. {} - {:?} ({})",
                i + 1,
                ip,
                details.ip_type,
                details.possible_use
            );
        }
    }

    // 3. 获取首选IP地址
    println!("\n首选的真实IPv4地址:");
    if let Some(preferred_ip) = get_preferred_real_ipv4()? {
        let details = get_ipv4_details(preferred_ip);
        println!(
            "  {} - {:?} ({})",
            preferred_ip, details.ip_type, details.possible_use
        );

        // 显示更多信息
        println!("\n详细信息:");
        println!("  IP地址: {}", preferred_ip);
        println!("  类型: {:?}", details.ip_type);
        println!("  可能用途: {}", details.possible_use);
        println!("  是否为私有地址: {}", is_private_address(&preferred_ip));
    } else {
        println!("  未找到可用的真实IPv4地址");
    }

    // 4. 获取网络接口信息
    println!("\n网络接口详细信息:");
    let interfaces_info = get_network_interfaces_info()?;
    for info in interfaces_info {
        let status = if info.is_up { "活跃" } else { "未激活" };
        let virtual_marker = if info.is_virtual { " [虚拟]" } else { "" };
        if let Some(ip) = info.ipv4 {
            println!("  {}: {} ({}{})", info.name, ip, status, virtual_marker);
        } else {
            println!("  {}: 无IPv4地址 ({}{})", info.name, status, virtual_marker);
        }
    }

    // 5. 统计信息
    println!("\n=== 统计信息 ===");
    println!("总网络接口数: {}", all_ips.len());
    println!("干净IPv4地址数: {}", clean_ips.len());
    println!(
        "虚拟接口数: {}",
        all_ips
            .iter()
            .filter(|(iface, _)| is_virtual_interface(iface))
            .count()
    );
    println!(
        "真实接口数: {}",
        all_ips
            .iter()
            .filter(|(iface, _)| !is_virtual_interface(iface))
            .count()
    );

    Ok(())
}
