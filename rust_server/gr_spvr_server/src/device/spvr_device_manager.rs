use crate::{gSpvrDatabase};
use crate::device::spvr_device::SpvrDevice;
use futures_util::StreamExt;
use mongodb::bson::{Bson, Document};
use mongodb::{
    bson::{doc}
};
use std::collections::HashMap;
use std::sync::Arc;
use mongodb::options::AggregateOptions;
use tokio::sync::Mutex;
use crate::spvr_api_error::SpvrApiError;

pub struct SpvrDeviceManager {

}

impl SpvrDeviceManager {
    pub fn new() -> Arc<SpvrDeviceManager> {
        Arc::new(SpvrDeviceManager {
        })
    }
    
    pub async fn query_devices(&self, device_name: String, device_id: String, ip: String, page: i32, page_size: i32) -> Result<Vec<SpvrDevice>, SpvrApiError> {
        let c_device = gSpvrDatabase.lock().await.device();

        let mut and_conditions: Vec<Document> = Vec::new();

        if !device_name.is_empty() {
            and_conditions.push(doc! {
                "device_name": {
                    "$regex": &device_name,
                    "$options": "i"
                }
            });
        }

        if !device_id.is_empty() {
            and_conditions.push(doc! {
                "device_id": {
                    "$regex": &device_id,
                    "$options": "i"
                }
            });
        }

        if !ip.is_empty() {
            and_conditions.push(doc! {
                "desktop_link_raw": {
                    "$regex": &ip,
                    "$options": "i"
                }
            });
        }

        let filter = if and_conditions.is_empty() {
            doc! {}
        } else {
            doc! {
                "$and": and_conditions
            }
        };

        //tracing::info!("the filter for device : {:#?}", filter);

        let skip = (page-1) * page_size;
        let limit = page_size as i64;
        let mut cursor = c_device.lock().await
            .find(filter)
            .skip(skip as u64)
            .limit(limit)
            .await
            .map_err(|e| {
                tracing::error!("failed to get cursor to query user device: {}", e);
                return SpvrApiError::DatabaseError;
            })?;

        println!("query device, skip:{} - limit:{}", skip, limit);

        let mut devices: Vec<SpvrDevice> = Vec::new();
        while let Some(device) = cursor.next().await {
            if let Err(e) = device {
                println!("error connecting to MongoDB: {}", e);
                break;
            }
            devices.push(device.unwrap());
        }
        Ok(devices)
    }
    
    pub async fn insert_device(&self, device: SpvrDevice) -> Result<bool, SpvrApiError> {
        let c_device = gSpvrDatabase.lock().await.device();
        let r = c_device.lock().await.insert_one(device).await;
        if let Err(e) = r {
            tracing::error!("error inserting device: {}", e);
            return Err(SpvrApiError::DatabaseError);
        }
        Ok(true)
    }
    
    pub async fn query_device_by_id_and_seed(&self, device_id: String, seed: String) -> Result<SpvrDevice, SpvrApiError> {
        let c_device = gSpvrDatabase
            .lock().await
            .device();
        let filter = doc! { 
            "device_id": device_id.clone(),
            "seed": seed.clone(),
        };
        let r = c_device
            .lock().await
            .find_one(filter).await;
        if let Err(e) = r {
            tracing::error!("error retrieving device from MongoDB: {}", e);
            return Err(SpvrApiError::DatabaseError);
        }
        let r = r.unwrap();
        if let Some(device) = r {
            Ok(device)
        }
        else {
            tracing::error!("device not found: {}, seed: {}", device_id, seed);
            Err(SpvrApiError::DeviceNotFound)
        }
    }
    
    pub async fn query_device_by_id(&self, device_id: String) -> Result<SpvrDevice, SpvrApiError> {
        let c_device = gSpvrDatabase.lock().await.device();
        let filter = doc! { 
            "device_id": device_id,
        };
        let r = c_device.lock().await.find_one(filter, ).await;
        if let Err(e) = r {
            tracing::error!("error querying device: {}", e);
            return Err(SpvrApiError::DatabaseError);
        }
        if let Some(device) = r.unwrap() {
            Ok(device)
        }
        else {
            Err(SpvrApiError::DeviceNotFound)
        }
    }

    pub async fn update_device(&self, device_id: String, update_info: HashMap<String, String>) -> Result<bool, SpvrApiError> {
        let c_device = gSpvrDatabase.lock().await.device();
        let filter_doc = doc! {
            "device_id": device_id,
        };
        let mut update_doc = doc! {};
        let mut sub_update_doc = doc! {};
        for (k, v) in update_info {
            sub_update_doc.insert(k, v);
        }
        sub_update_doc.insert("last_update_timestamp", gr_base::get_current_timestamp());
        update_doc.insert("$set", sub_update_doc);
        let r = c_device
            .lock().await
            .update_one(filter_doc, update_doc).await;
        if let Err(e) = r {
            println!("error updating device: {}", e);
            Err(SpvrApiError::DatabaseError)
        }
        else {
            Ok(true)
        }
    }

    pub async fn update_device_field<T>(&self, device_id: String, key: String, val: T) -> Result<bool, SpvrApiError>
        where T: Into<Bson> {
        let c_device = gSpvrDatabase.lock().await.device();
        let filter_doc = doc! {
            "device_id": device_id,
        };
        let mut update_doc = doc! {};
        let mut sub_update_doc = doc! {
            key: val,
        };

        sub_update_doc.insert("last_update_timestamp", gr_base::get_current_timestamp());
        update_doc.insert("$set", sub_update_doc);
        let r = c_device.lock().await.update_one(filter_doc, update_doc).await;
        if let Err(e) = r {
            println!("error updating device: {}", e);
            Err(SpvrApiError::DatabaseError)
        }
        else {
            Ok(true)
        }
    }

    pub async fn bind_logged_in_user(&self, device_id: String, user_id: String) -> Result<bool, SpvrApiError> {
        self.update_device_field(device_id, "logged_in_user_id".to_string(), user_id).await
    }

    pub async fn query_total_devices_count(&self) -> Result<u64, SpvrApiError> {
        let c_device = gSpvrDatabase
            .lock().await
            .device();
        let r = c_device.lock().await.count_documents(doc! {}).await;
        if let Err(e) = r {
            return Err(SpvrApiError::DatabaseError);
        }
        Ok(r.unwrap())
    }

    pub async fn query_total_used_time(&self) -> Result<u64, SpvrApiError> {
        let c_device = gSpvrDatabase
            .lock().await
            .device();
        let pipeline = vec![
            doc! {
                "$group": {
                    "_id": null,
                    "total": {
                        "$sum": {
                            "$ifNull": [format!("${}", "used_time"), 0]  // 处理 null 值
                        }
                    }
                }
            }
        ];
        let options = AggregateOptions::builder()
            .allow_disk_use(true)
            .build();
        // 5. 执行查询
        let mut cursor = c_device.lock().await.aggregate(pipeline)
            .await
            .map_err(|e| SpvrApiError::DatabaseError)?;
        // 6. 获取结果
        if let Some(result) = cursor.next().await {
            let doc = result.map_err(|e| SpvrApiError::DatabaseError)?;

            match doc.get("total") {
                Some(mongodb::bson::Bson::Int32(val)) => Ok(*val as u64),
                Some(mongodb::bson::Bson::Int64(val)) => Ok(*val as u64),
                _ => Err(SpvrApiError::DatabaseError),
            }
        } else {
            Err(SpvrApiError::DatabaseError)
        }
    }
}