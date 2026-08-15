use crate::update_api_error::UpdateApiError;
use crate::update_info::UpdateInfo;
use crate::update_keys::KEY_UPDATE_VERSION;
use crate::gUpdateDatabase;
use futures_util::StreamExt;
use mongodb::bson;
use mongodb::bson::{Bson, doc};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct UpdateInfoManager {}

impl UpdateInfoManager {
    pub fn new() -> Arc<Mutex<Self>> {
        Arc::new(Mutex::new(Self {}))
    }

    pub async fn insert_update_info(&self, info: UpdateInfo) -> Result<UpdateInfo, UpdateApiError> {
        let version = info.version.clone();

        let c_update_info = gUpdateDatabase.lock().await.update_info().await;

        let coll = c_update_info.lock().await;

        let existing = coll.find_one(doc! { KEY_UPDATE_VERSION: &version }).await;

        if let Err(e) = existing {
            tracing::error!("db find version error: {}", e);
            return Err(UpdateApiError::DatabaseError);
        }

        //存在则更新
        if existing.unwrap().is_some() {
            tracing::info!("update version {}", version);
            let update_doc = doc! {
                "$set": bson::to_document(&info).unwrap()
            };
            let r = coll
                .update_one(doc! { KEY_UPDATE_VERSION: &version }, update_doc)
                .await;
            if let Err(e) = r {
                tracing::error!("update error: {}", e);
                return Err(UpdateApiError::DatabaseError);
            }
            return Ok(info);
        }

        //不存在则插入
        tracing::info!("insert new version {}", version);
        let r = coll.insert_one(info.clone()).await;
        if let Err(e) = r {
            tracing::error!("insert error: {}", e);
            return Err(UpdateApiError::DatabaseError);
        }
        Ok(info)
    }

    pub async fn query_info<T>(
        &self,
        page: i32,
        page_size: i32,
        filters: HashMap<String, T>,
        sort_field: Option<String>,
        sort_order: Option<i32>, // 1= asc, -1=dec
    ) -> Result<Vec<UpdateInfo>, UpdateApiError>
    where
        T: Into<Bson>,
    {
        let c_update_info = gUpdateDatabase.lock().await.update_info().await;
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

        let cursor = c_update_info
            .lock()
            .await
            .find(filter)
            .sort(sort_doc)
            .skip(skip as u64)
            .limit(limit)
            .await;
        if let Err(e) = cursor {
            tracing::error!("query users error: {}", e);
            return Err(UpdateApiError::DatabaseError);
        }
        let mut cursor = cursor.unwrap();

        let mut streams: Vec<UpdateInfo> = Vec::new();
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
    pub async fn query_update_info_by_version(
        &self,
        version: String,
    ) -> Result<UpdateInfo, UpdateApiError> {
        let c_update_info = gUpdateDatabase.lock().await.update_info().await;
        let filter = doc! {
            KEY_UPDATE_VERSION: version,
        };
        let r = c_update_info.lock().await.find_one(filter).await;
        if let Err(e) = r {
            tracing::error!("query user by uid error: {}", e);
            return Err(UpdateApiError::DatabaseError);
        }
        let r = r.unwrap();
        if r.is_none() {
            return Err(UpdateApiError::VersionNotFound);
        }
        Ok(r.unwrap())
    }
}
