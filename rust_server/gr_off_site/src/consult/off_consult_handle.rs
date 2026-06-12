use std::collections::HashMap;
use std::sync::Arc;
use axum::body::Body;
use axum::extract::{Query, State};
use axum::Json;
use mongodb::bson::doc;
use mongodb::bson::oid::ObjectId;
use serde_json::Value;
use tokio::sync::Mutex;
use base::{ok_resp, RespMessage, RespStringMap};
use crate::consult::off_consult::OffConsult;
use crate::{gOffConsultManager, gOffDatabase};
use crate::off_api_error::OffApiError;
use crate::off_api_keys::{KEY_CONSULT_TYPE, KEY_CONTENT, KEY_EMAIL, KEY_ITEM_ID, KEY_PROCESSED, KEY_QQ, KEY_TITLE, KEY_WECHAT, KEY_YOUR_NAME};
use crate::off_context::OffContext;
use crate::off_http_utils::{get_body, get_int_param, get_int_param_or, get_str_param};

pub async fn create_new_consult(State(_ctx): State<Arc<Mutex<OffContext>>>,
                                body: Body)
                                -> Result<Json<RespMessage<OffConsult>>, OffApiError> {
    let body = get_body(body).await?;
    tracing::info!("body: {}", body);
    let mut consult = OffConsult::default();
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    consult.item_id = ObjectId::new().to_string();
    consult.title = r[KEY_TITLE].as_str().unwrap().to_string();
    consult.your_name = r[KEY_YOUR_NAME].as_str().unwrap().to_string();
    consult.consult_type = r[KEY_CONSULT_TYPE].as_str().unwrap().to_string();
    consult.content = r[KEY_CONTENT].as_str().unwrap().to_string();
    consult.email = r[KEY_EMAIL].as_str().unwrap().to_string();
    consult.wechat = r[KEY_WECHAT].as_str().unwrap().to_string();
    consult.qq = r[KEY_QQ].as_str().unwrap().to_string();
    consult.created_ts = base::get_current_timestamp();
    consult.created_ts_readable = base::get_current_readable_timestamp();
    consult.updated_ts = base::get_current_timestamp();
    consult.updated_ts_readable = base::get_current_readable_timestamp();
    consult.processed = false;
    if consult.content.is_empty() || consult.consult_type.is_empty() {
        return Err(OffApiError::NeedDescParam);
    }
    let c_consult = gOffDatabase
        .lock().await
        .consult().await;

    let r = c_consult
        .lock().await
        .find_one(doc!{
            KEY_TITLE: consult.title.clone(),
            KEY_YOUR_NAME: consult.your_name.clone(),
            KEY_CONSULT_TYPE: consult.consult_type.clone(),
            KEY_CONTENT: consult.content.clone(),
        }).await;
    if let Err(e) = r {
        tracing::error!("create consult error: {:?}", e);
        return Err(OffApiError::DatabaseError);
    }

    let r = r.unwrap();
    if let Some(r) = r {
        Ok(Json(ok_resp(r)))
    }
    else {
        let r = c_consult
            .lock().await
            .insert_one(consult.clone()).await;
        if let Err(e) = r {
            tracing::error!("insert one error: {:?}", e);
            return Err(OffApiError::DatabaseError);
        }
        Ok(Json(ok_resp(consult)))
    }
}

pub async fn query_consults(State(_ctx): State<Arc<Mutex<OffContext>>>,
                              query: Query<HashMap<String, String>>)
                              -> Result<Json<RespMessage<Vec<OffConsult>>>, OffApiError> {
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;
    let sort_time = get_int_param_or(&query, "sort_time", -1)?;
    let consults = gOffConsultManager
        .lock().await
        .query_consults::<String>(page, page_size, HashMap::default(), Some(String::from("created_ts")), Some(sort_time)).await?;
    Ok(Json(ok_resp(consults)))
}

pub async fn mark_consult_processed(State(_ctx): State<Arc<Mutex<OffContext>>>,
                                    body: Body)
                                    -> Result<Json<RespMessage<String>>, OffApiError> {
    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let cid = r[KEY_ITEM_ID].as_str().unwrap().to_string();
    let p = r[KEY_PROCESSED].as_bool().unwrap();
    let r = gOffConsultManager
        .lock().await
        .mark_processed(cid, p).await?;
    Ok(Json(ok_resp("".to_string())))
}