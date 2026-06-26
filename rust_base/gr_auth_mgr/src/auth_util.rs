use crate::app_secret_util::is_appkey_secret_paired;
use crate::authorization::Authorization;
use crate::crypto_keys::AES_DEPLOY_AUTH;
use gr_base::crypto_util::aes_decrypt;

pub fn parse_authorization(auth: String) -> Result<Authorization, String> {
    let r = aes_decrypt(auth.as_str(), &AES_DEPLOY_AUTH)?;
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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::app_secret_util::calculate_app_secret;
    use gr_base::crypto_util::aes_encrypt;

    #[test]
    fn legacy_aes_deploy_string_still_parses() {
        let auth = Authorization {
            auth_id: "legacy-id".to_string(),
            auth_name: "legacy".to_string(),
            machine_code: "mc-legacy".to_string(),
            appkey: "appkey-legacy".to_string(),
            app_secret: calculate_app_secret("appkey-legacy".to_string()),
            username: "admin".to_string(),
            password: "secret".to_string(),
            days: 30,
            max_streams: 4,
            created_timestamp_ms: 1000,
            end_timestamp_ms: 1000 + 30 * 24 * 60 * 60 * 1000,
            role: 1,
            ..Default::default()
        };
        let json = serde_json::to_string(&auth).unwrap();
        let encrypted = aes_encrypt(&json, &AES_DEPLOY_AUTH).unwrap();

        let parsed = parse_authorization(encrypted).unwrap();
        assert_eq!(parsed.auth_id, "legacy-id");
        assert_eq!(parsed.appkey, "appkey-legacy");
        assert!(verify_authorization(&parsed).unwrap());
    }

    #[test]
    fn verify_authorization_rejects_mismatched_secret() {
        let auth = Authorization {
            appkey: "appkey-1".to_string(),
            app_secret: "wrong-secret".to_string(),
            ..Default::default()
        };
        assert!(verify_authorization(&auth).is_err());
    }
}
