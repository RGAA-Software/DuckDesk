//! Opt-in transport diagnostics for investigating real endpoint routing failures.
use std::{
    fs::OpenOptions,
    sync::{Mutex, Once},
};

static INITIALIZE: Once = Once::new();

pub(super) fn initialize() {
    INITIALIZE.call_once(|| {
        let Some(path) = std::env::var_os("PIXELS_IROH_TRACE_FILE") else {
            return;
        };
        let Ok(log_file) = OpenOptions::new().create(true).append(true).open(path) else {
            eprintln!("event=iroh.diagnostics outcome=failed reason=log_file_unavailable");
            return;
        };
        let _ = tracing_subscriber::fmt()
            .with_env_filter(
                tracing_subscriber::EnvFilter::try_from_env("PIXELS_IROH_TRACE")
                    .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("iroh=debug,noq=debug")),
            )
            .with_ansi(false)
            .with_writer(Mutex::new(log_file))
            .try_init();
    });
}
