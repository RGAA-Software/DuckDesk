use axum::body::Body;
use axum::http::{Request};
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use serde::Deserialize;
use crate::author_api_error::AuthorApiError;

#[derive(Debug, Deserialize)]
struct PageSizeQueryParams {
    page: i32,
    page_size: i32,
}

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    if let Some(query) = req.uri().query() {
        if let Ok(params) = serde_urlencoded::from_str::<PageSizeQueryParams>(query) {
            if params.page > 0 && params.page_size > 0 {
                return next.run(req).await;
            }
        }
    }
    tracing::error!("don't have page / page_size");
    AuthorApiError::InvalidPageSize.into_response()
}