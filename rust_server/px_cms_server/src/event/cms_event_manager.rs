use crate::cms_api_error::CmsApiError;
use crate::event::cms_event::CmsEvent;
use crate::event::cms_event_keys::{
    EVENT_CPU, EVENT_DISK, EVENT_GPU, EVENT_MEMORY, EVENT_TYPE, KEY_CPU_USAGE, KEY_DISK_PATH,
    KEY_DISK_USAGE, KEY_EVENT_ID, KEY_GPU_ID, KEY_GPU_USAGE, KEY_MEMORY_USAGE,
};
use crate::gCmsDatabase;
use mongodb::bson::{doc, to_document, Bson, Document};
use std::collections::HashMap;
use std::sync::Arc;
use tokio_stream::StreamExt;

pub struct CmsEventManager {}

impl CmsEventManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    pub async fn add_event(&self, warn: CmsEvent) -> Result<(), CmsApiError> {
        let r = gCmsDatabase
            .lock()
            .await
            .event()
            .lock()
            .await
            .insert_one(warn)
            .await;
        if let Err(err) = r {
            tracing::error!("failed to insert warn: {}", err);
            return Err(CmsApiError::DatabaseError);
        }
        Ok(())
    }

    /// Coalesce identical telemetry reports into one event document. The
    /// latest time and device/user metadata are refreshed, while the first
    /// time and occurrence count retain the history represented by the row.
    /// Legacy duplicate documents for the same identity are removed after the
    /// canonical row has been updated.
    pub async fn add_or_refresh_telemetry_event(
        &self,
        mut event: CmsEvent,
    ) -> Result<CmsEvent, CmsApiError> {
        let identity = telemetry_identity_filter(&event).ok_or(CmsApiError::InvalidParams)?;
        let collection = gCmsDatabase.lock().await.event();
        let collection = collection.lock().await;

        let latest = collection
            .find_one(identity.clone())
            .sort(doc! { "timestamp": -1 })
            .await
            .map_err(|err| {
                tracing::error!("failed to find telemetry event: {}", err);
                CmsApiError::DatabaseError
            })?;

        let Some(latest) = latest else {
            collection.insert_one(event.clone()).await.map_err(|err| {
                tracing::error!("failed to insert telemetry event: {}", err);
                CmsApiError::DatabaseError
            })?;
            return Ok(event);
        };

        let earliest = collection
            .find_one(identity.clone())
            .sort(doc! { "timestamp": 1 })
            .await
            .map_err(|err| {
                tracing::error!("failed to find first telemetry event: {}", err);
                CmsApiError::DatabaseError
            })?
            .unwrap_or_else(|| latest.clone());
        let duplicate_count =
            collection
                .count_documents(identity.clone())
                .await
                .map_err(|err| {
                    tracing::error!("failed to count duplicate telemetry events: {}", err);
                    CmsApiError::DatabaseError
                })?;

        event.event_id = latest.event_id.clone();
        event.first_timestamp = if earliest.first_timestamp > 0 {
            earliest.first_timestamp
        } else {
            earliest.timestamp
        };
        event.first_readable_timestamp = if !earliest.first_readable_timestamp.is_empty() {
            earliest.first_readable_timestamp.clone()
        } else {
            earliest.readable_timestamp.clone()
        };
        event.occurrence_count = latest.occurrence_count.max(duplicate_count).max(1) + 1;

        let mut set_fields = to_document(&event).map_err(|err| {
            tracing::error!("failed to serialize telemetry event: {}", err);
            CmsApiError::DatabaseError
        })?;
        set_fields.remove(KEY_EVENT_ID);
        set_fields.remove("total");
        collection
            .update_one(
                doc! { KEY_EVENT_ID: &event.event_id },
                doc! { "$set": set_fields },
            )
            .await
            .map_err(|err| {
                tracing::error!("failed to refresh telemetry event: {}", err);
                CmsApiError::DatabaseError
            })?;

        if duplicate_count > 1 {
            let mut duplicate_filter = identity;
            duplicate_filter.insert(KEY_EVENT_ID, doc! { "$ne": &event.event_id });
            let deleted = collection
                .delete_many(duplicate_filter)
                .await
                .map_err(|err| {
                    tracing::error!("failed to compact duplicate telemetry events: {}", err);
                    CmsApiError::DatabaseError
                })?
                .deleted_count;
            tracing::info!(
                "compacted {} duplicate telemetry events into {} (occurrences={})",
                deleted,
                event.event_id,
                event.occurrence_count
            );
        }

        Ok(event)
    }

    pub async fn remove_event(&self, warn_id: String) -> Result<(), CmsApiError> {
        let r = gCmsDatabase
            .lock()
            .await
            .event()
            .lock()
            .await
            .delete_one(doc! {KEY_EVENT_ID: warn_id.clone()})
            .await;
        if let Err(err) = r {
            tracing::error!("failed to remove warn: {}", err);
            return Err(CmsApiError::DatabaseError);
        }
        tracing::info!("removed warn: {}", warn_id);
        Ok(())
    }

    pub async fn query_events<T>(
        &self,
        page: i32,
        page_size: i32,
        filters: HashMap<String, T>,
        sort_field: Option<String>, // sort field
        sort_order: Option<i32>,    // 1= asc, -1=dec
    ) -> Result<Vec<CmsEvent>, CmsApiError>
    where
        T: Into<Bson>,
    {
        let skip = (page - 1) * page_size;
        let limit = page_size as i64;

        let mut and_conditions: Vec<Document> = Vec::new();

        for (key, value) in filters {
            if key == EVENT_TYPE {
                let event_type: Bson = value.into().clone();
                and_conditions.push(doc! {
                    key: event_type.clone()
                });
            } else {
                and_conditions.push(doc! {
                    key: {
                        "$regex": value,
                        "$options": "i"
                    }
                });
            }
        }

        let filter = if and_conditions.is_empty() {
            doc! {} // 全部为空 → 查询全部
        } else {
            doc! {
                "$and": and_conditions
            }
        };

        let sort_doc = if let Some(field) = sort_field {
            let order = sort_order.unwrap_or(-1);
            doc! { field: order }
        } else {
            doc! {}
        };

        let r = gCmsDatabase
            .lock()
            .await
            .event()
            .lock()
            .await
            .find(filter.clone())
            .sort(sort_doc)
            .skip(skip as u64)
            .limit(limit)
            .await;
        if let Err(err) = r {
            tracing::error!("failed to query warns: {}", err);
            return Err(CmsApiError::DatabaseError);
        }

        let total = self.count_total_events_with_filters(filter).await?;

        let mut cursor = r.unwrap();
        let mut events: Vec<CmsEvent> = Vec::new();
        while let Some(event) = cursor.next().await {
            if let Err(err) = event {
                tracing::error!("failed to query warn: {}", err);
                break;
            }
            let mut event = event.unwrap();
            event.total = total;
            events.push(event);
        }
        Ok(events)
    }

    pub async fn count_total_events(&self, event_type: String) -> Result<u64, CmsApiError> {
        let filter = if event_type.is_empty() {
            doc! {}
        } else {
            doc! {EVENT_TYPE: event_type}
        };
        tracing::info!("count_total_events filter: {}", filter);
        gCmsDatabase
            .lock()
            .await
            .event()
            .lock()
            .await
            .count_documents(filter)
            .await
            .map_err(|err| {
                tracing::error!("failed to count documents: {}", err);
                CmsApiError::DatabaseError
            })
    }

    pub async fn count_total_events_with_filters(
        &self,
        filters: Document,
    ) -> Result<u64, CmsApiError> {
        gCmsDatabase
            .lock()
            .await
            .event()
            .lock()
            .await
            .count_documents(filters)
            .await
            .map_err(|err| {
                tracing::error!("failed to count documents: {}", err);
                CmsApiError::DatabaseError
            })
    }
}

fn telemetry_identity_filter(event: &CmsEvent) -> Option<Document> {
    let base = doc! {
        EVENT_TYPE: &event.event_type,
        "device_id": &event.device_id,
    };
    match event.event_type.as_str() {
        EVENT_CPU => Some(doc! {
            "$and": [base, { KEY_CPU_USAGE: event.cpu_usage as i64 }]
        }),
        EVENT_MEMORY => Some(doc! {
            "$and": [base, { KEY_MEMORY_USAGE: event.mem_usage as i64 }]
        }),
        EVENT_DISK => Some(doc! {
            "$and": [base, {
                KEY_DISK_PATH: &event.disk_path,
                KEY_DISK_USAGE: event.disk_usage as i64,
            }]
        }),
        EVENT_GPU => Some(doc! {
            "$and": [base, {
                KEY_GPU_ID: &event.gpu_id,
                KEY_GPU_USAGE: event.gpu_usage as i64,
            }]
        }),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn disk_identity_contains_device_path_and_usage() {
        let event = CmsEvent::new_disk(
            "device-1".into(),
            "127.0.0.1".into(),
            "node".into(),
            String::new(),
            String::new(),
            97,
            "C:\\".into(),
        );
        let filter = telemetry_identity_filter(&event).expect("disk identity");
        let conditions = filter.get_array("$and").expect("identity conditions");
        let device = conditions[0].as_document().expect("device condition");
        let resource = conditions[1].as_document().expect("resource condition");
        assert_eq!(device.get_str("device_id").unwrap(), "device-1");
        assert_eq!(resource.get_str(KEY_DISK_PATH).unwrap(), "C:\\");
        assert_eq!(resource.get_i64(KEY_DISK_USAGE).unwrap(), 97);
    }

    #[test]
    fn non_telemetry_event_has_no_identity() {
        assert!(telemetry_identity_filter(&CmsEvent::default()).is_none());
    }
}
