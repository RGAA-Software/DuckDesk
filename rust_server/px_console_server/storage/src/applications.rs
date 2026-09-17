use crate::{
    control, resource_policy, ApplicationAccess, ApplicationLaunch, ApplicationSpec, ClientType,
    StoreError, TokenDigest, VideoCodec, VideoSpec,
};
use sqlx::{PgConnection, PgPool};
use std::collections::BTreeSet;
use uuid::Uuid;

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct ApplicationDefinition {
    pub id: Uuid,
    pub revision: i64,
    pub access_revision: i64,
    pub spec: ApplicationSpec,
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct ApplicationCard {
    pub id: Uuid,
    pub name: String,
    pub kind: String,
    pub access_mode: String,
    pub revision: i64,
    pub access_revision: i64,
}
#[derive(sqlx::FromRow)]
struct ApplicationRow {
    id: Uuid,
    name: String,
    kind: String,
    access_mode: String,
    entry_url: Option<String>,
    executable_relative: Option<String>,
    arguments: Option<String>,
    bitrate_kbps: Option<i32>,
    codec: Option<String>,
    allow_observer: bool,
    allow_takeover: bool,
    disabled: bool,
    revision: i64,
    access_revision: i64,
}
impl ApplicationRow {
    fn decode(self) -> Result<ApplicationDefinition, StoreError> {
        let invalid = StoreError::Database(px_pg::DatabaseError::Operation);
        let video = match self.kind.as_str() {
            "game_hook" | "webview" => Some(VideoSpec {
                codec: match self.codec.as_deref() {
                    Some("h264") => VideoCodec::H264,
                    Some("h265") => VideoCodec::H265,
                    _ => return Err(invalid),
                },
                bitrate_kbps: self
                    .bitrate_kbps
                    .ok_or(invalid)?
                    .try_into()
                    .map_err(|_| invalid)?,
            }),
            "rdp" => None,
            _ => return Err(invalid),
        };
        let launch = match self.kind.as_str() {
            "game_hook" => ApplicationLaunch::GameHook {
                executable_relative: self.executable_relative.ok_or(invalid)?,
                arguments: self.arguments.ok_or(invalid)?,
                video: video.ok_or(invalid)?,
            },
            "webview" => ApplicationLaunch::Webview {
                entry_url: self.entry_url.ok_or(invalid)?,
                video: video.ok_or(invalid)?,
            },
            "rdp" => ApplicationLaunch::Rdp,
            _ => return Err(invalid),
        };
        let spec = ApplicationSpec {
            name: self.name,
            launch,
            access: match self.access_mode.as_str() {
                "public" => ApplicationAccess::Public,
                "acl" => ApplicationAccess::Acl,
                _ => return Err(invalid),
            },
            allow_observer: self.allow_observer,
            allow_takeover: self.allow_takeover,
            disabled: self.disabled,
        };
        spec.validate().map_err(|_| invalid)?;
        Ok(ApplicationDefinition {
            id: self.id,
            revision: self.revision,
            access_revision: self.access_revision,
            spec,
        })
    }
}
#[derive(Clone)]
pub struct ApplicationStore {
    pub(crate) pool: PgPool,
}
impl ApplicationStore {
    pub async fn list_visible_guest(
        &self,
        token: &TokenDigest,
        client: ClientType,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<ApplicationCard>, StoreError> {
        Self::page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        crate::GuestStore::authorize(&mut tx, token, client).await?;
        let result = Self::visible(&mut tx, None, after, limit, None).await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn get_visible_guest(
        &self,
        token: &TokenDigest,
        client: ClientType,
        id: Uuid,
    ) -> Result<ApplicationCard, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        crate::GuestStore::authorize(&mut tx, token, client).await?;
        let result = Self::visible(&mut tx, None, None, 1, Some(id))
            .await?
            .pop()
            .ok_or(StoreError::Rejected)?;
        tx.commit().await?;
        Ok(result)
    }
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
        spec: &ApplicationSpec,
    ) -> Result<ApplicationDefinition, StoreError> {
        spec.validate()?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, token, true).await?;
        let bitrate = spec
            .launch
            .video()
            .map(|video| i32::try_from(video.bitrate_kbps))
            .transpose()
            .map_err(|_| StoreError::InvalidInput)?;
        let result = sqlx::query_file_as!(
            ApplicationRow,
            "queries/create_application.sql",
            Uuid::new_v4(),
            spec.name,
            spec.launch.kind(),
            spec.access.name(),
            spec.launch.entry_url(),
            spec.launch.executable(),
            spec.launch.arguments(),
            bitrate,
            spec.launch.video().map(|video| video.codec.name()),
            spec.allow_observer,
            spec.allow_takeover,
            spec.disabled
        )
        .fetch_one(&mut *tx)
        .await?
        .decode()?;
        Self::event(&mut tx, actor, &result, "created").await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_managed(
        &self,
        token: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<ApplicationDefinition>, StoreError> {
        Self::page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        let result = sqlx::query_file_as!(
            ApplicationRow,
            "queries/managed_applications.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?
        .into_iter()
        .map(ApplicationRow::decode)
        .collect::<Result<Vec<_>, _>>()?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_visible(
        &self,
        token: &TokenDigest,
        client: ClientType,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<ApplicationCard>, StoreError> {
        Self::page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let user = resource_policy::resource_user(&mut tx, token, client).await?;
        let result = Self::visible(&mut tx, Some(user), after, limit, None).await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn get_visible(
        &self,
        token: &TokenDigest,
        client: ClientType,
        id: Uuid,
    ) -> Result<ApplicationCard, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let user = resource_policy::resource_user(&mut tx, token, client).await?;
        let result = Self::visible(&mut tx, Some(user), None, 1, Some(id))
            .await?
            .pop()
            .ok_or(StoreError::Rejected)?;
        tx.commit().await?;
        Ok(result)
    }
    // Caller must first authenticate the actual User/Guest and retain the shared gate through
    // its complete admission transaction. None represents an authenticated guest, not anonymous HTTP.
    pub(crate) async fn visible(
        connection: &mut PgConnection,
        user: Option<Uuid>,
        after: Option<Uuid>,
        limit: u32,
        id: Option<Uuid>,
    ) -> Result<Vec<ApplicationCard>, StoreError> {
        Ok(sqlx::query_file_as!(
            ApplicationCard,
            "queries/visible_applications.sql",
            user,
            after,
            i64::from(limit),
            id
        )
        .fetch_all(connection)
        .await?)
    }
    pub async fn update(
        &self,
        token: &TokenDigest,
        id: Uuid,
        revision: i64,
        spec: &ApplicationSpec,
    ) -> Result<ApplicationDefinition, StoreError> {
        spec.validate()?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, token, true).await?;
        let previous = Self::lock(&mut tx, id, Some(revision)).await?;
        // Kind is an immutable identity boundary; a new kind uses a new application UUID.
        // In particular, an RDP workspace must never be reinterpreted as a game process.
        if previous.spec.launch.kind() != spec.launch.kind() {
            return Err(StoreError::Rejected);
        }
        if &previous.spec == spec {
            tx.commit().await?;
            return Ok(previous);
        }
        let access_changed = previous.spec.access != spec.access
            || previous.spec.disabled != spec.disabled
            || previous.spec.allow_observer != spec.allow_observer
            || previous.spec.allow_takeover != spec.allow_takeover;
        let bitrate = spec
            .launch
            .video()
            .map(|video| i32::try_from(video.bitrate_kbps))
            .transpose()
            .map_err(|_| StoreError::InvalidInput)?;
        let result = sqlx::query_file_as!(
            ApplicationRow,
            "queries/update_application.sql",
            id,
            spec.name,
            spec.access.name(),
            spec.launch.entry_url(),
            spec.launch.executable(),
            spec.launch.arguments(),
            bitrate,
            spec.launch.video().map(|video| video.codec.name()),
            spec.allow_observer,
            spec.allow_takeover,
            spec.disabled,
            i64::from(access_changed)
        )
        .fetch_one(&mut *tx)
        .await?
        .decode()?;
        Self::event(&mut tx, actor, &result, "updated").await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn groups(&self, token: &TokenDigest, id: Uuid) -> Result<Vec<Uuid>, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        Self::lock(&mut tx, id, None).await?;
        let result = sqlx::query_file_scalar!("queries/application_groups.sql", id)
            .fetch_all(&mut *tx)
            .await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn replace_groups(
        &self,
        token: &TokenDigest,
        id: Uuid,
        revision: i64,
        groups: &[Uuid],
    ) -> Result<ApplicationDefinition, StoreError> {
        let next: BTreeSet<_> = groups.iter().copied().collect();
        if groups.len() > 1000 || next.len() != groups.len() {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, token, true).await?;
        let previous = Self::lock(&mut tx, id, Some(revision)).await?;
        let current = sqlx::query_file_scalar!("queries/application_groups.sql", id)
            .fetch_all(&mut *tx)
            .await?;
        if current == next.into_iter().collect::<Vec<_>>() {
            tx.commit().await?;
            return Ok(previous);
        }
        let found = sqlx::query_file_scalar!("queries/validate_device_groups.sql", groups)
            .fetch_all(&mut *tx)
            .await?;
        if found.len() != groups.len() {
            return Err(StoreError::Rejected);
        }
        let previous_users = Self::group_users(&mut tx, id).await?;
        sqlx::query_file!("queries/clear_application_groups.sql", id)
            .execute(&mut *tx)
            .await?;
        sqlx::query_file!("queries/insert_application_groups.sql", id, groups)
            .execute(&mut *tx)
            .await?;
        let next_users = Self::group_users(&mut tx, id).await?;
        if previous.spec.access == ApplicationAccess::Acl {
            let affected: Vec<_> = previous_users
                .symmetric_difference(&next_users)
                .copied()
                .collect();
            sqlx::query_file!("queries/lock_member_users.sql", &affected)
                .fetch_all(&mut *tx)
                .await?;
            sqlx::query_file!("queries/bump_permission_users.sql", &affected)
                .execute(&mut *tx)
                .await?;
        }
        let result =
            sqlx::query_file_as!(ApplicationRow, "queries/bump_application_access.sql", id)
                .fetch_one(&mut *tx)
                .await?
                .decode()?;
        Self::event(&mut tx, actor, &result, "grants_changed").await?;
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
        let mut previous = Self::lock(&mut tx, id, Some(revision)).await?;
        let result = sqlx::query_file!("queries/delete_application.sql", id)
            .fetch_one(&mut *tx)
            .await?;
        previous.revision = result.revision;
        previous.access_revision = result.access_revision;
        Self::event(&mut tx, actor, &previous, "deleted").await?;
        tx.commit().await?;
        Ok(())
    }
    async fn group_users(
        connection: &mut PgConnection,
        id: Uuid,
    ) -> Result<BTreeSet<Uuid>, StoreError> {
        Ok(
            sqlx::query_file_scalar!("queries/application_group_users.sql", id)
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
    ) -> Result<ApplicationDefinition, StoreError> {
        let result = sqlx::query_file_as!(ApplicationRow, "queries/lock_application.sql", id)
            .fetch_optional(connection)
            .await?
            .ok_or(StoreError::Rejected)?
            .decode()?;
        if revision.is_some_and(|revision| revision < 1 || revision != result.revision) {
            return Err(StoreError::Rejected);
        }
        Ok(result)
    }
    async fn event(
        connection: &mut PgConnection,
        actor: Uuid,
        app: &ApplicationDefinition,
        kind: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/application_event.sql",
            Uuid::new_v4(),
            app.id,
            actor,
            app.revision,
            app.access_revision,
            kind
        )
        .execute(connection)
        .await?;
        Ok(())
    }
    fn page(limit: u32) -> Result<(), StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        Ok(())
    }
}
