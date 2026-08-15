use crate::gCmsDatabase;
use crate::cms_api_error::CmsApiError;
use crate::update::update_info::UpdateInfo;
use crate::update::update_keys::KEY_UPDATE_VERSION;
use futures_util::StreamExt;
use mongodb::bson;
use mongodb::bson::{doc, Bson};
use std::collections::HashMap;
use std::sync::Arc;

pub struct UpdateInfoManager {}

impl UpdateInfoManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    pub async fn insert_update_info(&self, info: UpdateInfo) -> Result<UpdateInfo, CmsApiError> {
        let version = info.version.clone();

        let c_update_info = gCmsDatabase.lock().await.update_info();

        let coll = c_update_info.lock().await;

        let existing = coll.find_one(doc! { KEY_UPDATE_VERSION: &version }).await;

        if let Err(e) = existing {
            tracing::error!("db find version error: {}", e);
            return Err(CmsApiError::DatabaseError);
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
                return Err(CmsApiError::DatabaseError);
            }
            return Ok(info);
        }

        //不存在则插入
        tracing::info!("insert new version {}", version);
        let r = coll.insert_one(info.clone()).await;
        if let Err(e) = r {
            tracing::error!("insert error: {}", e);
            return Err(CmsApiError::DatabaseError);
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
    ) -> Result<Vec<UpdateInfo>, CmsApiError>
    where
        T: Into<Bson>,
    {
        let c_update_info = gCmsDatabase.lock().await.update_info();
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
            return Err(CmsApiError::DatabaseError);
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
    ) -> Result<UpdateInfo, CmsApiError> {
        let c_update_info = gCmsDatabase.lock().await.update_info();
        let filter = doc! {
            KEY_UPDATE_VERSION: version,
        };
        let r = c_update_info.lock().await.find_one(filter).await;
        if let Err(e) = r {
            tracing::error!("query user by uid error: {}", e);
            return Err(CmsApiError::DatabaseError);
        }
        let r = r.unwrap();
        if r.is_none() {
            return Err(CmsApiError::VersionNotFound);
        }
        Ok(r.unwrap())
    }
}
