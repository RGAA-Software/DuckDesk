use std::net::SocketAddr;
use std::sync::Arc;
use axum::body::Body;
use axum::extract::{ConnectInfo, State};
use axum::Extension;
use axum::Json;
use serde_json::Value;
use tokio::sync::Mutex;
use gr_base::{get_current_timestamp, ok_resp, RespMessage};
use crate::author::Author;
use crate::author_api_error::AuthorApiError;
use crate::author_context::AuthorContext;
use crate::author_http_util::{get_body_str, get_body};
use crate::author_keys::{KEY_AUTHOR_NAME, KEY_AUTHOR_TOKEN};
use crate::gAuthorManager;
use crate::author_claims::{AuthorClaims};
use crate::author_resp::{AuthorLoginResp, AuthorLogOutResp, AuthorMeResp};

pub async fn handle_ping(State(_ctx): State<Arc<Mutex<AuthorContext>>>) -> Json<RespMessage<String>> {
    Json(RespMessage::<String> {
        code: 200,
        message: "ok".to_string(),
        timestamp: get_current_timestamp(),
        data: "Pong".to_string(),
    })
}

pub async fn handle_verify_author(State(_context): State<Arc<Mutex<AuthorContext>>>,
                                  ConnectInfo(_addr): ConnectInfo<SocketAddr>, body: Body)
                                  -> Result<Json<RespMessage<AuthorLoginResp>>, AuthorApiError> {

    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(body.as_str())
        .map_err(|_| AuthorApiError::InvalidParams)?;
    let author_name = get_body_str(&r, KEY_AUTHOR_NAME)?;
    let author_token = get_body_str(&r, KEY_AUTHOR_TOKEN)?;
    let jwt_name = author_name.clone();
    tracing::info!("gr_auth_server login requested, author_name={}", author_name);
    if let Some(author) = gAuthorManager
        .verify_author(author_name, author_token).await {
        //tracing::info!("gr_auth_server _author: {:#?}", _author);

        let claims = AuthorClaims::new(jwt_name, author.permission, 3600); // 1小时有效期
        let login_token = claims
            .generate_token()
            .map_err(|_| AuthorApiError::DatabaseError)?;
        Ok(Json(ok_resp(AuthorLoginResp {
            token: login_token,
        })))
    } else {
        Err(AuthorApiError::InvalidPassword)
    }
}

pub async fn handle_query_authors(State(_context): State<Arc<Mutex<AuthorContext>>>,
                                  ConnectInfo(_addr): ConnectInfo<SocketAddr>,)
                                  -> Result<Json<RespMessage<Vec<Author>>>, AuthorApiError> {
    let authors = gAuthorManager
        .find_authors().await;
    if let Err(e) = authors {
        tracing::error!("find authors failed: {}", e);
        return Err(AuthorApiError::NoAuthorsFound);
    }
    let authors = authors.unwrap();
    Ok(Json(ok_resp(authors)))
}

pub async fn handle_me(Extension(claims): Extension<AuthorClaims>)
                       -> Result<Json<RespMessage<AuthorMeResp>>, AuthorApiError> {
    Ok(Json(ok_resp(AuthorMeResp {
        name: claims.sub,
        permission: claims.permission,
    })))
}

pub async fn handle_log_out(State(_context): State<Arc<Mutex<AuthorContext>>>,
                                             ConnectInfo(_addr): ConnectInfo<SocketAddr>, _body: Body)
                                             -> Result<Json<RespMessage<AuthorLogOutResp>>, AuthorApiError> {
    AuthorClaims::logout().await;
    Ok(Json(ok_resp(AuthorLogOutResp {
        message: "logout success".to_string(),
    })))
}
