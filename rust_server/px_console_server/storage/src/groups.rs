use crate::{control, StoreError, TokenDigest};
#[cfg(feature = "pg-integration")]
use px_pg::DatabaseConfig;
use sqlx::{PgConnection, PgPool};
use std::collections::BTreeSet;
use uuid::Uuid;

#[derive(Clone)]
pub struct GroupStore {
    pub(crate) pool: PgPool,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct GroupProfile {
    pub id: Uuid,
    pub name: String,
    pub remark: String,
    pub revision: i64,
}

impl GroupStore {
    #[cfg(feature = "pg-integration")]
    pub async fn connect(config: &DatabaseConfig, deployment: Uuid) -> Result<Self, StoreError> {
        Ok(Self {
            pool: crate::connect(config, deployment).await?,
        })
    }

    #[cfg(feature = "pg-integration")]
    pub async fn close(&self) {
        self.pool.close().await;
    }

    /// Privileged application service only; administrative authorization is not inferred from a supplied UUID.
    pub async fn create(
        &self,
        token: &TokenDigest,
        name: &str,
        remark: &str,
    ) -> Result<GroupProfile, StoreError> {
        if !(1..=128).contains(&name.chars().count())
            || name.trim() != name
            || name.chars().any(char::is_control)
            || remark.chars().count() > 1024
        {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, token, true).await?;
        let group = sqlx::query_file_as!(
            GroupProfile,
            "queries/create_group.sql",
            Uuid::new_v4(),
            name,
            name.to_lowercase(),
            remark
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::event(&mut tx, actor, group.id, "created", group.revision, 0).await?;
        tx.commit().await?;
        Ok(group)
    }

    pub async fn get(&self, token: &TokenDigest, id: Uuid) -> Result<GroupProfile, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        let group = sqlx::query_file_as!(GroupProfile, "queries/get_group.sql", id)
            .fetch_optional(&mut *tx)
            .await?
            .ok_or(StoreError::Rejected)?;
        tx.commit().await?;
        Ok(group)
    }
    pub async fn list(
        &self,
        token: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<GroupProfile>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        let rows = sqlx::query_file_as!(
            GroupProfile,
            "queries/list_groups.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(rows)
    }
    pub async fn members(&self, token: &TokenDigest, id: Uuid) -> Result<Vec<Uuid>, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        let members = sqlx::query_file_scalar!("queries/group_members.sql", id)
            .fetch_all(&mut *tx)
            .await?;
        tx.commit().await?;
        Ok(members)
    }

    /// Atomic replacement. Lock order: exclusive gate, actor/session, group, affected users in UUID order.
    pub async fn replace_members(
        &self,
        token: &TokenDigest,
        id: Uuid,
        revision: i64,
        members: &[Uuid],
    ) -> Result<GroupProfile, StoreError> {
        let next: BTreeSet<_> = members.iter().copied().collect();
        if revision <= 0 || members.len() > 1000 || next.len() != members.len() {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, token, true).await?;
        let group = Self::lock(&mut tx, id, revision).await?;
        let previous: BTreeSet<_> = sqlx::query_file_scalar!("queries/group_members.sql", id)
            .fetch_all(&mut *tx)
            .await?
            .into_iter()
            .collect();
        if next == previous {
            tx.commit().await?;
            return Ok(group);
        }
        let affected: Vec<_> = previous.union(&next).copied().collect();
        let users = sqlx::query_file!("queries/lock_member_users.sql", &affected)
            .fetch_all(&mut *tx)
            .await?;
        if users.len() != affected.len()
            || users
                .iter()
                .any(|user| next.contains(&user.id) && !user.active)
        {
            return Err(StoreError::Rejected);
        }
        let changed: Vec<_> = previous.symmetric_difference(&next).copied().collect();
        sqlx::query_file!("queries/delete_group_members.sql", id)
            .execute(&mut *tx)
            .await?;
        sqlx::query_file!("queries/insert_group_members.sql", id, members)
            .execute(&mut *tx)
            .await?;
        sqlx::query_file!(
            "queries/bump_member_users.sql",
            &changed,
            actor,
            "group_membership",
            id
        )
        .execute(&mut *tx)
        .await?;
        let result = sqlx::query_file_as!(GroupProfile, "queries/update_group_revision.sql", id)
            .fetch_one(&mut *tx)
            .await?;
        Self::event(
            &mut tx,
            actor,
            id,
            "members_changed",
            result.revision,
            members.len() as i32,
        )
        .await?;
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
        Self::lock(&mut tx, id, revision).await?;
        let previous = sqlx::query_file_scalar!("queries/group_members.sql", id)
            .fetch_all(&mut *tx)
            .await?;
        sqlx::query_file!("queries/lock_member_users.sql", &previous)
            .fetch_all(&mut *tx)
            .await?;
        sqlx::query_file!("queries/delete_group_members.sql", id)
            .execute(&mut *tx)
            .await?;
        sqlx::query_file!(
            "queries/bump_member_users.sql",
            &previous,
            actor,
            "group_deleted",
            id
        )
        .execute(&mut *tx)
        .await?;
        sqlx::query_file!("queries/delete_group.sql", id)
            .execute(&mut *tx)
            .await?;
        Self::event(&mut tx, actor, id, "deleted", revision + 1, 0).await?;
        tx.commit().await?;
        Ok(())
    }

    async fn event(
        connection: &mut PgConnection,
        actor: Uuid,
        group: Uuid,
        action: &str,
        revision: i64,
        members: i32,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/group_event.sql",
            Uuid::new_v4(),
            group,
            actor,
            action,
            revision,
            members
        )
        .execute(connection)
        .await?;
        Ok(())
    }
    async fn lock(
        connection: &mut PgConnection,
        id: Uuid,
        revision: i64,
    ) -> Result<GroupProfile, StoreError> {
        let group = sqlx::query_file_as!(GroupProfile, "queries/lock_group.sql", id)
            .fetch_optional(connection)
            .await?
            .ok_or(StoreError::Rejected)?;
        if group.revision != revision {
            return Err(StoreError::Rejected);
        }
        Ok(group)
    }
}
