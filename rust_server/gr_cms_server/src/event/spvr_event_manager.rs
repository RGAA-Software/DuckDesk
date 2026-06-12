use std::collections::HashMap;
use std::sync::Arc;
use mongodb::bson::{doc, Bson, Document};
use tokio::sync::Mutex;
use tokio_stream::StreamExt;
use crate::{gSpvrDatabase};
use crate::spvr_api_error::SpvrApiError;
use crate::event::spvr_event::{SpvrEvent};
use crate::event::spvr_event_keys::{EVENT_TYPE, KEY_EVENT_ID};

pub struct SpvrEventManager {

}

impl SpvrEventManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    pub async fn add_event(&self, warn: SpvrEvent) -> Result<(), SpvrApiError> {
        let r = gSpvrDatabase
            .lock().await
            .event()
            .lock().await
            .insert_one(warn).await;
        if let Err(err) = r {
            tracing::error!("failed to insert warn: {}", err);
            return Err(SpvrApiError::DatabaseError);
        }
        Ok(())
    }

    pub async fn remove_event(&self, warn_id: String) -> Result<(), SpvrApiError> {
        let r = gSpvrDatabase
        .lock().await
            .event()
            .lock().await
            .delete_one(doc!{KEY_EVENT_ID: warn_id.clone()}).await;
        if let Err(err) = r {
            tracing::error!("failed to remove warn: {}", err);
            return Err(SpvrApiError::DatabaseError);
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
        sort_order: Option<i32>, // 1= asc, -1=dec
    ) -> Result<Vec<SpvrEvent>, SpvrApiError> where T: Into<Bson> {
        let skip = (page-1) * page_size;
        let limit = page_size as i64;

        let mut event_type: Bson = Bson::String("".to_string());

        let mut and_conditions: Vec<Document> = Vec::new();

        for (key, value) in filters {
            if key == EVENT_TYPE {
                event_type = value.into().clone();
                and_conditions.push(doc!{
                    key: event_type.clone()
                });
            }
            else {
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

        let r = gSpvrDatabase
            .lock().await
            .event()
            .lock().await
            .find(filter.clone())
            .sort(sort_doc)
            .skip(skip as u64)
            .limit(limit).await;
        if let Err(err) = r {
            tracing::error!("failed to query warns: {}", err);
            return Err(SpvrApiError::DatabaseError);
        }

        let total = self.count_total_events_with_filters(filter).await?;

        let mut cursor = r.unwrap();
        let mut events: Vec<SpvrEvent> = Vec::new();
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

    pub async fn count_total_events(&self, event_type: String) -> Result<u64, SpvrApiError> {
        let filter = if event_type.is_empty() {
            doc! {}
        } else {
            doc! {EVENT_TYPE: event_type}
        };
        tracing::info!("count_total_events filter: {}", filter);
        gSpvrDatabase
            .lock().await
            .event()
            .lock().await
            .count_documents(filter).await
            .map_err(|err| {
                tracing::error!("failed to count documents: {}", err);
                SpvrApiError::DatabaseError
            })
    }

    pub async fn count_total_events_with_filters(&self, filters: Document) -> Result<u64, SpvrApiError> {
        gSpvrDatabase
            .lock().await
            .event()
            .lock().await
            .count_documents(filters).await
            .map_err(|err| {
                tracing::error!("failed to count documents: {}", err);
                SpvrApiError::DatabaseError
            })
    }
}