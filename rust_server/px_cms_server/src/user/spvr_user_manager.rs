use crate::device::spvr_id_generator::PrIdGenerator;
use crate::gSpvrDatabase;
use crate::spvr_api_error::SpvrApiError;
use crate::user::spvr_user::SpvrUser;
use crate::user::spvr_user_keys::{KEY_DELETED, KEY_PASSWORD, KEY_USER_ID, KEY_USER_NAME};
use futures_util::StreamExt;
use mongodb::bson::oid::ObjectId;
use mongodb::bson::{doc, Bson, Document};
use std::sync::Arc;

pub struct SpvrUserManager {}

impl SpvrUserManager {
    pub fn new() -> Arc<Self> {
        Arc::new(SpvrUserManager {})
    }

    pub async fn register_user(
        &self,
        username: String,
        hash_password: String,
    ) -> Result<SpvrUser, SpvrApiError> {
        let r = self.query_user_by_username(username.clone()).await;
        if let Ok(_user) = r {
            tracing::warn!("the user: {} already exists", username);
            return Err(SpvrApiError::UserAlreadyExists);
        }

        let object_id = ObjectId::new();
        let uid = px_base::md5_hex(&object_id.to_string());
        let user = SpvrUser {
            uid,
            username: username.clone(),
            password: hash_password,
            assigned: false,
            created_timestamp: px_base::get_current_timestamp(),
            update_timestamp: px_base::get_current_timestamp(),
            deleted: false,
            avatar_path: "".to_string(),
            administrator: false,
            total: 0,
        };

        let c_user = gSpvrDatabase.lock().await.user();
        if let Err(e) = c_user.lock().await.insert_one(user).await {
            tracing::error!("insert spvr user failed: {}", e);
            return Err(SpvrApiError::DatabaseError);
        }

        self.query_user_by_username(username).await
    }

    pub async fn delete_user(&self, uid: String) -> Result<SpvrUser, SpvrApiError> {
        let c_user = gSpvrDatabase.lock().await.user();
        let r = c_user
            .lock()
            .await
            .update_one(
                doc! {"uid": uid.clone()},
                doc! {"$set": doc!{KEY_DELETED: true}},
            )
            .await;
        if let Err(e) = r {
            tracing::error!("failed to delete user: {}, id: {}", e, uid);
            return Err(SpvrApiError::DatabaseError);
        }
        let user = self.query_user_by_id(uid).await?;
        Ok(user)
    }

    pub async fn active_user(&self, uid: String) -> Result<SpvrUser, SpvrApiError> {
        let c_user = gSpvrDatabase.lock().await.user();
        let r = c_user
            .lock()
            .await
            .update_one(
                doc! {"uid": uid.clone()},
                doc! {"$set": doc!{KEY_DELETED: false}},
            )
            .await;
        if let Err(e) = r {
            tracing::error!("failed to active user: {}, id: {}", e, uid);
            return Err(SpvrApiError::DatabaseError);
        }
        let user = self.query_user_by_id(uid).await?;
        Ok(user)
    }

    pub async fn update_user<T>(
        &self,
        uid: String,
        key: String,
        val: T,
    ) -> Result<SpvrUser, SpvrApiError>
    where
        T: Into<Bson>,
    {
        let filter_doc = doc! {"uid": uid.clone()};
        let mut update_doc = doc! {};
        let mut sub_update_doc = doc! {key: val};
        sub_update_doc.insert("update_timestamp", px_base::get_current_timestamp());
        update_doc.insert("$set", sub_update_doc);

        let c_user = gSpvrDatabase.lock().await.user();

        if let Err(e) = c_user.lock().await.update_one(filter_doc, update_doc).await {
            tracing::error!("update user failed: {}", e);
            return Err(SpvrApiError::DatabaseError);
        }

        let user = self.query_user_by_id(uid).await?;
        Ok(user)
    }

    pub async fn update_user_password(
        &self,
        uid: String,
        password: String,
    ) -> Result<SpvrUser, SpvrApiError> {
        self.update_user(uid, KEY_PASSWORD.to_string(), password)
            .await
    }

    pub async fn query_user_by_id(&self, uid: String) -> Result<SpvrUser, SpvrApiError> {
        let c_user = gSpvrDatabase.lock().await.user();
        let filter = doc! {
            KEY_USER_ID: uid,
        };
        let r = c_user.lock().await.find_one(filter).await;
        if let Err(e) = r {
            tracing::error!("query user by uid error: {}", e);
            return Err(SpvrApiError::DatabaseError);
        }
        let r = r.unwrap();
        if r.is_none() {
            return Err(SpvrApiError::UserNotFound);
        }
        Ok(r.unwrap())
    }

    pub async fn query_user_by_username(&self, username: String) -> Result<SpvrUser, SpvrApiError> {
        let c_user = gSpvrDatabase.lock().await.user();
        let filter = doc! {
            KEY_USER_NAME: username.clone(),
        };
        let r = c_user.lock().await.find_one(filter).await;
        if let Err(e) = r {
            tracing::error!("query user by username error: {}", e);
            return Err(SpvrApiError::DatabaseError);
        }
        let r = r.unwrap();
        if r.is_none() {
            tracing::error!("user not found: {}", username);
            return Err(SpvrApiError::UserNotFound);
        }
        Ok(r.unwrap())
    }

    pub async fn query_users(
        &self,
        page: i32,
        page_size: i32,
        username: String,           // Like
        uid: String,                // Like
        sort_field: Option<String>, // sort field
        sort_order: Option<i32>,    // 1= asc, -1=dec
    ) -> Result<Vec<SpvrUser>, SpvrApiError> {
        let c_user = gSpvrDatabase.lock().await.user();
        let skip = (page - 1) * page_size;
        let limit = page_size as i64;

        let mut and_conditions: Vec<Document> = Vec::new();
        if !username.is_empty() {
            and_conditions.push(doc! {
                "username": {
                    "$regex": username,
                    "$options": "i"
                }
            });
        }
        if !uid.is_empty() {
            and_conditions.push(doc! {
               "uid": {
                    "$regex": uid,
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

        //tracing::info!("filter: {:#?}", filter);

        let sort_doc = if let Some(field) = sort_field {
            let order = sort_order.unwrap_or(1);
            doc! { field: order }
        } else {
            doc! {}
        };

        let cursor = c_user
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

        let mut users: Vec<SpvrUser> = Vec::new();
        while let Some(device) = cursor.next().await {
            if let Err(e) = device {
                tracing::error!("error to get value in cursor: {}", e);
                break;
            } else {
                users.push(device.unwrap());
            }
        }
        Ok(users)
    }

    pub async fn count_users(&self) -> Result<u32, SpvrApiError> {
        let c_user = gSpvrDatabase.lock().await.user();
        let r = c_user.lock().await;
        if let Ok(count) = r.count_documents(doc! {}).await {
            Ok(count as u32)
        } else {
            Err(SpvrApiError::DatabaseError)
        }
    }

    pub async fn batch_gen_random_users(
        &self,
        batch_size: i32,
        name_prefix: String,
    ) -> Result<Vec<SpvrUser>, SpvrApiError> {
        let prefix = if name_prefix.is_empty() {
            "User:".to_string()
        } else {
            name_prefix
        };
        let mut user_index = 0;
        let mut users = Vec::new();
        for _i in 0..batch_size {
            loop {
                let username = format!("{}{}", prefix, user_index);
                user_index += 1;
                let user = self.query_user_by_username(username.clone()).await;
                if let Ok(_user) = user {
                    continue;
                }
                let password = PrIdGenerator::generate_random_pwd();
                tracing::info!("gen : {} {}", username, password);
                let hash_password = px_base::md5_hex(&password);
                let mut r = self.register_user(username, hash_password).await?;
                r.password = password;
                users.push(r);
                break;
            }
        }
        Ok(users)
    }

    pub fn batch_gen_csv_users() -> Result<Vec<SpvrUser>, SpvrApiError> {
        Ok(vec![SpvrUser::default()])
    }
}

#[cfg(test)]
mod tests {
    
    #[test]
    fn test_gen_random_users() {
        for _i in 0..10 {}
    }
}
