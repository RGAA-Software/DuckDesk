use crate::{
    control, node_lifecycle, transfer_model::TransferRow, BeginFileTransfer, ClientType,
    FileTransferRecord, InstanceStore, NodeConnection, ResourceCredential, ResourceSessionStore,
    StoreError, TokenDigest, TransferProgress,
};
use sqlx::{PgConnection, PgPool};
use uuid::Uuid;

#[derive(Clone)]
pub struct FileTransferStore {
    pub(crate) pool: PgPool,
}
impl FileTransferStore {
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
    pub async fn begin(
        &self,
        node: &NodeConnection,
        request: &BeginFileTransfer,
    ) -> Result<FileTransferRecord, StoreError> {
        let (hash, total) = request.validate()?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let authority = node_lifecycle::authorize(&mut tx, node).await?;
        if let Some(row) = sqlx::query_file_as!(
            TransferRow,
            "queries/find_transfer_request.sql",
            authority.id,
            request.request_id
        )
        .fetch_optional(&mut *tx)
        .await?
        {
            if row.request_hash.as_slice() != hash
                || row.node_generation != authority.generation
                || row.control_epoch != authority.control_epoch
            {
                return Err(StoreError::Rejected);
            }
            let result = row.view();
            tx.commit().await?;
            return Ok(result);
        }
        let session = ResourceSessionStore::lock(&mut tx, request.session_id).await?;
        if session.node_id != authority.id
            || session.node_generation != authority.generation
            || session.control_epoch != authority.control_epoch
            || !sqlx::query_file_scalar!("queries/transfer_session_ready.sql", session.id)
                .fetch_one(&mut *tx)
                .await?
        {
            return Err(StoreError::Rejected);
        }
        ResourceSessionStore::live_endpoint(&mut tx, &session).await?;
        let row = sqlx::query_file_as!(
            TransferRow,
            "queries/create_file_transfer.sql",
            Uuid::new_v4(),
            authority.id,
            session.id,
            authority.generation,
            authority.control_epoch,
            request.request_id,
            hash.as_slice(),
            request.direction.name(),
            request.file_name,
            total,
            request
                .expected_sha256
                .as_ref()
                .map(|value| value.as_slice())
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::event(&mut tx, &row, "created").await?;
        let result = row.view();
        tx.commit().await?;
        Ok(result)
    }
    /// Reporting is bookkeeping, not permission to continue transferring bytes.
    /// Original producers may record a failure after access was revoked; another node
    /// or a new connection generation cannot adopt this transfer.
    pub async fn report(
        &self,
        node: &NodeConnection,
        id: Uuid,
        progress: &TransferProgress,
    ) -> Result<FileTransferRecord, StoreError> {
        let checked = progress.validate()?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let authority = node_lifecycle::authorize(&mut tx, node).await?;
        let previous = sqlx::query_file_as!(TransferRow, "queries/lock_file_transfer.sql", id)
            .fetch_optional(&mut *tx)
            .await?
            .ok_or(StoreError::Rejected)?;
        if previous.node_id != authority.id
            || previous.node_generation != authority.generation
            || previous.control_epoch != authority.control_epoch
        {
            return Err(StoreError::Rejected);
        }
        if checked.sequence == previous.sequence
            && previous.report_hash.as_deref() == Some(checked.hash.as_slice())
        {
            let result = previous.view();
            tx.commit().await?;
            return Ok(result);
        }
        if previous.state != "active"
            || checked.sequence <= previous.sequence
            || checked.bytes < previous.transferred_bytes
            || checked.bytes > previous.total_bytes
        {
            return Err(StoreError::Rejected);
        }
        // Only final failure/cancellation bookkeeping may outlive the original grant.
        // A report is not transport authority, but must not manufacture post-revocation success.
        if matches!(checked.state, "active" | "completed") {
            let session = ResourceSessionStore::lock(&mut tx, previous.session_id).await?;
            if !sqlx::query_file_scalar!("queries/transfer_session_ready.sql", session.id)
                .fetch_one(&mut *tx)
                .await?
            {
                return Err(StoreError::Rejected);
            }
            ResourceSessionStore::live_endpoint(&mut tx, &session).await?;
        }
        if checked.state == "completed" {
            if checked.bytes != previous.total_bytes || checked.received.is_none() {
                return Err(StoreError::Rejected);
            }
            if let Some(expected_sha256) = previous.expected_sha256.as_ref() {
                if checked
                    .received
                    .as_ref()
                    .map(|received_hash| received_hash.as_slice())
                    != Some(expected_sha256.as_slice())
                {
                    return Err(StoreError::Rejected);
                }
            }
        }
        let row = sqlx::query_file_as!(
            TransferRow,
            "queries/update_file_transfer.sql",
            id,
            checked.sequence,
            checked.bytes,
            checked.state,
            checked.reason,
            checked
                .received
                .as_ref()
                .map(|received_hash| received_hash.as_slice()),
            checked.hash.as_slice()
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::event(
            &mut tx,
            &row,
            if checked.state == "active" {
                "progress"
            } else {
                checked.state
            },
        )
        .await?;
        let result = row.view();
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_managed(
        &self,
        token: &TokenDigest,
        after: Option<Uuid>,
        node: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<FileTransferRecord>, StoreError> {
        valid_page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        let result = sqlx::query_file_as!(
            FileTransferRecord,
            "queries/managed_file_transfers.sql",
            after,
            node,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_owned(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<FileTransferRecord>, StoreError> {
        valid_page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let subject = InstanceStore::authorize(&mut tx, credential, client).await?;
        let (user, guest) = subject.owner.columns();
        let result = sqlx::query_file_as!(
            FileTransferRecord,
            "queries/owned_file_transfers.sql",
            user,
            guest,
            client.name(),
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(result)
    }
    async fn event(
        connection: &mut PgConnection,
        row: &TransferRow,
        kind: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/file_transfer_event.sql",
            Uuid::new_v4(),
            row.id,
            row.revision,
            row.sequence,
            row.transferred_bytes,
            kind
        )
        .execute(connection)
        .await?;
        Ok(())
    }
}
fn valid_page(limit: u32) -> Result<(), StoreError> {
    if !(1..=100).contains(&limit) {
        return Err(StoreError::InvalidInput);
    }
    Ok(())
}

/// Caller holds the control gate and commits the connection/frontend transition
/// in the same transaction. No inferred completion and no filesystem operations.
pub(crate) async fn invalidate(
    connection: &mut PgConnection,
    node: Option<Uuid>,
    session: Option<Uuid>,
) -> Result<(), StoreError> {
    sqlx::query_file!("queries/invalidate_file_transfers.sql", node, session)
        .execute(connection)
        .await?;
    Ok(())
}
