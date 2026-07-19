use crate::issue::off_issue::OffIssue;
use crate::issue::off_issue_keys::{
    KEY_ISSUE_DESC, KEY_ISSUE_OS, KEY_ISSUE_TITLE, KEY_ISSUE_VERSION,
};
use crate::off_admin_handle::check_admin_token;
use crate::off_api_error::OffApiError;
use crate::off_api_keys::{
    KEY_DESC, KEY_EMAIL, KEY_ITEM_ID, KEY_PROCESSED, KEY_QQ,
    KEY_TITLE, KEY_VERSION, KEY_WECHAT, KEY_YOUR_NAME,
};
use crate::off_context::OffContext;
use crate::off_http_utils::{get_body, get_int_param, get_int_param_or};
use crate::{gOffDatabase, gOffIssueManager};
use axum::body::Body;
use axum::extract::{Query, State};
use axum::http::HeaderMap;
use axum::Json;
use gr_base::{ok_resp, RespMessage};
use mongodb::bson::doc;
use mongodb::bson::oid::ObjectId;
use serde_json::Value;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn create_new_issue(
    State(_ctx): State<Arc<Mutex<OffContext>>>,
    _query: Query<HashMap<String, String>>,
    b: Body,
) -> Result<Json<RespMessage<OffIssue>>, OffApiError> {
    let body = get_body(b).await?;
    let r: Value = serde_json::from_str(body.as_str()).map_err(|_| OffApiError::InvalidParams)?;
    let issue = OffIssue {
        item_id: ObjectId::new().to_string(),
        title: r[KEY_ISSUE_TITLE].as_str().unwrap_or("").to_string(),
        your_name: r[KEY_YOUR_NAME].as_str().unwrap_or("").to_string(),
        desc: r[KEY_ISSUE_DESC].as_str().unwrap_or("").to_string(),
        version: r[KEY_ISSUE_VERSION].as_str().unwrap_or("").to_string(),
        os: r[KEY_ISSUE_OS].as_str().unwrap_or("").to_string(),
        email: r[KEY_EMAIL].as_str().unwrap().to_string(),
        wechat: r[KEY_WECHAT].as_str().unwrap().to_string(),
        qq: r[KEY_QQ].as_str().unwrap().to_string(),
        created_ts: gr_base::get_current_timestamp(),
        created_ts_readable: gr_base::get_current_readable_timestamp(),
        processed: false,
    };
    if issue.desc.is_empty() {
        return Err(OffApiError::NeedDescParam);
    }

    let c_issue = gOffDatabase.lock().await.issue().await;

    let r = c_issue
        .lock()
        .await
        .find_one(doc! {
            KEY_TITLE: issue.title.clone(),
            KEY_DESC: issue.desc.clone(),
            KEY_VERSION: issue.version.clone(),
        })
        .await;
    if let Err(e) = r {
        tracing::error!("create consult error: {:?}", e);
        return Err(OffApiError::DatabaseError);
    }

    if let Some(existing) = r.unwrap() {
        Ok(Json(ok_resp(existing)))
    } else {
        let r = gOffIssueManager.lock().await.insert_issue(issue).await?;
        Ok(Json(ok_resp(r)))
    }
}

pub async fn query_issues(
    State(_ctx): State<Arc<Mutex<OffContext>>>,
    headers: HeaderMap,
    query: Query<HashMap<String, String>>,
    _body: Body,
) -> Result<Json<RespMessage<Vec<OffIssue>>>, OffApiError> {
    check_admin_token(&headers)?;
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;
    let sort_time = get_int_param_or(&query, "sort_time", -1)?;
    // 可选 processed 过滤（0/1）
    let mut filters: HashMap<String, bool> = HashMap::new();
    if let Some(p) = query.get(KEY_PROCESSED) {
        if let Ok(v) = p.parse::<i32>() {
            filters.insert(KEY_PROCESSED.to_string(), v == 1);
        }
    }
    let r = gOffIssueManager
        .lock()
        .await
        .query_issues::<bool>(
            page,
            page_size,
            filters,
            Some(String::from("created_ts")),
            Some(sort_time),
        )
        .await?;
    Ok(Json(ok_resp(r)))
}

pub async fn mark_issue_processed(
    State(_ctx): State<Arc<Mutex<OffContext>>>,
    headers: HeaderMap,
    body: Body,
) -> Result<Json<RespMessage<String>>, OffApiError> {
    check_admin_token(&headers)?;
    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(body.as_str()).map_err(|_| OffApiError::InvalidParams)?;
    let cid = r[KEY_ITEM_ID].as_str().unwrap().to_string();
    let p = r[KEY_PROCESSED].as_bool().unwrap();
    gOffIssueManager.lock().await.mark_processed(cid, p).await?;
    Ok(Json(ok_resp("".to_string())))
}
