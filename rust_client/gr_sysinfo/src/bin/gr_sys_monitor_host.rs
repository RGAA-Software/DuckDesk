#![cfg_attr(not(test), windows_subsystem = "windows")]

use clap::Parser;

fn main() {
    let cli = gr_sysinfo::monitor_host_app::HostCli::parse();
    gr_sysinfo::monitor_host_app::run(cli);
}
