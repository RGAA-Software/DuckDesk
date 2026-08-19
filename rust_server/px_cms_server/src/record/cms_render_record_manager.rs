//! mongo access for `c_records` (cms render records cache).

use crate::cms_api_error::CmsApiError;
use crate::gCmsDatabase;
use crate::record::cms_render_record::{
    make_record_id, CmsRenderRecord, RECORD_STATE_ERROR, RECORD_STATE_FETCHING, RECORD_STATE_READY,
};
use futures_util::StreamExt;
use mongodb::bson::doc;
use std::sync::Arc;

pub struct CmsRenderRecordManager {}

impl CmsRenderRecordManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    pub async fn find(
        &self,
        device_id: &str,
        filename: &str,
    ) -> Result<Option<CmsRenderRecord>, CmsApiError> {
        let coll = gCmsDatabase.lock().await.records();
        let r = coll
            .lock()
            .await
            .find_one(doc! {"id": make_record_id(device_id, filename)})
            .await;
        match r {
            Ok(v) => Ok(v),
            Err(e) => {
                tracing::error!("query c_records error: {}", e);
                Err(CmsApiError::DatabaseError)
            }
        }
    }

    pub async fn query_by_device(
        &self,
        device_id: &str,
    ) -> Result<Vec<CmsRenderRecord>, CmsApiError> {
        let coll = gCmsDatabase.lock().await.records();
        let cursor = coll.lock().await.find(doc! {"device_id": device_id}).await;
        let mut cursor = match cursor {
            Ok(c) => c,
            Err(e) => {
                tracing::error!("query c_records by device error: {}", e);
                return Err(CmsApiError::DatabaseError);
            }
        };
        let mut out = Vec::new();
        while let Some(item) = cursor.next().await {
            match item {
                Ok(rec) => out.push(rec),
                Err(e) => {
                    tracing::error!("c_records cursor error: {}", e);
                    break;
                }
            }
        }
        Ok(out)
    }

    /// all records, oldest updated first (cleanup scans)
    pub async fn query_all_oldest_first(&self) -> Result<Vec<CmsRenderRecord>, CmsApiError> {
        let coll = gCmsDatabase.lock().await.records();
        let cursor = coll
            .lock()
            .await
            .find(doc! {})
            .sort(doc! {"updated_timestamp": 1})
            .await;
        let mut cursor = match cursor {
            Ok(c) => c,
            Err(e) => {
                tracing::error!("query all c_records error: {}", e);
                return Err(CmsApiError::DatabaseError);
            }
        };
        let mut out = Vec::new();
        while let Some(item) = cursor.next().await {
            match item {
                Ok(rec) => out.push(rec),
                Err(e) => {
                    tracing::error!("c_records cursor error: {}", e);
                    break;
                }
            }
        }
        Ok(out)
    }

    /// create or reset a record to the fetching state; the keep flag is
    /// preserved when the record already exists (download-to-cms may have
    /// pinned it before the upload starts)
    pub async fn upsert_fetch_start(
        &self,
        device_id: &str,
        filename: &str,
    ) -> Result<(), CmsApiError> {
        let now = px_base::get_current_timestamp();
        let coll = gCmsDatabase.lock().await.records();
        let r = coll
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
        if let Err(e) = r {
            tracing::error!("upsert c_records fetch-start error: {}", e);
            return Err(CmsApiError::DatabaseError);
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
    ) -> Result<(), CmsApiError> {
        let coll = gCmsDatabase.lock().await.records();
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
        let r = coll
            .lock()
            .await
            .update_one(
                doc! {"id": make_record_id(device_id, filename)},
                doc! {"$set": set_doc},
            )
            .upsert(true)
            .await;
        if let Err(e) = r {
            tracing::error!("update c_records progress error: {}", e);
            return Err(CmsApiError::DatabaseError);
        }
        Ok(())
    }

    pub async fn mark_ready(
        &self,
        device_id: &str,
        filename: &str,
        size: i64,
        mtime: i64,
    ) -> Result<(), CmsApiError> {
        let coll = gCmsDatabase.lock().await.records();
        let r = coll
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
        if let Err(e) = r {
            tracing::error!("mark c_records ready error: {}", e);
            return Err(CmsApiError::DatabaseError);
        }
        Ok(())
    }

    pub async fn mark_error(
        &self,
        device_id: &str,
        filename: &str,
        error: &str,
    ) -> Result<(), CmsApiError> {
        let coll = gCmsDatabase.lock().await.records();
        let r = coll
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
        if let Err(e) = r {
            tracing::error!("mark c_records error error: {}", e);
            return Err(CmsApiError::DatabaseError);
        }
        Ok(())
    }

    pub async fn set_keep(
        &self,
        device_id: &str,
        filename: &str,
        keep: bool,
    ) -> Result<(), CmsApiError> {
        let coll = gCmsDatabase.lock().await.records();
        let r = coll
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
        if let Err(e) = r {
            tracing::error!("set c_records keep error: {}", e);
            return Err(CmsApiError::DatabaseError);
        }
        Ok(())
    }

    pub async fn remove(&self, id: &str) -> Result<Option<CmsRenderRecord>, CmsApiError> {
        let coll = gCmsDatabase.lock().await.records();
        let found = {
            let r = coll.lock().await.find_one(doc! {"id": id}).await;
            match r {
                Ok(v) => v,
                Err(e) => {
                    tracing::error!("find c_records error: {}", e);
                    return Err(CmsApiError::DatabaseError);
                }
            }
        };
        if found.is_none() {
            return Ok(None);
        }
        let r = coll.lock().await.delete_one(doc! {"id": id}).await;
        if let Err(e) = r {
            tracing::error!("delete c_records error: {}", e);
            return Err(CmsApiError::DatabaseError);
        }
        Ok(found)
    }
}
