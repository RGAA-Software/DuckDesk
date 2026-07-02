use std::path::PathBuf;

pub const DEFAULT_SERVICE_NAME: &str = "GammaRayService";
pub const DEFAULT_SERVICE_DISPLAY_NAME: &str = "GammaRayService";
pub const DEFAULT_SERVICE_DESCRIPTION: &str = "** GammaRay Service **";
pub const DEFAULT_SERVICE_PATH: &str = "/service/message";
pub const DEFAULT_LISTEN_PORT: u16 = 20375;
pub const DEFAULT_LISTEN_HOST: &str = "0.0.0.0";
pub const DEFAULT_CLIENT_HOST: &str = "127.0.0.1";
pub const SERVICE_DATA_FILE: &str = "godesk_service.json";
pub const SERVICE_LOG_DIR: &str = "gr_logs";
pub const SERVICE_LOG_FILE: &str = "godesk_service.log";
pub const SERVICE_DATA_DIR: &str = "gr_data";
pub const RENDER_EXE_NAME: &str = "GammaRayRender.exe";
pub const GUARD_EXE_NAME: &str = "GammaRayGuard.exe";
pub const CLIENT_INNER_EXE_NAME: &str = "GammaRayClientInner.exe";
pub const SYSINFO_EXE_NAME: &str = "GammaRaySysInfo.exe";
pub const USER_PROXY_EXE_NAME: &str = "GammaRayUserProxy.exe";
pub const PANEL_EXE_NAME: &str = "GammaRay.exe";

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ServiceConfig {
    pub service_name: String,
    pub display_name: String,
    pub description: String,
    pub listen_host: String,
    pub listen_port: u16,
    pub ws_path: String,
    pub data_root: PathBuf,
    pub log_root: PathBuf,
}

impl ServiceConfig {
    pub fn new(listen_port: u16, data_root: PathBuf, log_root: PathBuf) -> Self {
        Self {
            service_name: DEFAULT_SERVICE_NAME.to_string(),
            display_name: DEFAULT_SERVICE_DISPLAY_NAME.to_string(),
            description: DEFAULT_SERVICE_DESCRIPTION.to_string(),
            listen_host: DEFAULT_LISTEN_HOST.to_string(),
            listen_port,
            ws_path: DEFAULT_SERVICE_PATH.to_string(),
            data_root,
            log_root,
        }
    }

    pub fn storage_file(&self) -> PathBuf {
        self.data_root.join(SERVICE_DATA_FILE)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn storage_file_uses_data_root() {
        let config = ServiceConfig::new(20375, PathBuf::from("data"), PathBuf::from("logs"));
        assert_eq!(
            config.storage_file(),
            PathBuf::from("data").join(SERVICE_DATA_FILE)
        );
        assert_eq!(config.ws_path, DEFAULT_SERVICE_PATH);
    }
}
