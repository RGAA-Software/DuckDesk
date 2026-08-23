#![windows_subsystem = "windows"]

mod console_api_error;
mod console_context;
mod console_defs;
mod device;
mod filter;
// mod console_grpc_relay_client;
// mod console_grpc_relay_client_mgr;
// mod console_grpc_ws_client_trait;
mod console_handler;
mod console_server;
mod console_settings;

mod app_schedule;
mod auth;
mod console_database;
mod console_http_util;
mod console_relay;
mod console_router;
mod config;
mod connection_ticket;
mod event;
mod identity;
mod interact;
mod live;
mod media_sidecar;
mod net_client;
mod net_cm;
mod net_panel;
mod net_service;
mod record;
mod rtc;
mod stream;
mod system;
mod test_reset;
mod update;
mod user;
mod user_device;
mod wall;

use crate::auth::console_auth_license_keys::init_license_verifier;
use crate::auth::console_auth_manager::AuthManager;
use crate::console_context::ConsoleContext;
use crate::console_database::ConsoleDatabase;
use crate::console_relay::relay_conn_mgr::RelayConnManager;
use crate::console_relay::relay_redis_conn::RelayRedisConn;
use crate::console_relay::relay_room_mgr::RelayRoomManager;
use crate::console_relay::relay_server::RelayServer;
use crate::console_server::ConsoleServer;
use crate::console_settings::ConsoleSettings;
use crate::device::console_device_manager::ConsoleDeviceManager;
use crate::device::console_id_generator::PrIdGenerator;
use crate::event::console_event_manager::ConsoleEventManager;
use crate::interact::console_lang::ConsoleLanguage;
use crate::interact::console_panel;
use crate::net_client::console_client_conn_mgr::ConsoleClientConnManager;
use crate::net_cm::console_cm_mgr::ConsoleCMManager;
use crate::net_panel::console_panel_conn_mgr::ConsolePanelConnManager;
use crate::net_service::console_service_conn_mgr::ConsoleServiceConnManager;
use crate::record::console_file_transfer_manager::ConsoleFileTransferManager;
use crate::record::console_render_record_manager::ConsoleRenderRecordManager;
use crate::record::console_visit_manager::ConsoleVisitManager;
use crate::record::record_tunnel::RecordTunnelManager;
use crate::rtc::manager::RtcConfigManager;
use crate::stream::console_stream_manager::ConsoleStreamManager;
use crate::system::console_system_manager::ConsoleSystemManager;
use crate::update::update_info_manager::UpdateInfoManager;
use crate::user::console_user_manager::ConsoleUserManager;
use crate::user::session::ConsoleUserSessionManager;
use crate::user_device::console_user_device_manager::ConsoleUserDeviceManager;
use clap::Parser;
use px_auth_mgr::auth_license::LicenseVerifier;
use px_base::{kv_storage::KvStorage, log_util, redis_util};
use redis::aio::ConnectionManager;
use std::sync::Arc;
use sys_locale::get_locale;
use tokio::sync::Mutex;

lazy_static::lazy_static! {
    // Console
    pub static ref gConsoleSettings: Arc<Mutex<ConsoleSettings>> = Arc::new(Mutex::new(ConsoleSettings::new()));

    pub static ref gConsoleClientConnMgr: Arc<ConsoleClientConnManager> = Arc::new(ConsoleClientConnManager::new());
    pub static ref gConsolePanelConnMgr: Arc<ConsolePanelConnManager> = Arc::new(ConsolePanelConnManager::new());
    pub static ref gConsoleServiceConnMgr: Arc<ConsoleServiceConnManager> = Arc::new(ConsoleServiceConnManager::new());


    pub static ref gConsoleContext: Arc<Mutex<ConsoleContext>> = Arc::new(Mutex::new(ConsoleContext::new()));
    pub static ref gConsoleStreamMgr: Arc<ConsoleStreamManager> = ConsoleStreamManager::new();
    pub static ref gConsoleCMMgr: Arc<ConsoleCMManager> = ConsoleCMManager::new();
    pub static ref gConsoleEventMgr: Arc<ConsoleEventManager> = ConsoleEventManager::new();
    pub static ref gConsoleSystemMgr: Arc<ConsoleSystemManager> = ConsoleSystemManager::new();
    pub static ref gConsoleUserDeviceMgr: Arc<ConsoleUserDeviceManager> = ConsoleUserDeviceManager::new();

    // Database
    pub static ref gConsoleDatabase: Arc<Mutex<ConsoleDatabase>> = Arc::new(Mutex::new(ConsoleDatabase::new()));
    pub static ref gUserManager: Arc<ConsoleUserManager> = ConsoleUserManager::new();
    pub static ref gUserSessionManager: Arc<ConsoleUserSessionManager> = ConsoleUserSessionManager::new();

    // Profile
    pub static ref gDeviceManager: Arc<ConsoleDeviceManager> = ConsoleDeviceManager::new();
    pub static ref gIdGenerator: Arc<Mutex<PrIdGenerator>> = PrIdGenerator::new();

    // Storage
    pub static ref gKvStorage: Arc<Mutex<KvStorage >> = Arc::new(Mutex::new(KvStorage::new()));

    // Auth Manager
    pub static ref gAuthManager: Arc<Mutex<AuthManager>> = Arc::new(Mutex::new(AuthManager::new()));

    // License verifier (Ed25519 public key)
    pub static ref gLicenseVerifier: Arc<Mutex<Option<Arc<LicenseVerifier>>>> = Arc::new(Mutex::new(None));

    // Relay
    pub static ref gRelayConnMgr: Arc<RelayConnManager> = Arc::new(RelayConnManager::new());
    pub static ref gRelayRoomMgr: Arc<RelayRoomManager> = Arc::new(RelayRoomManager::new());
    pub static ref gRelayRedisConn: Arc<Mutex<RelayRedisConn<ConnectionManager>>> = Arc::new(Mutex::new(RelayRedisConn::new()));

    // Update
    pub static ref gUpdateInfoManager: Arc<UpdateInfoManager> = UpdateInfoManager::new();

    // record
    pub static ref gRecordVisitManager: Arc<ConsoleVisitManager> = ConsoleVisitManager::new();
    pub static ref gRecordFileTransferManager: Arc<ConsoleFileTransferManager> = ConsoleFileTransferManager::new();

    // render records view (design doc 6.2 / 6.3)
    pub static ref gRenderRecordManager: Arc<ConsoleRenderRecordManager> = ConsoleRenderRecordManager::new();
    pub static ref gRecordTunnel: Arc<RecordTunnelManager> = RecordTunnelManager::new();

    // Standard WebRTC ICE/STUN/TURN configuration. net_rtc_local never reads it.
    pub static ref gRtcConfigManager: Arc<RtcConfigManager> = Arc::new(RtcConfigManager::new());
}

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    // Display UI or not
    #[arg(short, long, default_value_t = String::from(""))]
    running_mode: String,

    /// Destructive test-only reset. Requires environment="test" and the exact
    /// database name in --reset-test-database.
    #[arg(long, default_value_t = false)]
    confirm_reset_test_identity_data: bool,
    #[arg(long, default_value_t = String::new())]
    reset_test_database: String,
}

fn main() {
    let args = Args::parse();
    if args.confirm_reset_test_identity_data {
        let result = tokio::runtime::Runtime::new()
            .expect("create reset runtime")
            .block_on(async {
                ConsoleSettings::load_settings().await;
                test_reset::reset(&args.reset_test_database).await
            });
        if let Err(error) = result {
            eprintln!("{error}");
            std::process::exit(2);
        }
        return;
    }
    let machine_code = px_base::machine_code::generate_machine_code();

    if args.running_mode == "server" {
        tokio::runtime::Runtime::new()
            .expect("create Console server runtime")
            .block_on(run_as_server(machine_code));
    } else if args.running_mode == "system_service" {
        run_as_system_service(machine_code);
    } else {
        run_as_panel(machine_code);
    }
}

fn run_as_system_service(_machine_code: String) {}

fn run_as_panel(machine_code: String) {
    // log
    let _guard = log_util::init_log("logs/px_console/".to_string(), "log_console_panel".to_string());
    let previous_panic_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |panic_info| {
        tracing::error!("Console panel panicked: {panic_info}");
        previous_panic_hook(panic_info);
    }));

    let locale = match get_locale() {
        Some(locale) => locale,
        None => "en-US".to_string(),
    };
    tracing::info!("Current system locale: {}", locale);

    let language = if locale.starts_with("en-") {
        ConsoleLanguage::new_english()
    } else {
        ConsoleLanguage::new_chinese()
    };

    // Iced owns the Tokio runtime for the panel event loop. Complete the
    // asynchronous bootstrap in a short-lived runtime first; otherwise
    // Iced would attempt to nest its runtime and panic during startup.
    let (auth, used_time, settings) = tokio::runtime::Runtime::new()
        .expect("create Console panel bootstrap runtime")
        .block_on(async {
            ConsoleSettings::load_settings().await;
            // load the cached authorization (KvStorage; 面板进程未初始化 KvStorage 时为空操作)
            gAuthManager.lock().await.load().await;
            let auth = gAuthManager.lock().await.get_auth().await;
            // 已使用时间由服务器锚定的有效期推导（见 AuthManager::get_used_time）
            let used_time = gAuthManager.lock().await.get_used_time().await;
            let settings = gConsoleSettings.lock().await.clone();
            (auth, used_time, settings)
        });

    tracing::info!("used time: {}", used_time);
    tracing::info!(
        auth_id = %auth.auth_id,
        auth_name = %auth.auth_name,
        mode = %auth.mode,
        days = auth.days,
        max_streams = auth.max_streams,
        "Console panel authorization loaded (credentials redacted)"
    );

    let r = console_panel::run(language, machine_code, auth, used_time, settings);
    if let Err(e) = r {
        tracing::error!("{}", e);
    }
}

async fn run_as_server(machine_code: String) {
    // log
    let _guard = log_util::init_log("logs/px_console/".to_string(), "log_console".to_string());

    // tls
    rustls::crypto::ring::default_provider()
        .install_default()
        .expect("Failed to install rustls crypto provider");

    // settings
    ConsoleSettings::load_settings().await;
    if let Err(error) = gConsoleSettings.lock().await.validate_for_server() {
        tracing::error!(%error, "Console security configuration rejected");
        return;
    }

    let rtc_storage_dir = std::env::current_exe()
        .ok()
        .and_then(|path| path.parent().map(|directory| directory.join("storage")))
        .unwrap_or_else(|| std::path::PathBuf::from("storage"));
    let (rtc_settings, rtc_console_host) = {
        let settings = gConsoleSettings.lock().await;
        (settings.rtc.clone(), settings.server_w3c_ip.clone())
    };
    if let Err(error) = gRtcConfigManager
        .initialize(rtc_settings, &rtc_console_host, rtc_storage_dir)
        .await
    {
        tracing::error!(%error, "RTC ICE configuration initialization failed");
        return;
    }

    // The fixed ZLMediaKit executable is deployed beside px_console.exe.  Start
    // it only for a local media_server_url; a remote ZLM remains externally
    // managed.
    let live_settings = gConsoleSettings.lock().await.live.clone();
    crate::media_sidecar::ensure_started(&live_settings).await;
    let rtc_config = gRtcConfigManager.config().await;
    match (
        gRtcConfigManager.turn_rest_secret_base64().await,
        gRtcConfigManager.storage_dir().await,
    ) {
        (Ok(secret), Ok(storage_dir)) => {
            if let Err(error) = crate::media_sidecar::apply_turn_config(
                &rtc_config.managed_console_server,
                rtc_config.revision,
                &secret,
                &storage_dir,
                true,
            )
            .await
            {
                tracing::error!(%error, "managed px_turn sidecar failed to start");
            }
        }
        (Err(error), _) | (_, Err(error)) => {
            tracing::error!(%error, "managed px_turn sidecar configuration is unavailable");
        }
    }

    // update machine code
    gConsoleContext
        .lock()
        .await
        .update_machine_code(machine_code.clone());
    tracing::info!("machine code: {}", machine_code);

    // pre-create some folders
    let _ = px_base::create_dir_if_not_exists("./web");
    let _ = px_base::create_dir_if_not_exists("./uploads");
    let _ = px_base::create_dir_if_not_exists("./uploads/logs");
    let _ = px_base::create_dir_if_not_exists("./uploads/avatar");
    let _ = px_base::create_dir_if_not_exists("./uploads/update_info");
    let _ = px_base::create_dir_if_not_exists("./uploads/records");

    {
        let exe_dir = px_base::current_exe_dir();
        let mut settings = gConsoleSettings.lock().await;
        settings.upload_path = "./uploads".to_string();
        settings.abs_upload_path = exe_dir.clone() + "/uploads";
        settings.upload_logs_path = "./uploads/logs".to_string();
        settings.abs_upload_logs_path = exe_dir.clone() + "/uploads/logs";
        settings.dump();
    }

    // Preserve the signed authorization cached by releases named Pixels CMS.
    // New installations use the product-neutral storage name, while an in-place upgrade keeps
    // opening the legacy directory instead of silently appearing unlicensed.
    let exe_dir = std::env::current_exe()
        .ok()
        .and_then(|path| path.parent().map(std::path::Path::to_path_buf));
    let storage_name = match exe_dir {
        Some(ref dir) if dir.join("storage").exists() => "storage",
        Some(ref dir) if dir.join("cms_storage").exists() => {
            tracing::warn!(
                "using legacy cms_storage authorization cache for upgrade compatibility"
            );
            "cms_storage"
        }
        _ => "storage",
    };
    if !gKvStorage.lock().await.init(storage_name) {
        tracing::error!("KvStorage initialization failed");
        return;
    }

    // database
    if !gConsoleDatabase.lock().await.init().await {
        tracing::error!("Database initialization failed");
        return;
    }
    crate::app_schedule::gAppScheduleManager
        .load_from_db()
        .await;

    // Redis
    let redis_url = gConsoleSettings.lock().await.redis_url.clone();
    let redis_conn = redis_util::get_redis_conn_mgr(redis_url.clone()).await;
    if redis_conn.is_err() {
        tracing::error!("connect to configured Redis server failed (details redacted)");
        return;
    }
    // The Redis URL may contain a password. Keep connection details out of logs.
    tracing::info!("connected to configured Redis server");
    gRelayRedisConn.lock().await.set_conn(redis_conn.unwrap());

    // generator
    gIdGenerator.lock().await.init().await;

    // License verifier
    match init_license_verifier() {
        Ok(verifier) => {
            *gLicenseVerifier.lock().await = Some(Arc::new(verifier));
        }
        Err(e) => {
            tracing::error!("license verifier initialization failed: {}", e);
            return;
        }
    }

    // Auth Manager — load is best-effort: if no valid authorization is found
    // (first run, expired license, etc.) the server still starts; the
    // background pull loop will fetch the authorization from the auth server.
    if !gAuthManager.lock().await.load().await {
        tracing::warn!(
            "no valid authorization loaded; starting unlicensed — will pull from the auth server"
        );
    }

    // 网络上报授权：启动时立即 pull 一次，之后按 auth_pull_interval_secs 周期 pull。
    crate::auth::console_auth_pull::start_pull_loop().await;

    let port = gConsoleSettings.lock().await.udp_broadcast_port;
    gConsoleContext.lock().await.broadcast_access_info(port).await;
    tracing::info!("broadcast port at: {}", port);

    // gConsoleContext
    //     .lock().await
    //     .test_broadcast(port).await;

    // relay server
    tokio::spawn(async {
        let relay_port = gConsoleSettings.lock().await.relay_port;
        let server = RelayServer::new("0.0.0.0".to_string(), relay_port, gConsoleContext.clone());
        server.start().await;
    });

    // render-records temp-cache cleanup (TTL 24h / 10GB threshold, keep exempt)
    crate::record::record_cleaner::start_cleanup_task(
        gRenderRecordManager.clone(),
        gRecordTunnel.clone(),
    );

    // console server
    let srv_task = async move {
        let console_port = gConsoleSettings.lock().await.console_port;
        let server = ConsoleServer::new("0.0.0.0".to_string(), console_port);
        server.start().await;
    };
    srv_task.await;
}
