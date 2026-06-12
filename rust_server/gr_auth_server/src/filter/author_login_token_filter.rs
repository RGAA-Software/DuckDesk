use crate::author_api_error::AuthorApiError;
use crate::gAuthorManager;
use axum::{http::HeaderMap};
use axum::body::Body;
use axum::http::Request;
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use serde::Deserialize;

use crate::author_claims::AuthorClaims;

#[derive(Debug, Deserialize)]
pub struct AuthorQueryParams {
    pub author_name: String,
    pub author_token: String,
}

pub async fn filter(headers: HeaderMap, req: Request<Body>, next: Next) -> Response {
    let login_token = match headers.get("Authorization")
        .and_then(|v| v.to_str().ok())
    {
        Some(t) => t,
        None => {
            return AuthorApiError::InvalidLoginToken.into_response()
        }
    };

    match AuthorClaims::verify(login_token) {
        Ok(data) => next.run(req).await,
        Err(_) => AuthorApiError::InvalidLoginToken.into_response(),
    }
}
