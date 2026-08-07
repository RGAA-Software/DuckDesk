pub mod app_instance;
pub mod command;
pub mod config;
pub mod process;
pub mod proto;
pub mod state;
pub mod storage;
pub mod windows_util;

pub use app_instance::{
    build_game_hook_launch_spec, build_web_client_url, resolve_game_path, AppInstanceRegistry,
    AppInstanceState, AppInstanceSummary, StartAppRequest, APP_MODE_GAME_HOOK,
};
pub use command::{Command, DispatchResult};
pub use config::{ServiceConfig, DEFAULT_SERVICE_NAME, DEFAULT_SERVICE_PATH};
pub use process::{ProcessKind, ProcessSnapshot, RenderMode};
pub use proto::{
    decode_service_message, encode_service_message, MsgAuthInfo, MsgHeartBeat, MsgHeartBeatResp,
    MsgReqCtrlAltDelete, MsgRestartServer, MsgStartServer, MsgStopServer, RenderStatus,
    ServiceMessage, ServiceMessageType,
};
pub use state::{RenderLaunchSpec, ServiceState, RENDER_HEARTBEAT_TIMEOUT, RENDER_STARTUP_GRACE};
pub use storage::{PersistedServiceState, ServiceStorage};
