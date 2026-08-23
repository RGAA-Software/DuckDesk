//! Temporary-cache cleanup for console render records (design doc 6.3):
//! TTL 24h (counted from the record's updated_timestamp) and a 10GB total
//! threshold on uploads/records, checked every 10 minutes. keep=true copies
//! are exempt from both (6.4). Threshold cleanup is per-device.

use crate::console_api_error::ConsoleApiError;
use crate::record::console_render_record::ConsoleRenderRecord;
use crate::record::console_render_record_manager::ConsoleRenderRecordManager;
use crate::record::record_tunnel::RecordTunnelManager;
use std::collections::BTreeMap;
use std::sync::Arc;
use std::time::Duration;

pub const RECORD_TTL_MS: i64 = 24 * 3600 * 1000;
pub const RECORD_DIR_THRESHOLD_BYTES: i64 = 10 * 1024 * 1024 * 1024;
pub const CLEAN_INTERVAL: Duration = Duration::from_secs(10 * 60);
pub const RECORDS_DIR: &str = "./uploads/records";

/// pure: ids of keep==false records idle for longer than ttl
pub fn select_ttl_expired(
    records: &[ConsoleRenderRecord],
    now_ms: i64,
    ttl_ms: i64,
) -> Vec<ConsoleRenderRecord> {
    records
        .iter()
        .filter(|r| !r.keep && now_ms - r.updated_timestamp > ttl_ms)
        .cloned()
        .collect()
}

/// pure: threshold cleanup picks whole device groups, oldest (by the group's
/// oldest updated_timestamp) first, until total_bytes fits the threshold.
/// `records` must be sorted oldest-updated first; keep=true is skipped.
/// Returns the device_ids whose (keep==false) files should be deleted.
pub fn select_threshold_devices(
    records_oldest_first: &[ConsoleRenderRecord],
    total_bytes: i64,
    threshold: i64,
) -> Vec<String> {
    if total_bytes <= threshold {
        return vec![];
    }
    // group keep==false records by device, preserving oldest-first order
    let mut groups: BTreeMap<String, (i64, i64)> = BTreeMap::new(); // device -> (oldest_ts, bytes)
    for r in records_oldest_first.iter().filter(|r| !r.keep) {
        let g = groups
            .entry(r.device_id.clone())
            .or_insert((r.updated_timestamp, 0));
        g.0 = g.0.min(r.updated_timestamp);
        g.1 += r.size.max(r.progress);
    }
    let mut ordered: Vec<(String, i64, i64)> = groups
        .into_iter()
        .map(|(d, (ts, bytes))| (d, ts, bytes))
        .collect();
    ordered.sort_by_key(|(_, ts, _)| *ts);

    let mut remaining = total_bytes;
    let mut out = Vec::new();
    for (device, _, bytes) in ordered {
        if remaining <= threshold {
            break;
        }
        remaining -= bytes;
        out.push(device);
    }
    out
}

pub fn record_file_path(device_id: &str, filename: &str) -> String {
    format!("{}/{}/{}", RECORDS_DIR, device_id, filename)
}

/// delete the disk file (and the device dir when empty); returns file size freed
pub async fn delete_record_file(rec: &ConsoleRenderRecord) {
    let path = record_file_path(&rec.device_id, &rec.filename);
    if let Err(e) = tokio::fs::remove_file(&path).await {
        if e.kind() != std::io::ErrorKind::NotFound {
            tracing::warn!("delete record file {} failed: {}", path, e);
        }
    }
    // remove the device dir when empty
    let dir = format!("{}/{}", RECORDS_DIR, rec.device_id);
    let _ = tokio::fs::remove_dir(&dir).await;
}

pub fn start_cleanup_task(mgr: Arc<ConsoleRenderRecordManager>, tunnel: Arc<RecordTunnelManager>) {
    tokio::spawn(async move {
        let _ = px_base::create_dir_all_if_not_exists(RECORDS_DIR);
        loop {
            tokio::time::sleep(CLEAN_INTERVAL).await;
            if let Err(e) = run_cleanup_once(&mgr, &tunnel).await {
                tracing::error!("record cleanup round failed: {:?}", e);
            }
        }
    });
}

pub async fn run_cleanup_once(
    mgr: &Arc<ConsoleRenderRecordManager>,
    tunnel: &Arc<RecordTunnelManager>,
) -> Result<(), ConsoleApiError> {
    tunnel.purge_expired_tokens();

    let records = mgr.query_all_oldest_first().await?;
    let now = px_base::get_current_timestamp();

    // pass 1: TTL
    for rec in select_ttl_expired(&records, now, RECORD_TTL_MS) {
        tracing::info!("record cleanup ttl: {}/{}", rec.device_id, rec.filename);
        if mgr.remove(&rec.id).await?.is_some() {
            delete_record_file(&rec).await;
        }
    }

    // pass 2: disk threshold (per device, oldest first)
    let total = px_base::calculate_dir_size(RECORDS_DIR.to_string());
    if total > RECORD_DIR_THRESHOLD_BYTES {
        let records = mgr.query_all_oldest_first().await?;
        for device_id in select_threshold_devices(&records, total, RECORD_DIR_THRESHOLD_BYTES) {
            tracing::info!("record cleanup threshold: device {}", device_id);
            let dev_records = mgr.query_by_device(&device_id).await?;
            for rec in dev_records.into_iter().filter(|r| !r.keep) {
                if mgr.remove(&rec.id).await?.is_some() {
                    delete_record_file(&rec).await;
                }
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::record::console_render_record::RECORD_STATE_READY;

    fn rec(id: &str, device: &str, keep: bool, updated: i64, size: i64) -> ConsoleRenderRecord {
        ConsoleRenderRecord {
            id: id.to_string(),
            device_id: device.to_string(),
            filename: format!("{}.mp4", id),
            size,
            keep,
            state: RECORD_STATE_READY.to_string(),
            updated_timestamp: updated,
            ..Default::default()
        }
    }

    #[test]
    fn ttl_selects_only_idle_non_kept() {
        let now = 100_000i64;
        let ttl = 24 * 3600 * 1000;
        let records = vec![
            rec("a", "d1", false, now - ttl - 1, 10), // expired
            rec("b", "d1", true, now - ttl - 1, 10),  // expired but kept
            rec("c", "d2", false, now - 1000, 10),    // fresh
        ];
        let expired = select_ttl_expired(&records, now, ttl);
        assert_eq!(expired.len(), 1);
        assert_eq!(expired[0].id, "a");
    }

    #[test]
    fn threshold_noop_when_under_limit() {
        let records = vec![rec("a", "d1", false, 1, 100)];
        assert!(select_threshold_devices(&records, 100, 1000).is_empty());
    }

    #[test]
    fn threshold_picks_oldest_devices_and_skips_keep() {
        // total 1000, threshold 400 -> must free >= 600
        let records = vec![
            rec("a", "d1", false, 1, 300), // d1 oldest, 300
            rec("b", "d2", false, 2, 400), // d2, 400
            rec("c", "d3", false, 3, 200), // d3 newest, 200
            rec("k", "d4", true, 0, 1000), // kept: never selected, size ignored
        ];
        let devices = select_threshold_devices(&records, 1000, 400);
        // d1 (300) not enough (700 left > 400) -> d2 too (300 left <= 400)
        assert_eq!(devices, vec!["d1".to_string(), "d2".to_string()]);
        assert!(!devices.contains(&"d4".to_string()));
    }
}
