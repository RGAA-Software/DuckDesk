use std::path::{Path, PathBuf};
use std::time::Duration;

use clap::Parser;

const DEFAULT_APP_NAME: &str = "GoDesk";

pub const USER_PROXY_LOG_DIR: &str = "gr_logs";
pub const USER_PROXY_LOG_FILE: &str = "godesk_user_proxy.log";
pub const USER_PROXY_LOCK_NAME: &str = "GammaRayUserProxy.Singleton";
pub const DEFAULT_RENDER_HOST: &str = "127.0.0.1";
pub const DEFAULT_RENDER_PORT: u16 = 20371;
pub const DEFAULT_WS_PATH: &str = "/user-proxy";
pub const RECONNECT_SECS: u64 = 2;
pub const PANEL_EXE_NAME: &str = "GammaRay.exe";
pub const SYSINFO_EXE_NAME: &str = "GammaRaySysInfo.exe";
pub const PANEL_TASK_NAME: &str = "GammaRay_Panel_Start";
pub const KEEPALIVE_POLL_INTERVAL: Duration = Duration::from_secs(5);

#[derive(Parser, Debug, Clone, PartialEq, Eq)]
#[command(name = "GammaRayUserProxy")]
pub struct CliArgs {
    #[arg(long, default_value = DEFAULT_RENDER_HOST)]
    pub render_host: String,
    #[arg(long, default_value_t = DEFAULT_RENDER_PORT)]
    pub render_port: u16,
    #[arg(long, default_value = DEFAULT_WS_PATH)]
    pub path: String,
    #[arg(long, default_value_t = RECONNECT_SECS)]
    pub reconnect_secs: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UserProxyConfig {
    pub render_host: String,
    pub render_port: u16,
    pub ws_path: String,
    pub reconnect_secs: u64,
}

impl Default for UserProxyConfig {
    fn default() -> Self {
        Self {
            render_host: DEFAULT_RENDER_HOST.to_string(),
            render_port: DEFAULT_RENDER_PORT,
            ws_path: DEFAULT_WS_PATH.to_string(),
            reconnect_secs: RECONNECT_SECS,
        }
    }
}

impl From<CliArgs> for UserProxyConfig {
    fn from(args: CliArgs) -> Self {
        Self {
            render_host: args.render_host,
            render_port: args.render_port,
            ws_path: args.path,
            reconnect_secs: args.reconnect_secs,
        }
    }
}

impl UserProxyConfig {
    pub fn render_ws_url(&self) -> String {
        format!(
            "ws://{}:{}{}",
            self.render_host, self.render_port, self.ws_path
        )
    }

    pub fn reconnect_duration(&self) -> Duration {
        Duration::from_secs(self.reconnect_secs)
    }

    pub fn with_render_port(mut self, port: u16) -> Self {
        self.render_port = port;
        self
    }

    pub fn with_ws_path(mut self, path: impl Into<String>) -> Self {
        self.ws_path = path.into();
        self
    }
}

pub fn public_share_dir() -> PathBuf {
    match std::env::var_os("PUBLIC") {
        Some(value) => PathBuf::from(value),
        None => PathBuf::from("C:/Users/Public"),
    }
}

pub fn app_shared_root() -> PathBuf {
    public_share_dir().join(DEFAULT_APP_NAME)
}

pub fn user_proxy_log_root() -> PathBuf {
    app_shared_root().join(USER_PROXY_LOG_DIR)
}

pub fn user_proxy_log_file() -> PathBuf {
    user_proxy_log_root().join(USER_PROXY_LOG_FILE)
}

pub fn sibling_exe_path(base_dir: &Path, exe_name: &str) -> PathBuf {
    base_dir.join(exe_name)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shared_root_extends_public_dir() {
        assert_eq!(app_shared_root(), public_share_dir().join("GoDesk"));
    }

    #[test]
    fn user_proxy_log_root_path() {
        assert_eq!(
            user_proxy_log_root(),
            app_shared_root().join("gr_logs")
        );
    }

    #[test]
    fn user_proxy_log_file_path() {
        assert_eq!(
            user_proxy_log_file(),
            app_shared_root()
                .join("gr_logs")
                .join("godesk_user_proxy.log")
        );
    }

    #[test]
    fn default_render_port() {
        assert_eq!(UserProxyConfig::default().render_port, 20371);
    }

    #[test]
    fn default_reconnect_secs() {
        assert_eq!(UserProxyConfig::default().reconnect_secs, 2);
        assert_eq!(RECONNECT_SECS, 2);
    }

    #[test]
    fn render_ws_url_build() {
        let cfg = UserProxyConfig::default();
        assert_eq!(cfg.render_ws_url(), "ws://127.0.0.1:20371/user-proxy");
    }

    #[test]
    fn render_ws_url_custom_host() {
        let cfg = UserProxyConfig {
            render_host: "10.0.0.5".to_string(),
            ..UserProxyConfig::default()
        };
        assert_eq!(cfg.render_ws_url(), "ws://10.0.0.5:20371/user-proxy");
    }

    #[test]
    fn singleton_lock_name() {
        assert_eq!(USER_PROXY_LOCK_NAME, "GammaRayUserProxy.Singleton");
    }

    #[test]
    fn cli_parse_defaults() {
        let args = CliArgs::parse_from(["GammaRayUserProxy"]);
        assert_eq!(args.render_host, DEFAULT_RENDER_HOST);
        assert_eq!(args.render_port, DEFAULT_RENDER_PORT);
        assert_eq!(args.path, DEFAULT_WS_PATH);
        assert_eq!(args.reconnect_secs, RECONNECT_SECS);
    }

    #[test]
    fn cli_parse_overrides() {
        let args = CliArgs::parse_from([
            "GammaRayUserProxy",
            "--render-port",
            "30000",
            "--reconnect-secs",
            "5",
        ]);
        assert_eq!(args.render_port, 30000);
        assert_eq!(args.reconnect_secs, 5);
    }
}
