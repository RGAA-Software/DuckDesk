use crate::cms_api_error::CmsApiError;
use crate::connection_ticket::model::{ConnectionTicket, TicketGrant};
use crate::event::audit;
use crate::gCmsDatabase;
use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine as _};
use mongodb::bson::{doc, Bson, DateTime};
use mongodb::options::ReturnDocument;
use px_base::hash_util::{compute_hash, HashAlgo};
use ring::rand::{SecureRandom, SystemRandom};

pub struct ConnectionTicketManager;

impl ConnectionTicketManager {
    fn validate_nonce(value: &str) -> Result<(), CmsApiError> {
        if value.is_empty()
            || value.len() > 128
            || !value
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_' | b'.'))
        {
            return Err(CmsApiError::InvalidParams);
        }
        Ok(())
    }

    fn new_token() -> Result<String, CmsApiError> {
        let mut bytes = [0_u8; 32];
        SystemRandom::new()
            .fill(&mut bytes)
            .map_err(|_| CmsApiError::InternalError)?;
        Ok(URL_SAFE_NO_PAD.encode(bytes))
    }

    fn hash(token: &str) -> String {
        compute_hash(HashAlgo::SHA256, token.as_bytes())
    }

    #[allow(clippy::too_many_arguments)]
    pub async fn issue(
        kind: &str,
        subject_type: &str,
        subject_id: &str,
        session_id: &str,
        device_id: &str,
        app_id: Option<String>,
        instance_id: Option<String>,
        permissions: Vec<String>,
        client_nonce: String,
    ) -> Result<(String, ConnectionTicket), CmsApiError> {
        Self::validate_nonce(&client_nonce)?;
        if !matches!(kind, "device" | "app_instance")
            || !matches!(subject_type, "guest" | "user" | "admin")
            || device_id.is_empty()
            || session_id.is_empty()
        {
            return Err(CmsApiError::InvalidParams);
        }
        let raw = Self::new_token()?;
        let now = px_base::get_current_timestamp();
        let ttl_seconds = crate::gCmsSettings
            .lock()
            .await
            .user
            .ticket_expire_seconds
            .clamp(5, 300);
        let expires_at = now + ttl_seconds * 1000;
        let ticket = ConnectionTicket {
            ticket_hash: Self::hash(&raw),
            kind: kind.to_string(),
            subject_type: subject_type.to_string(),
            subject_id: subject_id.to_string(),
            session_id: session_id.to_string(),
            device_id: device_id.to_string(),
            app_id,
            instance_id,
            permissions,
            client_nonce,
            created_at: now,
            expires_at,
            cleanup_at: DateTime::from_millis(expires_at),
            consumed_at: None,
        };
        gCmsDatabase
            .lock()
            .await
            .connection_ticket()
            .lock()
            .await
            .insert_one(ticket.clone())
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        Ok((raw, ticket))
    }

    pub async fn redeem(
        raw: &str,
        bound_device_id: &str,
        client_nonce: &str,
        instance_id: Option<&str>,
        request_id: &str,
    ) -> Result<TicketGrant, CmsApiError> {
        Self::validate_nonce(client_nonce)?;
        if request_id.is_empty()
            || request_id.len() > 128
            || request_id.chars().any(char::is_control)
        {
            return Err(CmsApiError::InvalidParams);
        }
        let now = px_base::get_current_timestamp();
        let ticket_hash = Self::hash(raw);
        let ticket_hash_prefix = &ticket_hash[..ticket_hash.len().min(8)];
        let mut filter = doc! {
            "ticket_hash": &ticket_hash,
            "device_id": bound_device_id,
            "client_nonce": client_nonce,
            "consumed_at": null,
            "expires_at": { "$gt": now },
        };
        match instance_id {
            Some(value) => {
                filter.insert("instance_id", value);
            }
            None => {
                filter.insert("instance_id", Bson::Null);
            }
        }
        let ticket = gCmsDatabase
            .lock()
            .await
            .connection_ticket()
            .lock()
            .await
            .find_one_and_update(filter, doc! { "$set": { "consumed_at": now } })
            .return_document(ReturnDocument::After)
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        let Some(ticket) = ticket else {
            audit::record(
                "unknown",
                "",
                "ticket_redeem",
                "failure",
                "device",
                bound_device_id,
                &format!("request_id={request_id} ticket_hash={ticket_hash_prefix} reason=invalid_expired_used_or_binding_mismatch"),
            )
            .await;
            return Err(CmsApiError::TicketExpiredOrUsed);
        };

        let session_active = gCmsDatabase
            .lock()
            .await
            .user_session()
            .lock()
            .await
            .find_one(doc! {
                "sid": &ticket.session_id,
                "subject_type": &ticket.subject_type,
                "subject_id": &ticket.subject_id,
                "revoked_at": null,
                "expires_at": { "$gt": now },
                "absolute_expires_at": { "$gt": now },
            })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?
            .is_some();
        if !session_active {
            audit::record(
                &ticket.subject_type,
                &ticket.subject_id,
                "ticket_redeem",
                "failure",
                &ticket.kind,
                ticket.instance_id.as_deref().unwrap_or(&ticket.device_id),
                &format!("request_id={request_id} ticket_hash={ticket_hash_prefix} reason=session_inactive"),
            )
            .await;
            return Err(CmsApiError::TicketExpiredOrUsed);
        }
        audit::record(
            &ticket.subject_type,
            &ticket.subject_id,
            "ticket_redeem",
            "success",
            &ticket.kind,
            ticket.instance_id.as_deref().unwrap_or(&ticket.device_id),
            &format!("request_id={request_id} ticket_hash={ticket_hash_prefix}"),
        )
        .await;
        Ok(TicketGrant {
            kind: ticket.kind,
            device_id: ticket.device_id,
            app_id: ticket.app_id,
            instance_id: ticket.instance_id,
            subject_type: ticket.subject_type,
            subject_id: ticket.subject_id,
            permissions: ticket.permissions,
            expires_at: ticket.expires_at,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::ConnectionTicketManager;

    #[test]
    fn nonce_is_url_fragment_safe() {
        assert!(ConnectionTicketManager::validate_nonce("browser_1-abc.xyz").is_ok());
        assert!(ConnectionTicketManager::validate_nonce("bad&ticket=x").is_err());
        assert!(ConnectionTicketManager::validate_nonce("").is_err());
    }
}
