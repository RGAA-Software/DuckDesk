use crate::device::cms_device::CmsDevice;
use crate::device::cms_device_keys::KEY_DEVICE_ID;
use crate::cms_api_error::CmsApiError;
use crate::user::cms_user::CmsUser;
use crate::user::cms_user_keys::KEY_USER_ID;
use crate::user_device::cms_user_device::{CmsUserDevice, CmsUserDeviceAdapter};
use crate::{gDeviceManager, gCmsDatabase, gUserManager};
use futures_util::StreamExt;
use mongodb::bson::doc;
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
            user,
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

        user_device_adapter
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
}
