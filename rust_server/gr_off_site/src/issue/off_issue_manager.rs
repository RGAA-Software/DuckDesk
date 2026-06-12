use std::collections::HashMap;
use std::sync::Arc;
use futures_util::StreamExt;
use mongodb::bson::{doc, Bson};
use tokio::sync::Mutex;
use crate::{gOffDatabase, gOffIssueManager};
use crate::issue::off_issue::OffIssue;
use crate::off_api_error::OffApiError;
use crate::off_api_error::OffApiError::DatabaseError;
use crate::off_api_keys::{KEY_ITEM_ID, KEY_PROCESSED, KEY_UPDATED_TS, KEY_UPDATED_TS_READABLE};

pub struct OffIssueManager {

}

impl OffIssueManager {
    pub fn new() -> Arc<Mutex<Self>> {
        Arc::new(Mutex::new(Self{}))
    }

    pub async fn insert_issue(&self, issue: OffIssue)-> Result<OffIssue, OffApiError> {
        let r = gOffDatabase
            .lock().await
            .issue().await
            .lock().await
            .insert_one(issue.clone()).await;
        if let Err(e) = r {
            return Err(DatabaseError)
        }
        Ok(issue)
    }

    pub async fn query_issues<T>(
        &self,
        page: i32,
        page_size: i32,
        filters: HashMap<String, T>,
        sort_field: Option<String>,
        sort_order: Option<i32>, // 1= asc, -1=dec
    ) -> Result<Vec<OffIssue>, OffApiError> where T: Into<Bson> {
        let c_issue = gOffDatabase
            .lock().await
            .issue().await;
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

        let cursor = c_issue.lock().await
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

        let mut streams: Vec<OffIssue> = Vec::new();
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
        let c_issue = gOffDatabase
            .lock().await
            .issue().await;
        let r = c_issue
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
            return Err(DatabaseError);
        }
        let r = r.unwrap();
        tracing::info!("mark processed, match: {}, modified: {}", r.matched_count, r.modified_count);
        if r.matched_count > 0 {
            Ok(())
        }
        else {
            Err(OffApiError::ItemNotFound)
        }
    }
}