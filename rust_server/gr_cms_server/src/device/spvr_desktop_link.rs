use serde::Deserialize;

#[derive(Debug, Deserialize)]
pub struct DesktopLinkRaw {
    pub did: String,      // device id
    pub dn: String,       // device name
    pub iidx: i32,        // index
    pub ips: Vec<IpItem>, // ip list

    pub ppt: i32,
    pub rdpt: i32,
    pub rlak: String,
    pub rlpt: i32,
    pub rlst: String,
    pub rpwd: String,
}

#[derive(Debug, Deserialize)]
pub struct IpItem {
    pub ip: String,
}

impl DesktopLinkRaw {
    pub fn from(m: &str) -> serde_json::Result<Self> {
        let r: DesktopLinkRaw = serde_json::from_str(m)?;
        Ok(r)
    }
}