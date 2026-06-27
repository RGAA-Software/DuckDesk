use crate::event::spvr_event::SpvrEvent;
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use crate::spvr_defs::KEY_DEVICE_ID;
use crate::spvr_http_util::{
    get_body, get_body_str, get_int_param, get_int_param_or, get_str_param, get_str_param_or,
};
use crate::user::spvr_user::{SpvrUser, SpvrUserAdapter};
use crate::user::spvr_user_keys::{
    KEY_AUTH_ID, KEY_AUTH_PASSWORD, KEY_AVATAR_PATH, KEY_FILE, KEY_HASH_PASSWORD,
    KEY_NEW_HASH_PASSWORD, KEY_PAGE, KEY_PAGE_SIZE, KEY_PASSWORD, KEY_SIZE, KEY_SORT_DIRECTION,
    KEY_SORT_FIELD, KEY_USER_ID, KEY_USER_NAME, KEY_USER_PREFIX,
};
use crate::{gAuthManager, gDeviceManager, gSpvrEventMgr, gUserManager};
use axum::body::Body;
use axum::extract::{Multipart, Query, State};
use axum::http::{HeaderValue, StatusCode};
use axum::response::Response;
use axum::Json;
use gr_base::{ok_resp, RespMessage};
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::io::AsyncWriteExt;
use tokio::sync::Mutex;

pub async fn handle_register_user(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    b: Body,
) -> Result<Json<RespMessage<SpvrUser>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let username = r[KEY_USER_NAME].as_str().unwrap();
    let hash_password = r[KEY_HASH_PASSWORD].as_str().unwrap();
    if username.is_empty() || hash_password.is_empty() {
        tracing::error!("register user failed, username or password is empty");
        return Err(SpvrApiError::InvalidParams);
    }

    let user = gUserManager
        .register_user(username.to_string(), hash_password.to_string())
        .await?;

    // record the event
    let event = SpvrEvent::new_register(user.uid.clone(), user.username.clone());
    let _ = gSpvrEventMgr.add_event(event).await;

    Ok(Json(ok_resp(user)))
}

pub async fn handle_login(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    b: Body,
) -> Result<Json<RespMessage<SpvrUser>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let username = get_body_str(&r, KEY_USER_NAME)?;
    let hash_password = get_body_str(&r, KEY_HASH_PASSWORD)?;
    let device_id = get_body_str(&r, KEY_DEVICE_ID)?;
    tracing::info!("login, username: {}, device id: {}", username, device_id);

    let device = gDeviceManager.query_device_by_id(device_id).await?;
    tracing::info!("found device when login: {}", device.device_id);

    let user = gUserManager.query_user_by_username(username).await?;
    tracing::info!("found user when login: {}", user.username);

    if user.password == hash_password {
        // record the event
        let event = SpvrEvent::new_login(user.uid.clone(), user.username.clone());
        let _ = gSpvrEventMgr.add_event(event).await?;

        // bind this device to this user
        let _ = gDeviceManager
            .bind_logged_in_user(device.device_id, user.uid.clone())
            .await?;

        return Ok(Json(ok_resp(user)));
    }

    Err(SpvrApiError::UserNotFound)
}

pub async fn handle_logout(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    b: Body,
) -> Result<Json<RespMessage<SpvrUser>>, SpvrApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let uid = r[KEY_USER_ID].as_str().unwrap();
    let hash_password = r[KEY_HASH_PASSWORD].as_str().unwrap();
    if uid.is_empty() || hash_password.is_empty() {
        tracing::error!("register user failed, username or password is empty");
        return Err(SpvrApiError::InvalidParams);
    }

    let user = gUserManager.query_user_by_id(uid.to_string()).await?;
    if user.password == hash_password {
        // record the event
        let event = SpvrEvent::new_logout(user.uid.clone(), user.username.clone());
        let _ = gSpvrEventMgr.add_event(event).await;

        // process logout
        // clear status in server if you have
        return Ok(Json(ok_resp(user)));
    }

    Err(SpvrApiError::UserNotFound)
}

pub async fn handle_delete_user(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    body: Body,
) -> Result<Json<RespMessage<SpvrUser>>, SpvrApiError> {
    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let uid = r[KEY_USER_ID].as_str().unwrap();
    if uid.is_empty() {
        tracing::error!("delete user failed, uid is empty");
        return Err(SpvrApiError::InvalidParams);
    }

    let user = gUserManager.delete_user(uid.to_string()).await?;

    // record the event
    let event = SpvrEvent::new_delete(user.uid.clone(), user.username.clone());
    let _ = gSpvrEventMgr.add_event(event).await;

    Ok(Json(ok_resp(user)))
}

pub async fn handle_active_user(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    body: Body,
) -> Result<Json<RespMessage<SpvrUser>>, SpvrApiError> {
    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let uid = r[KEY_USER_ID].as_str().unwrap();
    if uid.is_empty() {
        tracing::error!("delete user failed, uid is empty");
        return Err(SpvrApiError::InvalidParams);
    }

    let user = gUserManager.active_user(uid.to_string()).await?;

    // record the event
    let event = SpvrEvent::new_active(user.uid.clone(), user.username.clone());
    let _ = gSpvrEventMgr.add_event(event).await;

    Ok(Json(ok_resp(user)))
}

pub async fn handle_update_user(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    body: Body,
) -> Result<Json<RespMessage<SpvrUser>>, SpvrApiError> {
    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let uid = r[KEY_USER_ID].as_str().unwrap().to_string();
    let user = gUserManager.query_user_by_id(uid.to_string()).await?;
    tracing::info!("found user to update: {}, {}", uid, user.username);

    let mut update_new_values = HashMap::new();

    let mut update_success = false;
    if let Value::Object(map) = &r {
        for (key, value) in map {
            if key == KEY_USER_ID || key == KEY_HASH_PASSWORD {
                continue;
            }
            match value {
                Value::String(s) => {
                    let value = s.clone();
                    if key == KEY_USER_NAME {
                        let user = gUserManager.query_user_by_username(value.to_string()).await;
                        if let Ok(_user) = user {
                            tracing::error!(
                                "can't update username, cause there's already same one: {}",
                                value.to_string()
                            );
                            continue;
                        }
                    }

                    tracing::warn!("update, uid: {}, {} -> {}", uid, key, value.to_string());
                    gUserManager
                        .update_user(uid.clone(), key.clone(), value.clone())
                        .await?;
                    update_success = true;

                    // record
                    update_new_values.insert(key.clone(), value);
                }
                Value::Number(n) => {
                    let n = n.as_i64();
                    if let None = n {
                        continue;
                    }
                    let n = n.unwrap();
                    tracing::warn!("update, uid: {}, {} -> {}", uid, key, value.to_string());
                    gUserManager
                        .update_user(uid.clone(), key.clone(), n)
                        .await?;
                    update_success = true;

                    // record
                    update_new_values.insert(key.clone(), n.to_string());
                }
                Value::Bool(b) => {
                    tracing::warn!("update, uid: {}, {} -> {}", uid, key, value.to_string());
                    gUserManager
                        .update_user(uid.clone(), key.clone(), b)
                        .await?;
                    update_success = true;

                    // record
                    update_new_values.insert(key.clone(), b.to_string());
                }
                _ => {}
            }
        }
    }

    if update_success {
        let user = gUserManager.query_user_by_id(uid.clone()).await?;

        // record the event
        let event =
            SpvrEvent::new_update(user.uid.clone(), user.username.clone(), update_new_values);
        let _ = gSpvrEventMgr.add_event(event).await;

        Ok(Json(ok_resp(user)))
    } else {
        Err(SpvrApiError::UserUpdateFailed)
    }
}

pub async fn handle_update_avatar(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
    mut multipart: Multipart,
) -> Result<Json<RespMessage<SpvrUser>>, SpvrApiError> {
    let uid = get_str_param_or(&query, KEY_USER_ID, "")?;
    tracing::info!("update avatar, uid: {}", uid);
    let user = gUserManager.query_user_by_id(uid.clone()).await?;
    tracing::info!("found user to update avatar: {}", user.username);

    let mut upload_file_path = "".to_string();
    while let Some(mut field) = multipart
        .next_field()
        .await
        .map_err(|_e| SpvrApiError::InvalidParams)?
    {
        let key = field.name().unwrap_or("").to_string();
        let filename = field.file_name().unwrap_or("").to_string();
        tracing::info!("upload key: {} filename: {}", key, filename);
        if key == KEY_FILE {
            // copy file
            let extension = gr_base::get_extension(filename.as_str())
                .map_err(|_e| SpvrApiError::InvalidParams)?;
            let target_name = format!("{}.{}", uid, extension);
            let target_path = format!("./uploads/avatar/{}", target_name);
            tracing::info!("upload avatar file: {}", target_path);
            let mut o_file = tokio::fs::File::create(&target_path).await.unwrap();
            let mut total_size = 0;
            loop {
                match field.chunk().await {
                    Ok(Some(bytes_data)) => {
                        o_file.write_all(&bytes_data).await.unwrap();
                        total_size += bytes_data.len();
                    }
                    Ok(None) => {
                        tracing::info!("upload success: {}", total_size);
                        break;
                    }
                    Err(err) => {
                        tracing::error!("upload avatar field error: {}", err);
                        return Err(SpvrApiError::UploadFileFailed);
                    }
                }
            }
            upload_file_path = target_path;
            break;
        }
    }

    if upload_file_path.is_empty() {
        return Err(SpvrApiError::UploadFileFailed);
    }

    if upload_file_path.starts_with(".") {
        upload_file_path = upload_file_path.trim_start_matches(".").to_string();
    }

    gUserManager
        .update_user(uid.clone(), KEY_AVATAR_PATH.to_string(), upload_file_path)
        .await?;

    let user = gUserManager.query_user_by_id(uid.clone()).await?;

    Ok(Json(ok_resp(user)))
}

pub async fn handle_update_password(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    body: Body,
) -> Result<Json<RespMessage<SpvrUser>>, SpvrApiError> {
    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let uid = r[KEY_USER_ID].as_str().unwrap().to_string();
    let hash_password = r[KEY_HASH_PASSWORD].as_str().unwrap().to_string();
    let new_hash_password = r[KEY_NEW_HASH_PASSWORD].as_str().unwrap().to_string();
    let auth_id = r[KEY_AUTH_ID].as_str().unwrap_or("").to_string();
    let auth_password = r[KEY_AUTH_PASSWORD].as_str().unwrap_or("").to_string();
    if uid.is_empty() || new_hash_password.is_empty() {
        tracing::error!(
            "error params, uid:{}, password:{}, new_password:{}",
            uid,
            hash_password,
            new_hash_password
        );
        return Err(SpvrApiError::InvalidParams);
    }

    let user = gUserManager.query_user_by_id(uid.clone()).await?;
    tracing::info!("found user to update password: {}", user.username);

    tracing::info!("in auth id: {}, auth password: {}", auth_id, auth_password);
    // verify authorization
    let is_auth_ok = gAuthManager
        .lock()
        .await
        .is_auth_ok(auth_id, auth_password)
        .await;
    if is_auth_ok {
        tracing::info!("will change password by gr_auth_server");
    } else {
        tracing::info!("will change password by user itself");
        // check old password
        if hash_password != user.password {
            tracing::error!("password is not equal");
            return Err(SpvrApiError::VerifyPasswordFailed);
        }
    }

    // update new password
    gUserManager
        .update_user_password(uid.clone(), new_hash_password.clone())
        .await?;

    // record the event
    let mut update_new_values = HashMap::new();
    update_new_values.insert(KEY_PASSWORD.to_string(), new_hash_password);
    let event =
        SpvrEvent::new_update_password(user.uid.clone(), user.username.clone(), update_new_values);
    let _ = gSpvrEventMgr.add_event(event).await;

    let user = gUserManager.query_user_by_id(uid).await?;
    Ok(Json(ok_resp(user)))
}

pub async fn query_user_by_id(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<SpvrUser>>, SpvrApiError> {
    let uid = get_str_param(&query, KEY_USER_ID)?;
    let user = gUserManager.query_user_by_id(uid.clone()).await?;
    Ok(Json(ok_resp(user)))
}

pub async fn query_user_by_name(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<SpvrUser>>, SpvrApiError> {
    let username = get_str_param(&query, KEY_USER_NAME)?;
    let user = gUserManager.query_user_by_username(username).await?;
    Ok(Json(ok_resp(user)))
}

pub async fn query_users(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<SpvrUser>>>, SpvrApiError> {
    let page = get_int_param(&query, KEY_PAGE)?;
    let page_size = get_int_param(&query, KEY_PAGE_SIZE)?;
    let sort_field = get_str_param_or(&query, KEY_SORT_FIELD, "")?;
    let sort_direction = get_int_param_or(&query, KEY_SORT_DIRECTION, 0)?;
    let username = get_str_param_or(&query, KEY_USER_NAME, "")?;
    let uid = get_str_param_or(&query, KEY_USER_ID, "")?;
    let key_sort_field = if sort_field.is_empty() {
        None
    } else {
        Some(sort_field)
    };
    let key_sort_direction = if sort_direction == 0 {
        None
    } else {
        Some(sort_direction)
    };

    let total_users = gUserManager.count_users().await?;
    tracing::info!("total users: {}", total_users);

    let mut users = gUserManager
        .query_users(
            page,
            page_size,
            username,
            uid,
            key_sort_field,
            key_sort_direction,
        )
        .await?;

    for user in &mut users {
        user.total = total_users;
    }

    Ok(Json(ok_resp(users)))
}

pub async fn count_users(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    _query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<u32>>, SpvrApiError> {
    let users = gUserManager.count_users().await?;
    Ok(Json(ok_resp(users)))
}

pub async fn handle_batch_generate_random_users(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    body: Body,
) -> Result<Response, SpvrApiError> {
    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let batch_size = r[KEY_SIZE].as_i64().unwrap() as i32;
    let user_prefix = r[KEY_USER_PREFIX].as_str().unwrap().to_string();
    let users = gUserManager
        .batch_gen_random_users(batch_size, user_prefix)
        .await?;
    let mut wtr = csv::Writer::from_writer(Vec::new());

    for user in users.clone() {
        let user_adapter = SpvrUserAdapter {
            uid: user.uid.clone(),
            user_name: user.username.clone(),
            password: user.password.clone(),
            created_time: gr_base::format_readable_timestamp(user.created_timestamp),
        };
        wtr.serialize(user_adapter).map_err(|e| {
            tracing::error!("failed to serialize user: {}", e);
            SpvrApiError::InternalError
        })?;
    }

    let csv_data = wtr.into_inner().map_err(|e| {
        tracing::error!("failed to generate csv: {}", e);
        SpvrApiError::InternalError
    })?;

    let content = String::from_utf8(csv_data).map_err(|e| {
        tracing::error!("failed to convert csv as UTF-8: {}", e);
        SpvrApiError::InternalError
    })?;

    let filename = "gen_users.csv";
    Ok(build_download_response(&content, filename, "text/csv"))

    //Ok(Json(ok_resp(users)))
}

// 构建下载响应
fn build_download_response(content: &str, filename: &str, content_type: &str) -> Response {
    let disposition = format!("attachment; filename=\"{}\"", filename);
    Response::builder()
        .status(StatusCode::OK)
        .header(
            axum::http::header::CONTENT_TYPE,
            HeaderValue::from_str(content_type).unwrap(),
        )
        .header(
            axum::http::header::CONTENT_DISPOSITION,
            HeaderValue::from_str(&disposition).unwrap(),
        )
        .header(
            axum::http::header::CACHE_CONTROL,
            HeaderValue::from_static("no-cache"),
        )
        .body(axum::body::Body::from(content.to_string()))
        .unwrap()
}

pub async fn handle_batch_generate_csv_users(
    State(_context): State<Arc<Mutex<SpvrContext>>>,
    _body: Body,
) -> Result<Json<RespMessage<SpvrUser>>, SpvrApiError> {
    Ok(Json(ok_resp(SpvrUser::default())))
}
