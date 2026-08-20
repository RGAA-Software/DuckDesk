use std::sync::Arc;

use tracing::{error, info};

use crate::clipboard::ClipboardService;
use crate::proto::build_clipboard_files_event;
use crate::render_client::{handle_inbound_rp, RenderClient};

pub struct UserProxyEngine {
    pub clipboard: Arc<ClipboardService>,
    pub render_client: Arc<RenderClient>,
}

impl UserProxyEngine {
    pub fn new(clipboard: Arc<ClipboardService>, render_client: Arc<RenderClient>) -> Self {
        Self {
            clipboard,
            render_client,
        }
    }

    pub fn start_render_loop(self: &Arc<Self>) {
        let engine = Arc::clone(self);
        self.render_client
            .clone()
            .spawn_reconnect_loop(move |bytes| {
                handle_inbound_rp(&bytes, &engine.clipboard, engine.render_client.clone());
            });
    }

    pub async fn on_local_clipboard_update(&self) {
        if !self.render_client.is_connected() {
            info!("skip local clipboard: render not connected");
            return;
        }
        if self.clipboard.echo.is_outbound_suppressed() {
            info!("suppressed outbound");
            return;
        }

        let content = match self.clipboard.read_local_content() {
            Ok(content) => content,
            Err(err) => {
                error!("read local clipboard failed: {err:#}");
                return;
            }
        };

        if content.is_empty() {
            info!("no syncable text");
            return;
        }

        if content.has_files() {
            if self.should_skip_files_outbound(&content.files) {
                info!("echo skip outbound files");
                return;
            }
            info!(
                "===> new Files: count={}, names={:?}",
                content.files.len(),
                content
                    .files
                    .iter()
                    .map(|file| file.file_name.as_str())
                    .collect::<Vec<_>>()
            );
            if let Err(err) = self
                .render_client
                .send_bytes(build_clipboard_files_event(&content.files))
                .await
            {
                error!("send clipboard files event failed: {err:#}");
            }
            return;
        }

        let text = match content.text {
            Some(text) if !text.trim().is_empty() => text,
            _ => {
                info!("no syncable text");
                return;
            }
        };

        if self.clipboard.echo.should_skip_outbound(&text) {
            info!("echo skip outbound");
            return;
        }
        info!("===> new Text: {}", text);
        if let Err(err) = self.render_client.send_clipboard_text(&text).await {
            error!("send clipboard event failed: {err:#}");
        }
    }

    fn should_skip_files_outbound(
        &self,
        files: &[crate::clipboard::content::ClipboardFileEntry],
    ) -> bool {
        let signature = crate::clipboard::content::files_signature(files);
        self.clipboard.echo.should_skip_outbound(&signature)
    }
}
