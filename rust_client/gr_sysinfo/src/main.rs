use gr_base::log_util;
use gr_base::path_util::default_log_root;
use gr_sysinfo::{gSysInfoMgr, gSysPanelClient};
use clap::Parser as ClapParser;
use clap_derive::Parser;

#[cfg(feature = "dhat-heap")]
#[global_allocator]
static ALLOC: dhat::Alloc = dhat::Alloc;

#[derive(Parser)]
#[command(name = "myapp", version, about, long_about = None)]
struct Cli {
    #[arg(long)]
    print: Option<bool>,

    #[arg(short, long)]
    duration: Option<i32>,

    #[arg(short, long)]
    port: Option<i32>,

    /// Run for the specified number of seconds and then exit (useful for heap profiling).
    #[arg(long)]
    exit_after: Option<u64>,
}

#[tokio::main]
async fn main() {
    #[cfg(feature = "dhat-heap")]
    let _profiler = dhat::Profiler::new_heap();

    let args = Cli::parse();
    let port = args.port.unwrap_or(20369);

    let log_root = default_log_root();
    let _guard = log_util::init_log(
        log_root.to_string_lossy().to_string(),
        "gr_sys_info".to_string(),
    );

    gSysPanelClient.lock().await.duration = args.duration.unwrap_or(1);

    tokio::spawn(async move {
        gSysPanelClient
            .lock()
            .await
            .connect(format!("ws://127.0.0.1:{}/sys/info", port))
            .await;
    });

    if let Some(secs) = args.exit_after {
        tokio::time::sleep(tokio::time::Duration::from_secs(secs)).await;
        tracing::info!("exit-after {}s reached, shutting down for heap profiling", secs);
        return;
    }

    loop {
        if args.print.unwrap_or(false) {
            let sys_info = gSysInfoMgr.lock().await.load_system_info();
            tracing::info!("info: {:#?}", sys_info);
        }
        tokio::time::sleep(tokio::time::Duration::from_secs(5)).await;
    }
}
