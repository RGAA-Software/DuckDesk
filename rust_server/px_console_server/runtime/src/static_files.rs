use axum::{
    body::Body,
    http::{header, HeaderValue, Method, StatusCode, Uri},
    response::Response,
};
use std::{path::Path, path::PathBuf, sync::Arc};

pub const MAX_FILE_BYTES: u64 = 32 * 1024 * 1024;

pub struct StaticFiles {
    root: PathBuf,
    index: PathBuf,
}

impl StaticFiles {
    pub fn new(root: PathBuf) -> Self {
        let root = root.canonicalize().unwrap_or(root);
        let index = root.join("index.html");
        Self { root, index }
    }
}

pub async fn serve(files: Arc<StaticFiles>, method: Method, uri: Uri) -> Response {
    if method != Method::GET && method != Method::HEAD {
        return empty(StatusCode::METHOD_NOT_ALLOWED);
    }
    let path = uri.path();
    if path == "/api"
        || path.starts_with("/api/")
        || path == "/health"
        || path.starts_with("/health/")
    {
        return empty(StatusCode::NOT_FOUND);
    }
    let requested = match safe_relative_path(path) {
        Some(relative) if !relative.as_os_str().is_empty() => files.root.join(relative),
        Some(_) => files.index.clone(),
        None => return empty(StatusCode::NOT_FOUND),
    };
    let selected = match usable_file(&files.root, &requested).await {
        Some(path) => Some(path),
        None => usable_file(&files.root, &files.index).await,
    };
    let Some(selected) = selected else {
        return empty(StatusCode::NOT_FOUND);
    };
    let Some(bytes) = read_bounded(&selected).await else {
        return empty(StatusCode::NOT_FOUND);
    };
    let content_type = content_type(&selected);
    let body = if method == Method::HEAD {
        Body::empty()
    } else {
        Body::from(bytes)
    };
    Response::builder()
        .status(StatusCode::OK)
        .header(header::CONTENT_TYPE, content_type)
        .header(header::CACHE_CONTROL, "no-cache")
        .header(header::X_CONTENT_TYPE_OPTIONS, "nosniff")
        .body(body)
        .unwrap_or_else(|_| empty(StatusCode::INTERNAL_SERVER_ERROR))
}

fn safe_relative_path(path: &str) -> Option<PathBuf> {
    let relative = path.strip_prefix('/')?;
    if relative.contains('\\') || relative.contains(':') || relative.as_bytes().contains(&0) {
        return None;
    }
    let candidate = Path::new(relative);
    if candidate
        .components()
        .any(|component| !matches!(component, std::path::Component::Normal(_)))
        && !relative.is_empty()
    {
        return None;
    }
    Some(candidate.to_path_buf())
}

async fn usable_file(root: &Path, candidate: &Path) -> Option<PathBuf> {
    let canonical = tokio::fs::canonicalize(candidate).await.ok()?;
    if !canonical.starts_with(root) {
        return None;
    }
    let metadata = tokio::fs::metadata(&canonical).await.ok()?;
    (metadata.is_file() && metadata.len() <= MAX_FILE_BYTES).then_some(canonical)
}

async fn read_bounded(path: &Path) -> Option<Vec<u8>> {
    let metadata = tokio::fs::metadata(path).await.ok()?;
    if !metadata.is_file() || metadata.len() > MAX_FILE_BYTES {
        return None;
    }
    tokio::fs::read(path).await.ok()
}

fn content_type(path: &Path) -> HeaderValue {
    let value = match path.extension().and_then(|extension| extension.to_str()) {
        Some("css") => "text/css; charset=utf-8",
        Some("html") => "text/html; charset=utf-8",
        Some("ico") => "image/x-icon",
        Some("jpeg" | "jpg") => "image/jpeg",
        Some("js" | "mjs") => "text/javascript; charset=utf-8",
        Some("json") => "application/json",
        Some("png") => "image/png",
        Some("svg") => "image/svg+xml",
        Some("wasm") => "application/wasm",
        Some("webp") => "image/webp",
        Some("woff") => "font/woff",
        Some("woff2") => "font/woff2",
        _ => "application/octet-stream",
    };
    HeaderValue::from_static(value)
}

fn empty(status: StatusCode) -> Response {
    Response::builder()
        .status(status)
        .header(header::CACHE_CONTROL, "no-store")
        .header(header::X_CONTENT_TYPE_OPTIONS, "nosniff")
        .body(Body::empty())
        .unwrap_or_else(|_| Response::new(Body::empty()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::body::to_bytes;

    #[test]
    fn relative_paths_reject_escape_and_windows_aliases() {
        assert_eq!(
            safe_relative_path("/assets/app.js"),
            Some(PathBuf::from("assets/app.js"))
        );
        for path in [
            "assets/app.js",
            "/../secret",
            "/a/../secret",
            "/a\\secret",
            "/a:stream",
        ] {
            assert!(safe_relative_path(path).is_none(), "accepted {path}");
        }
    }

    #[tokio::test]
    async fn static_assets_spa_routes_and_api_misses_are_distinct() {
        let directory = tempfile::tempdir().unwrap();
        tokio::fs::write(directory.path().join("index.html"), b"pixels-index")
            .await
            .unwrap();
        tokio::fs::write(directory.path().join("app.js"), b"pixels-script")
            .await
            .unwrap();
        let files = Arc::new(StaticFiles::new(directory.path().to_path_buf()));
        let asset = serve(files.clone(), Method::GET, "/app.js".parse().unwrap()).await;
        assert_eq!(asset.status(), StatusCode::OK);
        assert_eq!(
            asset.headers()[header::CONTENT_TYPE],
            "text/javascript; charset=utf-8"
        );
        assert_eq!(
            to_bytes(asset.into_body(), 1024).await.unwrap(),
            "pixels-script"
        );
        let spa = serve(
            files.clone(),
            Method::GET,
            "/settings/profile".parse().unwrap(),
        )
        .await;
        assert_eq!(
            to_bytes(spa.into_body(), 1024).await.unwrap(),
            "pixels-index"
        );
        assert_eq!(
            serve(files, Method::GET, "/api/retired".parse().unwrap())
                .await
                .status(),
            StatusCode::NOT_FOUND
        );
    }
}
