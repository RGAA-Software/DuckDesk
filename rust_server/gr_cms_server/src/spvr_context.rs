use crate::config::spvr_access_info::SpvrAccessInfo;
use crate::config::spvr_server_config::SpvrServerConfig;
use crate::{gSpvrClientConnMgr, gSpvrContext, gSpvrSettings};
use gr_auth_mgr::crypto_keys::AES_DEPLOY_AUTH;
use gr_base::crypto_util::aes_encrypt;
use tokio::net::UdpSocket;

pub struct SpvrContext {
    pub machine_code: String,
}

impl SpvrContext {
    pub fn new() -> SpvrContext {
        SpvrContext {
            machine_code: "".to_string(),
        }
    }

    pub fn update_machine_code(&mut self, machine_code: String) {
        self.machine_code = machine_code;
    }

    pub async fn gen_access_info(&self) -> SpvrAccessInfo {
        // myself
        let self_config = gSpvrSettings.lock().await.get_server_config().await;
        SpvrAccessInfo {
            spvr_srv_config: self_config,
        }
    }

    pub async fn get_encrypt_access_info(&self) -> Result<String, String> {
        let info = self.gen_access_info().await;
        //tracing::info!("raw access info: {:#?}", info);
        if let Ok(v) = serde_json::to_string(&info) {
            let v = aes_encrypt(v.as_str(), &AES_DEPLOY_AUTH);
            return if let Ok(v) = v {
                Ok(format!("spvr://access##{}", v))
            } else {
                Err("Failed to encrypt spvr.".to_string())
            };
        }
        Err("Failed to serialize as json.".to_string())
    }

    pub async fn broadcast_access_info(&self, port: u16) {
        tokio::spawn(async move {
            // 创建UDP socket
            let socket = UdpSocket::bind("0.0.0.0:0").await;
            if let Err(e) = socket {
                tracing::error!("Failed to bind socket: {:?}", e);
                return;
            }
            let socket = socket.unwrap();

            // 启用广播
            socket.set_broadcast(true).unwrap();
            loop {
                let broadcast_addr = format!("255.255.255.255:{}", port);
                let msg = gSpvrContext.lock().await.get_encrypt_access_info().await;
                if let Err(e) = msg {
                    tokio::time::sleep(std::time::Duration::from_secs(2)).await;
                    continue;
                }
                let msg = msg.unwrap();
                let bytes_sent = socket.send_to(msg.as_bytes(), &broadcast_addr).await;
                if let Err(e) = bytes_sent {
                    tracing::error!("Failed to send message: {:?}", e);
                    tokio::time::sleep(std::time::Duration::from_secs(2)).await;
                    continue;
                }
                tokio::time::sleep(std::time::Duration::from_secs(2)).await;
            }
        });
    }

    pub async fn test_broadcast(&self, port: u16) {
        tokio::spawn(async move {
            let socket = UdpSocket::bind(format!("0.0.0.0:{}", port)).await.unwrap();
            let mut buf = [0u8; 4096];
            loop {
                let (n, addr) = socket.recv_from(&mut buf).await.unwrap();
                println!("recv from {}: {}", addr, String::from_utf8_lossy(&buf[..n]));
            }
        });
    }
}

#[cfg(test)]
mod tests {}
