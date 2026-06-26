use crate::device::spvr_device::SpvrDevice;
use crate::device::spvr_device_vo::SpvrDeviceVo;
use crate::device::spvr_id_generator::PrIdGenerator;
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use crate::spvr_defs::{
    KEY_ACTIVE, KEY_ALL, KEY_DEVICE_DESKTOP_LINK, KEY_DEVICE_DESKTOP_LINK_RAW, KEY_DEVICE_ID,
    KEY_DEVICE_NAME, KEY_IP, KEY_OFFLINE, KEY_ONLINE, KEY_ONLINE_STATE, KEY_PWD_TYPE,
};
use crate::spvr_http_util::{
    get_body, get_body_bool, get_body_str, get_bool_param, get_int_param, get_str_param,
    get_str_param_allow_empty, get_str_param_or,
};
use crate::user::spvr_user::SpvrUser;
use crate::user::spvr_user_keys::{KEY_HASH_PASSWORD, KEY_USER_ID, KEY_USER_NAME};
use crate::{gDeviceManager, gIdGenerator, gSpvrPanelConnMgr, gUserManager};
use axum::body::Body;
use axum::extract::{Query, State};
use axum::Json;
use gr_base::{ok_resp, ok_resp_str_map, RespMessage, RespStringMap};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_create_new_device(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<SpvrDevice>>, SpvrApiError> {
    let mut hw_info = get_str_param(&query, "hw_info")?;
    let platform = get_str_param(&query, "platform")?;
    let device_name = get_str_param(&query, "device_name")?;

    let device = loop {
        let id_generator = gIdGenerator.clone();
        let new_device_info = id_generator
            .lock()
            .await
            .generate_new_id(&hw_info, &platform);

        // new random pwd md5
        let new_random_pwd_md5 = gr_base::md5_hex(&new_device_info.random_pwd);

        tracing::info!(
            "will find in database: {}",
            new_device_info.device_id.clone()
        );
        let exist_device = gDeviceManager
            .query_device_by_id_and_seed(
                new_device_info.device_id.clone(),
                new_device_info.seed.clone(),
            )
            .await;
        let device_found: bool;
        let mut device: Option<SpvrDevice> = None;
        if let Err(e) = exist_device {
            if e == SpvrApiError::DatabaseError {
                tracing::error!("database error, can't query device when creating device.");
                break None;
            } else {
                device_found = false;
            }
        } else {
            device_found = true;
            device = Some(exist_device?);
        }

        tracing::info!("device found ? {}", device_found);

        if device_found {
            let mut match_device = device.unwrap();
            // todo: generate new random pwd, update random pwd
            tracing::info!("Match exists device: {}", new_device_info.device_id);

            let new_random_pwd = PrIdGenerator::generate_random_pwd();
            let update_info = HashMap::<String, String>::from([
                (
                    String::from("random_pwd_md5"),
                    gr_base::md5_hex(&new_random_pwd),
                ),
                (String::from("gen_random_pwd"), new_random_pwd.clone()),
                (String::from("device_name"), device_name.clone()),
            ]);
            let update_result = gDeviceManager
                .update_device(match_device.device_id.clone(), update_info)
                .await?;
            if update_result {
                match_device.gen_random_pwd = new_random_pwd.clone();
                match_device.random_pwd_md5 = gr_base::md5_hex(&new_random_pwd);
                match_device.device_name = device_name;
                break Some(match_device);
            } else {
                break None;
            }
        } else {
            tracing::info!("to find: {} in database", new_device_info.device_id);
            let exist_device = gDeviceManager
                .query_device_by_id(new_device_info.device_id.clone())
                .await;
            if let Ok(exist_device) = exist_device {
                tracing::warn!(
                    "already exist: {}, will regenerate!",
                    exist_device.device_id
                );
                // need to regenerate
                hw_info = "".to_string();
                continue;
            } else {
                tracing::info!(
                    "the device is a new one, insert to db: {}",
                    new_device_info.device_id
                );
                let mut device = SpvrDevice::default();
                device.device_id = new_device_info.device_id;
                device.device_name = device_name.clone();
                device.seed = new_device_info.seed;
                device.created_timestamp = gr_base::get_current_timestamp();
                device.last_update_timestamp = gr_base::get_current_timestamp();
                device.random_pwd_md5 = new_random_pwd_md5;
                device.gen_random_pwd = new_device_info.random_pwd;
                device.active = true;

                let resp_device = device.clone();
                let ok = gDeviceManager.insert_device(device.clone()).await?;
                if ok {
                    break Some(resp_device);
                } else {
                    tracing::error!("insert failed: {}", device.device_id.clone());
                    break None;
                }
            }
        }
    };

    // resp
    if let Some(device) = device {
        Ok(Json(ok_resp(device)))
    } else {
        Err(SpvrApiError::CreateDeviceFailed)
    }
}

#[derive(Serialize, Debug, Deserialize)]
pub struct RespDeviceMessage {
    pub code: i32,
    pub message: String,
    pub timestamp: i64,
    pub total: u64,
    pub data: Vec<SpvrDeviceVo>,
}

pub async fn handle_query_devices(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespDeviceMessage>, SpvrApiError> {
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;
    let device_id = get_str_param_or(&query, KEY_DEVICE_ID, "")?;
    let device_name = get_str_param_or(&query, KEY_DEVICE_NAME, "")?;
    let ip = get_str_param_or(&query, KEY_IP, "")?;
    let online_state = get_str_param_or(&query, KEY_ONLINE_STATE, "")?;

    tracing::info!("query devices, device name: {}, device id: {}, ip: {}, online_state: {}, page: {}, page size: {}",
        device_name, device_id, ip, online_state, page, page_size);

    let devices = gDeviceManager
        .query_devices(device_name, device_id, ip, page, page_size)
        .await?;
    let total = gDeviceManager.query_total_devices_count().await?;

    let mut vo_devices = Vec::new();
    for device in devices {
        tracing::info!("link: {}", device.desktop_link_raw);

        // online or not
        let online = gSpvrPanelConnMgr
            .is_panel_online(device.device_id.clone())
            .await?;

        if !online_state.is_empty() && online_state != KEY_ALL {
            if online_state == KEY_ONLINE {
                if !online {
                    tracing::info!(
                        "this device is NOT online: {}, ignore it.",
                        device.device_id
                    );
                    continue;
                }
            } else if online_state == KEY_OFFLINE {
                if online {
                    tracing::info!(
                        "this device is online: {}, but we need offline devices, ignore it.",
                        device.device_id
                    );
                    continue;
                }
            }
        }
        let mut device_vo = SpvrDeviceVo::from(&device);
        device_vo.online = online;

        vo_devices.push(device_vo)
    }

    for device in &mut vo_devices {
        // spvr conn info
        let conn = gSpvrPanelConnMgr
            .get_conn_info(device.device_id.clone())
            .await;
        if let Ok(conn) = conn {
            //device.device_ip_addr = conn.device_ip_addr.clone();
            device.sys_info = conn.sys_info;
        }
    }
    Ok(Json(RespDeviceMessage {
        code: 200,
        message: "ok".to_string(),
        timestamp: gr_base::get_current_timestamp(),
        total,
        data: vo_devices,
    }))
}

pub async fn handle_count_devices(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<u64>>, SpvrApiError> {
    let device = gDeviceManager.query_total_devices_count().await?;
    Ok(Json(ok_resp(device)))
}

pub async fn query_device_by_id(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<SpvrDevice>>, SpvrApiError> {
    let device_id = get_str_param(&query, "device_id")?;
    let device = gDeviceManager.query_device_by_id(device_id).await?;
    Ok(Json(ok_resp(device)))
}

pub async fn append_used_time(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<String>>, SpvrApiError> {
    let device_id = get_str_param(&query, "device_id")?;
    let period = get_int_param(&query, "period")?;

    // exists device
    let device = gDeviceManager.query_device_by_id(device_id.clone()).await?;
    let target_used_time = device.used_time + period as i64;

    let r = gDeviceManager
        .update_device_field(device_id.clone(), "used_time".to_string(), target_used_time)
        .await?;
    if r {
        Ok(Json(ok_resp(device_id)))
    } else {
        Err(SpvrApiError::DatabaseError)
    }
}

pub async fn handle_query_total_used_time(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
) -> Result<Json<RespMessage<u64>>, SpvrApiError> {
    let beg = gr_base::get_current_timestamp();
    let r = gDeviceManager.query_total_used_time().await?;
    tracing::info!("used: {}ms", (gr_base::get_current_timestamp() - beg));
    Ok(Json(ok_resp(r)))
}

pub async fn verify_device_info(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespStringMap>, SpvrApiError> {
    let device_id = get_str_param(&query, "device_id")?;
    let random_pwd_md5 = get_str_param_allow_empty(&query, "random_pwd_md5")?;
    let safety_pwd_md5 = get_str_param_allow_empty(&query, "safety_pwd_md5")?;

    let device = gDeviceManager.query_device_by_id(device_id.clone()).await?;
    tracing::info!("found device to verify: {}", device.device_id);

    let ok_random_pwd = if !random_pwd_md5.is_empty() && random_pwd_md5 == device.random_pwd_md5 {
        true
    } else {
        false
    };
    let ok_safety_pwd = if !safety_pwd_md5.is_empty() && safety_pwd_md5 == device.safety_pwd_md5 {
        true
    } else {
        false
    };
    if !ok_random_pwd && !ok_safety_pwd {
        tracing::error!("verify failed, invalid password");
        return Err(SpvrApiError::PasswordInvalid);
    }

    let mut pwd_type = "unknown";
    if ok_random_pwd && ok_safety_pwd {
        pwd_type = "all";
    } else if ok_random_pwd {
        pwd_type = "random"
    } else if ok_safety_pwd {
        pwd_type = "safety"
    }

    let mut hm = HashMap::new();
    hm.insert(KEY_DEVICE_ID.to_string(), device_id);
    hm.insert(KEY_PWD_TYPE.to_string(), pwd_type.to_string());
    Ok(Json(ok_resp_str_map(hm)))
}

pub async fn update_random_password(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<SpvrDevice>>, SpvrApiError> {
    let device_id = get_str_param(&query, "device_id")?;
    let mut device = gDeviceManager.query_device_by_id(device_id.clone()).await?;

    // generate new random password
    let id_generator = gIdGenerator.clone();
    let new_random_pwd = PrIdGenerator::generate_random_pwd();

    // update to database
    let random_pwd_md5 = gr_base::md5_hex(&new_random_pwd.clone());
    let update_info = HashMap::<String, String>::from([
        (String::from("random_pwd_md5"), random_pwd_md5.clone()),
        (String::from("gen_random_pwd"), new_random_pwd.clone()),
    ]);

    let r = gDeviceManager
        .update_device(device_id.clone(), update_info)
        .await?;
    if !r {
        tracing::error!("update device failed: {}", device_id);
        return Err(SpvrApiError::DeviceNotFound);
    }

    device.random_pwd_md5 = random_pwd_md5;
    device.gen_random_pwd = new_random_pwd;
    Ok(Json(ok_resp(device)))
}

pub async fn update_safety_password(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<SpvrDevice>>, SpvrApiError> {
    let device_id = get_str_param(&query, "device_id")?;
    let safety_pwd_md5 = get_str_param(&query, "safety_pwd_md5")?;
    if device_id.is_empty() || safety_pwd_md5.is_empty() {
        return Err(SpvrApiError::InvalidParams);
    }

    let mut device = gDeviceManager.query_device_by_id(device_id.clone()).await?;

    // update to database
    let update_info =
        HashMap::<String, String>::from([(String::from("safety_pwd_md5"), safety_pwd_md5.clone())]);
    if !gDeviceManager
        .update_device(device_id.clone(), update_info)
        .await?
    {
        return Err(SpvrApiError::DeviceNotFound);
    }

    device.safety_pwd_md5 = safety_pwd_md5;
    Ok(Json(ok_resp(device)))
}

pub async fn update_desktop_link(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    b: Body,
) -> Result<Json<RespMessage<SpvrDevice>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let device_id = get_body_str(&r, KEY_DEVICE_ID)?;
    let desktop_link = get_body_str(&r, KEY_DEVICE_DESKTOP_LINK)?;
    let desktop_link_raw = get_body_str(&r, KEY_DEVICE_DESKTOP_LINK_RAW)?;

    let _ = gDeviceManager
        .update_device_field(
            device_id.clone(),
            KEY_DEVICE_DESKTOP_LINK.to_string(),
            desktop_link,
        )
        .await?;

    let _ = gDeviceManager
        .update_device_field(
            device_id.clone(),
            KEY_DEVICE_DESKTOP_LINK_RAW.to_string(),
            desktop_link_raw,
        )
        .await?;

    let device = gDeviceManager.query_device_by_id(device_id.clone()).await?;
    Ok(Json(ok_resp(device)))
}

pub async fn update_device_name(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    b: Body,
) -> Result<Json<RespMessage<SpvrDevice>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let device_id = get_body_str(&r, KEY_DEVICE_ID)?;
    let device_name = get_body_str(&r, KEY_DEVICE_NAME)?;

    let _ = gDeviceManager
        .update_device_field(device_id.clone(), KEY_DEVICE_NAME.to_string(), device_name)
        .await?;

    let device = gDeviceManager.query_device_by_id(device_id.clone()).await?;
    Ok(Json(ok_resp(device)))
}

pub async fn update_device_active(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    b: Body,
) -> Result<Json<RespMessage<SpvrDevice>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let device_id = get_body_str(&r, KEY_DEVICE_ID)?;
    let active = get_body_bool(&r, KEY_ACTIVE)?;

    let _ = gDeviceManager
        .update_device_field(device_id.clone(), KEY_ACTIVE.to_string(), active)
        .await?;

    let device = gDeviceManager.query_device_by_id(device_id.clone()).await?;
    Ok(Json(ok_resp(device)))
}
