use crate::gSpvrDatabase;
use crate::record::spvr_file_transfer::{SpvrFileTransfer, SpvrUpdateFileTransfer};
use crate::spvr_api_error::SpvrApiError;
use futures_util::StreamExt;
use mongodb::bson::{doc, Bson};
use mongodb::options::ReturnDocument;
use std::collections::HashMap;
use std::sync::Arc;

pub struct SpvrFileTransferManager {}

impl SpvrFileTransferManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    pub async fn insert_file_transfer_info(
        &self,
        info: SpvrFileTransfer,
    ) -> Result<SpvrFileTransfer, SpvrApiError> {
        let c_file_transfer_info = gSpvrDatabase.lock().await.file_transfer();
        let coll = c_file_transfer_info.lock().await;

        tracing::info!("insert new file_transfer {:?}", info);
        // Use replace_one with upsert to make insert idempotent based on the_file_id.
        let filter = doc! { "the_file_id": &info.the_file_id };
        let r = coll
            .replace_one(filter, info.clone())
            .upsert(true)
            .await;
        if let Err(e) = r {
            tracing::error!("insert/replace error: {}", e);
            return Err(SpvrApiError::DatabaseError);
        }
        Ok(info)
    }

    pub async fn update_file_transfer_info(
        &self,
        update: SpvrUpdateFileTransfer,
    ) -> Result<SpvrFileTransfer, SpvrApiError> {
        if update.the_file_id.is_empty() {
            return Err(SpvrApiError::InvalidParams);
        }

        let c_file_trans_info = gSpvrDatabase.lock().await.file_transfer();
        let coll = c_file_trans_info.lock().await;

        let filter = doc! {
            "the_file_id": &update.the_file_id
        };

        let mut set_doc = doc! {};
        set_doc.insert("end", update.end);
        set_doc.insert("success", update.success);
        set_doc.insert("duration", update.duration);

        if set_doc.is_empty() {
            return Err(SpvrApiError::InvalidParams);
        }

        let update_doc = doc! {
            "$set": set_doc
        };

        match coll
            .find_one_and_update(filter, update_doc)
            .return_document(ReturnDocument::After)
            .upsert(true)
            .await
        {
            Ok(Some(doc)) => Ok(doc),
            Ok(None) => Err(SpvrApiError::FileTransferNotFound),
            Err(e) => {
                tracing::error!("update file transfer error: {}", e);
                Err(SpvrApiError::DatabaseError)
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
    ) -> Result<Vec<SpvrFileTransfer>, SpvrApiError>
    where
        T: Into<Bson>,
    {
        let c_file_transfer_info = gSpvrDatabase.lock().await.file_transfer();
        let limit = page_size as i64;
        let filter = Self::build_filter(filters, visit_device_id, target_device_id);

        let sort_doc = if let Some(field) = sort_field {
            let order = sort_order.unwrap_or(1);
            doc! { field: order }
        } else {
            doc! {}
        };

        let cursor = c_file_transfer_info
            .lock()
            .await
            .find(filter)
            .sort(sort_doc)
            .skip(((page - 1) * page_size) as u64)
            .limit(limit)
            .await;
        if let Err(e) = cursor {
            tracing::error!("query file transfer error: {}", e);
            return Err(SpvrApiError::DatabaseError);
        }
        let mut cursor = cursor.unwrap();

        let mut streams: Vec<SpvrFileTransfer> = Vec::new();
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
    ) -> Result<i64, SpvrApiError>
    where
        T: Into<Bson>,
    {
        let c_file_transfer_info = gSpvrDatabase.lock().await.file_transfer();
        let filter = Self::build_filter(filters, visit_device_id, target_device_id);
        let r = c_file_transfer_info
            .lock()
            .await
            .count_documents(filter)
            .await;
        if let Err(_e) = r {
            return Err(SpvrApiError::DatabaseError);
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
