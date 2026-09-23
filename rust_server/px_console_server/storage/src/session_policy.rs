use crate::{
    instance_state,
    instances::AdmissionSubject,
    session_model::{SessionEndpoint, SessionRow},
    ClientType, DeviceStore, InstanceStore, ResourceSessionStore, SessionTarget, StoreError,
};
use sqlx::PgConnection;
use uuid::Uuid;

impl ResourceSessionStore {
    pub(crate) async fn close_instance_sessions(
        connection: &mut PgConnection,
        instance_id: Uuid,
    ) -> Result<(), StoreError> {
        let sessions = sqlx::query_file_as!(
            SessionRow,
            "queries/active_instance_resource_sessions.sql",
            instance_id
        )
        .fetch_all(&mut *connection)
        .await?;
        for session in sessions {
            crate::file_transfers::invalidate(
                &mut *connection,
                Some(session.node_id),
                Some(session.id),
            )
            .await?;
            crate::activity::invalidate(&mut *connection, Some(session.node_id), Some(session.id))
                .await?;
            Self::change(connection, &session, "closed").await?;
        }
        Ok(())
    }

    pub(crate) async fn lock(
        connection: &mut PgConnection,
        id: Uuid,
    ) -> Result<SessionRow, StoreError> {
        sqlx::query_file_as!(SessionRow, "queries/lock_resource_session.sql", id)
            .fetch_optional(connection)
            .await?
            .ok_or(StoreError::Rejected)
    }
    pub(crate) async fn event(
        connection: &mut PgConnection,
        row: &SessionRow,
        kind: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/resource_session_event.sql",
            Uuid::new_v4(),
            row.id,
            row.revision,
            kind
        )
        .execute(connection)
        .await?;
        Ok(())
    }
    pub(crate) async fn change(
        connection: &mut PgConnection,
        row: &SessionRow,
        state: &str,
    ) -> Result<SessionRow, StoreError> {
        let result = sqlx::query_file_as!(
            SessionRow,
            "queries/move_resource_session.sql",
            row.id,
            state
        )
        .fetch_optional(&mut *connection)
        .await?
        .ok_or(StoreError::Rejected)?;
        Self::event(connection, &result, state).await?;
        Ok(result)
    }
    pub(crate) async fn endpoint(
        connection: &mut PgConnection,
        target: SessionTarget,
        subject: &AdmissionSubject,
        client: ClientType,
        access: &str,
    ) -> Result<SessionEndpoint, StoreError> {
        match target {
            SessionTarget::Desktop { device_id } => {
                let user = subject.owner.columns().0.ok_or(StoreError::Rejected)?;
                // Desktop observer/takeover policy is not inferred from an application policy.
                if access != "controller"
                    || DeviceStore::visible(connection, user, None, 1, Some(device_id))
                        .await?
                        .is_empty()
                {
                    return Err(StoreError::Rejected);
                }
                sqlx::query_file_as!(
                    SessionEndpoint,
                    "queries/resource_desktop_endpoint.sql",
                    device_id
                )
                .fetch_optional(connection)
                .await?
                .ok_or(StoreError::Rejected)
            }
            SessionTarget::CloudApplication {
                application_id,
                instance_id,
            } => {
                InstanceStore::visible(connection, subject.owner, application_id).await?;
                if !instance_state::owner_authorized(connection, instance_id).await? {
                    return Err(StoreError::Rejected);
                }
                let (user, guest) = subject.owner.columns();
                sqlx::query_file_as!(
                    SessionEndpoint,
                    "queries/resource_application_endpoint.sql",
                    instance_id,
                    application_id,
                    user,
                    guest,
                    subject.login_session,
                    subject.revision,
                    client.name(),
                    access
                )
                .fetch_optional(connection)
                .await?
                .ok_or(StoreError::Rejected)
            }
        }
    }
}
