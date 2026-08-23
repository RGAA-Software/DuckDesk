use crate::console_api_error::ConsoleApiError;
use crate::gConsoleDatabase;
use crate::record::console_file_transfer::{ConsoleFileTransfer, ConsoleUpdateFileTransfer};
use futures_util::StreamExt;
use mongodb::bson::{doc, Bson};
use std::collections::HashMap;
use std::sync::Arc;

pub struct ConsoleFileTransferManager {}

impl ConsoleFileTransferManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    pub async fn insert_file_transfer_info(
        &self,
        mut info: ConsoleFileTransfer,
    ) -> Result<ConsoleFileTransfer, ConsoleApiError> {
        validate_file_transfer_start(&info)?;
        info.status = if info.end > 0 {
            if info.success { "succeeded" } else { "failed" }.to_string()
        } else {
            "running".to_string()
        };
        let c_file_transfer_info = gConsoleDatabase.lock().await.file_transfer();
        let coll = c_file_transfer_info.lock().await;

        tracing::info!("insert new file_transfer {:?}", info);
        let filter = doc! { "the_file_id": &info.the_file_id };
        let insert_doc =
            mongodb::bson::to_document(&info).map_err(|_| ConsoleApiError::InvalidParams)?;
        let r = coll
            .update_one(filter.clone(), doc! { "$setOnInsert": insert_doc })
            .upsert(true)
            .await;
        if let Err(e) = r {
            tracing::error!("insert/replace error: {}", e);
            return Err(ConsoleApiError::DatabaseError);
        }
        coll.find_one(filter)
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?
            .ok_or(ConsoleApiError::FileTransferNotFound)
    }

    pub async fn update_file_transfer_info(
        &self,
        update: ConsoleUpdateFileTransfer,
        reporter_device: Option<&str>,
    ) -> Result<ConsoleFileTransfer, ConsoleApiError> {
        if update.the_file_id.is_empty() {
            return Err(ConsoleApiError::InvalidParams);
        }

        let c_file_trans_info = gConsoleDatabase.lock().await.file_transfer();
        let coll = c_file_trans_info.lock().await;

        if update.end <= 0 || update.duration < 0 || update.end_reason.len() > 128 {
            return Err(ConsoleApiError::InvalidParams);
        }
        let status = normalize_file_terminal_status(&update.status, update.success)?;
        let filter = doc! { "the_file_id": &update.the_file_id };
        let existing = coll
            .find_one(filter.clone())
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?
            .ok_or(ConsoleApiError::FileTransferNotFound)?;
        if let Some(device_id) = reporter_device {
            if device_id != existing.visitor_device && device_id != existing.target_device {
                return Err(ConsoleApiError::Forbidden);
            }
        }
        let existing_status = if existing.status.is_empty() {
            if existing.end > 0 {
                if existing.success {
                    "succeeded"
                } else {
                    "failed"
                }
            } else {
                "running"
            }
        } else {
            existing.status.as_str()
        };
        let duration = if existing.begin > 0 {
            if update.end < existing.begin {
                return Err(ConsoleApiError::InvalidParams);
            }
            update.end - existing.begin
        } else {
            update.duration
        };
        if existing_status != "running" {
            if existing.end == update.end
                && existing.duration == duration
                && existing.success == update.success
                && existing_status == status
            {
                return Ok(existing);
            }
            return Err(ConsoleApiError::VersionConflict);
        }

        let update_doc = doc! {
            "$set": {
                "end": update.end,
                "success": update.success,
                "duration": duration,
                "status": status,
                "end_reason": update.end_reason,
                "recovered": update.recovered,
            }
        };

        match coll
            .find_one_and_update(
                doc! {
                    "the_file_id": &update.the_file_id,
                    "$or": [
                        { "status": "running" },
                        { "status": "" },
                        { "status": { "$exists": false }, "end": 0_i64 },
                    ]
                },
                update_doc,
            )
            .return_document(mongodb::options::ReturnDocument::After)
            .await
        {
            Ok(Some(doc)) => Ok(doc),
            Ok(None) => Err(ConsoleApiError::FileTransferNotFound),
            Err(e) => {
                tracing::error!("update file transfer error: {}", e);
                Err(ConsoleApiError::DatabaseError)
            }
        }
    }

    #[allow(clippy::too_many_arguments)]
    pub async fn query_info<T>(
        &self,
        page: i32,
        page_size: i32,
        filters: HashMap<String, T>,
        sort_field: Option<String>,
        sort_order: Option<i32>,          // 1= asc, -1=dec
        visit_device_id: Option<String>,  // like
        target_device_id: Option<String>, // like
    ) -> Result<Vec<ConsoleFileTransfer>, ConsoleApiError>
    where
        T: Into<Bson>,
    {
        let c_file_transfer_info = gConsoleDatabase.lock().await.file_transfer();
        let limit = page_size as i64;
        let filter = Self::build_filter(filters, visit_device_id, target_device_id);

        let sort_doc = if let Some(field) = sort_field {
            let order = sort_order.unwrap_or(1);
            doc! { field: order }
        } else {
            doc! {}
        };

        let cursor = c_file_transfer_info
            .lock()
            .await
            .find(filter)
            .sort(sort_doc)
            .skip(((page - 1) * page_size) as u64)
            .limit(limit)
            .await;
        if let Err(e) = cursor {
            tracing::error!("query file transfer error: {}", e);
            return Err(ConsoleApiError::DatabaseError);
        }
        let mut cursor = cursor.unwrap();

        let mut streams: Vec<ConsoleFileTransfer> = Vec::new();
        while let Some(stream) = cursor.next().await {
            if let Err(e) = stream {
                tracing::error!("error to get stream value in cursor: {}", e);
                break;
            } else {
                streams.push(stream.unwrap());
            }
        }
        Ok(streams)
    }

    pub async fn total_size<T>(
        &self,
        filters: HashMap<String, T>,
        visit_device_id: Option<String>,
        target_device_id: Option<String>,
    ) -> Result<i64, ConsoleApiError>
    where
        T: Into<Bson>,
    {
        let c_file_transfer_info = gConsoleDatabase.lock().await.file_transfer();
        let filter = Self::build_filter(filters, visit_device_id, target_device_id);
        let r = c_file_transfer_info
            .lock()
            .await
            .count_documents(filter)
            .await;
        if let Err(_e) = r {
            return Err(ConsoleApiError::DatabaseError);
        }
        Ok(r.unwrap() as i64)
    }

    fn build_filter<T>(
        filters: HashMap<String, T>,
        visit_device_id: Option<String>,
        target_device_id: Option<String>,
    ) -> mongodb::bson::Document
    where
        T: Into<Bson>,
    {
        let mut filter = doc! {};
        for (key, value) in filters {
            filter.insert(key, value.into());
        }

        if let Some(v) = visit_device_id {
            if !v.is_empty() {
                filter.insert(
                    "visitor_device",
                    doc! {
                        "$regex": v,
                        "$options": "i" // 不区分大小写（可选）
                    },
                );
            }
        }

        if let Some(v) = target_device_id {
            if !v.is_empty() {
                filter.insert(
                    "target_device",
                    doc! {
                        "$regex": v,
                        "$options": "i"
                    },
                );
            }
        }

        filter
    }
}

fn validate_file_transfer_start(info: &ConsoleFileTransfer) -> Result<(), ConsoleApiError> {
    if info.the_file_id.trim().is_empty()
        || info.the_file_id.len() > 256
        || info.visitor_device.trim().is_empty()
        || info.visitor_device.len() > 256
        || info.target_device.trim().is_empty()
        || info.target_device.len() > 256
        || !matches!(info.direction.as_str(), "In" | "Out")
        || info.file_detail.len() > 4096
        || info.begin <= 0
        || info.duration < 0
        || (info.end > 0 && info.end < info.begin)
    {
        return Err(ConsoleApiError::InvalidParams);
    }
    Ok(())
}

fn normalize_file_terminal_status(
    status: &str,
    success: bool,
) -> Result<&'static str, ConsoleApiError> {
    match status.trim().to_ascii_lowercase().as_str() {
        "" => Ok(if success { "succeeded" } else { "failed" }),
        "succeeded" if success => Ok("succeeded"),
        "failed" if !success => Ok("failed"),
        "aborted" if !success => Ok("aborted"),
        _ => Err(ConsoleApiError::InvalidParams),
    }
}

#[cfg(test)]
mod lifecycle_tests {
    use super::*;

    fn valid_transfer() -> ConsoleFileTransfer {
        ConsoleFileTransfer {
            the_file_id: "f1".into(),
            visitor_device: "visitor".into(),
            target_device: "target".into(),
            begin: 100,
            end: 0,
            direction: "In".into(),
            file_detail: "file.bin".into(),
            success: false,
            duration: 0,
            created_timestamp: 0,
            total: 0,
            status: String::new(),
            end_reason: String::new(),
            recovered: false,
        }
    }

    #[test]
    fn file_start_requires_valid_direction_and_time() {
        let mut item = valid_transfer();
        assert!(validate_file_transfer_start(&item).is_ok());
        item.direction = "sideways".into();
        assert_eq!(
            validate_file_transfer_start(&item),
            Err(ConsoleApiError::InvalidParams)
        );
    }

    #[test]
    fn file_terminal_status_must_match_success() {
        assert_eq!(
            normalize_file_terminal_status("succeeded", true),
            Ok("succeeded")
        );
        assert_eq!(
            normalize_file_terminal_status("aborted", false),
            Ok("aborted")
        );
        assert_eq!(
            normalize_file_terminal_status("failed", true),
            Err(ConsoleApiError::InvalidParams)
        );
    }
}
