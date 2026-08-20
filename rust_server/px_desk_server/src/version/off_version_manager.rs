use crate::gOffDatabase;
use crate::off_api_error::OffApiError;
use crate::off_api_error::OffApiError::DatabaseError;
use crate::version::off_version::OffVersion;
use futures_util::StreamExt;
use mongodb::bson::{doc, Bson};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct OffVersionManager {
    pub current_version: OffVersion,
}

impl OffVersionManager {
    pub fn new() -> Arc<Mutex<Self>> {
        Arc::new(Mutex::new(Self {
            current_version: Default::default(),
        }))
    }

    pub async fn insert_version(&self, version: OffVersion) -> Result<OffVersion, OffApiError> {
        let r = gOffDatabase
            .lock()
            .await
            .version()
            .await
            .lock()
            .await
            .insert_one(version.clone())
            .await;
        if let Err(_e) = r {
            return Err(DatabaseError);
        }
        Ok(version)
    }

    pub async fn query_versions<T>(
        &self,
        page: i32,
        page_size: i32,
        filters: HashMap<String, T>,
        sort_field: Option<String>,
        sort_order: Option<i32>, // 1= asc, -1=dec
    ) -> Result<Vec<OffVersion>, OffApiError>
    where
        T: Into<Bson>,
    {
        let c_version = gOffDatabase.lock().await.version().await;
        let skip = (page - 1) * page_size;
        let limit = page_size as i64;
        let mut filter = doc! {};
        for (key, value) in filters {
            filter.insert(key, value.into());
        }

        let sort_doc = if let Some(field) = sort_field {
            let order = sort_order.unwrap_or(1);
            doc! { field: order }
        } else {
            doc! {}
        };

        let cursor = c_version
            .lock()
            .await
            .find(filter)
            .sort(sort_doc)
            .skip(skip as u64)
            .limit(limit)
            .await;
        if let Err(e) = cursor {
            tracing::error!("query versions error: {}", e);
            return Err(OffApiError::DatabaseError);
        }
        let mut cursor = cursor.unwrap();

        let mut streams: Vec<OffVersion> = Vec::new();
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

    pub async fn query_latest_version(&self) -> Result<Option<OffVersion>, OffApiError> {
        let mut result = self
            .query_versions::<Bson>(
                1,
                1,
                HashMap::new(),
                Some("created_at".to_string()),
                Some(-1),
            )
            .await?;

        Ok(result.pop())
    }
}
