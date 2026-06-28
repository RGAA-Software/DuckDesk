use crate::author_api_error::AuthorApiError;
use axum::body::Body;
use axum::http::Request;
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use serde::Deserialize;

#[derive(Debug, Deserialize)]
struct AuthIdQueryParams {
    auth_id: String,
}

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    if let Some(query) = req.uri().query()
        && let Ok(params) = serde_urlencoded::from_str::<AuthIdQueryParams>(query)
            && !params.auth_id.is_empty() {
                return next.run(req).await;
            }
    AuthorApiError::InvalidParams.into_response()
}
