use crate::author::Author;
use crate::author_api_error::AuthorApiError;
use crate::author_claims::AuthorClaims;
use crate::author_http_util::{get_body, get_body_str};
use crate::author_keys::{KEY_AUTHOR_NAME, KEY_AUTHOR_TOKEN};
use crate::author_resp::{AuthorLogOutResp, AuthorLoginResp, AuthorMeResp};
use crate::gAuthorManager;
use axum::Extension;
use axum::Json;
use axum::body::Body;
use gr_base::{RespMessage, get_current_timestamp, ok_resp};
use serde_json::Value;

pub async fn handle_ping() -> Json<RespMessage<String>> {
    Json(RespMessage::<String> {
        code: 200,
        message: "ok".to_string(),
        timestamp: get_current_timestamp(),
        data: "Pong".to_string(),
    })
}

pub async fn handle_verify_author(
    body: Body,
) -> Result<Json<RespMessage<AuthorLoginResp>>, AuthorApiError> {
    let body = get_body(body).await?;
    let r: Value =
        serde_json::from_str(body.as_str()).map_err(|_| AuthorApiError::InvalidParams)?;
    let author_name = get_body_str(&r, KEY_AUTHOR_NAME)?;
    let author_token = get_body_str(&r, KEY_AUTHOR_TOKEN)?;
    let jwt_name = author_name.clone();
    tracing::info!(
        "gr_auth_server login requested, author_name={}",
        author_name
    );
    if let Some(author) = gAuthorManager
        .verify_author(author_name, author_token)
        .await
    {
        //tracing::info!("gr_auth_server _author: {:#?}", _author);

        let claims = AuthorClaims::new(jwt_name, author.role, 3600); // 1小时有效期
        let login_token = claims
            .generate_token()
            .map_err(|_| AuthorApiError::DatabaseError)?;
        Ok(Json(ok_resp(AuthorLoginResp { token: login_token })))
    } else {
        Err(AuthorApiError::InvalidPassword)
    }
}

pub async fn handle_query_authors(
) -> Result<Json<RespMessage<Vec<Author>>>, AuthorApiError> {
    let authors = gAuthorManager.find_authors().await;
    if let Err(e) = authors {
        tracing::error!("find authors failed: {}", e);
        return Err(AuthorApiError::NoAuthorsFound);
    }
    let authors = authors.unwrap();
    Ok(Json(ok_resp(authors)))
}

pub async fn handle_me(
    Extension(claims): Extension<AuthorClaims>,
) -> Result<Json<RespMessage<AuthorMeResp>>, AuthorApiError> {
    Ok(Json(ok_resp(AuthorMeResp {
        name: claims.sub,
        role: claims.role,
    })))
}

pub async fn handle_log_out(
    Extension(claims): Extension<AuthorClaims>,
    _body: Body,
) -> Result<Json<RespMessage<AuthorLogOutResp>>, AuthorApiError> {
    claims.logout();
    Ok(Json(ok_resp(AuthorLogOutResp {
        message: "logout success".to_string(),
    })))
}
