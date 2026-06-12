use crate::auth::auth_stat::StatAuth;
use crate::stat_api_error::StatApiError;
use crate::stat_api_keys::{KEY_DEVICE_ID, KEY_SYS_INFO};
use crate::stat_context::StatContext;
use crate::stat_http_utils::{get_body, get_body_str, get_body_str_or_empty};
use axum::body::Body;
use axum::extract::State;
use axum::Json;
use gr_base::{ok_resp, RespMessage};
use serde_json::Value;
use std::sync::Arc;
use tokio::sync::Mutex;
use crate::gStatUsingManager;
use crate::using::stat_open_up::StatOpenUp;

pub async fn handle_open_up(State(_ctx): State<Arc<Mutex<StatContext>>>,
                            b: Body)
                            -> Result<Json<RespMessage<String>>, StatApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let device_id = get_body_str_or_empty(&r, KEY_DEVICE_ID);
    let sys_info = get_body_str(&r, KEY_SYS_INFO)?;

    let open_up = StatOpenUp {
        device_id,
        sys_info,
        created_ts: 0,
        updated_ts: gr_base::get_current_timestamp(),
    };

    gStatUsingManager.insert_or_update(open_up).await?;

    Ok(Json(ok_resp("ok".to_string())))
}