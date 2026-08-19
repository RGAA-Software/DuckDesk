#![windows_subsystem = "windows"]

mod cms_api_error;
mod cms_context;
mod cms_defs;
mod device;
mod filter;
// mod cms_grpc_relay_client;
// mod cms_grpc_relay_client_mgr;
// mod cms_grpc_ws_client_trait;
mod cms_handler;
mod cms_server;
mod cms_settings;

mod app_schedule;
mod auth;
mod cms_database;
mod cms_http_util;
mod cms_relay;
mod cms_router;
mod config;
mod event;
mod interact;
mod live;
mod media_sidecar;
mod net_client;
mod net_cm;
mod net_panel;
mod net_service;
mod record;
mod stream;
mod system;
mod update;
mod user;
mod user_device;

use crate::auth::cms_auth_license_keys::init_license_verifier;
use crate::auth::cms_auth_manager::AuthManager;
use crate::cms_context::CmsContext;
use crate::cms_database::CmsDatabase;
use crate::cms_relay::relay_conn_mgr::RelayConnManager;
use crate::cms_relay::relay_redis_conn::RelayRedisConn;
use crate::cms_relay::relay_room_mgr::RelayRoomManager;
use crate::cms_relay::relay_server::RelayServer;
use crate::cms_server::CmsServer;
use crate::cms_settings::CmsSettings;
use crate::device::cms_device_manager::CmsDeviceManager;
use crate::device::cms_id_generator::PrIdGenerator;
use crate::event::cms_event_manager::CmsEventManager;
use crate::interact::cms_lang::CmsLanguage;
use crate::interact::cms_ui;
use crate::interact::cms_ui::CmsUIState;
use crate::net_client::cms_client_conn_mgr::CmsClientConnManager;
use crate::net_cm::cms_cm_mgr::CmsCMManager;
use crate::net_panel::cms_panel_conn_mgr::CmsPanelConnManager;
use crate::net_service::cms_service_conn_mgr::CmsServiceConnManager;
use crate::record::cms_file_transfer_manager::CmsFileTransferManager;
use crate::record::cms_render_record_manager::CmsRenderRecordManager;
use crate::record::cms_visit_manager::CmsVisitManager;
use crate::record::record_tunnel::RecordTunnelManager;
use crate::stream::cms_stream_manager::CmsStreamManager;
use crate::system::cms_system_manager::CmsSystemManager;
use crate::update::update_info_manager::UpdateInfoManager;
use crate::user::cms_user_manager::CmsUserManager;
use crate::user_device::cms_user_device_manager::CmsUserDeviceManager;
use clap::Parser;
use egui::IconData;
use px_auth_mgr::auth_license::LicenseVerifier;
use px_base::{kv_storage::KvStorage, log_util, redis_util};
use redis::aio::ConnectionManager;
use std::sync::Arc;
use sys_locale::get_locale;
use tokio::sync::Mutex;

lazy_static::lazy_static! {
    // Cms
    pub static ref gCmsSettings: Arc<Mutex<CmsSettings>> = Arc::new(Mutex::new(CmsSettings::new()));

    pub static ref gCmsClientConnMgr: Arc<CmsClientConnManager> = Arc::new(CmsClientConnManager::new());
    pub static ref gCmsPanelConnMgr: Arc<CmsPanelConnManager> = Arc::new(CmsPanelConnManager::new());
    pub static ref gCmsServiceConnMgr: Arc<CmsServiceConnManager> = Arc::new(CmsServiceConnManager::new());


    pub static ref gCmsContext: Arc<Mutex<CmsContext>> = Arc::new(Mutex::new(CmsContext::new()));
    pub static ref gCmsStreamMgr: Arc<CmsStreamManager> = CmsStreamManager::new();
    pub static ref gCmsCMMgr: Arc<CmsCMManager> = CmsCMManager::new();
    pub static ref gCmsEventMgr: Arc<CmsEventManager> = CmsEventManager::new();
    pub static ref gCmsSystemMgr: Arc<CmsSystemManager> = CmsSystemManager::new();
    pub static ref gCmsUserDeviceMgr: Arc<CmsUserDeviceManager> = CmsUserDeviceManager::new();

    // Database
    pub static ref gCmsDatabase: Arc<Mutex<CmsDatabase>> = Arc::new(Mutex::new(CmsDatabase::new()));
    pub static ref gUserManager: Arc<CmsUserManager> = CmsUserManager::new();

    // Profile
    pub static ref gDeviceManager: Arc<CmsDeviceManager> = CmsDeviceManager::new();
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
    pub static ref gRecordVisitManager: Arc<CmsVisitManager> = CmsVisitManager::new();
    pub static ref gRecordFileTransferManager: Arc<CmsFileTransferManager> = CmsFileTransferManager::new();

    // render records view (design doc 6.2 / 6.3)
    pub static ref gRenderRecordManager: Arc<CmsRenderRecordManager> = CmsRenderRecordManager::new();
    pub static ref gRecordTunnel: Arc<RecordTunnelManager> = RecordTunnelManager::new();
}

#[derive(Parser, Debug)]
#[command(author, version, about, long_about = None)]
struct Args {
    // Display UI or not
    #[arg(short, long, default_value_t = String::from(""))]
    running_mode: String,
}

#[tokio::main]
async fn main() {
    let args = Args::parse();
    let machine_code = px_base::machine_code::generate_machine_code();

    if args.running_mode == "server" {
        run_as_server(machine_code).await;
    } else if args.running_mode == "system_service" {
        run_as_system_service(machine_code);
    } else {
        run_as_panel(machine_code).await;
    }
}

fn run_as_system_service(_machine_code: String) {}

async fn run_as_panel(machine_code: String) {
    // load settings
    CmsSettings::load_settings().await;

    // log
    let _guard = log_util::init_log("logs/px_cms/".to_string(), "log_cms_panel".to_string());

    let locale = match get_locale() {
        Some(locale) => locale,
        None => "en-US".to_string(),
    };
    tracing::info!("Current system locale: {}", locale);

    let language = if locale.starts_with("en-") {
        CmsLanguage::new_english()
    } else {
        CmsLanguage::new_chinese()
    };

    // load the cached authorization (KvStorage; 面板进程未初始化 KvStorage 时为空操作)
    gAuthManager.lock().await.load().await;
    let auth = gAuthManager.lock().await.get_auth().await;

    // 已使用时间由服务器锚定的有效期推导（见 AuthManager::get_used_time）
    let used_time: i64 = gAuthManager.lock().await.get_used_time().await;

    tracing::info!("used time: {}", used_time);
    tracing::info!("auth: {:#?}", auth);

    // icon
    let icon_data = include_bytes!("../assets/px_icon.png");
    let img = image::load_from_memory_with_format(icon_data, image::ImageFormat::Png).unwrap();
    let rgba_data = img.into_rgba8();
    let (w, h) = (rgba_data.width(), rgba_data.height());
    let raw_data: Vec<u8> = rgba_data.into_raw();

    let mut options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([960.0, 540.0]),
        ..Default::default()
    };
    options.viewport.icon = Some(Arc::<IconData>::new(IconData {
        rgba: raw_data,
        width: w,
        height: h,
    }));

    let (cms_port, ssl_enable) = {
        let s = gCmsSettings.lock().await;
        (s.cms_port, s.ssl_enable)
    };
    let state = CmsUIState {
        cms_alive: false,
        cms_alive_pid: 0,
        relay_alive: true,
        auth,
        used_time,
        redis_ok: false,
        mongodb_ok: false,
        show_exit_dialog: false,
        cms_port,
        ssl_enable,
    };
    tracing::info!("state: {:#?}", state);

    let state = Arc::new(std::sync::Mutex::new(state));

    let r = eframe::run_native(
        language.app_name.clone().as_str(),
        options,
        Box::new(|cc| {
            cc.egui_ctx.set_visuals(egui::Visuals::dark());
            // This gives us image support:
            egui_extras::install_image_loaders(&cc.egui_ctx);

            Ok(Box::new(cms_ui::CmsUI::new(
                cc,
                language,
                machine_code,
                state,
            )))
        }),
    );
    if let Err(e) = r {
        tracing::error!("{}", e);
    }
}

async fn run_as_server(machine_code: String) {
    // log
    let _guard = log_util::init_log("logs/px_cms/".to_string(), "log_cms".to_string());

    // tls
    rustls::crypto::ring::default_provider()
        .install_default()
        .expect("Failed to install rustls crypto provider");

    // settings
    CmsSettings::load_settings().await;

    // The fixed ZLMediaKit executable is deployed beside px_cms.exe.  Start
    // it only for a local media_server_url; a remote ZLM remains externally
    // managed.
    let live_settings = gCmsSettings.lock().await.live.clone();
    crate::media_sidecar::ensure_started(&live_settings).await;

    // update machine code
    gCmsContext
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
        let mut settings = gCmsSettings.lock().await;
        settings.upload_path = "./uploads".to_string();
        settings.abs_upload_path = exe_dir.clone() + "/uploads";
        settings.upload_logs_path = "./uploads/logs".to_string();
        settings.abs_upload_logs_path = exe_dir.clone() + "/uploads/logs";
        settings.dump();
    }

    // KvStorage
    if !gKvStorage.lock().await.init("cms_storage") {
        tracing::error!("KvStorage initialization failed");
        return;
    }

    // database
    if !gCmsDatabase.lock().await.init().await {
        tracing::error!("Database initialization failed");
        return;
    }
    crate::app_schedule::gAppScheduleManager
        .load_from_db()
        .await;

    // Redis
    let redis_url = gCmsSettings.lock().await.redis_url.clone();
    let redis_conn = redis_util::get_redis_conn_mgr(redis_url.clone()).await;
    if let Err(err) = redis_conn {
        tracing::error!("connect to redis failed: {}", err.to_string());
        return;
    }
    tracing::info!("connected to redis: {}", redis_url);
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
    crate::auth::cms_auth_pull::start_pull_loop().await;

    let port = gCmsSettings.lock().await.udp_broadcast_port;
    gCmsContext.lock().await.broadcast_access_info(port).await;
    tracing::info!("broadcast port at: {}", port);

    // gCmsContext
    //     .lock().await
    //     .test_broadcast(port).await;

    // relay server
    tokio::spawn(async {
        let relay_port = gCmsSettings.lock().await.relay_port;
        let server = RelayServer::new("0.0.0.0".to_string(), relay_port, gCmsContext.clone());
        server.start().await;
    });

    // render-records temp-cache cleanup (TTL 24h / 10GB threshold, keep exempt)
    crate::record::record_cleaner::start_cleanup_task(
        gRenderRecordManager.clone(),
        gRecordTunnel.clone(),
    );

    // cms server
    let srv_task = async move {
        let cms_port = gCmsSettings.lock().await.cms_port;
        let server = CmsServer::new("0.0.0.0".to_string(), cms_port);
        server.start().await;
    };
    srv_task.await;
}
