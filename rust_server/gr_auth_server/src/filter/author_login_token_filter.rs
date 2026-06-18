use crate::author_api_error::AuthorApiError;
use axum::{http::HeaderMap};
use axum::body::Body;
use axum::http::Request;
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};

use crate::author_claims::AuthorClaims;

pub fn verify_headers(headers: &HeaderMap) -> Result<AuthorClaims, AuthorApiError> {
    let login_token = match headers.get("Authorization")
        .and_then(|v| v.to_str().ok())
    {
        Some(t) => t,
        None => {
            return Err(AuthorApiError::MissLoginToken)
        }
    };

    match AuthorClaims::verify(login_token) {
        Ok(data) => Ok(data.claims),
        Err(_) => Err(AuthorApiError::InvalidLoginToken),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::author::AuthorRole;
    use axum::http::HeaderValue;

    #[test]
    fn verify_headers_rejects_missing_authorization() {
        let headers = HeaderMap::new();

        let err = verify_headers(&headers).expect_err("missing token should fail");

        assert_eq!(err.business_code(), AuthorApiError::MissLoginToken.business_code());
    }

    #[test]
    fn verify_headers_rejects_invalid_token() {
        let mut headers = HeaderMap::new();
        headers.insert("Authorization", HeaderValue::from_static("invalid-token"));

        let err = verify_headers(&headers).expect_err("invalid token should fail");

        assert_eq!(err.business_code(), AuthorApiError::InvalidLoginToken.business_code());
    }

    #[test]
    fn verify_headers_accepts_valid_token() {
        let claims = AuthorClaims::new(
            "Admin".to_string(),
            AuthorRole::Admin,
            3600,
        );
        let token = claims.generate_token().expect("token should encode");
        let mut headers = HeaderMap::new();
        headers.insert("Authorization", HeaderValue::from_str(&token).unwrap());

        let verified = verify_headers(&headers).expect("valid token should pass");

        assert_eq!(verified.sub, "Admin");
        assert_eq!(verified.role, AuthorRole::Admin);
    }
}

pub async fn filter(headers: HeaderMap, mut req: Request<Body>, next: Next) -> Response {
    match verify_headers(&headers) {
        Ok(claims) => {
            req.extensions_mut().insert(claims);
            next.run(req).await
        },
        Err(err) => err.into_response(),
    }
}
