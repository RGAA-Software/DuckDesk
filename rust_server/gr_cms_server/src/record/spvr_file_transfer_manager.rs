use crate::gSpvrDatabase;
use crate::record::spvr_file_transfer::{SpvrFileTransfer, SpvrUpdateFileTransfer};
use crate::record::spvr_visit::{SpvrUpdateVisit, SpvrVisit};
use crate::spvr_api_error::SpvrApiError;
use futures_util::StreamExt;
use mongodb::bson;
use mongodb::bson::{doc, Bson};
use mongodb::options::ReturnDocument;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

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
        let r = coll.insert_one(info.clone()).await;
        if let Err(e) = r {
            tracing::error!("insert error: {}", e);
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

        // 唯一键过滤
        let filter = doc! {
            "the_file_id": &update.the_file_id
        };

        // 构造 $set
        let mut set_doc = doc! {};

        set_doc.insert("end", update.end);

        set_doc.insert("success", update.success);

        if set_doc.is_empty() {
            return Err(SpvrApiError::InvalidParams);
        }

        let update_doc = doc! {
            "$set": set_doc
        };

        match coll
            .find_one_and_update(filter, update_doc)
            .return_document(ReturnDocument::After)
            .upsert(false)
            .await
        {
            Ok(Some(doc)) => Ok(doc),
            Ok(None) => Err(SpvrApiError::VisitNotFound),
            Err(e) => {
                tracing::error!("update visit error: {}", e);
                Err(SpvrApiError::DatabaseError)
            }
        }
    }

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
        let skip = (page - 1) * page_size;
        let limit = page_size as i64;
        let mut filter = doc! {};
        for (key, value) in filters {
            filter.insert(key, value.into());
        }

        // visit_device_id 模糊查询
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

        // target_device_id 模糊查询
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
            .skip(skip as u64)
            .limit(limit)
            .await;
        if let Err(e) = cursor {
            tracing::error!("query users error: {}", e);
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

    pub async fn total_size(&self) -> Result<i64, SpvrApiError> {
        let c_file_transfer_info = gSpvrDatabase.lock().await.file_transfer();
        let r = c_file_transfer_info
            .lock()
            .await
            .count_documents(doc! {})
            .await;
        if let Err(e) = r {
            return Err(SpvrApiError::DatabaseError);
        }
        Ok(r.unwrap() as i64)
    }
}
