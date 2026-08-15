use px_base::hash_util::{compute_hash, HashAlgo};

pub const APP_SECRET_SALT: &str = "bfa900206bed4db59156ae5fead1d249";

pub fn calculate_app_secret(appkey: String) -> String {
    let input = compute_hash(HashAlgo::SHA256, (appkey + APP_SECRET_SALT).as_bytes());
    compute_hash(HashAlgo::MD5, input.as_bytes())
}

pub fn is_appkey_secret_paired(appkey: String, app_secret: String) -> bool {
    if appkey.is_empty() || app_secret.is_empty() {
        return false;
    }
    let calculated_secret = calculate_app_secret(appkey);
    calculated_secret == app_secret
}
