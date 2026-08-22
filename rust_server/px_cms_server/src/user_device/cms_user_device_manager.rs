use crate::cms_api_error::CmsApiError;
use crate::device::cms_device::CmsDevice;
use crate::device::cms_device_keys::KEY_DEVICE_ID;
use crate::user::cms_user::{CmsUser, CmsUserView};
use crate::user::cms_user_keys::KEY_USER_ID;
use crate::user_device::cms_user_device::{
    CmsUserDevice, CmsUserDeviceAdapter, CmsUserDeviceSummary,
};
use crate::{gCmsDatabase, gDeviceManager, gUserManager};
use futures_util::StreamExt;
use mongodb::bson::doc;
use px_base::get_current_readable_timestamp;
use std::collections::BTreeSet;
use std::sync::Arc;

pub struct CmsUserDeviceManager {}

impl CmsUserDeviceManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    fn make_user_device_adapter(
        &self,
        user_device: CmsUserDevice,
        user: CmsUser,
        device: CmsDevice,
    ) -> CmsUserDeviceAdapter {
        CmsUserDeviceAdapter {
            uid: user_device.uid,
            device_id: user_device.device_id,
            created_ts: user_device.created_ts,
            created_ts_readable: user_device.created_ts_readable,
            user: CmsUserView::from(user),
            device,
        }
    }

    pub async fn insert_user_device(
        &self,
        user_device: CmsUserDevice,
    ) -> Result<CmsUserDeviceAdapter, CmsApiError> {
        let user_device_adapter = self
            .query_by_uid_device_id(user_device.uid.clone(), user_device.device_id.clone())
            .await;
        if let Ok(_uda) = user_device_adapter {
            tracing::info!(
                "user-device already exist: {} {}",
                user_device.uid,
                user_device.device_id
            );
            return Err(CmsApiError::UserDeviceAlreadyExists);
        }

        let c_user_device = gCmsDatabase.lock().await.user_device();
        if let Err(e) = c_user_device
            .lock()
            .await
            .insert_one(user_device.clone())
            .await
        {
            tracing::error!("Failed to insert user-device: {:?}", e);
            return Err(CmsApiError::DatabaseError);
        }

        self.query_by_uid_device_id(user_device.uid, user_device.device_id)
            .await
    }

    pub async fn remove_device_from_user(
        &self,
        uid: String,
        device_id: String,
    ) -> Result<CmsUserDeviceAdapter, CmsApiError> {
        let c_user_device = gCmsDatabase.lock().await.user_device();
        let filter = doc! {
            KEY_USER_ID: uid.clone(),
            KEY_DEVICE_ID: device_id.clone()
        };

        let user_device = self.query_by_uid_device_id(uid, device_id).await?;

        let r = c_user_device.lock().await.delete_one(filter).await;
        if let Err(e) = r {
            tracing::error!("Failed to remove user-device: {:?}", e);
            return Err(CmsApiError::DatabaseError);
        }

        Ok(user_device)
    }

    pub async fn query_by_uid_device_id(
        &self,
        uid: String,
        device_id: String,
    ) -> Result<CmsUserDeviceAdapter, CmsApiError> {
        let user = gUserManager.query_user_by_id(uid.clone()).await?;

        let device = gDeviceManager.query_device_by_id(device_id.clone()).await?;

        let c_user_device = gCmsDatabase.lock().await.user_device();

        let filter = doc! {
            KEY_USER_ID: uid,
            KEY_DEVICE_ID: device_id
        };
        let r = c_user_device.lock().await.find_one(filter).await;
        if let Err(e) = r {
            tracing::error!("Failed to find user-device: {:?}", e);
            return Err(CmsApiError::DatabaseError);
        }
        let r = r.unwrap();
        if r.is_none() {
            return Err(CmsApiError::UserDeviceNotFound);
        }
        Ok(self.make_user_device_adapter(r.unwrap(), user, device))
    }

    pub async fn query_user_devices(
        &self,
        uid: String,
        page: i32,
        page_size: i32,
    ) -> Result<Vec<CmsUserDeviceAdapter>, CmsApiError> {
        let user = gUserManager.query_user_by_id(uid.clone()).await?;

        let c_user_device = gCmsDatabase.lock().await.user_device();

        let filter = doc! {
            KEY_USER_ID: uid.clone(),
        };
        let skip = (page - 1) * page_size;
        let limit = page_size as i64;
        let mut cursor = c_user_device
            .lock()
            .await
            .find(filter)
            .skip(skip as u64)
            .limit(limit)
            .await
            .map_err(|e| {
                tracing::error!("failed to get cursor to query user device: {}", e);
                CmsApiError::DatabaseError
            })?;

        let mut devices: Vec<CmsUserDeviceAdapter> = Vec::new();
        while let Some(device) = cursor.next().await {
            if let Err(e) = device {
                println!("error connecting to MongoDB: {}", e);
                break;
            }
            let user_device = device.unwrap();

            let device = gDeviceManager
                .query_device_by_id(user_device.device_id.clone())
                .await?;

            devices.push(self.make_user_device_adapter(user_device, user.clone(), device));
        }
        Ok(devices)
    }

    pub async fn query_user_device_summaries(
        &self,
        uid: String,
    ) -> Result<Vec<CmsUserDeviceSummary>, CmsApiError> {
        // A valid CMS account can use every registered device. Personal and
        // group device grants are retained only for storage/API compatibility;
        // they no longer participate in device visibility or ticket issuance.
        gUserManager.query_user_by_id(uid).await?;
        let c_device = gCmsDatabase.lock().await.device();
        let mut cursor = c_device.lock().await.find(doc! {}).await.map_err(|e| {
            tracing::error!("failed to query registered devices for user: {}", e);
            CmsApiError::DatabaseError
        })?;

        let mut devices = Vec::new();
        while let Some(device) = cursor.next().await {
            let device = device.map_err(|e| {
                tracing::error!("failed to read registered device for user: {}", e);
                CmsApiError::DatabaseError
            })?;
            devices.push(CmsUserDeviceSummary::from(device));
        }
        devices.sort_by(|left, right| {
            left.name
                .cmp(&right.name)
                .then(left.device_id.cmp(&right.device_id))
        });
        Ok(devices)
    }

    pub async fn personal_device_ids(&self, uid: &str) -> Result<Vec<String>, CmsApiError> {
        let mut cursor = gCmsDatabase
            .lock()
            .await
            .user_device()
            .lock()
            .await
            .find(doc! { KEY_USER_ID: uid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        let mut ids = Vec::new();
        while let Some(item) = cursor.next().await {
            ids.push(item.map_err(|_| CmsApiError::DatabaseError)?.device_id);
        }
        ids.sort();
        ids.dedup();
        Ok(ids)
    }

    pub async fn replace_personal_devices(
        &self,
        uid: &str,
        device_ids: Vec<String>,
    ) -> Result<Vec<String>, CmsApiError> {
        let user = gUserManager.query_user_by_id(uid.to_string()).await?;
        if user.deleted {
            return Err(CmsApiError::UserNotFound);
        }
        let mut desired = BTreeSet::new();
        for device_id in device_ids {
            if device_id.is_empty() || !desired.insert(device_id.clone()) {
                continue;
            }
            gDeviceManager.query_device_by_id(device_id).await?;
        }
        let collection = gCmsDatabase.lock().await.user_device();
        collection
            .lock()
            .await
            .delete_many(doc! { KEY_USER_ID: uid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        let now = px_base::get_current_timestamp();
        for device_id in &desired {
            collection
                .lock()
                .await
                .insert_one(CmsUserDevice {
                    uid: uid.to_string(),
                    device_id: device_id.clone(),
                    created_ts: now,
                    created_ts_readable: get_current_readable_timestamp(),
                })
                .await
                .map_err(|_| CmsApiError::DatabaseError)?;
        }
        Ok(desired.into_iter().collect())
    }

    pub async fn remove_personal_devices_for_user(&self, uid: &str) -> Result<(), CmsApiError> {
        gCmsDatabase
            .lock()
            .await
            .user_device()
            .lock()
            .await
            .delete_many(doc! { KEY_USER_ID: uid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        Ok(())
    }
}
