use crate::gStatDatabase;
use crate::stat_api_error::StatApiError;
use crate::stat_api_keys::{KEY_SYS_INFO, KEY_UPDATED_TS};
use crate::using::stat_open_up::StatOpenUp;
use px_base::get_current_timestamp;
use mongodb::bson::doc;

pub struct StatUsingManager {}

impl StatUsingManager {
    pub fn new() -> Self {
        Self {}
    }

    pub async fn insert_or_update(&self, stat: StatOpenUp) -> Result<StatOpenUp, StatApiError> {
        let c_open_up = gStatDatabase.lock().await.open_up();
        let filter = doc! {
            KEY_SYS_INFO: stat.sys_info.as_str(),
        };
        if let Ok(count) = c_open_up.count_documents(filter.clone()).await {
            if count == 0 {
                let created_ts = px_base::get_current_timestamp();
                let mut stat_cpy = stat.clone();
                stat_cpy.created_ts = created_ts;
                if c_open_up.insert_one(stat_cpy.clone()).await.is_ok() {
                    Ok(stat_cpy)
                } else {
                    Err(StatApiError::DatabaseError)
                }
            } else {
                if let Ok(_r) = c_open_up
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
