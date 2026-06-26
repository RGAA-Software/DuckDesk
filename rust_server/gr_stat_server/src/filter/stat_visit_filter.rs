use crate::stat_api_error::StatApiError;
use axum::body::Body;
use axum::http::{Request, StatusCode};
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use serde::Deserialize;

const VISIT_TOKEN: &str = "G7pK3mR9tY5xW2vS8qL6nZ4bC1dF0jH5uA3oP8iM7cX9wE2rT4yU6iJ1hN5gB8vF3dS6kL9pM2aQ4zX7wC0vB5nK8jH3mG6fD9sA";

#[derive(Debug, Deserialize)]
struct VisitQueryParams {
    token: String,
}

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    if let Some(query) = req.uri().query() {
        return if let Ok(params) = serde_urlencoded::from_str::<VisitQueryParams>(query) {
            if params.token == VISIT_TOKEN {
                next.run(req).await
            } else {
                StatApiError::InvalidParams.into_response()
            }
        } else {
            StatApiError::InvalidParams.into_response()
        };
    }
    StatApiError::InvalidParams.into_response()
}
