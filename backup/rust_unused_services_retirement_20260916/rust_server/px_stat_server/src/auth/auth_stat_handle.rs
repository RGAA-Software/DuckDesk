use crate::auth::auth_stat::StatAuth;
use crate::gStatAuthManager;
use crate::stat_api_error::StatApiError;
use crate::stat_api_keys::{KEY_AUTH_ID, KEY_AUTH_MACHINE_CODE, KEY_AUTH_NAME, KEY_SYS_INFO};
use crate::stat_context::StatContext;
use crate::stat_http_utils::{get_body, get_body_str};
use axum::body::Body;
use axum::extract::State;
use axum::Json;
use px_base::{ok_resp, RespMessage};
use serde_json::Value;
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_insert_or_update_auth_stat(
    State(_ctx): State<Arc<Mutex<StatContext>>>,
    b: Body,
) -> Result<Json<RespMessage<StatAuth>>, StatApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let auth_id = get_body_str(&r, KEY_AUTH_ID)?;
    let auth_name = get_body_str(&r, KEY_AUTH_NAME)?;
    let auth_machine_code = get_body_str(&r, KEY_AUTH_MACHINE_CODE)?;
    let sys_info = get_body_str(&r, KEY_SYS_INFO)?;
    //let created_ts = px_base::get_current_timestamp();
    let updated_ts = px_base::get_current_timestamp();
    let auth_stat = StatAuth {
        auth_id,
        auth_name,
        auth_machine_code,
        sys_info,
        created_ts: 0,
        updated_ts,
    };
    //tracing::info!("auth auth: {:?}", auth_stat);

    let auth_stat = gStatAuthManager.insert_or_update(auth_stat).await?;
    Ok(Json(ok_resp(auth_stat)))
}
