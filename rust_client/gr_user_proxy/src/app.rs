use std::sync::Arc;
use std::sync::mpsc::Receiver;

use tracing::{error, info};

use crate::clipboard::{spawn_win_clipboard_listener, ClipboardService};
use crate::config::{CliArgs, UserProxyConfig, USER_PROXY_LOCK_NAME};
use crate::engine::UserProxyEngine;
use crate::logging::init_user_proxy_logging;
use crate::render_client::RenderClient;
use crate::single_instance::SingleInstanceGuard;

pub struct UserProxyApp {
    _log_guard: gr_base::log_util::LogGuard,
    _instance: SingleInstanceGuard,
    engine: Arc<UserProxyEngine>,
    clip_rx: Receiver<()>,
}

impl UserProxyApp {
    pub fn bootstrap() -> anyhow::Result<Self> {
        let args = CliArgs::parse();
        let config = UserProxyConfig::from(args);
        let log_guard = init_user_proxy_logging();
        let instance = SingleInstanceGuard::acquire(USER_PROXY_LOCK_NAME)
            .map_err(|err| anyhow::anyhow!(err))?;
        info!(
            "GammaRayUserProxy starting, render_url={}",
            config.render_ws_url()
        );

        let (win_backend, clip_rx) = spawn_win_clipboard_listener()?;
        let clipboard = Arc::new(ClipboardService::new(Arc::new(win_backend)));
        let render_client = RenderClient::new(config);
        let engine = Arc::new(UserProxyEngine::new(clipboard, render_client));

        Ok(Self {
            _log_guard: log_guard,
            _instance: instance,
            engine,
            clip_rx,
        })
    }

    pub async fn run(self) -> anyhow::Result<()> {
        self.engine.start_render_loop();
        loop {
            match self.clip_rx.recv() {
                Ok(()) => self.engine.on_local_clipboard_update().await,
                Err(_) => {
                    error!("clipboard channel closed");
                    break;
                }
            }
        }
        Ok(())
    }
}

use clap::Parser;
