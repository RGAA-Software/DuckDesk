use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine as _};
use mongodb::bson::{doc, DateTime};
use mongodb::options::ReturnDocument;
use px_base::hash_util::{compute_hash, HashAlgo};
use ring::rand::{SecureRandom, SystemRandom};
use serde::{Deserialize, Serialize};

use crate::cms_api_error::CmsApiError;
use crate::gCmsDatabase;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UserInvite {
    pub invite_hash: String,
    #[serde(default)]
    pub group_ids: Vec<String>,
    pub created_at: i64,
    pub expires_at: i64,
    pub cleanup_at: DateTime,
    #[serde(default)]
    pub used_at: Option<i64>,
    #[serde(default)]
    pub used_by: Option<String>,
    #[serde(default)]
    pub reservation_expires_at: Option<i64>,
}

pub struct ReservedInvite {
    pub invite_hash: String,
    pub group_ids: Vec<String>,
    pub reservation_id: String,
}

fn hash(raw: &str) -> String {
    compute_hash(HashAlgo::SHA256, raw.as_bytes())
}

pub async fn issue(
    group_ids: Vec<String>,
    lifetime_minutes: i64,
) -> Result<(String, UserInvite), CmsApiError> {
    let mut bytes = [0_u8; 24];
    SystemRandom::new()
        .fill(&mut bytes)
        .map_err(|_| CmsApiError::InternalError)?;
    let raw = URL_SAFE_NO_PAD.encode(bytes);
    let now = px_base::get_current_timestamp();
    let expires_at = now + lifetime_minutes.clamp(1, 7 * 24 * 60) * 60_000;
    let invite = UserInvite {
        invite_hash: hash(&raw),
        group_ids,
        created_at: now,
        expires_at,
        cleanup_at: DateTime::from_millis(expires_at),
        used_at: None,
        used_by: None,
        reservation_expires_at: None,
    };
    gCmsDatabase
        .lock()
        .await
        .user_invite()
        .lock()
        .await
        .insert_one(invite.clone())
        .await
        .map_err(|_| CmsApiError::DatabaseError)?;
    Ok((raw, invite))
}

pub async fn reserve(raw: &str) -> Result<ReservedInvite, CmsApiError> {
    if raw.is_empty() || raw.len() > 256 {
        return Err(CmsApiError::InvalidParams);
    }
    let reservation_id = uuid::Uuid::new_v4().simple().to_string();
    let now = px_base::get_current_timestamp();
    let reservation_expires_at = now + 5 * 60_000;
    let invite = gCmsDatabase
        .lock()
        .await
        .user_invite()
        .lock()
        .await
        .find_one_and_update(
            doc! {
                "invite_hash": hash(raw),
                "expires_at": { "$gt": now },
                "$or": [
                    { "used_at": null },
                    { "reservation_expires_at": { "$lte": now } },
                ],
            },
            doc! { "$set": {
                "used_at": now,
                "used_by": &reservation_id,
                "reservation_expires_at": reservation_expires_at,
            } },
        )
        .return_document(ReturnDocument::After)
        .await
        .map_err(|_| CmsApiError::DatabaseError)?
        .ok_or(CmsApiError::InvalidCredentials)?;
    Ok(ReservedInvite {
        invite_hash: invite.invite_hash,
        group_ids: invite.group_ids,
        reservation_id,
    })
}

pub async fn commit(reservation: &ReservedInvite, uid: &str) -> Result<(), CmsApiError> {
    let result = gCmsDatabase.lock().await.user_invite().lock().await
        .update_one(
            doc! { "invite_hash": &reservation.invite_hash, "used_by": &reservation.reservation_id },
            doc! { "$set": { "used_by": uid, "reservation_expires_at": null } },
        )
        .await.map_err(|_| CmsApiError::DatabaseError)?;
    if result.matched_count != 1 {
        return Err(CmsApiError::InvalidCredentials);
    }
    Ok(())
}

pub async fn release(reservation: &ReservedInvite) {
    let _ = gCmsDatabase.lock().await.user_invite().lock().await
        .update_one(
            doc! { "invite_hash": &reservation.invite_hash, "used_by": &reservation.reservation_id },
            doc! { "$set": { "used_at": null, "used_by": null, "reservation_expires_at": null } },
        ).await;
}
