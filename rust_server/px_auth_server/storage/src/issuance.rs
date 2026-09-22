use crate::{model::enum_text, AuthError, IssueRequest, IssuedLicense, LicenseStore};
use chrono::{DateTime, Utc};
use sha2::{Digest, Sha256};
use uuid::Uuid;

impl LicenseStore {
    pub async fn issue(
        &self,
        token: &[u8; 32],
        request_id: Uuid,
        request: IssueRequest,
    ) -> Result<IssuedLicense, AuthError> {
        if request_id.is_nil() {
            return Err(AuthError::Invalid);
        }
        let bytes = serde_json::to_vec(&request).map_err(|_| AuthError::Invalid)?;
        let body = Sha256::digest(&bytes);
        let mut tx = self.pool.begin().await?;
        let actor = Self::authorize(&mut tx, token, true).await?;
        sqlx::query_file!("queries/claim_request.sql", actor, request_id, &body[..])
            .execute(&mut *tx)
            .await?;
        let saved = sqlx::query_file!("queries/lock_request.sql", actor, request_id)
            .fetch_one(&mut *tx)
            .await?;
        if saved.body_sha256 != body[..] {
            return Err(AuthError::Conflict);
        }
        if let Some(id) = saved.issuance_id {
            let cached = sqlx::query_file_as!(IssuedLicense, "queries/cached_issuance.sql", id)
                .fetch_optional(&mut *tx)
                .await?
                .ok_or(AuthError::Rejected)?;
            tx.commit().await?;
            return Ok(cached);
        }
        let (id, revision, terms, action) = match request {
            IssueRequest::Create { terms } => (Uuid::new_v4(), 1, terms, "issued"),
            IssueRequest::Renew {
                license_id,
                expected_revision,
                terms,
            } => {
                if expected_revision < 1 || license_id.is_nil() {
                    return Err(AuthError::Invalid);
                }
                let current = sqlx::query_file!("queries/lock_license.sql", license_id)
                    .fetch_optional(&mut *tx)
                    .await?;
                if current.map(|row| (row.revision, row.revoked_at))
                    != Some((expected_revision, None))
                {
                    return Err(AuthError::Conflict);
                }
                (
                    license_id,
                    expected_revision.checked_add(1).ok_or(AuthError::Invalid)?,
                    terms,
                    "renewed",
                )
            }
        };
        let now: DateTime<Utc> = sqlx::query_file_scalar!("queries/database_time.sql")
            .fetch_one(&mut *tx)
            .await?;
        let payload = terms.payload(id, revision, now.timestamp(), self.signer.key_id())?;
        let not_before =
            DateTime::from_timestamp(payload.not_before, 0).ok_or(AuthError::Invalid)?;
        let expires = DateTime::from_timestamp(terms.expires_at, 0).ok_or(AuthError::Invalid)?;
        let services = terms
            .services
            .iter()
            .map(enum_text)
            .collect::<Result<Vec<_>, _>>()?;
        let product = enum_text(&terms.product)?;
        let distribution = enum_text(&terms.distribution)?;
        let mode = enum_text(&terms.mode)?;
        if revision == 1 {
            sqlx::query_file!(
                "queries/insert_license.sql",
                id,
                terms.customer_id,
                terms.deployment_id,
                product,
                distribution,
                &terms.release_namespace,
                terms.oem_id.as_deref(),
                &terms.machine_sha256,
                mode,
                not_before,
                expires,
                i64::from(terms.max_streams),
                &services
            )
            .execute(&mut *tx)
            .await?;
        } else {
            // Renewal changes entitlement, never customer/deployment/product/release-domain/machine identity.
            let rows = sqlx::query_file!(
                "queries/renew_license.sql",
                id,
                revision,
                mode,
                not_before,
                expires,
                i64::from(terms.max_streams),
                &services,
                terms.customer_id,
                terms.deployment_id,
                product,
                distribution,
                &terms.release_namespace,
                terms.oem_id.as_deref(),
                &terms.machine_sha256
            )
            .execute(&mut *tx)
            .await?
            .rows_affected();
            if rows != 1 {
                return Err(AuthError::Conflict);
            }
        }
        let wire = self.signer.sign(&payload).map_err(|_| AuthError::Invalid)?;
        let issuance = Uuid::new_v4();
        sqlx::query_file!(
            "queries/insert_issuance.sql",
            issuance,
            id,
            revision,
            payload.key_id.clone(),
            &payload.canonical_bytes().map_err(|_| AuthError::Invalid)?,
            &wire
        )
        .execute(&mut *tx)
        .await?;
        sqlx::query_file!(
            "queries/audit_issuance.sql",
            Uuid::new_v4(),
            actor,
            id,
            action,
            revision
        )
        .execute(&mut *tx)
        .await?;
        sqlx::query_file!(
            "queries/insert_license_notification.sql",
            Uuid::new_v4(),
            id,
            revision,
            action,
            Some(issuance)
        )
        .execute(&mut *tx)
        .await?;
        sqlx::query_file!("queries/complete_request.sql", actor, request_id, issuance)
            .execute(&mut *tx)
            .await?;
        tx.commit().await?;
        Ok(IssuedLicense {
            license_id: id,
            revision,
            wire,
        })
    }
}
