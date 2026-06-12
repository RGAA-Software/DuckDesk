use crate::author_api_error::AuthorApiError;
use crate::gAuthorManager;
use axum::body::Body;
use axum::http::Request;
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use serde::Deserialize;

#[derive(Debug, Deserialize)]
pub struct AuthorQueryParams {
    pub author_name: String,
    pub author_token: String,
}

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    if let Some(query) = req.uri().query() {
        if let Ok(params) = serde_urlencoded::from_str::<AuthorQueryParams>(query) {
            return if let Some(_author) = gAuthorManager
                .verify_author(params.author_name, params.author_token).await {
                next.run(req).await
            } else {
                AuthorApiError::InvalidPassword.into_response()
            }
        }
    }
    AuthorApiError::InvalidParams.into_response()
}