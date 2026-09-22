//! Device directory and authorization. No endpoint guessing or online-state cache.
use crate::resource_policy::resource_user;
use crate::{control, ClientType, StoreError, TokenDigest};
use chrono::{DateTime, Utc};
use sqlx::{PgConnection, PgPool};
use std::collections::BTreeSet;
use uuid::Uuid;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DevicePlatform {
    Windows,
    Linux,
    Macos,
    Android,
}
impl DevicePlatform {
    fn name(self) -> &'static str {
        match self {
            Self::Windows => "windows",
            Self::Linux => "linux",
            Self::Macos => "macos",
            Self::Android => "android",
        }
    }
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct DeviceProfile {
    pub id: Uuid,
    pub public_code: String,
    pub name: String,
    pub platform: String,
    pub disabled: bool,
    pub revision: i64,
    pub registered_at: DateTime<Utc>,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DeviceAccess {
    pub users: Vec<Uuid>,
    pub groups: Vec<Uuid>,
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DeviceIdentity {
    pub id: Uuid,
    pub revision: i64,
}
#[derive(Clone)]
pub struct DeviceStore {
    pub(crate) pool: PgPool,
}

fn valid_name(name: &str) -> Result<(), StoreError> {
    if !(1..=128).contains(&name.chars().count())
        || name.trim() != name
        || name.chars().any(char::is_control)
    {
        return Err(StoreError::InvalidInput);
    }
    Ok(())
}
fn valid_page(limit: u32) -> Result<(), StoreError> {
    if !(1..=100).contains(&limit) {
        return Err(StoreError::InvalidInput);
    }
    Ok(())
}

impl DeviceStore {
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
    pub async fn create(
        &self,
        token: &TokenDigest,
        name: &str,
        platform: DevicePlatform,
        enrollment: &TokenDigest,
    ) -> Result<DeviceProfile, StoreError> {
        valid_name(name)?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, token, true).await?;
        // Public lookup number, never a credential; unrelated to hardware/owner identity.
        // Unique constraints reject a collision without creating a partially registered device.
        let code = format!("{:012}", Uuid::new_v4().as_u128() % 1_000_000_000_000);
        let device = sqlx::query_file_as!(
            DeviceProfile,
            "queries/create_device.sql",
            Uuid::new_v4(),
            code,
            name,
            platform.name(),
            enrollment.0.as_slice()
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::audit(&mut tx, actor, device.id, device.revision, "created").await?;
        tx.commit().await?;
        Ok(device)
    }
    pub async fn list_managed(
        &self,
        token: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<DeviceProfile>, StoreError> {
        valid_page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        let result = sqlx::query_file_as!(
            DeviceProfile,
            "queries/managed_devices.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_visible(
        &self,
        token: &TokenDigest,
        client: ClientType,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<DeviceProfile>, StoreError> {
        valid_page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let user = resource_user(&mut tx, token, client).await?;
        let result = Self::visible(&mut tx, user, after, limit, None).await?;
        tx.commit().await?;
        Ok(result)
    }
    /// Read current access, not a reusable authorization grant for a later start/descriptor.
    pub async fn get_visible(
        &self,
        token: &TokenDigest,
        client: ClientType,
        id: Uuid,
    ) -> Result<DeviceProfile, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let user = resource_user(&mut tx, token, client).await?;
        let result = Self::visible(&mut tx, user, None, 1, Some(id))
            .await?
            .pop()
            .ok_or(StoreError::Rejected)?;
        tx.commit().await?;
        Ok(result)
    }
    pub(crate) async fn visible(
        connection: &mut PgConnection,
        user: Uuid,
        after: Option<Uuid>,
        limit: u32,
        id: Option<Uuid>,
    ) -> Result<Vec<DeviceProfile>, StoreError> {
        Ok(sqlx::query_file_as!(
            DeviceProfile,
            "queries/visible_devices.sql",
            user,
            after,
            i64::from(limit),
            id
        )
        .fetch_all(connection)
        .await?)
    }
    pub async fn access(&self, token: &TokenDigest, id: Uuid) -> Result<DeviceAccess, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        // Lock after the global gate; access to unknown/deleted resources is not an empty success.
        Self::lock(&mut tx, id, None).await?;
        let result = Self::relations(&mut tx, id).await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn replace_access(
        &self,
        token: &TokenDigest,
        id: Uuid,
        revision: i64,
        access: &DeviceAccess,
    ) -> Result<DeviceProfile, StoreError> {
        let users: BTreeSet<_> = access.users.iter().copied().collect();
        let groups: BTreeSet<_> = access.groups.iter().copied().collect();
        if revision < 1
            || users.len() != access.users.len()
            || groups.len() != access.groups.len()
            || users.len() > 1000
            || groups.len() > 1000
        {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, token, true).await?;
        let device = Self::lock(&mut tx, id, Some(revision)).await?;
        let previous = Self::relations(&mut tx, id).await?;
        if users.iter().copied().collect::<Vec<_>>() == previous.users
            && groups.iter().copied().collect::<Vec<_>>() == previous.groups
        {
            tx.commit().await?;
            return Ok(device);
        }
        let valid_groups =
            sqlx::query_file_scalar!("queries/validate_device_groups.sql", &access.groups)
                .fetch_all(&mut *tx)
                .await?;
        let valid_users = sqlx::query_file!("queries/lock_member_users.sql", &access.users)
            .fetch_all(&mut *tx)
            .await?;
        if valid_groups.len() != groups.len()
            || valid_users.len() != users.len()
            || valid_users.iter().any(|user| !user.active)
        {
            return Err(StoreError::Rejected);
        }
        let previous = Self::effective_users(&mut tx, id).await?;
        sqlx::query_file!("queries/clear_device_users.sql", id)
            .execute(&mut *tx)
            .await?;
        sqlx::query_file!("queries/clear_device_grants.sql", id)
            .execute(&mut *tx)
            .await?;
        sqlx::query_file!("queries/insert_device_users.sql", id, &access.users)
            .execute(&mut *tx)
            .await?;
        sqlx::query_file!("queries/insert_device_grants.sql", id, &access.groups)
            .execute(&mut *tx)
            .await?;
        let next = Self::effective_users(&mut tx, id).await?;
        let affected: Vec<_> = previous.symmetric_difference(&next).copied().collect();
        Self::invalidate(&mut tx, &affected).await?;
        let result = sqlx::query_file_as!(
            DeviceProfile,
            "queries/update_device.sql",
            id,
            device.name,
            device.disabled
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::audit(&mut tx, actor, id, result.revision, "access_changed").await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn update(
        &self,
        token: &TokenDigest,
        id: Uuid,
        revision: i64,
        name: &str,
        disabled: bool,
    ) -> Result<DeviceProfile, StoreError> {
        valid_name(name)?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, token, true).await?;
        let previous = Self::lock(&mut tx, id, Some(revision)).await?;
        if previous.name == name && previous.disabled == disabled {
            tx.commit().await?;
            return Ok(previous);
        }
        if previous.disabled != disabled {
            if let Some(node) = sqlx::query_file_scalar!("queries/invalidate_device_node.sql", id)
                .fetch_optional(&mut *tx)
                .await?
            {
                crate::node_lifecycle::invalidate(&mut tx, Some(node)).await?;
            }
            let affected: Vec<_> = Self::effective_users(&mut tx, id)
                .await?
                .into_iter()
                .collect();
            Self::invalidate(&mut tx, &affected).await?;
        }
        let result = sqlx::query_file_as!(
            DeviceProfile,
            "queries/update_device.sql",
            id,
            name,
            disabled
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::audit(&mut tx, actor, id, result.revision, "updated").await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn rotate_key(
        &self,
        token: &TokenDigest,
        id: Uuid,
        revision: i64,
        key: &TokenDigest,
    ) -> Result<DeviceProfile, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, token, true).await?;
        Self::lock(&mut tx, id, Some(revision)).await?;
        let affected: Vec<_> = Self::effective_users(&mut tx, id)
            .await?
            .into_iter()
            .collect();
        Self::invalidate(&mut tx, &affected).await?;
        let result = sqlx::query_file_as!(
            DeviceProfile,
            "queries/rotate_device_key.sql",
            id,
            key.0.as_slice()
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::audit(&mut tx, actor, id, result.revision, "key_rotated").await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn delete(
        &self,
        token: &TokenDigest,
        id: Uuid,
        revision: i64,
    ) -> Result<(), StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, token, true).await?;
        Self::lock(&mut tx, id, Some(revision)).await?;
        let affected: Vec<_> = Self::effective_users(&mut tx, id)
            .await?
            .into_iter()
            .collect();
        Self::invalidate(&mut tx, &affected).await?;
        let next = sqlx::query_file_scalar!("queries/delete_device.sql", id)
            .fetch_one(&mut *tx)
            .await?;
        if let Some(node) = sqlx::query_file_scalar!("queries/invalidate_device_node.sql", id)
            .fetch_optional(&mut *tx)
            .await?
        {
            crate::node_lifecycle::invalidate(&mut tx, Some(node)).await?;
        }
        Self::audit(&mut tx, actor, id, next, "deleted").await?;
        tx.commit().await?;
        Ok(())
    }
    /// Device registration authentication only. Does not authenticate a node or assert liveness.
    pub async fn authenticate_enrollment(
        &self,
        key: &TokenDigest,
    ) -> Result<DeviceIdentity, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let result = sqlx::query_file_as!(
            DeviceIdentity,
            "queries/device_enrollment.sql",
            key.0.as_slice()
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        tx.commit().await?;
        Ok(result)
    }
    async fn relations(
        connection: &mut PgConnection,
        id: Uuid,
    ) -> Result<DeviceAccess, StoreError> {
        let users = sqlx::query_file_scalar!("queries/device_users.sql", id)
            .fetch_all(&mut *connection)
            .await?;
        let groups = sqlx::query_file_scalar!("queries/device_grants.sql", id)
            .fetch_all(connection)
            .await?;
        Ok(DeviceAccess { users, groups })
    }
    async fn effective_users(
        connection: &mut PgConnection,
        id: Uuid,
    ) -> Result<BTreeSet<Uuid>, StoreError> {
        Ok(
            sqlx::query_file_scalar!("queries/device_effective_users.sql", id)
                .fetch_all(connection)
                .await?
                .into_iter()
                .collect(),
        )
    }
    async fn lock(
        connection: &mut PgConnection,
        id: Uuid,
        revision: Option<i64>,
    ) -> Result<DeviceProfile, StoreError> {
        let device = sqlx::query_file_as!(DeviceProfile, "queries/lock_device.sql", id)
            .fetch_optional(connection)
            .await?
            .ok_or(StoreError::Rejected)?;
        if revision.is_some_and(|revision| revision < 1 || revision != device.revision) {
            return Err(StoreError::Rejected);
        }
        Ok(device)
    }
    async fn invalidate(connection: &mut PgConnection, users: &[Uuid]) -> Result<(), StoreError> {
        sqlx::query_file!("queries/lock_member_users.sql", users)
            .fetch_all(&mut *connection)
            .await?;
        sqlx::query_file!("queries/bump_permission_users.sql", users)
            .execute(connection)
            .await?;
        Ok(())
    }
    async fn audit(
        connection: &mut PgConnection,
        actor: Uuid,
        id: Uuid,
        revision: i64,
        action: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/device_audit.sql",
            Uuid::new_v4(),
            actor,
            id,
            revision,
            action
        )
        .execute(connection)
        .await?;
        Ok(())
    }
}
