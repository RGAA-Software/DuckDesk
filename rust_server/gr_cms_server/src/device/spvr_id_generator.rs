use rand::Rng;
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct PrIdGenerator {}

pub struct GenDeviceInfo {
    pub device_id: String,
    pub seed: String,
    pub random_pwd: String,
}

impl PrIdGenerator {
    pub fn new() -> Arc<Mutex<PrIdGenerator>> {
        Arc::new(Mutex::new(PrIdGenerator {}))
    }

    pub async fn init(&self) {}

    pub fn generate_new_id(&self, info: &String, platform: &String) -> GenDeviceInfo {
        let ignore_info = false;
        let mut seed = info.clone();
        if info.is_empty() || ignore_info {
            seed = uuid::Uuid::new_v4().to_string();
        } else {
            seed = platform.clone() + info;
        }

        let mut device_id = "".to_string();
        let digest = gr_base::md5_hex(&seed.clone());
        for (index, value) in digest.as_bytes().iter().enumerate() {
            if index == 0
                || index == 7
                || index == 11
                || index == 16
                || index == 18
                || index == 23
                || index == 26
                || index == 28
                || index == 30
            {
                let v = (*value) % 10;
                device_id += &v.to_string();
            }
        }

        tracing::info!("generate target_id: {}", device_id);

        GenDeviceInfo {
            device_id,
            seed,
            random_pwd: PrIdGenerator::generate_random_pwd(),
        }
    }

    pub fn generate_random_pwd() -> String {
        let mut rng = rand::rng();
        let charset: &[u8] = b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789";
        let password: String = (0..8)
            .map(|_| {
                let idx = rng.random_range(0..charset.len());
                charset[idx] as char
            })
            .collect();
        password
    }
}
