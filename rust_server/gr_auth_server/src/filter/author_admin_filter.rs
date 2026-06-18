use crate::author_api_error::AuthorApiError;
use crate::author::AuthorRole;
use crate::author_claims::AuthorClaims;
use crate::filter::author_login_token_filter::verify_headers;
use axum::body::Body;
use axum::http::{HeaderMap, Request};
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};

pub fn is_admin_claims(claims: &AuthorClaims) -> bool {
    claims.role == AuthorRole::Admin
}

pub async fn filter(headers: HeaderMap, mut req: Request<Body>, next: Next) -> Response {
    let claims = match req.extensions().get::<AuthorClaims>() {
        Some(claims) => claims.clone(),
        None => match verify_headers(&headers) {
            Ok(claims) => claims,
            Err(err) => return err.into_response(),
        }
    };

    if is_admin_claims(&claims) {
        req.extensions_mut().insert(claims);
        next.run(req).await
    } else {
        AuthorApiError::MustBeAdministrator.into_response()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn admin_claims_are_allowed() {
        let claims = AuthorClaims::new(
            "Admin".to_string(),
            AuthorRole::Admin,
            3600,
        );

        assert!(is_admin_claims(&claims));
    }

    #[test]
    fn visitor_claims_are_rejected() {
        let claims = AuthorClaims::new(
            "Visitor".to_string(),
            AuthorRole::Visitor,
            3600,
        );

        assert!(!is_admin_claims(&claims));
    }
}
