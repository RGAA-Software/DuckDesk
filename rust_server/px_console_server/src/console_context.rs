use crate::config::console_access_info::ConsoleAccessInfo;
use crate::config::console_server_config::LegacyCmsServerConfig;
use crate::{gConsoleContext, gConsoleSettings};
use px_auth_mgr::crypto_keys::AES_DEPLOY_AUTH;
use px_base::crypto_util::aes_encrypt;
use tokio::net::UdpSocket;

pub struct ConsoleContext {
    pub machine_code: String,
}

impl ConsoleContext {
    pub fn new() -> ConsoleContext {
        ConsoleContext {
            machine_code: "".to_string(),
        }
    }

    pub fn update_machine_code(&mut self, machine_code: String) {
        self.machine_code = machine_code;
    }

    pub async fn gen_access_info(&self) -> ConsoleAccessInfo {
        // myself
        let self_config = gConsoleSettings.lock().await.get_server_config().await;
        let legacy_cms_srv_config = LegacyCmsServerConfig::from(&self_config);
        ConsoleAccessInfo {
            console_srv_config: self_config,
            legacy_cms_srv_config: Some(legacy_cms_srv_config),
        }
    }

    pub async fn get_encrypt_access_info(&self) -> Result<String, String> {
        let info = self.gen_access_info().await;
        //tracing::info!("raw access info: {:#?}", info);
        if let Ok(serialized_access_info) = serde_json::to_string(&info) {
            let encrypted_access_info =
                aes_encrypt(serialized_access_info.as_str(), &AES_DEPLOY_AUTH);
            return if let Ok(encrypted_access_info) = encrypted_access_info {
                Ok(format!("console://access##{}", encrypted_access_info))
            } else {
                Err("Failed to encrypt console.".to_string())
            };
        }
        Err("Failed to serialize as json.".to_string())
    }

    pub async fn broadcast_access_info(&self, port: u16) {
        tokio::spawn(async move {
            // 创建UDP socket
            let socket = UdpSocket::bind("0.0.0.0:0").await;
            if let Err(bind_error) = socket {
                tracing::error!("Failed to bind socket: {:?}", bind_error);
                return;
            }
            let socket = socket.unwrap();

            // 启用广播
            socket.set_broadcast(true).unwrap();
            loop {
                let broadcast_addr = format!("255.255.255.255:{}", port);
                let msg = gConsoleContext.lock().await.get_encrypt_access_info().await;
                if let Err(_e) = msg {
                    tokio::time::sleep(std::time::Duration::from_secs(2)).await;
                    continue;
                }
                let msg = msg.unwrap();
                let bytes_sent = socket.send_to(msg.as_bytes(), &broadcast_addr).await;
                if let Err(send_error) = bytes_sent {
                    tracing::error!("Failed to send message: {:?}", send_error);
                    tokio::time::sleep(std::time::Duration::from_secs(2)).await;
                    continue;
                }
                // Older Panels only recognize the pre-Console URI scheme.
                // The encrypted JSON contains both canonical and legacy keys.
                let legacy_msg = msg.replacen("console://access##", "cms://access##", 1);
                if let Err(send_error) =
                    socket.send_to(legacy_msg.as_bytes(), &broadcast_addr).await
                {
                    tracing::warn!("Failed to send legacy access broadcast: {:?}", send_error);
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
                let (received_length, remote_address) = socket.recv_from(&mut buf).await.unwrap();
                println!(
                    "recv from {}: {}",
                    remote_address,
                    String::from_utf8_lossy(&buf[..received_length])
                );
            }
        });
    }
}

#[cfg(test)]
mod tests {}
