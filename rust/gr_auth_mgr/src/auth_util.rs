use crate::app_secret_util::is_appkey_secret_paired;
use crate::authorization::Authorization;
use crate::crypto_keys::AES_DEPLOY_AUTH;
use gr_base::crypto_util::aes_decrypt;

pub fn parse_authorization(auth: String) -> Result<Authorization, String> {
    let r = aes_decrypt(auth.as_str(), &AES_DEPLOY_AUTH)?;
    tracing::info!("Auth decoded: {:?}", r);
    let auth: Authorization = serde_json::from_str(r.as_str()).map_err(|e| {
        tracing::error!("Auth deserialize failed: {}", e);
        "invalid authorization json"
    })?;
    Ok(auth)
}

pub fn verify_authorization(auth: &Authorization) -> Result<bool, String> {
    if auth.appkey.is_empty() || auth.app_secret.is_empty() {
        return Err("invalid appkey, secret".to_string());
    }
    if is_appkey_secret_paired(auth.appkey.clone(), auth.app_secret.clone()) {
        Ok(true)
    } else {
        Err("invalid authorization".to_string())
    }
}
