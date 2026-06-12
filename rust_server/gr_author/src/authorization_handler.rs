use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use std::thread::current;
use axum::body::Body;
use axum::extract::{ConnectInfo, Query, State};
use axum::Json;
use serde_json::Value;
use tokio::sync::Mutex;
use gr_auth_mgr::app_secret_util::is_appkey_secret_paired;
use gr_base::{ok_resp, RespMessage};
use gr_base::crypto_util::{aes_decrypt, aes_encrypt};
use crate::author_api_error::AuthorApiError;
use crate::author_api_error::AuthorApiError::AppkeySecretNotPaired;
use crate::author_context::AuthorContext;
use crate::author_http_util::{get_body_int, get_body_str, get_body, get_int_param, get_str_param};
use gr_auth_mgr::authorization::{Authorization, AuthorizationVo};
use crate::author_keys::{KEY_AUTHOR_NAME, KEY_AUTHOR_TOKEN, KEY_CREATE_AUTHORIZATION_ROLE, KEY_CREATE_AUTHORIZATION_DAYS, KEY_CREATE_AUTHORIZATION_MACHINE_CODE,
KEY_CREATE_AUTHORIZATION_MAX_STREAMS, KEY_CREATE_AUTHORIZATION_USER_NAME, KEY_CREATE_AUTHORIZATION_AUTH_ID};
use crate::author_resp::AuthorLoginResp;
use crate::authorization_manager::AuthorizationError;
use crate::{gAuthorManager, gAuthorizationManager};
use crate::author_claims::AuthorClaims;

pub async fn handle_create_new_authorization(State(_context): State<Arc<Mutex<AuthorContext>>>,
                                  ConnectInfo(_addr): ConnectInfo<SocketAddr>, body: Body)
                                  -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {

    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let name = get_body_str(&r, KEY_CREATE_AUTHORIZATION_USER_NAME)?;
    let days = get_body_int(&r, KEY_CREATE_AUTHORIZATION_DAYS)? as i32;
    let max_streams = get_body_int(&r, KEY_CREATE_AUTHORIZATION_MAX_STREAMS)? as i32;
    let machine_code = get_body_str(&r, KEY_CREATE_AUTHORIZATION_MACHINE_CODE)?;
    let customer_role = get_body_int(&r, KEY_CREATE_AUTHORIZATION_ROLE)? as i32;

    tracing::info!("customer_role {}", customer_role);

    let auth = gAuthorizationManager
        .gen_new_authorization(name, machine_code, days, max_streams, customer_role).await;
    match auth {
        Ok(auth) => {
            Ok(Json(ok_resp(auth)))
        },
        Err(e) => {
            match e {
                AuthorizationError::AlreadyExist => {
                    Err(AuthorApiError::AlreadyExists)
                }
                AuthorizationError::DatabaseError => {
                    Err(AuthorApiError::DatabaseError)
                }
                _ => Err(AuthorApiError::DatabaseError)
            }
        }
    }
}

pub async fn handle_create_new_deploy_authorization(State(ctx): State<Arc<Mutex<AuthorContext>>>,
                                                    ConnectInfo(_addr): ConnectInfo<SocketAddr>, body: Body)
                                                    -> Result<Json<RespMessage<String>>, AuthorApiError> {
    let r = handle_create_new_authorization(State(ctx), ConnectInfo(_addr), body).await?;
    let auth = r.0.data;
    if let Ok(str) = auth.as_deploy_str() {
        return Ok(Json(ok_resp(str)));
    }
    Err(AuthorApiError::CantCreateAuthorization)
}

pub async fn handle_query_authorization_by_id(State(_ctx): State<Arc<Mutex<AuthorContext>>>,
                                             query: Query<HashMap<String, String>>)
                                             -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    let auth_id = query
        .get("auth_id")
        .unwrap()
        .clone();

    if let Some(auth) = gAuthorizationManager
        .query_authorization_by_id(auth_id).await {
        Ok(Json(ok_resp(auth)))
    }
    else {
        Err(AuthorApiError::AuthorizationNotFound)
    }
}

pub async fn handle_query_deploy_authorization_by_id(State(_ctx): State<Arc<Mutex<AuthorContext>>>,
                                             query: Query<HashMap<String, String>>)
                                             -> Result<Json<RespMessage<String>>, AuthorApiError> {
    let auth_id = get_str_param(&query, "auth_id")?;

    if let Some(auth) = gAuthorizationManager
        .query_authorization_by_id(auth_id).await {
        if let Ok(str ) = auth.as_deploy_str() {
            Ok(Json(ok_resp(str)))
        }
        else {
            Err(AuthorApiError::AuthorizationNotFound)
        }
    }
    else {
        Err(AuthorApiError::AuthorizationNotFound)
    }
}

pub async fn handle_query_authorization_by_name(State(_ctx): State<Arc<Mutex<AuthorContext>>>,
                                              query: Query<HashMap<String, String>>)
                                              -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    let auth_name = query
        .get("auth_name")
        .unwrap_or(&"".to_string())
        .clone();
    if auth_name.is_empty() {
        return Err(AuthorApiError::InvalidParams);
    }

    if let Some(auth) = gAuthorizationManager
        .query_authorization_by_name(auth_name).await {
        Ok(Json(ok_resp(auth)))
    }
    else {
        Err(AuthorApiError::AuthorizationNotFound)
    }
}

pub async fn handle_query_authorization_like_name(State(_ctx): State<Arc<Mutex<AuthorContext>>>,
                                                query: Query<HashMap<String, String>>)
                                                -> Result<Json<RespMessage<Vec<AuthorizationVo>>>, AuthorApiError> {
    let auth_name = get_str_param(&query, "auth_name")?;
    if auth_name.is_empty() {
        return Err(AuthorApiError::InvalidParams);
    }

    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;

    if let Ok(auth) = gAuthorizationManager
        .query_authorizations_like_name(auth_name, page, page_size).await {
        Ok(Json(ok_resp(auth)))
    }
    else {
        Err(AuthorApiError::AuthorizationNotFound)
    }
}

pub async fn handle_query_authorizations(State(_ctx): State<Arc<Mutex<AuthorContext>>>,
                                                  query: Query<HashMap<String, String>>)
                                                  -> Result<Json<RespMessage<Vec<AuthorizationVo>>>, AuthorApiError> {
    let page = get_int_param(&query, "page")?;
    let page_size = get_int_param(&query, "page_size")?;

    if let Ok(auth) = gAuthorizationManager
        .query_authorizations_no_filter(page, page_size).await {
        Ok(Json(ok_resp(auth)))
    }
    else {
        Err(AuthorApiError::AuthorizationNotFound)
    }
}

pub async fn handle_verify_appkey_secret(State(_ctx): State<Arc<Mutex<AuthorContext>>>,
                                         query: Query<HashMap<String, String>>)
                                         -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    let appkey = get_str_param(&query, "appkey")?;
    let app_secret = get_str_param(&query, "app_secret")?;
    if !is_appkey_secret_paired(appkey.clone(), app_secret.clone()) {
        return Err(AppkeySecretNotPaired)
    }

    if let Some(auth) = gAuthorizationManager
        .query_authorization_by_appkey_secret(appkey, app_secret).await {
        Ok(Json(ok_resp(auth)))
    }
    else {
        Err(AuthorApiError::AuthorizationNotFound)
    }
}



pub async fn handle_update_authorization(State(_context): State<Arc<Mutex<AuthorContext>>>,
                                             ConnectInfo(_addr): ConnectInfo<SocketAddr>, body: Body)
                                             -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {

    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(body.as_str()).unwrap();
    let auth_id = get_body_str(&r, KEY_CREATE_AUTHORIZATION_AUTH_ID)?;
    tracing::info!("Auth ID: {}", auth_id);
    let days = get_body_int(&r, KEY_CREATE_AUTHORIZATION_DAYS)? as i32;
    tracing::info!("Days: {}", days);
    let max_streams = get_body_int(&r, KEY_CREATE_AUTHORIZATION_MAX_STREAMS)? as i32;
    tracing::info!("Max streams: {}", max_streams);
    let role = get_body_int(&r, KEY_CREATE_AUTHORIZATION_ROLE)? as i32;
    tracing::info!("Role: {}", role);

    let auth = gAuthorizationManager
        .query_authorization_by_id(auth_id.clone()).await;
    if let None = auth {
        return Err(AuthorApiError::AuthorizationNotFound);
    }

    let mut auth = auth.unwrap();
    let current_ts = gr_base::get_current_timestamp();
    auth.days = days;
    auth.role = role;
    auth.max_streams = max_streams;
    auth.created_timestamp_ms = current_ts;
    auth.end_timestamp_ms = current_ts + (days as i64) * 24 * 3600 * 1000;
    auth.last_modify_timestamp = current_ts;
    let r = gAuthorizationManager
        .update_authorization(auth_id.clone(), auth).await;
    if r {
        let auth = gAuthorizationManager
            .query_authorization_by_id(auth_id).await
            .unwrap();
        Ok(Json(ok_resp(auth)))
    }
    else {
        Err(AuthorApiError::UpdateAuthFailed)
    }
}