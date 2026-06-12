#![windows_subsystem = "windows"]

mod spvr_context;
mod spvr_server;
mod spvr_grpc_relay_client;
mod spvr_settings;
mod spvr_grpc_relay_client_mgr;
mod spvr_grpc_ws_client_trait;
mod spvr_handler;
mod spvr_defs;
mod device;
mod filter;
mod spvr_api_error;

mod auth;
mod spvr_http_util;
mod spvr_database;
mod event;
mod spvr_router;
mod config;
mod net_client;
mod net_panel;
mod user;
mod stream;
mod record;
mod net_cm;
mod interact;
mod system;
mod user_device;
mod relay;
mod update;

use std::fs::File;
use std::io::Read;
use crate::spvr_grpc_relay_client_mgr::SpvrGrpcRelayClientManager;
use crate::spvr_server::SpvrServer;
use crate::spvr_settings::SpvrSettings;
use gr_base::{kv_storage::KvStorage, log_util, redis_util};
use std::sync::Arc;
use tokio::sync::Mutex;
use clap::Parser;
use egui::IconData;
use sys_locale::get_locale;
use gr_base::hwid_util::HardwareIdUtil;
use crate::auth::spvr_auth_manager::AuthManager;
use crate::device::spvr_device_manager::SpvrDeviceManager;
use crate::device::spvr_id_generator::PrIdGenerator;
use crate::interact::spvr_ui;
use crate::net_client::spvr_client_conn_mgr::SpvrClientConnManager;
use crate::net_cm::spvr_cm_mgr::SpvrCMManager;
use crate::net_panel::spvr_panel_conn_mgr::SpvrPanelConnManager;
use crate::spvr_context::SpvrContext;
use crate::spvr_database::SpvrDatabase;
use crate::stream::spvr_stream_manager::SpvrStreamManager;
use crate::user::spvr_user_manager::SpvrUserManager;
use crate::event::spvr_event_manager::SpvrEventManager;
use crate::interact::spvr_lang::SpvrLanguage;
use crate::interact::spvr_ui::SpvrUIState;
use crate::relay::relay_conn_mgr::RelayConnManager;
use crate::relay::relay_redis_conn::RelayRedisConn;
use crate::relay::relay_room_mgr::RelayRoomManager;
use crate::system::spvr_system_manager::SpvrSystemManager;
use crate::user_device::spvr_user_device_manager::SpvrUserDeviceManager;
use redis::aio::{ConnectionManager};
use crate::relay::relay_server::RelayServer;
use crate::update::update_info_manager::UpdateInfoManager;
use crate::record::spvr_file_transfer_manager::SpvrFileTransferManager;
use crate::record::spvr_visit_manager::SpvrVisitManager;

lazy_static::lazy_static! {
    // Spvr
    pub static ref gSpvrSettings: Arc<Mutex<SpvrSettings>> = Arc::new(Mutex::new(SpvrSettings::new()));

    pub static ref gSpvrClientConnMgr: Arc<SpvrClientConnManager> = Arc::new(SpvrClientConnManager::new());
    pub static ref gSpvrPanelConnMgr: Arc<SpvrPanelConnManager> = Arc::new(SpvrPanelConnManager::new());

    pub static ref gSpvrContext: Arc<Mutex<SpvrContext>> = Arc::new(Mutex::new(SpvrContext::new()));
    pub static ref gSpvrStreamMgr: Arc<SpvrStreamManager> = SpvrStreamManager::new();
    pub static ref gSpvrCMMgr: Arc<SpvrCMManager> = SpvrCMManager::new();
    pub static ref gSpvrEventMgr: Arc<SpvrEventManager> = SpvrEventManager::new();
    pub static ref gSpvrSystemMgr: Arc<SpvrSystemManager> = SpvrSystemManager::new();
    pub static ref gSpvrUserDeviceMgr: Arc<SpvrUserDeviceManager> = SpvrUserDeviceManager::new();

    // Database
    pub static ref gSpvrDatabase: Arc<Mutex<SpvrDatabase>> = Arc::new(Mutex::new(SpvrDatabase::new()));
    pub static ref gUserManager: Arc<SpvrUserManager> = SpvrUserManager::new();

    // Profile
    pub static ref gDeviceManager: Arc<SpvrDeviceManager> = SpvrDeviceManager::new();
    pub static ref gIdGenerator: Arc<Mutex<PrIdGenerator>> = PrIdGenerator::new();

    // Storage
    pub static ref gKvStorage: Arc<Mutex<KvStorage >> = Arc::new(Mutex::new(KvStorage::new()));

    // Auth Manager
    pub static ref gAuthManager: Arc<Mutex<AuthManager>> = Arc::new(Mutex::new(AuthManager::new()));

    // Relay
    pub static ref gRelayConnMgr: Arc<RelayConnManager> = Arc::new(RelayConnManager::new());
    pub static ref gRelayRoomMgr: Arc<RelayRoomManager> = Arc::new(RelayRoomManager::new());
    pub static ref gRelayRedisConn: Arc<Mutex<RelayRedisConn<ConnectionManager>>> = Arc::new(Mutex::new(RelayRedisConn::new()));
    
    // Update
    pub static ref gUpdateInfoManager: Arc<UpdateInfoManager> = UpdateInfoManager::new();

    // record
    pub static ref gRecordVisitManager: Arc<SpvrVisitManager> = SpvrVisitManager::new();
    pub static ref gRecordFileTransferManager: Arc<SpvrFileTransferManager> = SpvrFileTransferManager::new();
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
    let machine_code = HardwareIdUtil::generate_hardware_id();

    if args.running_mode == String::from("server") {
        run_as_server(machine_code).await;
    }
    else if args.running_mode == "system_service" {
        run_as_system_service(machine_code);
    }
    else {
        run_as_panel(machine_code).await;
    }

}

fn run_as_system_service(machine_code: String) {

}

async fn run_as_panel(machine_code: String) {
    // load settings
    SpvrSettings::load_settings().await;

    // log
    let _guard = log_util::init_log("logs/gr_cms_server/".to_string(), "log_spvr_panel".to_string());

    let locale = match get_locale() {
        Some(locale) => {
            locale
        }
        None => {
            "en-US".to_string()
        },
    };
    tracing::info!("Current system locale: {}", locale);

    let mut language: SpvrLanguage;
    if locale.starts_with("en-") {
        language = SpvrLanguage::new_english();
    }
    else {
        language = SpvrLanguage::new_chinese();
    }

    // test //
    // language = SpvrLanguage::new_chinese();

    // read the auth/auth.info
    gAuthManager
        .lock().await
        .load().await;
    let auth = gAuthManager
        .lock().await
        .get_auth().await;

    let mut used_time: i64 = 0;
    let file = File::options()
        .read(true)
        .open("au.dat");
    if let Ok(mut file) = file {
        let mut buffer: String = String::default();
        if let Ok(size) = file.read_to_string(&mut buffer) {
            if let Ok(value) = buffer.parse::<i64>() {
                used_time = value;
            }
        }
    }

    tracing::info!("used time: {}", used_time);
    tracing::info!("auth: {:#?}", auth);

    // icon
    let icon_data = include_bytes!("../assets/tc_icon.png");
    let img = image::load_from_memory_with_format(icon_data, image::ImageFormat::Png).unwrap();
    let rgba_data = img.into_rgba8();
    let (w,h)=(rgba_data.width(),rgba_data.height());
    let raw_data: Vec<u8> = rgba_data.into_raw();

    let mut options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_inner_size([960.0, 540.0]),
        ..Default::default()
    };
    options.viewport.icon = Some(Arc::<IconData>::new(IconData {
        rgba: raw_data,
        width: w,
        height: h
    }));

    let state = SpvrUIState {
        spvr_alive: false,
        spvr_alive_pid: 0,
        relay_alive: true,
        auth,
        used_time,
        redis_ok: false,
        mongodb_ok: false,
        show_exit_dialog: false,
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

            Ok(Box::new(spvr_ui::SpvrUI::new(cc, language, machine_code, state)))
        }),
    );
    if let Err(e) = r {
        tracing::error!("{}", e);
    }
}

async fn run_as_server(machine_code: String) {
    // log
    let _guard = log_util::init_log("logs/gr_cms_server/".to_string(), "log_spvr".to_string());

    // tls
    rustls::crypto::ring::default_provider()
        .install_default()
        .expect("Failed to install rustls crypto provider");

    // settings
    SpvrSettings::load_settings().await;

    // update machine code
    gSpvrContext
        .lock().await
        .update_machine_code(machine_code.clone());
    tracing::info!("machine code: {}", machine_code);

    // pre-create some folders
    let _ = gr_base::create_dir_if_not_exists("./web");
    let _ = gr_base::create_dir_if_not_exists("./uploads");
    let _ = gr_base::create_dir_if_not_exists("./uploads/logs");
    let _ = gr_base::create_dir_if_not_exists("./uploads/avatar");
    let _ = gr_base::create_dir_if_not_exists("./uploads/update_info");

    {
        let exe_dir = gr_base::current_exe_dir();
        let mut settings = gSpvrSettings.lock().await;
        settings.upload_path = "./uploads".to_string();
        settings.abs_upload_path = exe_dir.clone() + "/uploads";
        settings.upload_logs_path = "./uploads/logs".to_string();
        settings.abs_upload_logs_path = exe_dir.clone() + "/uploads/logs";
        settings.dump();
    }

    // KvStorage
    if !gKvStorage.lock().await.init("spvr_storage") {
        tracing::error!("KvStorage initialization failed");
        return;
    }

    // database
    if !gSpvrDatabase.lock().await.init().await {
        tracing::error!("Database initialization failed");
        return;
    }

    // Redis
    let redis_url = gSpvrSettings
        .lock().await
        .redis_url.clone();
    let redis_conn = redis_util::get_redis_conn_mgr(redis_url.clone()).await;
    if let Err(err) = redis_conn {
        tracing::error!("connect to redis failed: {}", err.to_string());
        return;
    }
    tracing::info!("connected to redis: {}", redis_url);
    gRelayRedisConn
        .lock().await
        .set_conn(redis_conn.unwrap());

    // generator
    gIdGenerator.lock().await.init().await;

    // Auth Manager
    if !gAuthManager.lock().await.load().await {
        tracing::error!("auth manager initialization failed");
        return;
    }
    AuthManager::start_count_down().await;

    let port = gSpvrSettings.lock().await.udp_broadcast_port;
    gSpvrContext
        .lock().await
        .broadcast_access_info(port).await;
    tracing::info!("broadcast port at: {}", port);

    // gSpvrContext
    //     .lock().await
    //     .test_broadcast(port).await;

    // relay server
    tokio::spawn(async {
        let relay_port = gSpvrSettings.lock().await.relay_port;
        let server = RelayServer::new("0.0.0.0".to_string(), relay_port, gSpvrContext.clone());
        server.start().await;
    });

    // spvr server
    let srv_task = async move {
        let spvr_port = gSpvrSettings.lock().await.spvr_port;
        let server = SpvrServer::new("0.0.0.0".to_string(), spvr_port);
        server.start().await;
    };
    srv_task.await;
}