use crate::console_api_error::ConsoleApiError;
use crate::gConsoleDatabase;
use crate::stream::console_stream::ConsoleStream;
use crate::stream::console_stream_keys::{
    KEY_STREAM_ID, KEY_STREAM_NAME, KEY_STREAM_UPDATED_TIMESTAMP,
};
use futures_util::StreamExt;
use mongodb::bson::{doc, Bson};
use std::collections::HashMap;
use std::sync::Arc;

pub struct ConsoleStreamManager {}

impl ConsoleStreamManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    pub async fn insert_stream(
        &self,
        stream: ConsoleStream,
    ) -> Result<ConsoleStream, ConsoleApiError> {
        let stream_id = stream.stream_id.clone();
        let c_stream = gConsoleDatabase.lock().await.stream().clone();
        let r = c_stream.lock().await.insert_one(stream).await;
        if let Err(err) = r {
            tracing::error!("Failed to insert stream: {}", err);
            Err(ConsoleApiError::DatabaseError)
        } else {
            let s = self.query_stream_by_id(stream_id).await?;
            Ok(s)
        }
    }

    pub async fn delete_stream(&self, stream_id: String) -> Result<ConsoleStream, ConsoleApiError> {
        let c_stream = gConsoleDatabase.lock().await.stream().clone();

        let stream = self.query_stream_by_id(stream_id.clone()).await?;

        let r = c_stream
            .lock()
            .await
            .delete_one(doc! {KEY_STREAM_ID: stream_id })
            .await;
        if let Err(err) = r {
            tracing::error!("Failed to delete stream: {}", err);
            Err(ConsoleApiError::DatabaseError)
        } else {
            Ok(stream)
        }
    }

    pub async fn update_stream(
        &self,
        in_stream: ConsoleStream,
    ) -> Result<ConsoleStream, ConsoleApiError> {
        let c_stream = gConsoleDatabase.lock().await.stream().clone();

        let r = c_stream
            .lock()
            .await
            .find_one_and_replace(doc! {KEY_STREAM_ID: in_stream.stream_id.clone()}, in_stream)
            .await;

        if let Err(err) = r {
            tracing::error!("Failed to find and replace stream: {}", err);
            return Err(ConsoleApiError::DatabaseError);
        }
        let r = r.unwrap();
        if let Some(s) = r {
            Ok(s)
        } else {
            Err(ConsoleApiError::StreamNotFound)
        }
    }

    pub async fn update_stream_field<T>(
        &self,
        stream_id: String,
        key: String,
        val: T,
    ) -> Result<ConsoleStream, ConsoleApiError>
    where
        T: Into<Bson>,
    {
        let filter_doc = doc! {KEY_STREAM_ID: stream_id.clone()};
        let mut update_doc = doc! {};
        let mut sub_update_doc = doc! {key: val};
        sub_update_doc.insert(
            KEY_STREAM_UPDATED_TIMESTAMP,
            px_base::get_current_timestamp(),
        );
        update_doc.insert("$set", sub_update_doc);

        let c_stream = gConsoleDatabase.lock().await.stream().clone();

        if let Err(e) = c_stream
            .lock()
            .await
            .update_one(filter_doc, update_doc)
            .await
        {
            tracing::error!("Failed to update stream: {}", e);
            return Err(ConsoleApiError::DatabaseError);
        }

        let s = self.query_stream_by_id(stream_id).await?;
        Ok(s)
    }

    pub async fn query_stream_by_id(
        &self,
        stream_id: String,
    ) -> Result<ConsoleStream, ConsoleApiError> {
        let c_stream = gConsoleDatabase.lock().await.stream().clone();
        let r = c_stream
            .lock()
            .await
            .find_one(doc! {KEY_STREAM_ID: stream_id})
            .await;
        if let Err(err) = r {
            tracing::error!("Failed to find stream: {}", err);
            return Err(ConsoleApiError::DatabaseError);
        }
        let s = r.unwrap();
        if let Some(s) = s {
            Ok(s)
        } else {
            Err(ConsoleApiError::StreamNotFound)
        }
    }

    pub async fn query_stream_by_name(
        &self,
        stream_name: String,
    ) -> Result<ConsoleStream, ConsoleApiError> {
        let c_stream = gConsoleDatabase.lock().await.stream().clone();
        let r = c_stream
            .lock()
            .await
            .find_one(doc! {KEY_STREAM_NAME: stream_name})
            .await;
        if let Err(err) = r {
            tracing::error!("Failed to find stream: {}", err);
            return Err(ConsoleApiError::DatabaseError);
        }
        let s = r.unwrap();
        if let Some(s) = s {
            Ok(s)
        } else {
            Err(ConsoleApiError::StreamNotFound)
        }
    }

    pub async fn query_streams<T>(
        &self,
        page: i32,
        page_size: i32,
        filters: HashMap<String, T>,
        sort_field: Option<String>,
        sort_order: Option<i32>, // 1= asc, -1=dec
    ) -> Result<Vec<ConsoleStream>, ConsoleApiError>
    where
        T: Into<Bson>,
    {
        let c_stream = gConsoleDatabase.lock().await.stream().clone();
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

        let cursor = c_stream
            .lock()
            .await
            .find(filter)
            .sort(sort_doc)
            .skip(skip as u64)
            .limit(limit)
            .await;
        if let Err(e) = cursor {
            tracing::error!("query users error: {}", e);
            return Err(ConsoleApiError::DatabaseError);
        }
        let mut cursor = cursor.unwrap();

        let mut streams: Vec<ConsoleStream> = Vec::new();
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
}
