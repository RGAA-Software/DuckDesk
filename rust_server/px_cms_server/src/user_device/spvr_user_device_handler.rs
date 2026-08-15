use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use crate::spvr_defs::KEY_DEVICE_ID;
use crate::spvr_http_util::{get_body, get_body_str, get_int_param, get_str_param};
use crate::user::spvr_user_keys::{KEY_PAGE, KEY_PAGE_SIZE, KEY_USER_ID};
use crate::user_device::spvr_user_device::{SpvrUserDevice, SpvrUserDeviceAdapter};
use crate::{gDeviceManager, gSpvrUserDeviceMgr, gUserManager};
use axum::body::Body;
use axum::extract::{Query, State};
use axum::Json;
use px_base::{get_current_readable_timestamp, ok_resp, RespMessage};
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_add_device_for_user(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    b: Body,
) -> Result<Json<RespMessage<SpvrUserDeviceAdapter>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let uid = get_body_str(&r, KEY_USER_ID)?;
    let device_id = get_body_str(&r, KEY_DEVICE_ID)?;

    let _ = gUserManager.query_user_by_id(uid.clone()).await?;

    let _ = gDeviceManager.query_device_by_id(device_id.clone()).await?;

    let user_device = SpvrUserDevice {
        uid,
        device_id,
        created_ts: px_base::get_current_timestamp(),
        created_ts_readable: get_current_readable_timestamp(),
    };

    let r = gSpvrUserDeviceMgr.insert_user_device(user_device).await?;

    Ok(Json(ok_resp(r)))
}

pub async fn handle_remove_device_from_user(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    b: Body,
) -> Result<Json<RespMessage<SpvrUserDeviceAdapter>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let uid = get_body_str(&r, KEY_USER_ID)?;
    let device_id = get_body_str(&r, KEY_DEVICE_ID)?;

    let _ = gUserManager.query_user_by_id(uid.clone()).await?;

    let _ = gDeviceManager.query_device_by_id(device_id.clone()).await?;

    let user_device = gSpvrUserDeviceMgr
        .remove_device_from_user(uid, device_id)
        .await?;

    Ok(Json(ok_resp(user_device)))
}

pub async fn handle_query_user_devices(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<SpvrUserDeviceAdapter>>>, SpvrApiError> {
    let uid = get_str_param(&query, KEY_USER_ID)?;
    let page = get_int_param(&query, KEY_PAGE)?;
    let page_size = get_int_param(&query, KEY_PAGE_SIZE)?;

    let devices = gSpvrUserDeviceMgr
        .query_user_devices(uid, page, page_size)
        .await?;

    Ok(Json(ok_resp(devices)))
}
