use aes_gcm::{
    aead::{Aead, KeyInit},
    Aes256Gcm, Nonce,
};
use base64::{engine::general_purpose, Engine as _};
use rand::RngCore;

pub fn aes_encrypt(plaintext: &str, key_bytes: &[u8; 32]) -> Result<String, String> {
    let cipher =
        Aes256Gcm::new_from_slice(key_bytes).unwrap();

    let mut nonce_bytes = [0u8; 12];
    rand::rng().fill_bytes(&mut nonce_bytes);
    let nonce = Nonce::from_slice(&nonce_bytes);
    let ciphertext = cipher
        .encrypt(nonce, plaintext.as_bytes())
        .map_err(|e| e.to_string())?;

    // base64(nonce + ciphertext)
    let mut result = nonce_bytes.to_vec();
    result.extend_from_slice(&ciphertext);
    let r = general_purpose::STANDARD.encode(result);
    Ok(r)
}

pub fn aes_decrypt(encoded: &str, key_bytes: &[u8; 32]) -> Result<String, String> {
    let cipher =
        Aes256Gcm::new_from_slice(key_bytes).unwrap();

    let data =
        general_purpose::STANDARD.decode(encoded)
        .map_err(|e| e.to_string())?;

    let (nonce_bytes, ciphertext) = data.split_at(12);
    let nonce = Nonce::from_slice(nonce_bytes);

    let plaintext = cipher
        .decrypt(nonce, ciphertext)
        .map_err(|e| e.to_string())?;

    String::from_utf8(plaintext)
        .map_err(|e| e.to_string())
}

pub fn base64_encode(input: &str) -> String {
    general_purpose::STANDARD.encode(input.as_bytes())
}

pub fn base64_decode(encoded: &str) -> Result<String, String> {
    match general_purpose::STANDARD.decode(encoded) {
        Ok(bytes) => String::from_utf8(bytes).map_err(|e| e.to_string()),
        Err(e) => Err(e.to_string()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_aes() {
        let key: [u8; 32] = *b"01234567012345670123456701234567";

        let text = "hello rust encryption!";
        println!("input: {}", text);
        let encrypted = aes_encrypt(text, &key).unwrap();
        println!("encrypt: {}", encrypted);

        let decrypted = aes_decrypt(&encrypted, &key).unwrap();
        println!("decrypt: {}", decrypted);
        assert_eq!(text, decrypted);
    }

    #[test]
    fn test_base64() {
        let text = "hello rust!";
        let encoded = base64_encode(text);
        println!("encoded: {}", encoded);

        let decoded = base64_decode(&encoded).unwrap();
        println!("decoded: {}", decoded);

        assert_eq!(decoded, text);
    }
}