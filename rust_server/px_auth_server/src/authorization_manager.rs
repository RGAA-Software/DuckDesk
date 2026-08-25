use crate::author_appkey_generator::gen_appkey_secret;
use crate::author_license_keys::sign_authorization_model;
use crate::{gAuthorDatabase, gAuthorSettings, gLicenseSigner};
use mongodb::bson::oid::ObjectId;
use mongodb::bson::{Bson, DateTime, Document, Regex, doc};
use px_auth_mgr::authorization::{
    Authorization, AuthorizationVo, LEGACY_PRODUCT_CMS, LEGACY_PRODUCT_PIXELS_CMS, PRODUCT_CONSOLE,
    PRODUCT_PIXELS_CONSOLE, normalize_product,
};
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
    use px_auth_mgr::authorization::{Authorization, normalize_product};
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

    pub fn remove(auth_id: &str) -> bool {
        STORE.lock().unwrap().remove(auth_id).is_some()
    }

    pub fn get_by_machine_code_product(machine_code: &str, product: &str) -> Option<Authorization> {
        let product = normalize_product(product);
        STORE
            .lock()
            .unwrap()
            .values()
            .find(|a| a.machine_code == machine_code && normalize_product(&a.product) == product)
            .cloned()
    }

    pub fn list_by_product(product: &str) -> Vec<Authorization> {
        let product = normalize_product(product);
        let mut items: Vec<_> = STORE
            .lock()
            .unwrap()
            .values()
            .filter(|a| normalize_product(&a.product) == product)
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

fn compatible_product_names(product: &str) -> Vec<String> {
    match normalize_product(product) {
        PRODUCT_CONSOLE => vec![PRODUCT_CONSOLE.to_string(), LEGACY_PRODUCT_CMS.to_string()],
        PRODUCT_PIXELS_CONSOLE => vec![
            PRODUCT_PIXELS_CONSOLE.to_string(),
            LEGACY_PRODUCT_PIXELS_CMS.to_string(),
        ],
        canonical => vec![canonical.to_string()],
    }
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
            px_auth_mgr::authorization::PRODUCT_CONSOLE.to_string(),
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
            product: normalize_product(&product).to_string(),
            revoked: false,
            revoked_at_ms: 0,
            ..Default::default()
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

    /// Find a device authorization by its device code (machine_code) + product.
    pub async fn query_authorization_by_machine_code_product(
        &self,
        machine_code: &str,
        product: &str,
    ) -> Option<Authorization> {
        #[cfg(test)]
        {
            return memory_store::get_by_machine_code_product(machine_code, product);
        }
        #[cfg(not(test))]
        {
            let c_authorization = gAuthorDatabase.lock().await.authorization();
            let products = compatible_product_names(product);
            let filter = doc! {
                "machine_code": machine_code,
                "product": { "$in": products },
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
            let products = compatible_product_names(&product);
            self.query_authorizations(doc! { "product": { "$in": products } }, page, page_size)
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

    pub async fn revoke_authorization(
        &self,
        auth_id: String,
    ) -> Result<Authorization, AuthorizationError> {
        let auth = self.query_authorization_by_id(auth_id.clone()).await;
        let mut auth = match auth {
            Some(a) => a,
            None => return Err(AuthorizationError::NotFound),
        };
        let now_ms = px_base::get_current_timestamp();
        auth.revoked = true;
        auth.revoked_at_ms = now_ms;
        auth.last_modify_timestamp = now_ms;
        if self.update_authorization(auth_id, auth.clone()).await {
            Ok(auth)
        } else {
            Err(AuthorizationError::DatabaseError)
        }
    }

    /// 硬删除授权记录（从数据库彻底移除，不可恢复）。
    pub async fn delete_authorization(&self, auth_id: String) -> Result<(), AuthorizationError> {
        #[cfg(test)]
        {
            return if memory_store::remove(&auth_id) {
                Ok(())
            } else {
                Err(AuthorizationError::NotFound)
            };
        }
        #[cfg(not(test))]
        {
            let c_authorization = gAuthorDatabase.lock().await.authorization();
            let r = c_authorization
                .lock()
                .await
                .delete_one(doc! {"auth_id": auth_id})
                .await;
            match r {
                Ok(res) if res.deleted_count > 0 => Ok(()),
                Ok(_) => Err(AuthorizationError::NotFound),
                Err(e) => {
                    tracing::error!("error deleting auth: {}", e);
                    Err(AuthorizationError::DatabaseError)
                }
            }
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

    /// Record a status report sent by a licensed client (heartbeat-like).
    /// Does NOT touch `last_modify_timestamp` — this is telemetry, not an admin edit.
    #[allow(clippy::too_many_arguments)]
    pub async fn update_client_report(
        &self,
        auth_id: &str,
        client_version: &str,
        client_status: &str,
        client_os: &str,
        client_device_count: i32,
        reported_at_ms: i64,
    ) -> bool {
        #[cfg(test)]
        {
            let Some(mut auth) = memory_store::get_by_id(auth_id) else {
                return false;
            };
            auth.client_version = client_version.to_string();
            auth.client_status = client_status.to_string();
            auth.client_os = client_os.to_string();
            auth.client_device_count = client_device_count;
            auth.client_reported_at_ms = reported_at_ms;
            return memory_store::update(auth_id, auth);
        }
        #[cfg(not(test))]
        {
            let c_authorization = gAuthorDatabase.lock().await.authorization();
            let update_doc = doc! {
                "$set": {
                    "client_version": client_version,
                    "client_status": client_status,
                    "client_os": client_os,
                    "client_device_count": client_device_count,
                    "client_reported_at_ms": reported_at_ms,
                }
            };
            let r = c_authorization
                .lock()
                .await
                .update_one(doc! {"auth_id": auth_id}, update_doc)
                .await;
            if let Err(e) = r {
                tracing::error!("error updating client report: {}", e);
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
        sub_update_doc.insert("last_modify_timestamp", px_base::get_current_timestamp());
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

        sub_update_doc.insert("last_modify_timestamp", px_base::get_current_timestamp());
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
