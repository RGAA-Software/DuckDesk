use crate::author_api_error::AuthorApiError;
use axum::body::Body;
use axum::http::Request;
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use serde::Deserialize;

const MAX_PAGE_SIZE: i32 = 100;

#[derive(Debug, Deserialize)]
struct PageSizeQueryParams {
    page: i32,
    page_size: i32,
}

fn is_valid_page_size(params: &PageSizeQueryParams) -> bool {
    params.page > 0 && params.page_size > 0 && params.page_size <= MAX_PAGE_SIZE
}

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    if let Some(query) = req.uri().query() {
        if let Ok(params) = serde_urlencoded::from_str::<PageSizeQueryParams>(query) {
            if is_valid_page_size(&params) {
                return next.run(req).await;
            }
        }
    }
    tracing::error!("don't have page / page_size");
    AuthorApiError::InvalidPageSize.into_response()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validates_positive_page_and_bounded_page_size() {
        assert!(is_valid_page_size(&PageSizeQueryParams {
            page: 1,
            page_size: 20,
        }));

        assert!(is_valid_page_size(&PageSizeQueryParams {
            page: 1,
            page_size: MAX_PAGE_SIZE,
        }));
    }

    #[test]
    fn rejects_zero_and_negative_values() {
        assert!(!is_valid_page_size(&PageSizeQueryParams {
            page: 0,
            page_size: 20,
        }));
        assert!(!is_valid_page_size(&PageSizeQueryParams {
            page: -1,
            page_size: 20,
        }));
        assert!(!is_valid_page_size(&PageSizeQueryParams {
            page: 1,
            page_size: 0,
        }));
        assert!(!is_valid_page_size(&PageSizeQueryParams {
            page: 1,
            page_size: -1,
        }));
    }

    #[test]
    fn rejects_page_size_above_limit() {
        assert!(!is_valid_page_size(&PageSizeQueryParams {
            page: 1,
            page_size: MAX_PAGE_SIZE + 1,
        }));
    }

    #[test]
    fn rejects_missing_or_non_numeric_query_fields() {
        assert!(serde_urlencoded::from_str::<PageSizeQueryParams>("page=1").is_err());
        assert!(serde_urlencoded::from_str::<PageSizeQueryParams>("page=1&page_size=abc").is_err());
    }
}
