use crate::{
    control,
    update_model::{
        NodeUpdateActivationRow, NodeUpdateTaskRow, NodeUpdateTrustSummaryRow, UpdateRow,
    },
    ClientType, NodeUpdateActivation, NodeUpdateCompletion, NodeUpdateTrust, NodeUpdateTrustStatus,
    NodeUpdateTrustSummary, StoreError, TokenDigest, UpdateActivationOutcome, UpdateDecision,
    UpdateRelease, UpdateTrustObservation,
};
use px_release_catalog::{Distribution, Product, ReleaseQuery, ReleaseSpec};
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

    pub async fn node_trust_summary(
        &self,
        token: &TokenDigest,
        release_id: Uuid,
        expected_distribution: Distribution,
    ) -> Result<NodeUpdateTrustSummary, StoreError> {
        if release_id.is_nil() {
            return Err(StoreError::InvalidInput);
        }
        let mut transaction = self.pool.begin().await?;
        control::read_gate(&mut transaction).await?;
        control::authorize(&mut transaction, token, false).await?;
        let release =
            Self::node_release(&mut transaction, release_id, expected_distribution).await?;
        let summary = sqlx::query_file_as!(
            NodeUpdateTrustSummaryRow,
            "queries/node_update_trust_summary.sql",
            release.artifact.target.product.name(),
            release.repository_root_version
        )
        .fetch_one(&mut *transaction)
        .await?;
        let unknown_or_behind_node_count = summary
            .eligible_node_count
            .checked_sub(summary.confirmed_node_count)
            .filter(|count| *count >= 0)
            .ok_or(StoreError::Rejected)?;
        let result = NodeUpdateTrustSummary {
            release_id: release.id,
            repository_publication_sha256: release.repository_publication_sha256,
            required_root_version: release.repository_root_version,
            eligible_node_count: summary.eligible_node_count,
            confirmed_node_count: summary.confirmed_node_count,
            unknown_or_behind_node_count,
            minimum_confirmed_root_version: summary.minimum_confirmed_root_version,
            oldest_confirmation_at: summary.oldest_confirmation_at,
        };
        transaction.commit().await?;
        Ok(result)
    }

    pub async fn node_trust_statuses(
        &self,
        token: &TokenDigest,
        release_id: Uuid,
        expected_distribution: Distribution,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<NodeUpdateTrustStatus>, StoreError> {
        if release_id.is_nil()
            || after.is_some_and(|cursor| cursor.is_nil())
            || !(1..=100).contains(&limit)
        {
            return Err(StoreError::InvalidInput);
        }
        let mut transaction = self.pool.begin().await?;
        control::read_gate(&mut transaction).await?;
        control::authorize(&mut transaction, token, false).await?;
        let release =
            Self::node_release(&mut transaction, release_id, expected_distribution).await?;
        let statuses = sqlx::query_file_as!(
            NodeUpdateTrustStatus,
            "queries/node_update_trust_statuses.sql",
            release.artifact.target.product.name(),
            release.repository_root_version,
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *transaction)
        .await?;
        transaction.commit().await?;
        Ok(statuses)
    }

    async fn node_release(
        transaction: &mut PgConnection,
        release_id: Uuid,
        expected_distribution: Distribution,
    ) -> Result<UpdateRelease, StoreError> {
        let release = sqlx::query_file_as!(UpdateRow, "queries/update_release.sql", release_id)
            .fetch_optional(&mut *transaction)
            .await?
            .ok_or(StoreError::Rejected)?
            .view()?;
        if release.artifact.target.distribution != expected_distribution
            || !matches!(
                release.artifact.target.product,
                Product::CloudNode | Product::Remote
            )
        {
            return Err(StoreError::Rejected);
        }
        Ok(release)
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
    pub async fn check_for_node(
        &self,
        node: &crate::NodeConnection,
        target: &ReleaseQuery,
        current_build_number: i64,
        trust_observation: Option<&UpdateTrustObservation>,
    ) -> Result<Option<UpdateRelease>, StoreError> {
        target.validate().map_err(|_| StoreError::InvalidInput)?;
        if current_build_number < 1 || target.product.name() != node.product().name() {
            return Err(StoreError::Rejected);
        }
        let mut tx = self.pool.begin().await?;
        if trust_observation.is_some() {
            control::write_gate(&mut tx).await?;
        } else {
            control::read_gate(&mut tx).await?;
        }
        crate::node_lifecycle::authorize(&mut tx, node).await?;
        if let Some(observation) = trust_observation {
            Self::record_trust_observation(&mut tx, node, target, observation).await?;
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
        .await?;
        let result = row
            .filter(|candidate| candidate.state == "approved")
            .map(UpdateRow::view)
            .transpose()?;
        tx.commit().await?;
        Ok(result)
    }

    async fn record_trust_observation(
        transaction: &mut PgConnection,
        node: &crate::NodeConnection,
        target: &ReleaseQuery,
        observation: &UpdateTrustObservation,
    ) -> Result<NodeUpdateTrust, StoreError> {
        if observation.release_id.is_nil()
            || !valid_sha256(&observation.repository_publication_sha256)
            || observation.root_version < 1
        {
            return Err(StoreError::InvalidInput);
        }
        let release =
            sqlx::query_file_as!(UpdateRow, "queries/lock_update.sql", observation.release_id)
                .fetch_optional(&mut *transaction)
                .await?
                .ok_or(StoreError::Rejected)?
                .view()?;
        if release.state == "pending"
            || release.artifact.target != *target
            || release.repository_publication_sha256 != observation.repository_publication_sha256
            || release.repository_root_version != observation.root_version
        {
            return Err(StoreError::Rejected);
        }
        sqlx::query_file_as!(
            NodeUpdateTrust,
            "queries/record_node_update_trust.sql",
            node.id,
            observation.release_id,
            node.generation,
            observation.repository_publication_sha256,
            observation.root_version
        )
        .fetch_optional(&mut *transaction)
        .await?
        .ok_or(StoreError::Rejected)
    }
    pub async fn begin_activation(
        &self,
        node: &crate::NodeConnection,
        target: &ReleaseQuery,
        release_id: Uuid,
        policy_revision: i64,
        prepared_sha256: &str,
    ) -> Result<NodeUpdateActivation, StoreError> {
        target.validate().map_err(|_| StoreError::InvalidInput)?;
        if release_id.is_nil()
            || policy_revision < 1
            || prepared_sha256.len() != 64
            || !prepared_sha256
                .bytes()
                .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
            || target.product.name() != node.product().name()
        {
            return Err(StoreError::InvalidInput);
        }
        let mut transaction = self.pool.begin().await?;
        control::write_gate(&mut transaction).await?;
        crate::node_lifecycle::authorize(&mut transaction, node).await?;
        sqlx::query_file!("queries/expire_node_update_activation.sql", node.id)
            .execute(&mut *transaction)
            .await?;
        let release = sqlx::query_file_as!(UpdateRow, "queries/lock_update.sql", release_id)
            .fetch_optional(&mut *transaction)
            .await?
            .ok_or(StoreError::Rejected)?;
        let release_state = release.state.clone();
        let release_revision = release.revision;
        let artifact = release.view()?.artifact;
        if release_state != "approved"
            || release_revision != policy_revision
            || artifact.target != *target
            || artifact.sha256 != prepared_sha256
        {
            return Err(StoreError::Rejected);
        }
        let status = sqlx::query_file!("queries/node_update_activation_status.sql", node.id)
            .fetch_one(&mut *transaction)
            .await?;
        if status.draining || status.busy || status.product_version_code >= artifact.build_number {
            return Err(StoreError::Rejected);
        }
        if let Some(existing) = sqlx::query_file_as!(
            NodeUpdateActivationRow,
            "queries/active_node_update_activation.sql",
            node.id
        )
        .fetch_optional(&mut *transaction)
        .await?
        {
            if existing.release_id != release_id
                || existing.from_build_number != status.product_version_code
                || existing.to_build_number != artifact.build_number
            {
                return Err(StoreError::Rejected);
            }
            let grant = existing.grant();
            transaction.commit().await?;
            return Ok(grant);
        }
        let task_id = Uuid::new_v4();
        let lease_id = Uuid::new_v4();
        let activation = sqlx::query_file!(
            "queries/create_node_update_activation.sql",
            task_id,
            node.id,
            release_id,
            status.product_version_code,
            artifact.build_number,
            lease_id
        )
        .fetch_one(&mut *transaction)
        .await?;
        let grant = NodeUpdateActivation {
            task_id: activation.id,
            lease_id: activation.lease_id,
            lease_until: activation.lease_until,
        };
        transaction.commit().await?;
        Ok(grant)
    }

    pub async fn finish_activation(
        &self,
        node: &crate::NodeConnection,
        task_id: Uuid,
        lease_id: Uuid,
        outcome: &UpdateActivationOutcome,
    ) -> Result<NodeUpdateCompletion, StoreError> {
        let (state, error_code) = outcome.fields();
        if task_id.is_nil()
            || lease_id.is_nil()
            || error_code.is_some_and(|value| {
                value.is_empty()
                    || value.len() > 64
                    || !value.bytes().all(|byte| {
                        byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'_'
                    })
            })
        {
            return Err(StoreError::InvalidInput);
        }
        let mut transaction = self.pool.begin().await?;
        control::write_gate(&mut transaction).await?;
        crate::node_lifecycle::authorize(&mut transaction, node).await?;
        let task = sqlx::query_file_as!(
            NodeUpdateTaskRow,
            "queries/lock_node_update_activation.sql",
            task_id,
            node.id
        )
        .fetch_optional(&mut *transaction)
        .await?
        .ok_or(StoreError::Rejected)?;
        if task.lease_id != lease_id {
            return Err(StoreError::Rejected);
        }
        if task.state != "activating" {
            if task.state == state && task.error_code.as_deref() == error_code {
                let completion = NodeUpdateCompletion {
                    state: task.state,
                    revision: task.revision,
                    error_code: task.error_code,
                };
                transaction.commit().await?;
                return Ok(completion);
            }
            return Err(StoreError::Rejected);
        }
        if matches!(outcome, UpdateActivationOutcome::Installed) {
            let installed = sqlx::query_file!("queries/node_installed_build.sql", node.id)
                .fetch_one(&mut *transaction)
                .await?;
            if installed.product_version_code != task.to_build_number {
                return Err(StoreError::Rejected);
            }
        }
        let completion = sqlx::query_file_as!(
            NodeUpdateCompletion,
            "queries/complete_node_update_activation.sql",
            task.id,
            task.lease_id,
            task.revision,
            state,
            error_code
        )
        .fetch_optional(&mut *transaction)
        .await?
        .ok_or(StoreError::Rejected)?;
        transaction.commit().await?;
        Ok(completion)
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
