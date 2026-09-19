//! mongo access for `c_records` (console render records cache).

use crate::console_api_error::ConsoleApiError;
use crate::gConsoleDatabase;
use crate::record::console_render_record::{
    make_record_id, ConsoleRenderRecord, RECORD_STATE_ERROR, RECORD_STATE_FETCHING,
    RECORD_STATE_READY,
};
use futures_util::StreamExt;
use mongodb::bson::doc;
use std::sync::Arc;

pub struct ConsoleRenderRecordManager {}

impl ConsoleRenderRecordManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    pub async fn find(
        &self,
        device_id: &str,
        filename: &str,
    ) -> Result<Option<ConsoleRenderRecord>, ConsoleApiError> {
        let record_collection = gConsoleDatabase.lock().await.records();
        let query_result = record_collection
            .lock()
            .await
            .find_one(doc! {"id": make_record_id(device_id, filename)})
            .await;
        match query_result {
            Ok(record) => Ok(record),
            Err(query_error) => {
                tracing::error!("query c_records error: {}", query_error);
                Err(ConsoleApiError::DatabaseError)
            }
        }
    }

    pub async fn query_by_device(
        &self,
        device_id: &str,
    ) -> Result<Vec<ConsoleRenderRecord>, ConsoleApiError> {
        let record_collection = gConsoleDatabase.lock().await.records();
        let cursor_result = record_collection
            .lock()
            .await
            .find(doc! {"device_id": device_id})
            .await;
        let mut record_cursor = match cursor_result {
            Ok(cursor) => cursor,
            Err(query_error) => {
                tracing::error!("query c_records by device error: {}", query_error);
                return Err(ConsoleApiError::DatabaseError);
            }
        };
        let mut records = Vec::new();
        while let Some(record_result) = record_cursor.next().await {
            match record_result {
                Ok(record) => records.push(record),
                Err(cursor_error) => {
                    tracing::error!("c_records cursor error: {}", cursor_error);
                    break;
                }
            }
        }
        Ok(records)
    }

    /// all records, oldest updated first (cleanup scans)
    pub async fn query_all_oldest_first(
        &self,
    ) -> Result<Vec<ConsoleRenderRecord>, ConsoleApiError> {
        let record_collection = gConsoleDatabase.lock().await.records();
        let cursor_result = record_collection
            .lock()
            .await
            .find(doc! {})
            .sort(doc! {"updated_timestamp": 1})
            .await;
        let mut record_cursor = match cursor_result {
            Ok(cursor) => cursor,
            Err(query_error) => {
                tracing::error!("query all c_records error: {}", query_error);
                return Err(ConsoleApiError::DatabaseError);
            }
        };
        let mut records = Vec::new();
        while let Some(record_result) = record_cursor.next().await {
            match record_result {
                Ok(record) => records.push(record),
                Err(cursor_error) => {
                    tracing::error!("c_records cursor error: {}", cursor_error);
                    break;
                }
            }
        }
        Ok(records)
    }

    /// create or reset a record to the fetching state; the keep flag is
    /// preserved when the record already exists (download-to-console may have
    /// pinned it before the upload starts)
    pub async fn upsert_fetch_start(
        &self,
        device_id: &str,
        filename: &str,
    ) -> Result<(), ConsoleApiError> {
        let now = px_base::get_current_timestamp();
        let record_collection = gConsoleDatabase.lock().await.records();
        let update_result = record_collection
            .lock()
            .await
            .update_one(
                doc! {"id": make_record_id(device_id, filename)},
                doc! {
                    "$set": {
                        "device_id": device_id,
                        "filename": filename,
                        "state": RECORD_STATE_FETCHING,
                        "progress": 0_i64,
                        "error": "",
                        "updated_timestamp": now,
                    },
                    "$setOnInsert": {
                        "keep": false,
                        "size": 0_i64,
                        "mtime": 0_i64,
                        "created_timestamp": now,
                    },
                },
            )
            .upsert(true)
            .await;
        if let Err(update_error) = update_result {
            tracing::error!("upsert c_records fetch-start error: {}", update_error);
            return Err(ConsoleApiError::DatabaseError);
        }
        Ok(())
    }

    pub async fn update_progress(
        &self,
        device_id: &str,
        filename: &str,
        received: i64,
        total: i64,
        mtime: i64,
    ) -> Result<(), ConsoleApiError> {
        let record_collection = gConsoleDatabase.lock().await.records();
        let mut set_doc = doc! {
            "progress": received,
            "updated_timestamp": px_base::get_current_timestamp(),
        };
        if total > 0 {
            set_doc.insert("size", total);
        }
        if mtime > 0 {
            set_doc.insert("mtime", mtime);
        }
        let update_result = record_collection
            .lock()
            .await
            .update_one(
                doc! {"id": make_record_id(device_id, filename)},
                doc! {"$set": set_doc},
            )
            .upsert(true)
            .await;
        if let Err(update_error) = update_result {
            tracing::error!("update c_records progress error: {}", update_error);
            return Err(ConsoleApiError::DatabaseError);
        }
        Ok(())
    }

    pub async fn mark_ready(
        &self,
        device_id: &str,
        filename: &str,
        size: i64,
        mtime: i64,
    ) -> Result<(), ConsoleApiError> {
        let record_collection = gConsoleDatabase.lock().await.records();
        let update_result = record_collection
            .lock()
            .await
            .update_one(
                doc! {"id": make_record_id(device_id, filename)},
                doc! {
                    "$set": {
                        "state": RECORD_STATE_READY,
                        "progress": size,
                        "size": size,
                        "mtime": mtime,
                        "error": "",
                        "updated_timestamp": px_base::get_current_timestamp(),
                    },
                },
            )
            .upsert(true)
            .await;
        if let Err(update_error) = update_result {
            tracing::error!("mark c_records ready error: {}", update_error);
            return Err(ConsoleApiError::DatabaseError);
        }
        Ok(())
    }

    pub async fn mark_error(
        &self,
        device_id: &str,
        filename: &str,
        error: &str,
    ) -> Result<(), ConsoleApiError> {
        let record_collection = gConsoleDatabase.lock().await.records();
        let update_result = record_collection
            .lock()
            .await
            .update_one(
                doc! {"id": make_record_id(device_id, filename)},
                doc! {
                    "$set": {
                        "state": RECORD_STATE_ERROR,
                        "error": error,
                        "updated_timestamp": px_base::get_current_timestamp(),
                    },
                },
            )
            .upsert(true)
            .await;
        if let Err(update_error) = update_result {
            tracing::error!("mark c_records error error: {}", update_error);
            return Err(ConsoleApiError::DatabaseError);
        }
        Ok(())
    }

    pub async fn set_keep(
        &self,
        device_id: &str,
        filename: &str,
        keep: bool,
    ) -> Result<(), ConsoleApiError> {
        let record_collection = gConsoleDatabase.lock().await.records();
        let update_result = record_collection
            .lock()
            .await
            .update_one(
                doc! {"id": make_record_id(device_id, filename)},
                doc! {
                    "$set": {
                        "keep": keep,
                        "updated_timestamp": px_base::get_current_timestamp(),
                    },
                },
            )
            .upsert(true)
            .await;
        if let Err(update_error) = update_result {
            tracing::error!("set c_records keep error: {}", update_error);
            return Err(ConsoleApiError::DatabaseError);
        }
        Ok(())
    }

    pub async fn remove(&self, id: &str) -> Result<Option<ConsoleRenderRecord>, ConsoleApiError> {
        let record_collection = gConsoleDatabase.lock().await.records();
        let found = {
            let query_result = record_collection
                .lock()
                .await
                .find_one(doc! {"id": id})
                .await;
            match query_result {
                Ok(record) => record,
                Err(query_error) => {
                    tracing::error!("find c_records error: {}", query_error);
                    return Err(ConsoleApiError::DatabaseError);
                }
            }
        };
        if found.is_none() {
            return Ok(None);
        }
        let delete_result = record_collection
            .lock()
            .await
            .delete_one(doc! {"id": id})
            .await;
        if let Err(delete_error) = delete_result {
            tracing::error!("delete c_records error: {}", delete_error);
            return Err(ConsoleApiError::DatabaseError);
        }
        Ok(found)
    }
}
