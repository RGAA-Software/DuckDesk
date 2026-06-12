use std::collections::HashMap;
use std::sync::Arc;
use futures_util::StreamExt;
use mongodb::bson::{doc, Bson};
use tokio::sync::Mutex;
use crate::consult::off_consult::OffConsult;
use crate::gOffDatabase;
use crate::off_api_error::OffApiError;
use crate::off_api_keys::{KEY_ITEM_ID, KEY_PROCESSED, KEY_UPDATED_TS, KEY_UPDATED_TS_READABLE};

pub struct OffConsultManager {
    
}

impl OffConsultManager {
    pub fn new() -> Arc<Mutex<Self>> {
        Arc::new(Mutex::new(Self{}))
    }

    pub async fn query_consults<T>(
        &self,
        page: i32,
        page_size: i32,
        filters: HashMap<String, T>,
        sort_field: Option<String>,
        sort_order: Option<i32>, // 1= asc, -1=dec
    ) -> Result<Vec<OffConsult>, OffApiError> where T: Into<Bson> {
        let c_consult = gOffDatabase
            .lock().await
            .consult().await;
        let skip = (page-1) * page_size;
        let limit = page_size as i64;
        let mut filter = doc! { };
        for (key, value) in filters {
            filter.insert(key, value.into());
        }

        let sort_doc = if let Some(field) = sort_field {
            let order = sort_order.unwrap_or(1);
            doc! { field: order }
        } else {
            doc! {}
        };

        let cursor = c_consult.lock().await
            .find(filter)
            .sort(sort_doc)
            .skip(skip as u64)
            .limit(limit)
            .await;
        if let Err(e) = cursor {
            tracing::error!("query users error: {}", e);
            return Err(OffApiError::DatabaseError);
        }
        let mut cursor = cursor.unwrap();

        let mut streams: Vec<OffConsult> = Vec::new();
        while let Some(stream) = cursor.next().await {
            if let Err(e) = stream {
                tracing::error!("error to get stream value in cursor: {}", e);
                break;
            }
            else {
                streams.push(stream.unwrap());
            }
        }
        Ok(streams)
    }

    pub async fn mark_processed(&self, id: String, p: bool) -> Result<(), OffApiError> {
        let c_consult = gOffDatabase
            .lock().await
            .consult().await;
        let r = c_consult
            .lock().await
            .update_one(doc!{KEY_ITEM_ID: id},
                        doc! {"$set":
                            doc!{
                                KEY_PROCESSED: p,
                                KEY_UPDATED_TS: gr_base::get_current_timestamp(),
                                KEY_UPDATED_TS_READABLE: gr_base::get_current_readable_timestamp(),
                            }}).await;
        if let Err(e) = r {
            tracing::error!("failed to mark processed user device: {}", e);
            return Err(OffApiError::DatabaseError);
        }
        Ok(())
    }
}