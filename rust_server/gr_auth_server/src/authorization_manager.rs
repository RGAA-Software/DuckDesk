use crate::author_appkey_generator::gen_appkey_secret;
use crate::author_license_keys::sign_authorization_model;
use crate::{gAuthorDatabase, gAuthorSettings, gLicenseSigner};
use gr_auth_mgr::authorization::{Authorization, AuthorizationVo};
use mongodb::bson::oid::ObjectId;
use mongodb::bson::{Bson, DateTime, Document, Regex, doc};
use std::collections::HashMap;
use thiserror::Error;

#[cfg(not(test))]
use futures_util::StreamExt;
#[cfg(not(test))]
use mongodb::Cursor;
#[cfg(not(test))]
use mongodb::bson::to_document;

/// In-memory authorization store used by unit/integration tests so they do not
/// require a live MongoDB. Production builds never compile this path.
#[cfg(test)]
mod memory_store {
    use gr_auth_mgr::authorization::Authorization;
    use std::collections::HashMap;
    use std::sync::Mutex;

    lazy_static::lazy_static! {
        static ref STORE: Mutex<HashMap<String, Authorization>> = Mutex::new(HashMap::new());
    }

    pub fn clear() {
        STORE.lock().unwrap().clear();
    }

    pub fn insert(auth: Authorization) -> bool {
        let mut g = STORE.lock().unwrap();
        if g.values().any(|a| a.auth_name == auth.auth_name) {
            return false;
        }
        g.insert(auth.auth_id.clone(), auth);
        true
    }

    pub fn get_by_id(auth_id: &str) -> Option<Authorization> {
        STORE.lock().unwrap().get(auth_id).cloned()
    }

    pub fn get_by_name(name: &str) -> Option<Authorization> {
        STORE
            .lock()
            .unwrap()
            .values()
            .find(|a| a.auth_name == name)
            .cloned()
    }

    pub fn update(auth_id: &str, auth: Authorization) -> bool {
        let mut g = STORE.lock().unwrap();
        if !g.contains_key(auth_id) {
            return false;
        }
        g.insert(auth_id.to_string(), auth);
        true
    }

    pub fn list_by_product(product: &str) -> Vec<Authorization> {
        let mut items: Vec<_> = STORE
            .lock()
            .unwrap()
            .values()
            .filter(|a| a.product == product)
            .cloned()
            .collect();
        items.sort_by(|a, b| b.created_timestamp_ms.cmp(&a.created_timestamp_ms));
        items
    }

    pub fn list_all() -> Vec<Authorization> {
        let mut items: Vec<_> = STORE.lock().unwrap().values().cloned().collect();
        items.sort_by(|a, b| b.created_timestamp_ms.cmp(&a.created_timestamp_ms));
        items
    }
}

#[cfg(test)]
pub fn clear_authorization_memory_store() {
    memory_store::clear();
}

#[derive(Debug, Error)]
pub enum AuthorizationError {
    #[error("Already exist")]
    AlreadyExist,

    #[error("Insert to db failed")]
    DatabaseError,

    #[error("Time convert error")]
    TimeConvertError,

    #[error("Authorization not found")]
    NotFound,
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
        self.gen_new_authorization_for_product(
            name,
            machine_code,
            days,
            max_streams,
            role,
            gr_auth_mgr::authorization::PRODUCT_CMS.to_string(),
        )
        .await
    }

    pub async fn gen_new_authorization_for_product(
        &self,
        name: String,
        machine_code: String,
        days: i32,
        max_streams: i32,
        role: i32,
        product: String,
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
            product,
            revoked: false,
            revoked_at_ms: 0,
        };

        if self.insert_authorization(auth.clone()).await {
            Ok(auth)
        } else {
            Err(AuthorizationError::DatabaseError)
        }
    }

    pub async fn insert_authorization(&self, auth: Authorization) -> bool {
        #[cfg(test)]
        {
            return memory_store::insert(auth);
        }
        #[cfg(not(test))]
        {
            let c_authorization = gAuthorDatabase.lock().await.authorization();
            let r = c_authorization.lock().await.insert_one(auth).await;
            if let Err(e) = r {
                tracing::error!("error inserting authorization: {}", e);
                return false;
            }
            true
        }
    }

    pub async fn query_authorization_by_id(&self, auth_id: String) -> Option<Authorization> {
        #[cfg(test)]
        {
            return memory_store::get_by_id(&auth_id);
        }
        #[cfg(not(test))]
        {
            let c_authorization = gAuthorDatabase.lock().await.authorization();
            let filter = doc! {
                "auth_id": auth_id,
            };
            let r = c_authorization.lock().await.find_one(filter).await;
            r.unwrap_or(None)
        }
    }

    pub async fn query_authorization_by_name(&self, name: String) -> Option<Authorization> {
        #[cfg(test)]
        {
            return memory_store::get_by_name(&name);
        }
        #[cfg(not(test))]
        {
            let c_authorization = gAuthorDatabase.lock().await.authorization();
            let filter = doc! {
                "auth_name": name,
            };
            let r = c_authorization.lock().await.find_one(filter).await;
            r.unwrap_or(None)
        }
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

    pub async fn query_authorizations_by_product(
        &self,
        product: String,
        page: i32,
        page_size: i32,
    ) -> Result<Vec<AuthorizationVo>, AuthorizationError> {
        #[cfg(test)]
        {
            let all = memory_store::list_by_product(&product);
            let total = all.len() as u64;
            let skip = ((page - 1).max(0) as usize) * (page_size.max(0) as usize);
            let mut out = Vec::new();
            for mut auth in all.into_iter().skip(skip).take(page_size.max(0) as usize) {
                if let Some(signer) = gLicenseSigner.lock().await.as_ref()
                    && let Ok(signed) = sign_authorization_model(signer, &auth)
                {
                    auth.deploy_str = signed.to_deploy_string().unwrap_or_default();
                }
                out.push(auth.as_vo(total));
            }
            return Ok(out);
        }
        #[cfg(not(test))]
        {
            self.query_authorizations(doc! { "product": product }, page, page_size)
                .await
        }
    }

    pub async fn query_authorizations(
        &self,
        filter: Document,
        page: i32,
        page_size: i32,
    ) -> Result<Vec<AuthorizationVo>, AuthorizationError> {
        #[cfg(test)]
        {
            let _ = filter;
            let all = memory_store::list_all();
            let total = all.len() as u64;
            let skip = ((page - 1).max(0) as usize) * (page_size.max(0) as usize);
            let mut out = Vec::new();
            for mut auth in all.into_iter().skip(skip).take(page_size.max(0) as usize) {
                if let Some(signer) = gLicenseSigner.lock().await.as_ref()
                    && let Ok(signed) = sign_authorization_model(signer, &auth)
                {
                    auth.deploy_str = signed.to_deploy_string().unwrap_or_default();
                }
                out.push(auth.as_vo(total));
            }
            return Ok(out);
        }
        #[cfg(not(test))]
        {
            let total = self.query_authorizations_count_filtered(&filter).await?;

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
                        && let Ok(signed) = sign_authorization_model(signer, &auth)
                    {
                        auth.deploy_str = signed.to_deploy_string().unwrap_or_default();
                    }
                    authorizations.push(auth.as_vo(total));
                }
            }
            Ok(authorizations)
        }
    }

    pub async fn revoke_authorization(&self, auth_id: String) -> Result<Authorization, AuthorizationError> {
        let auth = self.query_authorization_by_id(auth_id.clone()).await;
        let mut auth = match auth {
            Some(a) => a,
            None => return Err(AuthorizationError::NotFound),
        };
        let now_ms = gr_base::get_current_timestamp();
        auth.revoked = true;
        auth.revoked_at_ms = now_ms;
        auth.last_modify_timestamp = now_ms;
        if self.update_authorization(auth_id, auth.clone()).await {
            Ok(auth)
        } else {
            Err(AuthorizationError::DatabaseError)
        }
    }

    pub async fn update_authorization(&self, auth_id: String, auth: Authorization) -> bool {
        #[cfg(test)]
        {
            return memory_store::update(&auth_id, auth);
        }
        #[cfg(not(test))]
        {
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
        self.query_authorizations_count_filtered(&doc! {}).await
    }

    pub async fn query_authorizations_count_filtered(
        &self,
        filter: &Document,
    ) -> Result<u64, AuthorizationError> {
        let c_authorization = gAuthorDatabase.lock().await.authorization();
        if let Ok(r) = c_authorization
            .lock()
            .await
            .count_documents(filter.clone())
            .await
        {
            Ok(r)
        } else {
            Err(AuthorizationError::DatabaseError)
        }
    }
}
