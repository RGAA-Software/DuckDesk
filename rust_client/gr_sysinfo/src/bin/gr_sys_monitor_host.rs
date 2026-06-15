#![cfg_attr(not(test), windows_subsystem = "windows")]

use clap::Parser;

fn main() {
    let _guard = gr_sysinfo::single_instance::ensure_single_instance("GrSysMonitorHost_SingleInstance");
    if _guard.is_none() {
        return;
    }

    let cli = gr_sysinfo::monitor_host_app::HostCli::parse();
    gr_sysinfo::monitor_host_app::run(cli);
}
