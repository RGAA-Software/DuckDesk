use crate::author_appkey_generator::gen_appkey_secret;
use crate::author_license_keys::sign_authorization_model;
use crate::{gAuthorDatabase, gAuthorSettings, gLicenseSigner};
use futures_util::StreamExt;
use gr_auth_mgr::authorization::{Authorization, AuthorizationVo};
use mongodb::Cursor;
use mongodb::bson::oid::ObjectId;
use mongodb::bson::{Bson, DateTime, Document, Regex, doc, to_document};
use std::collections::HashMap;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum AuthorizationError {
    #[error("Already exist")]
    AlreadyExist,

    #[error("Insert to db failed")]
    DatabaseError,

    #[error("Time convert error")]
    TimeConvertError,
}

pub struct AuthorizationManager {}

impl AuthorizationManager {
    pub fn new() -> AuthorizationManager {
        AuthorizationManager {}
    }

    pub async fn init(&self) -> bool {
        true
    }

    pub async fn gen_new_authorization(
        &self,
        name: String,
        machine_code: String,
        days: i32,
        max_streams: i32,
        role: i32,
    ) -> Result<Authorization, AuthorizationError> {
        let auth = self.query_authorization_by_name(name.clone()).await;
        if auth.is_some() {
            return Err(AuthorizationError::AlreadyExist);
        }
        let auth_id = ObjectId::new().to_hex();
        let ks = gen_appkey_secret(name.clone(), machine_code.clone());
        let now = DateTime::now();
        let now_ms = now.timestamp_millis();
        let future_ms = now_ms + (days as i64) * 24 * 3600 * 1000;
        //let future_date = DateTime::from_millis(future_ms);
        let verify_server = gAuthorSettings.lock().await.verify_server.clone();
        let auth = Authorization {
            auth_id,
            auth_name: name,
            machine_code,
            description: "".to_string(),
            max_streams,
            appkey: ks.appkey,
            app_secret: ks.app_secret,
            username: ks.username,
            password: ks.password,
            created_timestamp_ms: now_ms,
            end_timestamp_ms: future_ms,
            last_modify_timestamp: now_ms,
            days,
            verify_server,
            deploy_str: "".to_string(),
            role,
            used_time_ms: 0,
        };

        if self.insert_authorization(auth.clone()).await {
            Ok(auth)
        } else {
            Err(AuthorizationError::DatabaseError)
        }
    }

    pub async fn insert_authorization(&self, auth: Authorization) -> bool {
        let c_authorization = gAuthorDatabase.lock().await.authorization();
        let r = c_authorization.lock().await.insert_one(auth).await;
        if let Err(e) = r {
            tracing::error!("error inserting authorization: {}", e);
            return false;
        }
        true
    }

    pub async fn query_authorization_by_id(&self, auth_id: String) -> Option<Authorization> {
        let c_authorization = gAuthorDatabase.lock().await.authorization();
        let filter = doc! {
            "auth_id": auth_id,
        };
        let r = c_authorization.lock().await.find_one(filter).await;
        r.unwrap_or(None)
    }

    pub async fn query_authorization_by_name(&self, name: String) -> Option<Authorization> {
        let c_authorization = gAuthorDatabase.lock().await.authorization();
        let filter = doc! {
            "auth_name": name,
        };
        let r = c_authorization.lock().await.find_one(filter).await;
        r.unwrap_or(None)
    }

    pub async fn query_authorization_by_appkey_secret(
        &self,
        appkey: String,
        app_secret: String,
    ) -> Option<Authorization> {
        let c_authorization = gAuthorDatabase.lock().await.authorization();
        let filter = doc! {
            "appkey": appkey,
            "app_secret": app_secret,
        };
        let r = c_authorization.lock().await.find_one(filter).await;
        r.unwrap_or(None)
    }

    pub async fn query_authorizations_like_name(
        &self,
        pattern: String,
        page: i32,
        page_size: i32,
    ) -> Result<Vec<AuthorizationVo>, AuthorizationError> {
        let regex = Regex {
            pattern,
            options: "i".to_string(), // i = ignore case
        };

        let filter = doc! {
            "auth_name": { "$regex": regex }
        };

        self.query_authorizations(filter, page, page_size).await
    }

    pub async fn query_authorizations_no_filter(
        &self,
        page: i32,
        page_size: i32,
    ) -> Result<Vec<AuthorizationVo>, AuthorizationError> {
        self.query_authorizations(doc! {}, page, page_size).await
    }

    pub async fn query_authorizations(
        &self,
        filter: Document,
        page: i32,
        page_size: i32,
    ) -> Result<Vec<AuthorizationVo>, AuthorizationError> {
        let total = self.query_authorizations_count().await?;

        let c_authorization = gAuthorDatabase.lock().await.authorization();
        let skip = (page - 1) * page_size;
        let limit = page_size as i64;
        let cursor = c_authorization
            .lock()
            .await
            .find(filter)
            .skip(skip as u64)
            .limit(limit)
            .sort(doc! {
                "created_timestamp_ms": -1
            })
            .await;
        if let Err(e) = cursor {
            tracing::error!("error querying MongoDB: {}", e);
            return Err(AuthorizationError::DatabaseError);
        }
        let mut cursor: Cursor<Authorization> = cursor.unwrap();

        let mut authorizations: Vec<AuthorizationVo> = Vec::new();
        while let Some(auth) = cursor.next().await {
            if let Err(e) = auth {
                tracing::error!("query group error: {}", e);
                break;
            } else {
                let mut auth = auth.unwrap();
                if let Some(signer) = gLicenseSigner.lock().await.as_ref()
                    && let Ok(signed) = sign_authorization_model(signer, &auth) {
                        auth.deploy_str = signed.to_deploy_string().unwrap_or_default();
                    }
                authorizations.push(auth.as_vo(total));
            }
        }
        Ok(authorizations)
    }

    pub async fn update_authorization(&self, auth_id: String, auth: Authorization) -> bool {
        let c_authorization = gAuthorDatabase.lock().await.authorization();

        let doc = to_document(&auth);
        if let Err(e) = doc {
            tracing::error!("error updating authorization: {}", e);
            return false;
        }
        let doc = doc.unwrap();
        let r = c_authorization
            .lock()
            .await
            .update_one(doc! {"auth_id": auth_id}, doc! { "$set": doc })
            .await;
        if let Err(e) = r {
            tracing::error!("error updating auth: {}", e);
            return false;
        }
        true
    }

    pub async fn update_authorization_str_map(
        &self,
        auth_id: String,
        update_info: HashMap<String, String>,
    ) -> bool {
        let c_authorization = gAuthorDatabase.lock().await.authorization();
        let filter_doc = doc! {
            "auth_id": auth_id,
        };
        let mut update_doc = doc! {};
        let mut sub_update_doc = doc! {};
        for (k, v) in update_info {
            sub_update_doc.insert(k, v);
        }
        sub_update_doc.insert("last_modify_timestamp", gr_base::get_current_timestamp());
        update_doc.insert("$set", sub_update_doc);
        let r = c_authorization
            .lock()
            .await
            .update_one(filter_doc, update_doc)
            .await;
        if let Err(e) = r {
            tracing::error!("error updating authorization str map: {}", e);
            false
        } else {
            true
        }
    }

    pub async fn update_authorization_field<T>(&self, auth_id: String, key: String, val: T) -> bool
    where
        T: Into<Bson>,
    {
        let c_authorization = gAuthorDatabase.lock().await.authorization();
        let filter_doc = doc! {
            "auth_id": auth_id,
        };
        let mut update_doc = doc! {};
        let mut sub_update_doc = doc! {
            key: val,
        };

        sub_update_doc.insert("last_modify_timestamp", gr_base::get_current_timestamp());
        update_doc.insert("$set", sub_update_doc);
        let r = c_authorization
            .lock()
            .await
            .update_one(filter_doc, update_doc)
            .await;
        if let Err(e) = r {
            tracing::error!("error updating authorization field: {}", e);
            false
        } else {
            true
        }
    }

    pub async fn query_authorizations_count(&self) -> Result<u64, AuthorizationError> {
        let c_authorization = gAuthorDatabase.lock().await.authorization();
        if let Ok(r) = c_authorization.lock().await.count_documents(doc! {}).await {
            Ok(r)
        } else {
            Err(AuthorizationError::DatabaseError)
        }
    }
}
