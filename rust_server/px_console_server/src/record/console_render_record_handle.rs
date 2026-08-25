//! Handlers for the console render-records api family
//! (docs/console_render_records_view_design.md section 6.3 / 6.4).

use crate::console_api_error::ConsoleApiError;
use crate::console_context::ConsoleContext;
use crate::console_http_util::{get_str_param, get_str_param_allow_empty};
use crate::device::console_device::ConsoleDevice;
use crate::gConsolePanelConnMgr;
use crate::gConsoleSettings;
use crate::gDeviceManager;
use crate::gRecordTunnel;
use crate::gRenderRecordManager;
use crate::record::console_render_record::{
    make_record_id, ConsoleRenderRecord, RECORD_STATE_FETCHING, RECORD_STATE_READY,
};
use crate::record::record_cleaner::record_file_path;
use crate::record::record_ticket::{make_ticket_exp, sign_record_ticket};
use axum::extract::{Multipart, Path, Query, State};
use axum::Json;
use futures_util::StreamExt;
use prost::Message as ProstMessage;
use protocol::console_panel::{
    ConsolePanelMessage, ConsolePanelMessageType, RecordFetchReq, RecordListReq,
};
use px_base::{ok_resp, RespMessage};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;
use tokio::io::AsyncWriteExt;
use tokio::sync::Mutex;

pub const DEFAULT_PANEL_HTTP_PORT: i64 = 20369;
/// write c_records progress at most once per this many received bytes
const PROGRESS_STEP_BYTES: i64 = 4 * 1024 * 1024;
/// timeout for one direct-pull attempt from panel
const DIRECT_PULL_TIMEOUT: Duration = Duration::from_secs(3600);

/// same whitelist as the panel side records_catalog::IsValidRecordFileName:
/// ^[A-Za-z0-9_.\-]+$ , ends with ".mp4", no ".."
pub fn is_valid_record_filename(name: &str) -> bool {
    if name.is_empty() || name.len() > 255 || !name.ends_with(".mp4") || name.contains("..") {
        return false;
    }
    name.chars()
        .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '.' || c == '-')
}

// ---------------- response types (web contract) ----------------

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct RecordAccessInfo {
    pub device_id: String,
    pub panel_lan_ips: Vec<String>,
    pub panel_port: i64,
    pub online: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct RecordTicketResp {
    pub tk: String,
    // unix seconds
    pub exp: i64,
}

/// one file entry of GET /api/v1/record/list
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct RecordWebItem {
    // c_records id ("{device_id}:{filename}"), "" when the file is panel-only
    #[serde(default)]
    pub id: String,
    pub name: String,
    pub size: i64,
    // device file mtime, unix seconds
    pub mtime: i64,
    #[serde(default)]
    pub monitor: String,
    #[serde(default)]
    pub codec: String,
    // "none" (only on device) | "fetching" | "ready" | "error"
    pub state: String,
    #[serde(default)]
    pub keep: bool,
    // bytes received so far (state == fetching)
    #[serde(default)]
    pub progress: i64,
    // expected total bytes (state == fetching)
    #[serde(default)]
    pub total: i64,
    // playable url when state == ready: /uploads/records/{device_id}/{name}
    #[serde(default)]
    pub url: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct RecordListWebResp {
    pub device_id: String,
    pub files: Vec<RecordWebItem>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct RecordFetchResp {
    // "fetching" | "ready"
    pub state: String,
    #[serde(default)]
    pub url: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RecordDownloadReq {
    pub device_id: String,
    pub filename: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct RecordDownloadResp {
    // "downloading" | "ready"
    pub state: String,
    pub id: String,
    #[serde(default)]
    pub url: String,
}

// ---------------- helpers ----------------

fn disk_file_exists(device_id: &str, filename: &str) -> bool {
    std::path::Path::new(&record_file_path(device_id, filename)).is_file()
}

fn panel_port_of(device: &ConsoleDevice) -> i64 {
    if device.panel_http_port > 0 {
        device.panel_http_port
    } else {
        DEFAULT_PANEL_HTTP_PORT
    }
}

/// console -> panel: RecordFetchReq; the caller owns in-flight dedupe
async fn send_fetch_req(device_id: &str, filename: &str) -> Result<(), ConsoleApiError> {
    let conn = gConsolePanelConnMgr
        .get_conn(device_id.to_string())
        .await
        .map_err(|_| ConsoleApiError::DeviceOffline)?;

    let token = gRecordTunnel.new_token(device_id, filename);
    let req_id = uuid::Uuid::new_v4().to_string();
    let (scheme, host, port) = {
        let s = gConsoleSettings.lock().await;
        (
            if s.ssl_enable { "https" } else { "http" }.to_string(),
            s.server_w3c_ip.clone(),
            s.console_port,
        )
    };
    let upload_url = format!("{}://{}:{}/api/v1/record/upload", scheme, host, port);

    let mut msg = ConsolePanelMessage::default();
    msg.set_msg_type(ConsolePanelMessageType::KRecordFetchReq);
    msg.record_fetch_req = Some(RecordFetchReq {
        device_id: device_id.to_string(),
        req_id,
        filename: filename.to_string(),
        token,
        upload_url,
    });
    let ok = conn
        .lock()
        .await
        .send_bin_message_bytes(msg.encode_to_vec().into())
        .await;
    if !ok {
        return Err(ConsoleApiError::DeviceOffline);
    }
    Ok(())
}

/// shared trigger: dedupe -> upsert record -> tunnel RecordFetchReq
async fn trigger_tunnel_fetch(device_id: &str, filename: &str) -> Result<(), ConsoleApiError> {
    if !gRecordTunnel.add_inflight(device_id, filename) {
        return Ok(());
    }
    gRenderRecordManager
        .upsert_fetch_start(device_id, filename)
        .await?;
    if let Err(e) = send_fetch_req(device_id, filename).await {
        gRecordTunnel.remove_inflight(device_id, filename);
        return Err(e);
    }
    Ok(())
}

fn web_item_from_record(rec: &ConsoleRenderRecord) -> RecordWebItem {
    RecordWebItem {
        id: rec.id.clone(),
        name: rec.filename.clone(),
        size: rec.size,
        mtime: rec.mtime,
        monitor: "".to_string(),
        codec: "".to_string(),
        state: rec.state.clone(),
        keep: rec.keep,
        progress: rec.progress,
        total: rec.size,
        url: if rec.state == RECORD_STATE_READY {
            rec.url()
        } else {
            "".to_string()
        },
    }
}

// ---------------- handlers ----------------

/// GET /access?device_id=  — web topology-1 entry (design 5.2)
pub async fn handle_record_access(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<RecordAccessInfo>>, ConsoleApiError> {
    let device_id = get_str_param(&query, "device_id")?;
    let device = gDeviceManager.query_device_by_id(device_id.clone()).await?;
    let online = gConsolePanelConnMgr
        .is_panel_online(device_id.clone())
        .await
        .unwrap_or(false);
    Ok(Json(ok_resp(RecordAccessInfo {
        device_id,
        panel_lan_ips: device.panel_lan_ips.clone(),
        panel_port: panel_port_of(&device),
        online,
    })))
}

/// GET /ticket?device_id=&file=  — sign a short-lived ticket (design 5.3);
/// file = "*" covers list / info requests
pub async fn handle_record_ticket(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<RecordTicketResp>>, ConsoleApiError> {
    let device_id = get_str_param(&query, "device_id")?;
    let file = get_str_param(&query, "file")?;
    if file != "*" && !is_valid_record_filename(&file) {
        return Err(ConsoleApiError::InvalidParams);
    }
    let device = gDeviceManager.query_device_by_id(device_id.clone()).await?;
    if device.safety_pwd_md5.is_empty() {
        tracing::warn!("record ticket: device {} has no safety password", device_id);
        return Err(ConsoleApiError::SafetyPwdMissing);
    }
    let exp = make_ticket_exp();
    let tk = sign_record_ticket(&device_id, &file, exp, &device.safety_pwd_md5);
    Ok(Json(ok_resp(RecordTicketResp { tk, exp })))
}

/// GET /list?device_id=  — topology 2: tunnel RecordListReq -> panel, merge
/// the console-side download state from c_records (design 6.3)
pub async fn handle_record_list(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<RecordListWebResp>>, ConsoleApiError> {
    let device_id = get_str_param(&query, "device_id")?;
    let conn = gConsolePanelConnMgr
        .get_conn(device_id.clone())
        .await
        .map_err(|_| ConsoleApiError::DeviceOffline)?;

    let req_id = uuid::Uuid::new_v4().to_string();
    let rx = gRecordTunnel.register_list(&req_id);
    let mut msg = ConsolePanelMessage::default();
    msg.set_msg_type(ConsolePanelMessageType::KRecordListReq);
    msg.record_list_req = Some(RecordListReq {
        device_id: device_id.clone(),
        req_id: req_id.clone(),
    });
    let sent = conn
        .lock()
        .await
        .send_bin_message_bytes(msg.encode_to_vec().into())
        .await;
    if !sent {
        return Err(ConsoleApiError::DeviceOffline);
    }
    let resp = gRecordTunnel.wait_list(&req_id, rx).await?;
    if !resp.error.is_empty() {
        tracing::error!("panel record list error: {}", resp.error);
        return Err(ConsoleApiError::InternalError);
    }

    // merge console-side state
    let stored = gRenderRecordManager.query_by_device(&device_id).await?;
    let mut stored_map: HashMap<String, ConsoleRenderRecord> = stored
        .into_iter()
        .map(|r| (r.filename.clone(), r))
        .collect();

    let mut files: Vec<RecordWebItem> = Vec::new();
    for f in resp.files {
        if let Some(rec) = stored_map.remove(&f.name) {
            let mut item = web_item_from_record(&rec);
            // panel-side truth wins for identity fields
            item.size = f.size;
            item.mtime = f.mtime;
            item.monitor = f.monitor;
            item.codec = f.codec;
            if item.state == RECORD_STATE_FETCHING {
                item.total = f.size;
            }
            files.push(item);
        } else {
            files.push(RecordWebItem {
                name: f.name,
                size: f.size,
                mtime: f.mtime,
                monitor: f.monitor,
                codec: f.codec,
                state: "none".to_string(),
                ..Default::default()
            });
        }
    }
    // console-only copies (device file may have been rotated out) stay visible
    for (_, rec) in stored_map {
        files.push(web_item_from_record(&rec));
    }

    Ok(Json(ok_resp(RecordListWebResp { device_id, files })))
}

/// GET /fetch?device_id=&file=  — topology 2: trigger the upload via tunnel;
/// concurrent requests for the same file are deduped (design 6.1)
pub async fn handle_record_fetch(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    query: Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<RecordFetchResp>>, ConsoleApiError> {
    let device_id = get_str_param(&query, "device_id")?;
    let file = get_str_param(&query, "file")?;
    if !is_valid_record_filename(&file) {
        return Err(ConsoleApiError::InvalidParams);
    }

    // idempotent: already fetched and still on disk -> no retransfer (6.3)
    if let Some(rec) = gRenderRecordManager.find(&device_id, &file).await? {
        if rec.state == RECORD_STATE_READY && disk_file_exists(&device_id, &file) {
            return Ok(Json(ok_resp(RecordFetchResp {
                state: RECORD_STATE_READY.to_string(),
                url: rec.url(),
            })));
        }
    }

    if gRecordTunnel.is_inflight(&device_id, &file) {
        return Ok(Json(ok_resp(RecordFetchResp {
            state: RECORD_STATE_FETCHING.to_string(),
            url: "".to_string(),
        })));
    }

    trigger_tunnel_fetch(&device_id, &file).await?;
    Ok(Json(ok_resp(RecordFetchResp {
        state: RECORD_STATE_FETCHING.to_string(),
        url: "".to_string(),
    })))
}

/// POST /upload?appkey=&token=&device_id=&filename=&size=&mtime=
/// multipart file field "file" — panel's upload target (design 6.3).
/// appkey is checked by the route filter; token is one-time and bound to
/// device_id + filename.
pub async fn handle_record_upload(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    query: Query<HashMap<String, String>>,
    mut multipart: Multipart,
) -> Result<Json<RespMessage<String>>, ConsoleApiError> {
    let token = get_str_param(&query, "token")?;
    let device_id = get_str_param(&query, "device_id")?;
    let filename = get_str_param(&query, "filename")?;
    if !is_valid_record_filename(&filename) {
        return Err(ConsoleApiError::InvalidParams);
    }
    let total_size = get_str_param_allow_empty(&query, "size")?
        .parse::<i64>()
        .unwrap_or(0);
    let mtime = get_str_param_allow_empty(&query, "mtime")?
        .parse::<i64>()
        .unwrap_or(0);

    if !gRecordTunnel.validate_token(&token, &device_id, &filename) {
        tracing::warn!(
            "record upload: invalid token for {}/{}",
            device_id,
            filename
        );
        return Err(ConsoleApiError::TokenInvalid);
    }

    // idempotent re-check (6.3): same device+file+size+mtime already ready and
    // on disk -> drain the body and report success without rewriting
    let skip_write = match gRenderRecordManager.find(&device_id, &filename).await? {
        Some(rec) => {
            rec.state == RECORD_STATE_READY
                && rec.size == total_size
                && (mtime == 0 || rec.mtime == mtime)
                && disk_file_exists(&device_id, &filename)
        }
        None => false,
    };

    let dir = format!("./uploads/records/{}", device_id);
    if !skip_write {
        px_base::create_dir_all_if_not_exists(&dir)
            .map_err(|_| ConsoleApiError::UploadFileFailed)?;
    }
    let target_path = record_file_path(&device_id, &filename);

    let mut received: i64 = 0;
    let mut last_reported: i64 = 0;
    let mut got_file = false;
    let mut o_file: Option<tokio::fs::File> = None;

    while let Some(mut field) = multipart
        .next_field()
        .await
        .map_err(|_e| ConsoleApiError::InvalidParams)?
    {
        let key = field.name().unwrap_or("").to_string();
        if key != "file" {
            // consume text fields and move on
            let _ = field.text().await;
            continue;
        }
        got_file = true;
        if !skip_write {
            o_file = Some(
                tokio::fs::File::create(&target_path)
                    .await
                    .map_err(|_| ConsoleApiError::UploadFileFailed)?,
            );
        }
        loop {
            match field.chunk().await {
                Ok(Some(bytes)) => {
                    received += bytes.len() as i64;
                    if let Some(f) = o_file.as_mut() {
                        if let Err(e) = f.write_all(&bytes).await {
                            tracing::error!("record upload write error: {}", e);
                            let _ = gRenderRecordManager
                                .mark_error(&device_id, &filename, "write failed")
                                .await;
                            gRecordTunnel.remove_inflight(&device_id, &filename);
                            return Err(ConsoleApiError::UploadFileFailed);
                        }
                    }
                    // throttled progress update for the "回传中 x%" display (6.1)
                    if received - last_reported >= PROGRESS_STEP_BYTES {
                        last_reported = received;
                        let _ = gRenderRecordManager
                            .update_progress(&device_id, &filename, received, total_size, mtime)
                            .await;
                    }
                }
                Ok(None) => break,
                Err(e) => {
                    tracing::error!("record upload chunk error: {}", e);
                    let _ = gRenderRecordManager
                        .mark_error(&device_id, &filename, "upload interrupted")
                        .await;
                    gRecordTunnel.remove_inflight(&device_id, &filename);
                    return Err(ConsoleApiError::UploadFileFailed);
                }
            }
        }
    }

    if !got_file {
        return Err(ConsoleApiError::InvalidParams);
    }

    if skip_write {
        tracing::info!("record upload idempotent hit: {}/{}", device_id, filename);
    } else {
        gRenderRecordManager
            .mark_ready(&device_id, &filename, received, mtime)
            .await?;
        tracing::info!(
            "record upload ok: {}/{} {} bytes",
            device_id,
            filename,
            received
        );
    }
    gRecordTunnel.remove_inflight(&device_id, &filename);
    Ok(Json(ok_resp("upload ok".to_string())))
}

/// POST /download  body {device_id, filename} — save a copy on the console host
/// and pin it (keep=true, exempt from cleanup; design 6.4)
pub async fn handle_record_download(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    Json(req): Json<RecordDownloadReq>,
) -> Result<Json<RespMessage<RecordDownloadResp>>, ConsoleApiError> {
    let device_id = req.device_id.clone();
    let filename = req.filename.clone();
    if device_id.is_empty() || !is_valid_record_filename(&filename) {
        return Err(ConsoleApiError::InvalidParams);
    }
    let id = make_record_id(&device_id, &filename);

    if let Some(rec) = gRenderRecordManager.find(&device_id, &filename).await? {
        match rec.state.as_str() {
            // already here: just pin it (6.4 idempotency)
            RECORD_STATE_READY if disk_file_exists(&device_id, &filename) => {
                if !rec.keep {
                    gRenderRecordManager
                        .set_keep(&device_id, &filename, true)
                        .await?;
                }
                return Ok(Json(ok_resp(RecordDownloadResp {
                    state: RECORD_STATE_READY.to_string(),
                    id,
                    url: rec.url(),
                })));
            }
            // in-flight: pin it, the completion path keeps the flag
            RECORD_STATE_FETCHING => {
                gRenderRecordManager
                    .set_keep(&device_id, &filename, true)
                    .await?;
                return Ok(Json(ok_resp(RecordDownloadResp {
                    state: "downloading".to_string(),
                    id,
                    url: "".to_string(),
                })));
            }
            _ => {}
        }
    }

    let device = gDeviceManager.query_device_by_id(device_id.clone()).await?;

    // fresh download: create the record pinned, then run async
    gRenderRecordManager
        .upsert_fetch_start(&device_id, &filename)
        .await?;
    gRenderRecordManager
        .set_keep(&device_id, &filename, true)
        .await?;
    if !gRecordTunnel.add_inflight(&device_id, &filename) {
        // someone else just triggered it; the record is already pinned
        return Ok(Json(ok_resp(RecordDownloadResp {
            state: "downloading".to_string(),
            id,
            url: "".to_string(),
        })));
    }

    tokio::spawn(async move {
        let r = download_one(device_id.clone(), filename.clone(), device).await;
        if let Err(e) = r {
            tracing::error!("record download failed {}/{}: {}", device_id, filename, e);
            let _ = gRenderRecordManager
                .mark_error(&device_id, &filename, &e)
                .await;
            gRecordTunnel.remove_inflight(&device_id, &filename);
        }
    });

    Ok(Json(ok_resp(RecordDownloadResp {
        state: "downloading".to_string(),
        id,
        url: "".to_string(),
    })))
}

/// async download task: topology 1 direct-pull first, tunnel fallback (6.4)
async fn download_one(
    device_id: String,
    filename: String,
    device: ConsoleDevice,
) -> Result<(), String> {
    if !device.panel_lan_ips.is_empty() && !device.safety_pwd_md5.is_empty() {
        match direct_pull_from_panel(&device, &filename).await {
            Ok(size) => {
                gRenderRecordManager
                    .mark_ready(&device_id, &filename, size, 0)
                    .await
                    .map_err(|e| e.to_string())?;
                gRecordTunnel.remove_inflight(&device_id, &filename);
                return Ok(());
            }
            Err(e) => {
                tracing::warn!(
                    "direct pull from panel failed ({}/{}): {}, fallback to tunnel",
                    device_id,
                    filename,
                    e
                );
            }
        }
    }
    // topology 2: tunnel fetch (in-flight already registered by the caller)
    send_fetch_req(&device_id, &filename)
        .await
        .map_err(|e| e.to_string())
}

/// console server pulls http://{panel_ip}:{port}/records/{file}?tk&exp directly,
/// streaming to disk with progress updates (design 6.4, topology 1)
async fn direct_pull_from_panel(device: &ConsoleDevice, filename: &str) -> Result<i64, String> {
    let port = panel_port_of(device);
    let mut last_err = "no panel lan ip".to_string();
    for ip in &device.panel_lan_ips {
        let exp = make_ticket_exp();
        let tk = sign_record_ticket(&device.device_id, filename, exp, &device.safety_pwd_md5);
        let url = format!(
            "http://{}:{}/records/{}?tk={}&exp={}",
            ip, port, filename, tk, exp
        );
        match pull_url_to_file(&url, &device.device_id, filename).await {
            Ok(size) => return Ok(size),
            Err(e) => {
                tracing::warn!("direct pull {} failed: {}", url, e);
                last_err = e;
            }
        }
    }
    Err(last_err)
}

async fn pull_url_to_file(url: &str, device_id: &str, filename: &str) -> Result<i64, String> {
    let client = reqwest::Client::builder()
        .connect_timeout(Duration::from_secs(5))
        .timeout(DIRECT_PULL_TIMEOUT)
        .build()
        .map_err(|e| e.to_string())?;
    let resp = client.get(url).send().await.map_err(|e| e.to_string())?;
    if !resp.status().is_success() {
        return Err(format!("http status {}", resp.status()));
    }

    let dir = format!("./uploads/records/{}", device_id);
    px_base::create_dir_all_if_not_exists(&dir).map_err(|e| e.to_string())?;
    let target_path = record_file_path(device_id, filename);
    let mut file = tokio::fs::File::create(&target_path)
        .await
        .map_err(|e| e.to_string())?;

    let mut received: i64 = 0;
    let mut last_reported: i64 = 0;
    let mut stream = resp.bytes_stream();
    while let Some(chunk) = stream.next().await {
        let bytes = chunk.map_err(|e| e.to_string())?;
        file.write_all(&bytes).await.map_err(|e| e.to_string())?;
        received += bytes.len() as i64;
        if received - last_reported >= PROGRESS_STEP_BYTES {
            last_reported = received;
            let _ = gRenderRecordManager
                .update_progress(device_id, filename, received, 0, 0)
                .await;
        }
    }
    tracing::info!(
        "direct pull ok: {}/{} {} bytes",
        device_id,
        filename,
        received
    );
    Ok(received)
}

/// DELETE /{id}  — id = "{device_id}:{filename}" (url-encoded); removes the
/// mongo record and the disk file; device-local files are never touched (7.3)
pub async fn handle_record_delete(
    State(_ctx): State<Arc<Mutex<ConsoleContext>>>,
    Path(id): Path<String>,
) -> Result<Json<RespMessage<String>>, ConsoleApiError> {
    let rec = gRenderRecordManager.remove(&id).await?;
    match rec {
        Some(rec) => {
            crate::record::record_cleaner::delete_record_file(&rec).await;
            Ok(Json(ok_resp("delete ok".to_string())))
        }
        None => Err(ConsoleApiError::RecordNotFound),
    }
}

// keep the file self-contained: no extra aliases

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn filename_whitelist() {
        assert!(is_valid_record_filename(
            "rec_DISPLAY1_20260817_10.30.00.mp4"
        ));
        assert!(is_valid_record_filename("a-b_c.d.mp4"));
        assert!(!is_valid_record_filename(""));
        assert!(!is_valid_record_filename("a.mp4x"));
        assert!(!is_valid_record_filename("a.mkv"));
        assert!(!is_valid_record_filename("../a.mp4"));
        assert!(!is_valid_record_filename("..\\a.mp4"));
        assert!(!is_valid_record_filename("a/b.mp4"));
        assert!(!is_valid_record_filename("a\\b.mp4"));
        assert!(!is_valid_record_filename("a b.mp4"));
        assert!(!is_valid_record_filename("中文.mp4"));
        assert!(!is_valid_record_filename("a;.mp4"));
        assert!(!is_valid_record_filename("a..mp4"));
    }
}
