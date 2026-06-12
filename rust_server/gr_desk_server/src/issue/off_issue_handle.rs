use std::collections::HashMap;
use std::sync::Arc;
use axum::body::Body;
use axum::extract::{Query, State};
use axum::Json;
use mongodb::bson::doc;
use mongodb::bson::oid::ObjectId;
use serde_json::Value;
use tokio::sync::Mutex;
use gr_base::{ok_resp, RespMessage, RespStringMap};
use crate::{gOffConsultManager, gOffDatabase, gOffIssueManager};
use crate::issue::off_issue::OffIssue;
use crate::issue::off_issue_keys::{KEY_ISSUE_DESC, KEY_ISSUE_OS, KEY_ISSUE_TITLE, KEY_ISSUE_VERSION};
use crate::off_api_error::OffApiError;
use crate::off_api_keys::{KEY_CONSULT_TYPE, KEY_CONTENT, KEY_DESC, KEY_EMAIL, KEY_ITEM_ID, KEY_PROCESSED, KEY_QQ, KEY_TITLE, KEY_VERSION, KEY_WECHAT, KEY_YOUR_NAME};
use crate::off_context::OffContext;
use crate::off_http_utils::{get_body, get_int_param, get_int_param_or, get_str_param};

pub async fn create_new_issue(State(_ctx): State<Arc<Mutex<OffContext>>>,
                                query: Query<HashMap<String, String>>,
                                b: Body)
                                -> Result<Json<RespMessage<OffIssue>>, OffApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let mut issue = OffIssue::default();
    issue.item_id = ObjectId::new().to_string();
    issue.title = r[KEY_ISSUE_TITLE].as_str().unwrap_or("").to_string();
    issue.your_name = r[KEY_YOUR_NAME].as_str().unwrap_or("").to_string();
    issue.desc = r[KEY_ISSUE_DESC].as_str().unwrap_or("").to_string();
    issue.version = r[KEY_ISSUE_VERSION].as_str().unwrap_or("").to_string();
    issue.os = r[KEY_ISSUE_OS].as_str().unwrap_or("").to_string();
    issue.email = r[KEY_EMAIL].as_str().unwrap().to_string();
    issue.wechat = r[KEY_WECHAT].as_str().unwrap().to_string();
    issue.qq = r[KEY_QQ].as_str().unwrap().to_string();
    issue.created_ts = gr_base::get_current_timestamp();
    issue.created_ts_readable = gr_base::get_current_readable_timestamp();
    issue.processed = false;
    if issue.desc.is_empty() {
        return Err(OffApiError::NeedDescParam);
    }

    let c_issue = gOffDatabase
        .lock().await
        .issue().await;

    let r = c_issue
        .lock().await
        .find_one(doc!{
            KEY_TITLE: issue.title.clone(),
            KEY_DESC: issue.desc.clone(),
            KEY_VERSION: issue.version.clone(),
        }).await;
    if let Err(e) = r {
        tracing::error!("create consult error: {:?}", e);
        return Err(OffApiError::DatabaseError);
    }

    let r = r.unwrap();
    if r.is_some() {
        Ok(Json(ok_resp(r.unwrap())))
    }
    else {
        let r = gOffIssueManager
            .lock().await
            .insert_issue(issue).await?;
        Ok(Json(ok_resp(r)))
    }
}

pub async fn query_issues(State(_ctx): State<Arc<Mutex<OffContext>>>,
                              query: Query<HashMap<String, String>>,
                              body: Body)
                              -> Result<Json<RespMessage<Vec<OffIssue>>>, OffApiError> {
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;
    let sort_time = get_int_param_or(&query, "sort_time", -1)?;
    let r = gOffIssueManager
        .lock().await
        .query_issues::<String>(page, page_size, HashMap::default(), Some(String::from("created_ts")), Some(sort_time)).await?;
    Ok(Json(ok_resp(r)))
}

pub async fn mark_issue_processed(State(_ctx): State<Arc<Mutex<OffContext>>>,
                                    body: Body)
                                    -> Result<Json<RespMessage<String>>, OffApiError> {
    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let cid = r[KEY_ITEM_ID].as_str().unwrap().to_string();
    let p = r[KEY_PROCESSED].as_bool().unwrap();
    let r = gOffIssueManager
        .lock().await
        .mark_processed(cid, p).await?;
    Ok(Json(ok_resp("".to_string())))
}