use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::connection_ticket::handler::{issue_device_ticket, issue_instance_ticket};
use crate::gUserSessionManager;
use crate::identity::resource_handler::{
    list_user_apps, list_user_apps_page, list_user_instances, list_user_instances_page,
    start_user_app, stop_user_instance, user_resource_summary,
};
use crate::identity::user_handler::logout_all;
use crate::user::session_handler::{
    admin_login, admin_logout, admin_me, change_password, cookie_value, guest_session, login,
    logout, me, refresh_user_csrf, register_user, update_avatar, update_profile,
    ADMIN_SESSION_COOKIE, GUEST_SESSION_COOKIE, USER_SESSION_COOKIE,
};
use crate::user_device::cms_user_device_handler::{
    handle_query_my_devices, handle_query_my_devices_page,
};
use axum::body::Body;
use axum::extract::DefaultBodyLimit;
use axum::http::{header, Request};
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use axum::routing::{get, patch, post, put};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn require_user(mut request: Request<Body>, next: Next) -> Response {
    let bearer = request
        .headers()
        .get(header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
        .map(str::trim)
        .filter(|value| !value.is_empty());
    let authenticated = if let Some(token) = bearer {
        gUserSessionManager.authenticate(token).await
    } else if let Some(token) = cookie_value(request.headers(), USER_SESSION_COOKIE) {
        gUserSessionManager.authenticate_user_web(&token).await
    } else {
        return CmsApiError::AuthenticationRequired.into_response();
    };
    match authenticated {
        Ok(subject) => {
            request.extensions_mut().insert(subject);
            next.run(request).await
        }
        Err(error) => error.into_response(),
    }
}

pub async fn require_active_user(mut request: Request<Body>, next: Next) -> Response {
    let bearer = request
        .headers()
        .get(header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
        .map(str::trim)
        .filter(|value| !value.is_empty());
    let authenticated = if let Some(token) = bearer {
        gUserSessionManager.authenticate(token).await
    } else if let Some(token) = cookie_value(request.headers(), USER_SESSION_COOKIE) {
        gUserSessionManager.authenticate_user_web(&token).await
    } else {
        return CmsApiError::AuthenticationRequired.into_response();
    };
    match authenticated {
        Ok(subject) if subject.must_change_password => CmsApiError::Forbidden.into_response(),
        Ok(subject) => {
            request.extensions_mut().insert(subject);
            next.run(request).await
        }
        Err(error) => error.into_response(),
    }
}

pub async fn require_user_write(mut request: Request<Body>, next: Next) -> Response {
    let bearer = request
        .headers()
        .get(header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
        .map(str::trim)
        .filter(|value| !value.is_empty());
    let subject = if let Some(token) = bearer {
        match gUserSessionManager.authenticate(token).await {
            Ok(subject) => subject,
            Err(error) => return error.into_response(),
        }
    } else if let Some(token) = cookie_value(request.headers(), USER_SESSION_COOKIE) {
        let subject = match gUserSessionManager.authenticate_user_web(&token).await {
            Ok(subject) => subject,
            Err(error) => return error.into_response(),
        };
        let csrf = request
            .headers()
            .get("x-csrf-token")
            .and_then(|value| value.to_str().ok())
            .unwrap_or("");
        if !same_origin(request.headers())
            || !crate::user::session::CmsUserSessionManager::verify_user_csrf(&subject, csrf)
        {
            return CmsApiError::Forbidden.into_response();
        }
        subject
    } else {
        return CmsApiError::AuthenticationRequired.into_response();
    };
    request.extensions_mut().insert(subject);
    next.run(request).await
}

pub async fn require_active_user_write(mut request: Request<Body>, next: Next) -> Response {
    let bearer = request
        .headers()
        .get(header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
        .map(str::trim)
        .filter(|value| !value.is_empty());
    let subject = if let Some(token) = bearer {
        match gUserSessionManager.authenticate(token).await {
            Ok(subject) => subject,
            Err(error) => return error.into_response(),
        }
    } else if let Some(token) = cookie_value(request.headers(), USER_SESSION_COOKIE) {
        let subject = match gUserSessionManager.authenticate_user_web(&token).await {
            Ok(subject) => subject,
            Err(error) => return error.into_response(),
        };
        let csrf = request
            .headers()
            .get("x-csrf-token")
            .and_then(|value| value.to_str().ok())
            .unwrap_or("");
        if !same_origin(request.headers())
            || !crate::user::session::CmsUserSessionManager::verify_user_csrf(&subject, csrf)
        {
            return CmsApiError::Forbidden.into_response();
        }
        subject
    } else {
        return CmsApiError::AuthenticationRequired.into_response();
    };
    if subject.must_change_password {
        return CmsApiError::Forbidden.into_response();
    }
    request.extensions_mut().insert(subject);
    next.run(request).await
}

pub(crate) fn same_origin(headers: &axum::http::HeaderMap) -> bool {
    let source = headers
        .get(header::ORIGIN)
        .or_else(|| headers.get(header::REFERER))
        .and_then(|value| value.to_str().ok());
    let Some(source) = source else {
        return false;
    };

    let expected_host = headers
        .get("x-forwarded-host")
        .or_else(|| headers.get(header::HOST))
        .and_then(|value| value.to_str().ok());
    let Some(expected_host) = expected_host else {
        // HTTP/2 carries the target host in the `:authority` pseudo-header.
        // Hyper consumes pseudo-headers before building HeaderMap, so a real
        // browser request can legitimately have Origin/Referer but no Host.
        // Fetch Metadata is browser-controlled and preserves the same-origin
        // protection for this case. Non-browser HTTP/1 clients still have to
        // supply a Host that matches Origin/Referer.
        return headers
            .get("sec-fetch-site")
            .and_then(|value| value.to_str().ok())
            .is_some_and(|value| value.eq_ignore_ascii_case("same-origin"));
    };
    let Some(rest) = source
        .strip_prefix("https://")
        .or_else(|| source.strip_prefix("http://"))
    else {
        return false;
    };
    rest.split('/').next() == Some(expected_host)
}

pub async fn require_same_origin(request: Request<Body>, next: Next) -> Response {
    if !same_origin(request.headers()) {
        return CmsApiError::Forbidden.into_response();
    }
    next.run(request).await
}

pub async fn require_admin(mut request: Request<Body>, next: Next) -> Response {
    let Some(token) = cookie_value(request.headers(), ADMIN_SESSION_COOKIE) else {
        return CmsApiError::AuthenticationRequired.into_response();
    };
    match gUserSessionManager.authenticate_admin(&token).await {
        Ok(subject) => {
            let actor_id = subject.auth_id.clone();
            request.extensions_mut().insert(subject);
            crate::event::audit::scope_actor("admin", &actor_id, next.run(request)).await
        }
        Err(error) => error.into_response(),
    }
}

pub async fn require_admin_write(mut request: Request<Body>, next: Next) -> Response {
    if !same_origin(request.headers()) {
        return CmsApiError::Forbidden.into_response();
    }
    let Some(token) = cookie_value(request.headers(), ADMIN_SESSION_COOKIE) else {
        return CmsApiError::AuthenticationRequired.into_response();
    };
    let subject = match gUserSessionManager.authenticate_admin(&token).await {
        Ok(subject) => subject,
        Err(error) => return error.into_response(),
    };
    let csrf = request
        .headers()
        .get("x-csrf-token")
        .and_then(|value| value.to_str().ok())
        .unwrap_or("");
    if !crate::user::session::CmsUserSessionManager::verify_csrf(&subject, csrf) {
        return CmsApiError::Forbidden.into_response();
    }
    let actor_id = subject.auth_id.clone();
    request.extensions_mut().insert(subject);
    crate::event::audit::scope_actor("admin", &actor_id, next.run(request)).await
}

pub async fn require_guest(mut request: Request<Body>, next: Next) -> Response {
    let bearer = request
        .headers()
        .get(header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
        .map(str::trim)
        .filter(|value| !value.is_empty());
    let authenticated = if let Some(token) = bearer {
        gUserSessionManager.authenticate_guest_panel(token).await
    } else if let Some(token) = cookie_value(request.headers(), GUEST_SESSION_COOKIE) {
        gUserSessionManager.authenticate_guest(&token).await
    } else {
        return CmsApiError::AuthenticationRequired.into_response();
    };
    match authenticated {
        Ok(subject) => {
            request.extensions_mut().insert(subject);
            next.run(request).await
        }
        Err(error) => error.into_response(),
    }
}

pub async fn require_guest_write(mut request: Request<Body>, next: Next) -> Response {
    let bearer = request
        .headers()
        .get(header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
        .map(str::trim)
        .filter(|value| !value.is_empty());
    let subject = if let Some(token) = bearer {
        match gUserSessionManager.authenticate_guest_panel(token).await {
            Ok(subject) => subject,
            Err(error) => return error.into_response(),
        }
    } else if let Some(token) = cookie_value(request.headers(), GUEST_SESSION_COOKIE) {
        let subject = match gUserSessionManager.authenticate_guest(&token).await {
            Ok(subject) => subject,
            Err(error) => return error.into_response(),
        };
        let csrf = request
            .headers()
            .get("x-csrf-token")
            .and_then(|value| value.to_str().ok())
            .unwrap_or("");
        if !same_origin(request.headers())
            || !crate::user::session::CmsUserSessionManager::verify_guest_csrf(&subject, csrf)
        {
            return CmsApiError::Forbidden.into_response();
        }
        subject
    } else {
        return CmsApiError::AuthenticationRequired.into_response();
    };
    request.extensions_mut().insert(subject);
    next.run(request).await
}

pub fn make_session_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route("/guest", post(guest_session))
        .route("/user/login", post(login))
        .route("/user/logout", post(logout))
        .route(
            "/user/csrf",
            get(refresh_user_csrf)
                .layer(middleware::from_fn(require_user))
                .layer(middleware::from_fn(require_same_origin)),
        )
        .route(
            "/user/logout-all",
            post(logout_all).layer(middleware::from_fn(require_user_write)),
        )
        .route(
            "/admin/login",
            post(admin_login).layer(middleware::from_fn(require_same_origin)),
        )
        .route(
            "/admin/logout",
            post(admin_logout).layer(middleware::from_fn(require_same_origin)),
        )
        .route(
            "/admin/me",
            get(admin_me).layer(middleware::from_fn(require_admin)),
        )
        .with_state(context)
}

pub fn make_user_self_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route(
            "/register",
            post(register_user).layer(middleware::from_fn(require_guest_write)),
        )
        .route("/me", get(me).layer(middleware::from_fn(require_user)))
        .route(
            "/me",
            patch(update_profile).layer(middleware::from_fn(require_active_user_write)),
        )
        .route(
            "/me/avatar",
            put(update_avatar)
                .layer(DefaultBodyLimit::max(3 * 1024 * 1024))
                .layer(middleware::from_fn(require_active_user_write)),
        )
        .route(
            "/me/password",
            post(change_password).layer(middleware::from_fn(require_user_write)),
        )
        .route(
            "/devices",
            get(handle_query_my_devices).layer(middleware::from_fn(require_active_user)),
        )
        .route(
            "/devices/page",
            get(handle_query_my_devices_page).layer(middleware::from_fn(require_active_user)),
        )
        .route(
            "/devices/{device_id}/ticket",
            post(issue_device_ticket).layer(middleware::from_fn(require_active_user_write)),
        )
        .route(
            "/resources/summary",
            get(user_resource_summary).layer(middleware::from_fn(require_active_user)),
        )
        .route(
            "/apps",
            get(list_user_apps).layer(middleware::from_fn(require_active_user)),
        )
        .route(
            "/apps/page",
            get(list_user_apps_page).layer(middleware::from_fn(require_active_user)),
        )
        .route(
            "/apps/{app_id}/start",
            post(start_user_app).layer(middleware::from_fn(require_active_user_write)),
        )
        .route(
            "/instances",
            get(list_user_instances).layer(middleware::from_fn(require_active_user)),
        )
        .route(
            "/instances/page",
            get(list_user_instances_page).layer(middleware::from_fn(require_active_user)),
        )
        .route(
            "/instances/{instance_id}/ticket",
            post(issue_instance_ticket).layer(middleware::from_fn(require_active_user_write)),
        )
        .route(
            "/instances/{instance_id}/stop",
            post(stop_user_instance).layer(middleware::from_fn(require_active_user_write)),
        )
        .with_state(context)
}

#[cfg(test)]
mod tests {
    use super::same_origin;
    use axum::http::{header, HeaderMap, HeaderValue};

    #[test]
    fn origin_must_match_forwarded_or_direct_host() {
        let mut headers = HeaderMap::new();
        headers.insert(header::HOST, HeaderValue::from_static("cms.local:30500"));
        headers.insert(
            header::ORIGIN,
            HeaderValue::from_static("https://cms.local:30500"),
        );
        assert!(same_origin(&headers));
        headers.insert(
            header::ORIGIN,
            HeaderValue::from_static("https://attacker.invalid"),
        );
        assert!(!same_origin(&headers));
    }

    #[test]
    fn http2_browser_request_uses_fetch_metadata_when_host_is_consumed() {
        let mut headers = HeaderMap::new();
        headers.insert(
            header::ORIGIN,
            HeaderValue::from_static("https://cms.local:30500"),
        );
        headers.insert("sec-fetch-site", HeaderValue::from_static("same-origin"));
        assert!(same_origin(&headers));

        headers.insert("sec-fetch-site", HeaderValue::from_static("cross-site"));
        assert!(!same_origin(&headers));
    }
}
