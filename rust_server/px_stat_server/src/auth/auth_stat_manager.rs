use crate::auth::auth_stat::StatAuth;
use crate::gStatDatabase;
use crate::stat_api_error::StatApiError;
use crate::stat_api_keys::{KEY_AUTH_ID, KEY_SYS_INFO, KEY_UPDATED_TS};
use px_base::get_current_timestamp;
use mongodb::bson::doc;
use std::sync::Arc;

pub struct StatAuthManager {}

impl StatAuthManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {})
    }

    pub async fn insert_or_update(&self, stat: StatAuth) -> Result<StatAuth, StatApiError> {
        let c_auth_stat = gStatDatabase.lock().await.auth_stat();
        let filter = doc! {
            KEY_AUTH_ID: stat.auth_id.as_str(),
            KEY_SYS_INFO: stat.sys_info.as_str(),
        };
        if let Ok(count) = c_auth_stat.count_documents(filter.clone()).await {
            if count == 0 {
                let created_ts = px_base::get_current_timestamp();
                let mut stat_cpy = stat.clone();
                stat_cpy.created_ts = created_ts;
                if c_auth_stat.insert_one(stat_cpy.clone()).await.is_ok() {
                    Ok(stat_cpy)
                } else {
                    Err(StatApiError::DatabaseError)
                }
            } else {
                if let Ok(_r) = c_auth_stat
                    .update_one(
                        filter,
                        doc! {
                            "$set": doc! {
                                KEY_UPDATED_TS: get_current_timestamp()
                            }
                        },
                    )
                    .await
                {
                    Ok(stat)
                } else {
                    Err(StatApiError::DatabaseError)
                }
            }
        } else {
            Err(StatApiError::DatabaseError)
        }
    }
}
