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
        let stream_collection = gConsoleDatabase.lock().await.stream().clone();
        let insert_result = stream_collection.lock().await.insert_one(stream).await;
        if let Err(insert_error) = insert_result {
            tracing::error!("Failed to insert stream: {}", insert_error);
            Err(ConsoleApiError::DatabaseError)
        } else {
            let stored_stream = self.query_stream_by_id(stream_id).await?;
            Ok(stored_stream)
        }
    }

    pub async fn delete_stream(&self, stream_id: String) -> Result<ConsoleStream, ConsoleApiError> {
        let stream_collection = gConsoleDatabase.lock().await.stream().clone();

        let stream = self.query_stream_by_id(stream_id.clone()).await?;

        let delete_result = stream_collection
            .lock()
            .await
            .delete_one(doc! {KEY_STREAM_ID: stream_id })
            .await;
        if let Err(delete_error) = delete_result {
            tracing::error!("Failed to delete stream: {}", delete_error);
            Err(ConsoleApiError::DatabaseError)
        } else {
            Ok(stream)
        }
    }

    pub async fn update_stream(
        &self,
        in_stream: ConsoleStream,
    ) -> Result<ConsoleStream, ConsoleApiError> {
        let stream_collection = gConsoleDatabase.lock().await.stream().clone();

        let replace_result = stream_collection
            .lock()
            .await
            .find_one_and_replace(doc! {KEY_STREAM_ID: in_stream.stream_id.clone()}, in_stream)
            .await;

        if let Err(replace_error) = replace_result {
            tracing::error!("Failed to find and replace stream: {}", replace_error);
            return Err(ConsoleApiError::DatabaseError);
        }
        let replaced_stream = replace_result.unwrap();
        if let Some(stream) = replaced_stream {
            Ok(stream)
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

        let stream_collection = gConsoleDatabase.lock().await.stream().clone();

        if let Err(update_error) = stream_collection
            .lock()
            .await
            .update_one(filter_doc, update_doc)
            .await
        {
            tracing::error!("Failed to update stream: {}", update_error);
            return Err(ConsoleApiError::DatabaseError);
        }

        let updated_stream = self.query_stream_by_id(stream_id).await?;
        Ok(updated_stream)
    }

    pub async fn query_stream_by_id(
        &self,
        stream_id: String,
    ) -> Result<ConsoleStream, ConsoleApiError> {
        let stream_collection = gConsoleDatabase.lock().await.stream().clone();
        let query_result = stream_collection
            .lock()
            .await
            .find_one(doc! {KEY_STREAM_ID: stream_id})
            .await;
        if let Err(query_error) = query_result {
            tracing::error!("Failed to find stream: {}", query_error);
            return Err(ConsoleApiError::DatabaseError);
        }
        let matching_stream = query_result.unwrap();
        if let Some(stream) = matching_stream {
            Ok(stream)
        } else {
            Err(ConsoleApiError::StreamNotFound)
        }
    }

    pub async fn query_stream_by_name(
        &self,
        stream_name: String,
    ) -> Result<ConsoleStream, ConsoleApiError> {
        let stream_collection = gConsoleDatabase.lock().await.stream().clone();
        let query_result = stream_collection
            .lock()
            .await
            .find_one(doc! {KEY_STREAM_NAME: stream_name})
            .await;
        if let Err(query_error) = query_result {
            tracing::error!("Failed to find stream: {}", query_error);
            return Err(ConsoleApiError::DatabaseError);
        }
        let matching_stream = query_result.unwrap();
        if let Some(stream) = matching_stream {
            Ok(stream)
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
        let stream_collection = gConsoleDatabase.lock().await.stream().clone();
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

        let cursor_result = stream_collection
            .lock()
            .await
            .find(filter)
            .sort(sort_doc)
            .skip(skip as u64)
            .limit(limit)
            .await;
        if let Err(query_error) = cursor_result {
            tracing::error!("query streams error: {}", query_error);
            return Err(ConsoleApiError::DatabaseError);
        }
        let mut stream_cursor = cursor_result.unwrap();

        let mut streams: Vec<ConsoleStream> = Vec::new();
        while let Some(stream_result) = stream_cursor.next().await {
            if let Err(cursor_error) = stream_result {
                tracing::error!("error to get stream value in cursor: {}", cursor_error);
                break;
            } else {
                streams.push(stream_result.unwrap());
            }
        }
        Ok(streams)
    }
}
