use px_auth_mgr::app_secret_util::calculate_app_secret;
use px_base::hash_util::{HashAlgo, compute_hash};
use mongodb::bson::uuid;
use rand::Rng;

pub struct AppkeySecret {
    pub appkey: String,
    pub app_secret: String,
    pub username: String,
    pub password: String,
}

pub fn gen_appkey_secret(name: String, machine_code: String) -> AppkeySecret {
    let random_salt = uuid::Uuid::new().to_string();
    let input = name + machine_code.as_str() + random_salt.as_str();
    let appkey = compute_hash(HashAlgo::MD5, input.as_bytes());
    let app_secret = calculate_app_secret(appkey.clone());
    let username = "CmsAdmin".to_string();
    let password = generate_random_password();
    AppkeySecret {
        appkey,
        app_secret,
        username,
        password,
    }
}

fn generate_random_password() -> String {
    let mut rng = rand::rng();
    let charset: &[u8] = b"@!#%$&*abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789";
    let password: String = (0..8)
        .map(|_| {
            let idx = rng.random_range(0..charset.len());
            charset[idx] as char
        })
        .collect();
    password
}
