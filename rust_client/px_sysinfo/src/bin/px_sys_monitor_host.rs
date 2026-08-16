#![cfg_attr(not(test), windows_subsystem = "windows")]

use clap::Parser;

fn main() {
    let _guard = px_sysinfo::single_instance::ensure_single_instance("PxSysMonitorHost_SingleInstance");
    if _guard.is_none() {
        return;
    }

    let cli = px_sysinfo::monitor_host_app::HostCli::parse();
    px_sysinfo::monitor_host_app::run(cli);
}
