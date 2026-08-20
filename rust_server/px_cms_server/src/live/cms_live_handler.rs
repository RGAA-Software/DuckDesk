use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::cms_settings::CmsLiveSettings;
use crate::gCmsSettings;
use axum::body::Body;
use axum::extract::{Path, Query, State};
use axum::http::{header, HeaderValue, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::sync::Arc;
use tokio::sync::Mutex;

use super::cms_live_ticket_manager::CmsLiveTicketManager;

lazy_static::lazy_static! {
    static ref LIVE_TICKETS: Arc<CmsLiveTicketManager> = CmsLiveTicketManager::new();
}

#[derive(Debug, Deserialize)]
pub struct LiveStatusQuery {
    pub device_id: String,
    #[serde(default)]
    pub app_id: String,
}

#[derive(Debug, Deserialize)]
pub struct LivePlayQuery {
    pub ticket: String,
}

#[derive(Debug, Serialize, Default)]
pub struct LiveStatus {
    pub online: bool,
    pub stream_id: String,
    pub app_id: String,
    pub video_codec: String,
    pub audio_codec: String,
    pub width: u64,
    pub height: u64,
    pub fps: u64,
    pub reader_count: u64,
    pub browser_playable: bool,
    pub message: String,
    #[serde(skip_serializing_if = "String::is_empty")]
    pub play_url: String,
}

impl LiveStatus {
    fn offline(stream_id: String, app_id: String, message: impl Into<String>) -> Self {
        Self {
            online: false,
            stream_id,
            app_id,
            video_codec: String::new(),
            audio_codec: String::new(),
            width: 0,
            height: 0,
            fps: 0,
            reader_count: 0,
            browser_playable: false,
            message: message.into(),
            play_url: String::new(),
        }
    }
}

pub async fn handle_live_status(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    Query(query): Query<LiveStatusQuery>,
) -> Result<Json<RespMessage<LiveStatus>>, CmsApiError> {
    let settings = gCmsSettings.lock().await.live.clone();
    let device_id = query.device_id.trim();
    let app_id = if query.app_id.trim().is_empty() {
        settings.default_app_id.trim()
    } else {
        query.app_id.trim()
    };

    if !is_safe_id(device_id) || !is_safe_id(app_id) {
        return Err(CmsApiError::InvalidParams);
    }
    let stream_id = format!("{}__app__{}", device_id, app_id);
    let status = match query_zlm(&settings, &stream_id).await {
        Ok(Some(media)) => make_online_status(media, stream_id.clone(), app_id.to_string()).await,
        Ok(None) => LiveStatus::offline(stream_id, app_id.to_string(), "等待 render 主流推送"),
        Err(err) => {
            tracing::warn!("ZLMediaKit live status query failed: {}", err);
            LiveStatus::offline(stream_id, app_id.to_string(), "媒体服务器暂时不可达")
        }
    };
    Ok(Json(ok_resp(status)))
}

async fn make_online_status(media: Value, stream_id: String, app_id: String) -> LiveStatus {
    let mut video_codec = String::new();
    let mut audio_codec = String::new();
    let mut width = 0;
    let mut height = 0;
    let mut fps = 0;
    if let Some(tracks) = media.get("tracks").and_then(Value::as_array) {
        for track in tracks {
            let codec = track
                .get("codec_id_name")
                .and_then(Value::as_str)
                .unwrap_or_default()
                .to_string();
            match track.get("codec_type").and_then(Value::as_i64) {
                // ZLMediaKit TrackVideo == 0 and TrackAudio == 1.
                Some(0) => {
                    video_codec = codec;
                    width = json_u64(track, "width");
                    height = json_u64(track, "height");
                    fps = json_u64(track, "fps");
                }
                Some(1) => audio_codec = codec,
                _ => {}
            }
        }
    }

    let h264 = video_codec.eq_ignore_ascii_case("h264") || video_codec.eq_ignore_ascii_case("avc");
    let mut status = LiveStatus {
        online: true,
        stream_id: stream_id.clone(),
        app_id,
        video_codec: video_codec.clone(),
        audio_codec,
        width,
        height,
        fps,
        reader_count: json_u64(&media, "readerCount"),
        browser_playable: h264,
        message: if h264 {
            "H.264 主流已就绪".to_string()
        } else if video_codec.eq_ignore_ascii_case("h265")
            || video_codec.eq_ignore_ascii_case("hevc")
        {
            "H.265 主流已就绪；当前浏览器观看端不保证 HEVC 解码，请切换 H.264 或使用支持 HEVC 的专用客户端。".to_string()
        } else {
            format!("主流已就绪，但浏览器不支持视频编码 {}", video_codec)
        },
        play_url: String::new(),
    };
    if h264 {
        let ticket = LIVE_TICKETS.issue(stream_id.clone()).await;
        status.play_url = format!(
            "/api/v1/live/control/play/{}/flv?ticket={}",
            stream_id, ticket
        );
    }
    status
}

/// Chrome's low-latency path.  Keep HTTP-FLV streaming through CMS so the
/// browser never needs a direct route to the local ZLMediaKit HTTP port.
pub async fn handle_live_flv(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    Path(stream_id): Path<String>,
    Query(query): Query<LivePlayQuery>,
) -> Response {
    if !is_safe_id(&stream_id) || !LIVE_TICKETS.validate(&query.ticket, &stream_id).await {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    let settings = gCmsSettings.lock().await.live.clone();
    let url = format!(
        "{}/{}/{}.live.flv",
        settings.media_server_url.trim_end_matches('/'),
        settings.app.trim(),
        stream_id
    );
    let response = match reqwest::Client::new().get(url).send().await {
        Ok(response) => response,
        Err(err) => {
            tracing::warn!("ZLMediaKit HTTP-FLV relay failed: {}", err);
            return StatusCode::BAD_GATEWAY.into_response();
        }
    };
    if !response.status().is_success() {
        return response.status().into_response();
    }
    let mut headers = axum::http::HeaderMap::new();
    headers.insert(header::CONTENT_TYPE, HeaderValue::from_static("video/x-flv"));
    headers.insert(header::CACHE_CONTROL, HeaderValue::from_static("no-store"));
    (headers, Body::from_stream(response.bytes_stream())).into_response()
}

async fn query_zlm(settings: &CmsLiveSettings, stream_id: &str) -> Result<Option<Value>, String> {
    let base_url = settings.media_server_url.trim_end_matches('/');
    if base_url.is_empty() || !is_safe_id(settings.app.trim()) {
        return Err("invalid [live] CMS configuration".to_string());
    }
    let mut query = vec![
        ("schema", "rtmp".to_string()),
        ("app", settings.app.clone()),
        ("stream", stream_id.to_string()),
    ];
    if let Some(secret) = effective_api_secret(settings) {
        query.push(("secret", secret));
    }
    let response = reqwest::Client::new()
        .get(format!("{}/index/api/getMediaList", base_url))
        .query(&query)
        .timeout(std::time::Duration::from_secs(3))
        .send()
        .await
        .map_err(|err| err.to_string())?;
    if !response.status().is_success() {
        return Err(format!("HTTP {}", response.status()));
    }
    let body: Value = response.json().await.map_err(|err| err.to_string())?;
    if body.get("code").and_then(Value::as_i64).unwrap_or(-1) != 0 {
        return Err("ZLMediaKit API returned a non-zero result".to_string());
    }
    Ok(body
        .get("data")
        .and_then(Value::as_array)
        .and_then(|data| data.first())
        .cloned())
}

/// CMS keeps an explicitly configured secret authoritative.  For the bundled
/// local sidecar we can also read the sidecar's own config so a fresh CMS
/// deployment works without duplicating that secret in px_cms.toml.  Remote
/// media servers must always be configured explicitly.
fn effective_api_secret(settings: &CmsLiveSettings) -> Option<String> {
    let configured = settings.api_secret.trim();
    if !configured.is_empty() {
        return Some(configured.to_string());
    }
    if !crate::media_sidecar::is_local_sidecar_url(&settings.media_server_url) {
        return None;
    }
    let config_path = std::env::current_exe().ok()?.parent()?.join("config.ini");
    let contents = std::fs::read_to_string(config_path).ok()?;
    ini_value(&contents, "api", "secret")
}

fn ini_value(contents: &str, section: &str, key: &str) -> Option<String> {
    let mut in_section = false;
    for line in contents.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
            continue;
        }
        if let Some(header) = line.strip_prefix('[').and_then(|value| value.strip_suffix(']')) {
            in_section = header.trim().eq_ignore_ascii_case(section);
            continue;
        }
        if !in_section {
            continue;
        }
        let Some((candidate, value)) = line.split_once('=') else {
            continue;
        };
        if candidate.trim().eq_ignore_ascii_case(key) {
            let value = value.trim();
            return (!value.is_empty()).then(|| value.to_string());
        }
    }
    None
}

pub async fn handle_live_play(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    Path((stream_id, asset)): Path<(String, String)>,
    Query(query): Query<LivePlayQuery>,
) -> Response {
    if !is_safe_id(&stream_id)
        || !is_safe_asset(&asset)
        || !LIVE_TICKETS.validate(&query.ticket, &stream_id).await
    {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    let settings = gCmsSettings.lock().await.live.clone();
    let url = format!(
        "{}/{}/{}/{}",
        settings.media_server_url.trim_end_matches('/'),
        settings.app.trim(),
        stream_id,
        asset
    );
    let response = match reqwest::Client::new()
        .get(url)
        .timeout(std::time::Duration::from_secs(10))
        .send()
        .await
    {
        Ok(response) => response,
        Err(err) => {
            tracing::warn!("ZLMediaKit HLS relay failed: {}", err);
            return StatusCode::BAD_GATEWAY.into_response();
        }
    };
    if !response.status().is_success() {
        return response.status().into_response();
    }
    let is_manifest = asset.eq_ignore_ascii_case("hls.m3u8") || asset.ends_with(".m3u8");
    let bytes = match response.bytes().await {
        Ok(bytes) => bytes,
        Err(_) => return StatusCode::BAD_GATEWAY.into_response(),
    };
    if is_manifest {
        let manifest = String::from_utf8_lossy(&bytes);
        let rewritten = rewrite_manifest(&manifest, &stream_id, &query.ticket);
        return (
            [(
                header::CONTENT_TYPE,
                HeaderValue::from_static("application/vnd.apple.mpegurl"),
            )],
            rewritten,
        )
            .into_response();
    }
    let content_type = content_type_for(&asset);
    ([(header::CONTENT_TYPE, content_type)], bytes).into_response()
}

fn rewrite_manifest(manifest: &str, stream_id: &str, ticket: &str) -> String {
    manifest
        .lines()
        .map(|line| {
            let asset = line.trim();
            if asset.is_empty() || asset.starts_with('#') || !is_safe_asset(asset) {
                line.to_string()
            } else {
                format!(
                    "/api/v1/live/control/play/{}/{}?ticket={}",
                    stream_id, asset, ticket
                )
            }
        })
        .collect::<Vec<_>>()
        .join("\n")
}

fn content_type_for(asset: &str) -> HeaderValue {
    if asset.ends_with(".ts") {
        HeaderValue::from_static("video/mp2t")
    } else if asset.ends_with(".m4s") {
        HeaderValue::from_static("video/iso.segment")
    } else {
        HeaderValue::from_static("application/octet-stream")
    }
}

fn json_u64(value: &Value, key: &str) -> u64 {
    value
        .get(key)
        .and_then(|number| {
            number.as_u64().or_else(|| {
                // ZLMediaKit reports video FPS as a JSON floating point value
                // (for example `60.0`), while dimensions and reader counts are
                // integral. Accept both representations for the status API.
                number
                    .as_f64()
                    .filter(|fps| fps.is_finite() && *fps >= 0.0)
                    .map(|fps| fps.round() as u64)
            })
        })
        .unwrap_or(0)
}

fn is_safe_id(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 160
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'_' | b'-' | b'.'))
}

fn is_safe_asset(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 300
        && !value.contains("..")
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'_' | b'-' | b'.' | b'/'))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ids_and_assets_are_strictly_bounded() {
        assert!(is_safe_id("debug1__app__cargame_debug"));
        assert!(!is_safe_id("../live"));
        assert!(is_safe_asset("hls-0.ts"));
        assert!(is_safe_asset("part/hls-0.ts"));
        assert!(!is_safe_asset("../secret"));
        assert!(!is_safe_asset("hls.ts?x=1"));
    }

    #[test]
    fn manifest_segments_are_relayed() {
        let manifest = "#EXTM3U\n#EXTINF:2,\nhls-0.ts\n";
        let rewritten = rewrite_manifest(manifest, "debug1__app__cargame_debug", "ticket");
        assert!(rewritten.contains(
            "/api/v1/live/control/play/debug1__app__cargame_debug/hls-0.ts?ticket=ticket"
        ));
        assert!(rewritten.contains("#EXTINF:2,"));
    }

    #[test]
    fn reads_only_the_api_secret_from_ini() {
        let config = "[http]\nsecret=wrong\n[api]\n; comment\nsecret = local-secret\n";
        assert_eq!(ini_value(config, "api", "secret").as_deref(), Some("local-secret"));
        assert_eq!(ini_value(config, "api", "missing"), None);
    }
}
