#![cfg_attr(not(test), windows_subsystem = "windows")]

use clap::Parser;

#[derive(Parser, Debug, Clone)]
struct MonitorCli {
    #[arg(long, default_value_t = false)]
    startup: bool,
}

fn main() {
    let cli = MonitorCli::parse();
    gr_sysinfo::monitor_app::run(cli.startup);
}
