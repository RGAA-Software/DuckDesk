use crate::{
    control, update_model::UpdateRow, ClientType, StoreError, TokenDigest, UpdateDecision,
    UpdateRelease,
};
use px_release_catalog::{ReleaseQuery, ReleaseSpec};
use sha2::{Digest, Sha256};
use sqlx::{PgConnection, PgPool};
use uuid::Uuid;

#[derive(Clone)]
pub struct UpdateStore {
    pub(crate) pool: PgPool,
}
impl UpdateStore {
    #[cfg(feature = "pg-integration")]
    pub async fn connect(
        config: &px_pg::DatabaseConfig,
        deployment: Uuid,
    ) -> Result<Self, StoreError> {
        Ok(Self {
            pool: crate::connect(config, deployment).await?,
        })
    }
    #[cfg(feature = "pg-integration")]
    pub async fn close(&self) {
        self.pool.close().await;
    }

    pub async fn register(
        &self,
        token: &TokenDigest,
        request_id: Uuid,
        repository_publication_sha256: &str,
        repository_root_version: i64,
        artifact: &ReleaseSpec,
    ) -> Result<UpdateRelease, StoreError> {
        if request_id.is_nil()
            || !valid_sha256(repository_publication_sha256)
            || repository_root_version < 1
        {
            return Err(StoreError::InvalidInput);
        }
        artifact
            .validate_immutable_target_name()
            .map_err(|_| StoreError::InvalidInput)?;
        let artifact_digest = artifact.digest().map_err(|_| StoreError::InvalidInput)?;
        let mut request_digest = Sha256::new();
        request_digest.update(artifact_digest);
        request_digest.update(repository_publication_sha256.as_bytes());
        request_digest.update(repository_root_version.to_be_bytes());
        let request_hash: [u8; 32] = request_digest.finalize().into();
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, token, true).await?;
        if let Some(row) = sqlx::query_file_as!(
            UpdateRow,
            "queries/update_by_request.sql",
            actor,
            request_id
        )
        .fetch_optional(&mut *tx)
        .await?
        {
            if row.request_hash.as_slice() != request_hash.as_slice() {
                return Err(StoreError::Rejected);
            }
            // Return the current policy; replay cannot undo a later withdrawal.
            let result = row.view()?;
            tx.commit().await?;
            return Ok(result);
        }
        let release_target = &artifact.target;
        let row = sqlx::query_file_as!(
            UpdateRow,
            "queries/register_update.sql",
            Uuid::new_v4(),
            actor,
            request_id,
            request_hash.as_slice(),
            release_target.product.name(),
            release_target.distribution.name(),
            release_target.release_namespace,
            release_target.oem_id,
            release_target.channel.name(),
            release_target.os.name(),
            release_target.architecture.name(),
            artifact.build_number,
            artifact.version,
            artifact.metadata_base_url,
            artifact.targets_base_url,
            artifact.target_name,
            artifact.sha256,
            repository_publication_sha256,
            repository_root_version,
            artifact.platform_signer_sha256,
            artifact.size_bytes
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::event(&mut tx, actor, &row).await?;
        let result = row.view()?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn decide(
        &self,
        token: &TokenDigest,
        id: Uuid,
        expected_revision: i64,
        decision: UpdateDecision,
    ) -> Result<UpdateRelease, StoreError> {
        if expected_revision < 1 {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, token, true).await?;
        let row = sqlx::query_file_as!(UpdateRow, "queries/lock_update.sql", id)
            .fetch_optional(&mut *tx)
            .await?
            .ok_or(StoreError::Rejected)?;
        if row.revision != expected_revision {
            return Err(StoreError::Rejected);
        }
        if row.state == decision.state() {
            let result = row.view()?;
            tx.commit().await?;
            return Ok(result);
        }
        let row =
            sqlx::query_file_as!(UpdateRow, "queries/decide_update.sql", id, decision.state())
                .fetch_one(&mut *tx)
                .await?;
        Self::event(&mut tx, actor, &row).await?;
        let result = row.view()?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_managed(
        &self,
        token: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<UpdateRelease>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        let rows = sqlx::query_file_as!(
            UpdateRow,
            "queries/list_updates.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        let result = rows
            .into_iter()
            .map(UpdateRow::view)
            .collect::<Result<Vec<_>, _>>()?;
        tx.commit().await?;
        Ok(result)
    }

    pub async fn latest(
        &self,
        token: &TokenDigest,
        client: ClientType,
        target: &ReleaseQuery,
    ) -> Result<UpdateRelease, StoreError> {
        target.validate().map_err(|_| StoreError::InvalidInput)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        if client == ClientType::AdminWeb {
            control::authorize(&mut tx, token, false).await?;
        } else {
            crate::resource_policy::resource_user(&mut tx, token, client).await?;
        }
        let row = sqlx::query_file_as!(
            UpdateRow,
            "queries/latest_update.sql",
            target.product.name(),
            target.distribution.name(),
            target.release_namespace,
            target.oem_id,
            target.channel.name(),
            target.os.name(),
            target.architecture.name()
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        // Never silently offer an older build after the newest candidate is withdrawn/pending.
        if row.state != "approved" {
            return Err(StoreError::Rejected);
        }
        let result = row.view()?;
        tx.commit().await?;
        Ok(result)
    }
    async fn event(
        connection: &mut PgConnection,
        actor: Uuid,
        row: &UpdateRow,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/update_release_event.sql",
            Uuid::new_v4(),
            row.id,
            actor,
            row.revision,
            row.state
        )
        .execute(connection)
        .await?;
        Ok(())
    }
}

fn valid_sha256(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte_value| byte_value.is_ascii_digit() || (b'a'..=b'f').contains(&byte_value))
}
