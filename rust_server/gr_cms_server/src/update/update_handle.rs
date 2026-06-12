use std::collections::HashMap;
use std::sync::Arc;
use axum::body::Body;
use axum::extract::{Multipart, Query, State};
use axum::http::{header, HeaderMap, HeaderValue};
use axum::Json;
use axum::response::IntoResponse;
use serde_json::Value;
use md5;
use tokio::io::AsyncWriteExt;
use tokio::sync::Mutex;
use tokio::fs::File;
use tokio_util::io::ReaderStream;
use gr_base::{ok_resp, RespMessage, RespStringMap};
use crate::gUpdateInfoManager;
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use crate::spvr_http_util::{get_int_param, get_int_param_or};
use crate::update::update_info::UpdateInfo;
use crate::update::update_keys::{KEY_UPDATE_DESC, KEY_UPDATE_FORCED, KEY_UPDATE_INSTALL_PACKAGE, KEY_UPDATE_VERSION};

const SAVE_INSTALL_PACKAGE_DIR: &str = "./uploads/update_info/";
const RESP_INSTALL_PACKAGE_DIR: &str = "/uploads/update_info/";

pub async fn handle_hello_world(State(_ctx): State<Arc<Mutex<SpvrContext>>>,
                                query: Query<HashMap<String, String>>,
                                body: Body)
                                -> Result<Json<RespMessage<String>>, SpvrApiError>
{
    Ok(Json(ok_resp("hello world".to_string())))
}

pub async fn handle_upload_update_info(State(_ctx): State<Arc<Mutex<SpvrContext>>>,
                                       query: Query<HashMap<String, String>>,
                                       mut multipart: Multipart)
                                       -> Result<Json<RespMessage<String>>, SpvrApiError>
{
    let mut update_info = UpdateInfo::default();
    let mut upload_file_path = "".to_string();
    while let Some(mut field) = multipart.next_field().await.map_err(|e| {SpvrApiError::InvalidParams})? {
        let key = field.name().unwrap_or("").to_string();
        if key == KEY_UPDATE_INSTALL_PACKAGE {
            let filename = field.file_name().unwrap_or("").to_string();
            tracing::info!("upload key: {} filename: {}", key, filename);
            // copy file
            let target_path = format!("{}{}", SAVE_INSTALL_PACKAGE_DIR, filename);
            let resp_path = format!("{}{}", RESP_INSTALL_PACKAGE_DIR, filename);
            update_info.down_addr = resp_path;
            update_info.file_name = filename;
            tracing::info!("upload target_path file: {}", target_path);
            let mut o_file = tokio::fs::File::create(&target_path).await.unwrap();
            let mut total_size = 0;
            let mut md5_ctx = md5::Context::new();
            loop {
                match field.chunk().await {
                    Ok(Some(bytes_data)) => {
                        o_file.write_all(&bytes_data).await.unwrap();
                        md5_ctx.consume(&bytes_data);
                        total_size += bytes_data.len();
                    }
                    Ok(None) => {
                        let md5_digest = md5_ctx.finalize();
                        let md5_hex = format!("{:x}", md5_digest);
                        tracing::info!("upload success: {}, file_md5: {}", total_size, md5_hex);
                        update_info.file_md5 = md5_hex;
                        update_info.file_size = total_size as i64;
                        break;
                    }
                    Err(err) => {
                        tracing::error!("upload avatar field error: {}", err);
                        return Err(SpvrApiError::UploadFileFailed);
                    }
                }
            }
            upload_file_path = target_path;
        }
        else if key == KEY_UPDATE_FORCED {
            update_info.forced = field.text().await.unwrap_or_default().to_lowercase() == "true";
        } else if key == KEY_UPDATE_VERSION {
            update_info.version = field.text().await.unwrap_or_default();
            tracing::info!("update version: {}", update_info.version);
        } else if key == KEY_UPDATE_DESC {
            update_info.desc = field.text().await.unwrap_or_default();
            tracing::info!("update desc: {}", update_info.desc);
        }
    }

    update_info.created_timestamp = gr_base::get_current_timestamp();

    let _info = gUpdateInfoManager
        .insert_update_info(update_info).await?;

    if upload_file_path.is_empty() {
        return Err(SpvrApiError::UploadFileFailed);
    }

    Ok(Json(ok_resp("upload ok".to_string())))
}

pub async fn handle_query_update_info(State(_ctx): State<Arc<Mutex<SpvrContext>>>,
                          query: Query<HashMap<String, String>>,
                          body: Body)
                          -> Result<Json<RespMessage<Vec<UpdateInfo>>>, SpvrApiError> {
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;
    let sort_time = get_int_param_or(&query, "sort_time", -1)?;
    let r = gUpdateInfoManager
        .query_info::<String>(page, page_size, HashMap::default(), Some(String::from("created_timestamp")), Some(sort_time)).await?;
    Ok(Json(ok_resp(r)))
}

pub async fn handle_download_install_package(State(_ctx): State<Arc<Mutex<SpvrContext>>>, query: Query<HashMap<String, String>>) -> impl IntoResponse {

    let filename = match query.get("down_file") {
        Some(v) => v.clone(),
        None => return (axum::http::StatusCode::BAD_REQUEST, "missing down_file").into_response(),
    };

    let filepath = format!("{}{}", SAVE_INSTALL_PACKAGE_DIR, filename);

    tracing::info!("download file path: {}", filepath);

    let file = match File::open(&filepath).await {
        Ok(f) => f,
        Err(_) => return (axum::http::StatusCode::NOT_FOUND, "File Not Found").into_response(),
    };
    
    let metadata = match file.metadata().await {
        Ok(m) => m,
        Err(_) => {
            return (axum::http::StatusCode::INTERNAL_SERVER_ERROR, "cannot read file metadata")
                .into_response();
        }
    };
    let file_size = metadata.len();

    // 转成 StreamBody（不会一次性把文件读到内存）
    let stream = ReaderStream::new(file);
    let body = Body::from_stream(stream);
    
    // 创建 HeaderMap
    let mut headers = HeaderMap::new();
    headers.insert(header::CONTENT_TYPE, HeaderValue::from_static("application/octet-stream"));
    headers.insert(
        header::CONTENT_LENGTH,
        HeaderValue::from_str(&file_size.to_string()).unwrap(),
    );
    headers.insert(
        header::CONTENT_DISPOSITION,
        HeaderValue::from_str(&format!("attachment; filename=\"{}\"", filename)).unwrap(),
    );

    (headers, body).into_response()
}
