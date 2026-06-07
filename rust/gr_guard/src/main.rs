#![cfg_attr(not(test), windows_subsystem = "windows")]

mod app;
mod config;
mod hidden_window;
mod logging;
mod panel_client;
mod process_lister;
mod process_monitor;
mod process_spawn;
mod runtime;
mod single_instance;
mod task_scheduler;

fn main() {
    match app::GuardApp::bootstrap().and_then(|mut app| app.run_message_loop()) {
        Ok(code) => std::process::exit(code),
        Err(err) => {
            eprintln!("GammaRayGuard failed: {err}");
            std::process::exit(1);
        }
    }
}
