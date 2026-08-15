use std::sync::mpsc::Receiver;
use std::sync::Arc;

use tracing::{error, info};

use crate::clipboard::virtual_file::VirtualFileCoordinator;
use crate::clipboard::{spawn_win_clipboard_listener, ClipboardService};
use crate::config::{CliArgs, UserProxyConfig, USER_PROXY_LOCK_NAME};
use crate::engine::UserProxyEngine;
use crate::logging::init_user_proxy_logging;
use crate::render_client::RenderClient;
use crate::single_instance::SingleInstanceGuard;

pub struct UserProxyApp {
    _log_guard: px_base::log_util::LogGuard,
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

        let virtual_files = VirtualFileCoordinator::new();
        let (outbound_tx, outbound_rx) = std::sync::mpsc::channel();
        virtual_files.set_outbound_sender(outbound_tx);

        let (win_backend, clip_rx) = spawn_win_clipboard_listener()?;
        let clipboard = Arc::new(ClipboardService::with_virtual_files(
            Arc::new(win_backend),
            virtual_files,
        ));
        let render_client = RenderClient::new(config);
        spawn_virtual_file_outbound_forwarder(render_client.clone(), outbound_rx);
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
        match std::env::current_exe()
            .ok()
            .and_then(|path| path.parent().map(std::path::PathBuf::from))
        {
            Some(app_dir) => {
                crate::keepalive::spawn_keepalive_loop(app_dir);
            }
            None => {
                error!("current_exe has no parent, keepalive loop disabled");
            }
        }
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

fn spawn_virtual_file_outbound_forwarder(
    client: Arc<RenderClient>,
    outbound_rx: std::sync::mpsc::Receiver<Vec<u8>>,
) {
    std::thread::Builder::new()
        .name("gr_user_proxy_vf_out".into())
        .spawn(move || {
            while let Ok(bytes) = outbound_rx.recv() {
                let client = client.clone();
                if let Err(err) = client.blocking_send_bytes(bytes) {
                    error!("virtual file outbound send failed: {err:#}");
                }
            }
        })
        .expect("spawn virtual file outbound forwarder");
}

use clap::Parser;
