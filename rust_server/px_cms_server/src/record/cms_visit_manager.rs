use futures_util::StreamExt;
use mongodb::bson::{doc, Bson};
use std::collections::HashMap;
use std::sync::Arc;

use crate::cms_api_error::CmsApiError;
use crate::gCmsDatabase;
use crate::record::cms_visit::{CmsUpdateVisit, CmsVisit};

pub struct CmsVisitManager {}

impl CmsVisitManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    pub async fn insert_visit_info(&self, mut info: CmsVisit) -> Result<CmsVisit, CmsApiError> {
        validate_visit_start(&info)?;
        info.status = if info.end > 0 {
            "succeeded".to_string()
        } else {
            "running".to_string()
        };
        let c_visit_info = gCmsDatabase.lock().await.visit();
        let coll = c_visit_info.lock().await;

        tracing::info!("insert new visit {:?}", info);
        let filter = doc! { "conn_id": &info.conn_id };
        let insert_doc =
            mongodb::bson::to_document(&info).map_err(|_| CmsApiError::InvalidParams)?;
        let r = coll
            .update_one(filter.clone(), doc! { "$setOnInsert": insert_doc })
            .upsert(true)
            .await;
        if let Err(e) = r {
            tracing::error!("insert/replace error: {}", e);
            return Err(CmsApiError::DatabaseError);
        }
        coll.find_one(filter)
            .await
            .map_err(|_| CmsApiError::DatabaseError)?
            .ok_or(CmsApiError::VisitNotFound)
    }

    pub async fn update_visit_info(
        &self,
        update: CmsUpdateVisit,
        reporter_device: Option<&str>,
    ) -> Result<CmsVisit, CmsApiError> {
        if update.conn_id.is_empty() {
            return Err(CmsApiError::InvalidParams);
        }

        let c_visit_info = gCmsDatabase.lock().await.visit();
        let coll = c_visit_info.lock().await;

        if update.end <= 0 || update.duration < 0 || update.end_reason.len() > 128 {
            return Err(CmsApiError::InvalidParams);
        }
        let status = normalize_visit_terminal_status(&update.status)?;
        let filter = doc! { "conn_id": &update.conn_id };
        let existing = coll
            .find_one(filter.clone())
            .await
            .map_err(|_| CmsApiError::DatabaseError)?
            .ok_or(CmsApiError::VisitNotFound)?;
        if let Some(device_id) = reporter_device {
            if device_id != existing.visitor_device && device_id != existing.target_device {
                return Err(CmsApiError::Forbidden);
            }
        }

        let existing_status = if existing.status.is_empty() {
            if existing.end > 0 {
                "succeeded"
            } else {
                "running"
            }
        } else {
            existing.status.as_str()
        };
        let duration = if existing.begin > 0 {
            if update.end < existing.begin {
                return Err(CmsApiError::InvalidParams);
            }
            update.end - existing.begin
        } else {
            update.duration
        };
        if existing_status != "running" {
            if existing.end == update.end
                && existing.duration == duration
                && existing_status == status
            {
                return Ok(existing);
            }
            return Err(CmsApiError::VersionConflict);
        }

        let update_doc = doc! {
            "$set": {
                "end": update.end,
                "duration": duration,
                "status": status,
                "end_reason": update.end_reason,
                "recovered": update.recovered,
            }
        };

        match coll
            .find_one_and_update(
                doc! {
                    "conn_id": &update.conn_id,
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
            Ok(None) => Err(CmsApiError::VisitNotFound),
            Err(e) => {
                tracing::error!("update visit error: {}", e);
                Err(CmsApiError::DatabaseError)
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
    ) -> Result<Vec<CmsVisit>, CmsApiError>
    where
        T: Into<Bson>,
    {
        let c_visit_info = gCmsDatabase.lock().await.visit();
        let limit = page_size as i64;
        let filter = Self::build_filter(filters, visit_device_id, target_device_id);

        let sort_doc = if let Some(field) = sort_field {
            let order = sort_order.unwrap_or(1);
            doc! { field: order }
        } else {
            doc! {}
        };

        let cursor = c_visit_info
            .lock()
            .await
            .find(filter)
            .sort(sort_doc)
            .skip(((page - 1) * page_size) as u64)
            .limit(limit)
            .await;
        if let Err(e) = cursor {
            tracing::error!("query visit error: {}", e);
            return Err(CmsApiError::DatabaseError);
        }
        let mut cursor = cursor.unwrap();

        let mut streams: Vec<CmsVisit> = Vec::new();
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
    ) -> Result<i64, CmsApiError>
    where
        T: Into<Bson>,
    {
        let c_visit_info = gCmsDatabase.lock().await.visit();
        let filter = Self::build_filter(filters, visit_device_id, target_device_id);
        let r = c_visit_info.lock().await.count_documents(filter).await;
        if let Err(_e) = r {
            return Err(CmsApiError::DatabaseError);
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

fn validate_visit_start(info: &CmsVisit) -> Result<(), CmsApiError> {
    if info.conn_id.trim().is_empty()
        || info.conn_id.len() > 256
        || info.stream_id.len() > 256
        || info.conn_type.len() > 32
        || info.visitor_device.trim().is_empty()
        || info.visitor_device.len() > 256
        || info.target_device.trim().is_empty()
        || info.target_device.len() > 256
        || info.begin <= 0
        || info.duration < 0
        || (info.end > 0 && info.end < info.begin)
    {
        return Err(CmsApiError::InvalidParams);
    }
    Ok(())
}

fn normalize_visit_terminal_status(status: &str) -> Result<&'static str, CmsApiError> {
    match status.trim().to_ascii_lowercase().as_str() {
        "" | "succeeded" => Ok("succeeded"),
        "aborted" => Ok("aborted"),
        _ => Err(CmsApiError::InvalidParams),
    }
}

#[cfg(test)]
mod lifecycle_tests {
    use super::*;

    #[test]
    fn visit_start_requires_identity_and_monotonic_time() {
        let mut visit = CmsVisit {
            conn_id: "c1".into(),
            stream_id: "s1".into(),
            conn_type: "Direct".into(),
            visitor_device: "visitor".into(),
            target_device: "target".into(),
            begin: 100,
            ..Default::default()
        };
        assert!(validate_visit_start(&visit).is_ok());
        visit.end = 99;
        assert_eq!(
            validate_visit_start(&visit),
            Err(CmsApiError::InvalidParams)
        );
        visit.end = 0;
        visit.conn_id.clear();
        assert_eq!(
            validate_visit_start(&visit),
            Err(CmsApiError::InvalidParams)
        );
    }

    #[test]
    fn visit_terminal_status_is_bounded() {
        assert_eq!(normalize_visit_terminal_status(""), Ok("succeeded"));
        assert_eq!(normalize_visit_terminal_status("aborted"), Ok("aborted"));
        assert_eq!(
            normalize_visit_terminal_status("running"),
            Err(CmsApiError::InvalidParams)
        );
    }
}
