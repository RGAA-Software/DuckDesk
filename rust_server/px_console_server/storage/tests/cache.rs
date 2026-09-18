#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, node_report, token, Fixture};
use px_console_store::{
    CacheCredential, CacheOptions, CacheRuntime, ClientType, CommandOutcome, CommandReceipt,
    DeploymentTarget, NodeCommandAction, OpenResourceSession, RecordingCacheStore, RecordingCodec,
    RecordingReport, RecordingStore, ResourceCredential, ResourceSessionStore, SessionAccess,
    SessionTarget, StoreError,
};
use px_private_files::{CacheRoot, ContentIdentity};
use sha2::{Digest, Sha256};
use std::{env, sync::Arc};
use tokio::sync::Barrier;
use uuid::Uuid;
const DATA: &[u8] = b"synthetic cache bytes; not an MP4 decoder test";

impl CacheFixture {
    async fn ready(&self) -> (Uuid, px_console_store::CachedFile) {
        let id = self.record(DATA.len() as u64).await;
        self.request(id).await;
        let cache_attempt = self.attempt().await;
        let mut writer = self
            .run
            .root()
            .try_lock_blob(cache_attempt.id())
            .unwrap()
            .begin_write(cache_attempt.content())
            .unwrap();
        writer.append(DATA).unwrap();
        let proof = writer.finish().unwrap();
        self.store
            .publish(&self.run, &self.node, &cache_attempt, &proof)
            .await
            .unwrap();
        drop(proof);
        let file = self
            .store
            .cached_file(
                &self.run,
                CacheCredential::Managed(&self.base_fixture.admin),
                id,
            )
            .await
            .unwrap();
        (id, file)
    }
    async fn age(&self) {
        sqlx::query("UPDATE pixels.recording_cache SET last_access_at=clock_timestamp()-interval '2 minutes'").execute(&self.base_fixture.owner).await.unwrap();
    }
    async fn revision(&self) -> i64 {
        self.store
            .list_managed(&self.run, &self.base_fixture.admin, None, 100)
            .await
            .unwrap()[0]
            .revision
    }
}
#[tokio::test]
async fn cloud_application_session_owner_can_cache_and_read_without_device_access() {
    let cache_fixture = CacheFixture::new().await;
    let (node, application, _) = cache_fixture
        .base_fixture
        .prepared(DeploymentTarget::Webview, 4)
        .await;
    let session_run = cache_fixture
        .store
        .begin_runtime(cache_fixture.run.root().clone(), node.epoch(), options())
        .await
        .unwrap();
    let (session_owner, instance, command) = cache_fixture
        .base_fixture
        .started(&node, application.id)
        .await;
    let port = match command.action {
        NodeCommandAction::Start { port, .. } => port,
        _ => panic!("start required"),
    };
    cache_fixture
        .base_fixture
        .instances
        .acknowledge_command(
            &node,
            &CommandReceipt {
                command_id: command.id,
                lease_id: command.lease_id,
                instance_id: instance.id,
                launch_id: command.launch_id,
                instance_revision: command.instance_revision,
                outcome: CommandOutcome::Running { port },
            },
        )
        .await
        .unwrap();
    let deployment: Uuid = env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap();
    let sessions = ResourceSessionStore::connect(&config("RUNTIME"), deployment)
        .await
        .unwrap();
    let session = sessions
        .open(
            ResourceCredential::User(&session_owner),
            ClientType::Android,
            &OpenResourceSession {
                request_id: Uuid::new_v4(),
                target: SessionTarget::CloudApplication {
                    application_id: application.id,
                    instance_id: instance.id,
                },
                access: SessionAccess::Controller,
            },
        )
        .await
        .unwrap();
    let recording = cache_fixture
        .recordings
        .report(
            &node,
            &RecordingReport {
                source_id: Uuid::new_v4(),
                source_sha256: Sha256::digest(DATA).into(),
                session_id: Some(session.id),
                file_name: "会话录像.mp4".into(),
                size_bytes: DATA.len() as u64,
                modified_unix_ms: 1_800_000_000_000,
                codec: RecordingCodec::H264,
                sequence: 1,
                present: true,
            },
        )
        .await
        .unwrap();
    let unrelated_user = cache_fixture
        .base_fixture
        .session("user", ClientType::Android)
        .await;
    assert!(cache_fixture
        .store
        .request(
            &session_run,
            CacheCredential::User {
                token: &unrelated_user,
                client: ClientType::Android,
            },
            recording.id,
        )
        .await
        .is_err());
    let fetching = cache_fixture
        .store
        .request(
            &session_run,
            CacheCredential::User {
                token: &session_owner,
                client: ClientType::Android,
            },
            recording.id,
        )
        .await
        .unwrap();
    assert_eq!(fetching.state, "fetching");
    let cache_attempt = cache_fixture
        .store
        .pending(&session_run, &node, None, 100)
        .await
        .unwrap()
        .remove(0);
    let mut writer = cache_fixture
        .run
        .root()
        .try_lock_blob(cache_attempt.id())
        .unwrap()
        .begin_write(cache_attempt.content())
        .unwrap();
    writer.append(DATA).unwrap();
    let proof = writer.finish().unwrap();
    cache_fixture
        .store
        .publish(&session_run, &node, &cache_attempt, &proof)
        .await
        .unwrap();
    drop(proof);
    let file = cache_fixture
        .store
        .cached_file(
            &session_run,
            CacheCredential::User {
                token: &session_owner,
                client: ClientType::Android,
            },
            recording.id,
        )
        .await
        .unwrap();
    let reader = cache_fixture
        .run
        .root()
        .try_read(file.id, file.content)
        .unwrap();
    let lease = cache_fixture
        .store
        .open_read(
            &session_run,
            CacheCredential::User {
                token: &session_owner,
                client: ClientType::Android,
            },
            recording.id,
            &reader,
        )
        .await
        .unwrap();
    cache_fixture
        .store
        .renew_read(&session_run, &lease, &reader)
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .cached_file(
            &session_run,
            CacheCredential::User {
                token: &unrelated_user,
                client: ClientType::Android,
            },
            recording.id,
        )
        .await
        .is_err());
    drop(reader);
    sessions.close().await;
    drop(session_run);
    cache_fixture.close().await;
}
#[tokio::test]
async fn read_leases_are_bounded_login_bound_revocable_and_not_blob_bearer_tokens() {
    let cache_fixture = CacheFixture::new().await;
    let (id, file) = cache_fixture.ready().await;
    let reader = cache_fixture
        .run
        .root()
        .try_read(file.id, file.content)
        .unwrap();
    let viewer = cache_fixture
        .base_fixture
        .session("viewer", ClientType::AdminWeb)
        .await;
    let android = cache_fixture
        .base_fixture
        .session("user", ClientType::Android)
        .await;
    assert!(cache_fixture
        .store
        .open_read(
            &cache_fixture.run,
            CacheCredential::User {
                token: &android,
                client: ClientType::Android
            },
            id,
            &reader
        )
        .await
        .is_err());
    let mut leases = Vec::new();
    for _ in 0..32 {
        leases.push(
            cache_fixture
                .store
                .open_read(
                    &cache_fixture.run,
                    CacheCredential::Managed(&viewer),
                    id,
                    &reader,
                )
                .await
                .unwrap(),
        );
    }
    assert!(leases
        .iter()
        .all(|read_lease| read_lease.valid_for_ms() > 0 && read_lease.valid_for_ms() <= 30000));
    assert!(cache_fixture
        .store
        .open_read(
            &cache_fixture.run,
            CacheCredential::Managed(&viewer),
            id,
            &reader
        )
        .await
        .is_err());
    cache_fixture
        .store
        .close_read(&cache_fixture.run, &leases[0])
        .await
        .unwrap();
    cache_fixture
        .store
        .close_read(&cache_fixture.run, &leases[0])
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .renew_read(&cache_fixture.run, &leases[0], &reader)
        .await
        .is_err());
    let fresh = cache_fixture
        .store
        .open_read(
            &cache_fixture.run,
            CacheCredential::Managed(&viewer),
            id,
            &reader,
        )
        .await
        .unwrap();
    cache_fixture
        .store
        .renew_read(&cache_fixture.run, &fresh, &reader)
        .await
        .unwrap();
    let identity = cache_fixture
        .base_fixture
        .identity
        .authenticate(&viewer, ClientType::AdminWeb)
        .await
        .unwrap();
    cache_fixture
        .base_fixture
        .identity
        .revoke_session(identity.user_id, identity.session_id)
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .renew_read(&cache_fixture.run, &fresh, &reader)
        .await
        .is_err());
    assert!(cache_fixture
        .store
        .open_read(
            &cache_fixture.run,
            CacheCredential::Managed(&viewer),
            id,
            &reader
        )
        .await
        .is_err());
    drop(reader);
    cache_fixture.close().await;
}
#[tokio::test]
async fn read_lease_and_physical_lock_each_independently_prevent_collection() {
    let cache_fixture = CacheFixture::new().await;
    let (id, file) = cache_fixture.ready().await;
    let reader = cache_fixture
        .run
        .root()
        .try_read(file.id, file.content)
        .unwrap();
    let lease = cache_fixture
        .store
        .open_read(
            &cache_fixture.run,
            CacheCredential::Managed(&cache_fixture.base_fixture.admin),
            id,
            &reader,
        )
        .await
        .unwrap();
    cache_fixture.age().await;
    assert!(cache_fixture
        .store
        .collection_candidates(&cache_fixture.run, None, 100)
        .await
        .unwrap()
        .is_empty());
    drop(reader);
    let guard = cache_fixture.run.root().try_lock_blob(file.id).unwrap();
    assert!(cache_fixture
        .store
        .begin_collection(&cache_fixture.run, &guard)
        .await
        .is_err());
    drop(guard);
    let reader = cache_fixture
        .run
        .root()
        .try_read(file.id, file.content)
        .unwrap();
    sqlx::query(
        "UPDATE pixels.cache_read_leases SET expires_at=clock_timestamp()-interval '1 second'",
    )
    .execute(&cache_fixture.base_fixture.owner)
    .await
    .unwrap();
    assert!(cache_fixture
        .store
        .renew_read(&cache_fixture.run, &lease, &reader)
        .await
        .is_err());
    assert_eq!(
        cache_fixture
            .store
            .collection_candidates(&cache_fixture.run, None, 100)
            .await
            .unwrap(),
        vec![file.id]
    );
    assert!(matches!(
        cache_fixture.run.root().try_lock_blob(file.id),
        Err(px_private_files::FileError::Busy)
    ));
    drop(reader);
    let guard = cache_fixture.run.root().try_lock_blob(file.id).unwrap();
    cache_fixture
        .store
        .begin_collection(&cache_fixture.run, &guard)
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .cached_file(
            &cache_fixture.run,
            CacheCredential::Managed(&cache_fixture.base_fixture.admin),
            id
        )
        .await
        .is_err());
    let proof = guard.delete().unwrap();
    cache_fixture
        .store
        .finish_collection(&cache_fixture.run, &proof)
        .await
        .unwrap();
    cache_fixture
        .store
        .finish_collection(&cache_fixture.run, &proof)
        .await
        .unwrap();
    drop(proof);
    assert!(cache_fixture
        .run
        .root()
        .try_read(file.id, file.content)
        .is_err());
    cache_fixture.close().await;
}
#[tokio::test]
async fn retention_is_admin_cas_and_survives_ttl_offline_and_capacity_pressure() {
    let cache_fixture = CacheFixture::new().await;
    let (id, file) = cache_fixture.ready().await;
    let viewer = cache_fixture
        .base_fixture
        .session("viewer", ClientType::AdminWeb)
        .await;
    let revision = cache_fixture.revision().await;
    assert!(cache_fixture
        .store
        .retain(&cache_fixture.run, &viewer, id, revision, true)
        .await
        .is_err());
    let retained = cache_fixture
        .store
        .retain(
            &cache_fixture.run,
            &cache_fixture.base_fixture.admin,
            id,
            revision,
            true,
        )
        .await
        .unwrap();
    assert!(retained.pinned);
    assert!(cache_fixture
        .store
        .retain(
            &cache_fixture.run,
            &cache_fixture.base_fixture.admin,
            id,
            revision,
            false
        )
        .await
        .is_err());
    cache_fixture.age().await;
    assert!(cache_fixture
        .store
        .collection_candidates(&cache_fixture.run, None, 100)
        .await
        .unwrap()
        .is_empty());
    let guard = cache_fixture.run.root().try_lock_blob(file.id).unwrap();
    assert!(cache_fixture
        .store
        .evict(
            &cache_fixture.run,
            &cache_fixture.base_fixture.admin,
            retained.revision,
            &guard
        )
        .await
        .is_err());
    let large = cache_fixture.record(1_048_576).await;
    assert!(cache_fixture
        .store
        .request(
            &cache_fixture.run,
            CacheCredential::Managed(&cache_fixture.base_fixture.admin),
            large
        )
        .await
        .is_err());
    cache_fixture
        .base_fixture
        .nodes
        .close_connection(&cache_fixture.node)
        .await
        .unwrap();
    assert_eq!(
        cache_fixture
            .store
            .cached_file(&cache_fixture.run, CacheCredential::Managed(&viewer), id)
            .await
            .unwrap()
            .id,
        file.id
    );
    let released = cache_fixture
        .store
        .retain(
            &cache_fixture.run,
            &cache_fixture.base_fixture.admin,
            id,
            retained.revision,
            false,
        )
        .await
        .unwrap();
    assert!(!released.pinned);
    assert!(cache_fixture
        .store
        .evict(&cache_fixture.run, &viewer, released.revision, &guard)
        .await
        .is_err());
    cache_fixture
        .store
        .evict(
            &cache_fixture.run,
            &cache_fixture.base_fixture.admin,
            released.revision,
            &guard,
        )
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .retain(
            &cache_fixture.run,
            &cache_fixture.base_fixture.admin,
            id,
            released.revision,
            true
        )
        .await
        .is_err());
    let proof = guard.delete().unwrap();
    cache_fixture
        .store
        .finish_collection(&cache_fixture.run, &proof)
        .await
        .unwrap();
    drop(proof);
    cache_fixture.close().await;
}
#[tokio::test]
async fn collection_audit_failures_preserve_reference_then_reservation_until_actual_delete_ack() {
    let cache_fixture = CacheFixture::new().await;
    let (id, file) = cache_fixture.ready().await;
    cache_fixture.age().await;
    let guard = cache_fixture.run.root().try_lock_blob(file.id).unwrap();
    sqlx::query("REVOKE INSERT ON pixels.cache_events FROM pixels_console_runtime")
        .execute(&cache_fixture.base_fixture.owner)
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .begin_collection(&cache_fixture.run, &guard)
        .await
        .is_err());
    assert_eq!(
        cache_fixture
            .store
            .cached_file(
                &cache_fixture.run,
                CacheCredential::Managed(&cache_fixture.base_fixture.admin),
                id
            )
            .await
            .unwrap()
            .id,
        file.id
    );
    sqlx::query("GRANT INSERT ON pixels.cache_events TO pixels_console_runtime")
        .execute(&cache_fixture.base_fixture.owner)
        .await
        .unwrap();
    cache_fixture
        .store
        .begin_collection(&cache_fixture.run, &guard)
        .await
        .unwrap();
    let proof = guard.delete().unwrap();
    sqlx::query("REVOKE INSERT ON pixels.cache_events FROM pixels_console_runtime")
        .execute(&cache_fixture.base_fixture.owner)
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .finish_collection(&cache_fixture.run, &proof)
        .await
        .is_err());
    let state: String = sqlx::query_scalar("SELECT state FROM pixels.cache_blobs WHERE id=$1")
        .bind(file.id)
        .fetch_one(&cache_fixture.base_fixture.owner)
        .await
        .unwrap();
    assert_eq!(state, "deleting");
    sqlx::query("GRANT INSERT ON pixels.cache_events TO pixels_console_runtime")
        .execute(&cache_fixture.base_fixture.owner)
        .await
        .unwrap();
    cache_fixture
        .store
        .finish_collection(&cache_fixture.run, &proof)
        .await
        .unwrap();
    drop(proof);
    // A response lost after commit is safe to retry with cache_attempt fresh exact-object deletion proof.
    let guard = cache_fixture.run.root().try_lock_blob(file.id).unwrap();
    cache_fixture
        .store
        .begin_collection(&cache_fixture.run, &guard)
        .await
        .unwrap();
    let proof = guard.delete().unwrap();
    cache_fixture
        .store
        .finish_collection(&cache_fixture.run, &proof)
        .await
        .unwrap();
    drop(proof);
    let large = cache_fixture.record(1_048_576).await;
    cache_fixture.request(large).await;
    cache_fixture.close().await;
}
#[tokio::test]
async fn never_started_attempt_is_tombstoned_by_collection_and_stale_worker_cannot_recreate_it() {
    let cache_fixture = CacheFixture::new().await;
    let id = cache_fixture.record(DATA.len() as u64).await;
    cache_fixture.request(id).await;
    let cache_attempt = cache_fixture.attempt().await;
    let guard = cache_fixture
        .run
        .root()
        .try_lock_blob(cache_attempt.id())
        .unwrap();
    assert!(cache_fixture
        .store
        .begin_collection(&cache_fixture.run, &guard)
        .await
        .is_err());
    assert!(cache_fixture
        .store
        .evict(
            &cache_fixture.run,
            &cache_fixture.base_fixture.admin,
            cache_fixture.revision().await,
            &guard
        )
        .await
        .is_err());
    cache_fixture
        .store
        .abandon(&cache_fixture.run, &cache_fixture.node, &cache_attempt)
        .await
        .unwrap();
    cache_fixture
        .store
        .begin_collection(&cache_fixture.run, &guard)
        .await
        .unwrap();
    let proof = guard.delete().unwrap();
    cache_fixture
        .store
        .finish_collection(&cache_fixture.run, &proof)
        .await
        .unwrap();
    drop(proof);
    assert!(cache_fixture
        .run
        .root()
        .try_lock_blob(cache_attempt.id())
        .unwrap()
        .begin_write(cache_attempt.content())
        .is_err());
    cache_fixture.request(id).await;
    assert_ne!(cache_fixture.attempt().await.id(), cache_attempt.id());
    // Unknown objects are not GC candidates, even with cache_attempt legitimate root's physical lock.
    let unknown = cache_fixture
        .run
        .root()
        .try_lock_blob(Uuid::new_v4())
        .unwrap();
    assert!(cache_fixture
        .store
        .begin_collection(&cache_fixture.run, &unknown)
        .await
        .is_err());
    drop(unknown);
    cache_fixture.close().await;
}
#[tokio::test]
async fn restart_fences_media_leases_until_current_file_revalidation_and_new_authorization() {
    let cache_fixture = CacheFixture::new().await;
    let (id, file) = cache_fixture.ready().await;
    let reader = cache_fixture
        .run
        .root()
        .try_read(file.id, file.content)
        .unwrap();
    let lease = cache_fixture
        .store
        .open_read(
            &cache_fixture.run,
            CacheCredential::Managed(&cache_fixture.base_fixture.admin),
            id,
            &reader,
        )
        .await
        .unwrap();
    let run = cache_fixture
        .store
        .begin_runtime(
            cache_fixture.run.root().clone(),
            cache_fixture.node.epoch(),
            options(),
        )
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .renew_read(&cache_fixture.run, &lease, &reader)
        .await
        .is_err());
    assert!(cache_fixture
        .store
        .renew_read(&run, &lease, &reader)
        .await
        .is_err());
    assert!(cache_fixture
        .store
        .open_read(
            &run,
            CacheCredential::Managed(&cache_fixture.base_fixture.admin),
            id,
            &reader
        )
        .await
        .is_err());
    cache_fixture
        .store
        .verify_cached(&run, id, &reader)
        .await
        .unwrap();
    cache_fixture
        .store
        .open_read(
            &run,
            CacheCredential::Managed(&cache_fixture.base_fixture.admin),
            id,
            &reader,
        )
        .await
        .unwrap();
    drop(reader);
    drop(run);
    cache_fixture.close().await;
}
#[tokio::test]
async fn android_media_requires_current_device_acl_not_application_or_other_client_identity() {
    let cache_fixture = CacheFixture::new().await;
    let (id, file) = cache_fixture.ready().await;
    let android = cache_fixture
        .base_fixture
        .session("user", ClientType::Android)
        .await;
    let identity = cache_fixture
        .base_fixture
        .identity
        .authenticate(&android, ClientType::Android)
        .await
        .unwrap();
    let device: Uuid = sqlx::query_scalar("SELECT device_id FROM pixels.nodes WHERE id=$1")
        .bind(cache_fixture.node.id())
        .fetch_one(&cache_fixture.base_fixture.owner)
        .await
        .unwrap();
    let access = cache_fixture
        .base_fixture
        .devices
        .replace_access(
            &cache_fixture.base_fixture.admin,
            device,
            1,
            &px_console_store::DeviceAccess {
                users: vec![identity.user_id],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    // Device ACL replacement revokes the previous authorization revision; issue cache_attempt fresh login.
    let revision: i64 =
        sqlx::query_scalar("SELECT authorization_revision FROM pixels.users WHERE id=$1")
            .bind(identity.user_id)
            .fetch_one(&cache_fixture.base_fixture.owner)
            .await
            .unwrap();
    let login = token();
    cache_fixture
        .base_fixture
        .identity
        .issue_session(
            identity.user_id,
            revision,
            &login,
            ClientType::Android,
            std::time::Duration::from_secs(3600),
        )
        .await
        .unwrap();
    let reader = cache_fixture
        .run
        .root()
        .try_read(file.id, file.content)
        .unwrap();
    let lease = cache_fixture
        .store
        .open_read(
            &cache_fixture.run,
            CacheCredential::User {
                token: &login,
                client: ClientType::Android,
            },
            id,
            &reader,
        )
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .open_read(
            &cache_fixture.run,
            CacheCredential::User {
                token: &login,
                client: ClientType::Panel
            },
            id,
            &reader
        )
        .await
        .is_err());
    cache_fixture
        .base_fixture
        .devices
        .replace_access(
            &cache_fixture.base_fixture.admin,
            device,
            access.revision,
            &px_console_store::DeviceAccess {
                users: vec![],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .renew_read(&cache_fixture.run, &lease, &reader)
        .await
        .is_err());
    drop(reader);
    cache_fixture.close().await;
}

fn options() -> CacheOptions {
    CacheOptions {
        byte_limit: 1_048_576,
        maximum_downloads: 2,
        ttl_seconds: 60,
    }
}

#[tokio::test]
async fn retain_and_eviction_race_has_one_cas_winner_and_no_dangling_ready_reference() {
    let cache_fixture = CacheFixture::new().await;
    for _ in 0..20 {
        let (id, file) = cache_fixture.ready().await;
        let revision: i64 =
            sqlx::query_scalar("SELECT revision FROM pixels.recording_cache WHERE recording_id=$1")
                .bind(id)
                .fetch_one(&cache_fixture.base_fixture.owner)
                .await
                .unwrap();
        let guard = cache_fixture.run.root().try_lock_blob(file.id).unwrap();
        let (pin, evict) = tokio::join!(
            cache_fixture.store.retain(
                &cache_fixture.run,
                &cache_fixture.base_fixture.admin,
                id,
                revision,
                true
            ),
            cache_fixture.store.evict(
                &cache_fixture.run,
                &cache_fixture.base_fixture.admin,
                revision,
                &guard
            )
        );
        assert_ne!(pin.is_ok(), evict.is_ok());
        if let Ok(pinned) = pin {
            assert_eq!(
                cache_fixture
                    .store
                    .cached_file(
                        &cache_fixture.run,
                        CacheCredential::Managed(&cache_fixture.base_fixture.admin),
                        id
                    )
                    .await
                    .unwrap()
                    .id,
                file.id
            );
            let unpinned = cache_fixture
                .store
                .retain(
                    &cache_fixture.run,
                    &cache_fixture.base_fixture.admin,
                    id,
                    pinned.revision,
                    false,
                )
                .await
                .unwrap();
            cache_fixture
                .store
                .evict(
                    &cache_fixture.run,
                    &cache_fixture.base_fixture.admin,
                    unpinned.revision,
                    &guard,
                )
                .await
                .unwrap();
        } else {
            assert!(cache_fixture
                .store
                .cached_file(
                    &cache_fixture.run,
                    CacheCredential::Managed(&cache_fixture.base_fixture.admin),
                    id
                )
                .await
                .is_err());
        }
        let proof = guard.delete().unwrap();
        cache_fixture
            .store
            .finish_collection(&cache_fixture.run, &proof)
            .await
            .unwrap();
        drop(proof);
    }
    cache_fixture.close().await;
}
fn private(path: &std::path::Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o700)).unwrap();
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        let identity = std::process::Command::new("whoami")
            .creation_flags(0x08000000)
            .output()
            .unwrap();
        assert!(identity.status.success());
        let grant = format!(
            "{}:(OI)(CI)F",
            String::from_utf8(identity.stdout).unwrap().trim()
        );
        assert!(std::process::Command::new("icacls")
            .arg(path)
            .args(["/inheritance:r", "/grant:r", &grant, "*S-1-5-18:(OI)(CI)F"])
            .creation_flags(0x08000000)
            .output()
            .unwrap()
            .status
            .success());
    }
}
struct CacheFixture {
    base_fixture: Fixture,
    store: RecordingCacheStore,
    recordings: RecordingStore,
    run: CacheRuntime,
    node: px_console_store::NodeConnection,
    key: px_console_store::TokenDigest,
    directory: tempfile::TempDir,
}
impl CacheFixture {
    async fn new() -> Self {
        let base_fixture = Fixture::new().await;
        // Test-only owner cleanup within the harness's disposable database. No runtime DELETE grant.
        sqlx::query("TRUNCATE pixels.cache_roots,pixels.cache_runs,pixels.cache_runtime,pixels.recording_cache,pixels.cache_blobs,pixels.cache_events CASCADE")
   .execute(&base_fixture.owner).await.unwrap();
        let deployment = env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap();
        let store = RecordingCacheStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let recordings = RecordingStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let (node, key) = base_fixture.connected().await;
        let directory = tempfile::Builder::new()
            .prefix("pixels-pg-cache-")
            .tempdir()
            .unwrap();
        private(directory.path());
        let root = CacheRoot::initialize(directory.path(), deployment).unwrap();
        let run = store
            .begin_runtime(root, node.epoch(), options())
            .await
            .unwrap();
        Self {
            base_fixture,
            store,
            recordings,
            run,
            node,
            key,
            directory,
        }
    }
    async fn record(&self, size: u64) -> Uuid {
        self.recordings
            .report(
                &self.node,
                &RecordingReport {
                    source_id: Uuid::new_v4(),
                    source_sha256: Sha256::digest(DATA).into(),
                    session_id: None,
                    file_name: "录像.mp4".into(),
                    size_bytes: size,
                    modified_unix_ms: 1_800_000_000_000,
                    codec: RecordingCodec::H264,
                    sequence: 1,
                    present: true,
                },
            )
            .await
            .unwrap()
            .id
    }
    async fn request(&self, id: Uuid) -> px_console_store::CacheProfile {
        self.store
            .request(
                &self.run,
                CacheCredential::Managed(&self.base_fixture.admin),
                id,
            )
            .await
            .unwrap()
    }
    async fn attempt(&self) -> px_console_store::CacheAttempt {
        self.store
            .pending(&self.run, &self.node, None, 100)
            .await
            .unwrap()
            .remove(0)
    }
    async fn close(self) {
        self.store.close().await;
        self.recordings.close().await;
        self.base_fixture.close().await;
        drop(self.run);
        self.directory.close().unwrap();
    }
}
#[tokio::test]
async fn publication_requires_real_hash_proof_and_retries_are_idempotent() {
    let cache_fixture = CacheFixture::new().await;
    let id = cache_fixture.record(DATA.len() as u64).await;
    assert_eq!(cache_fixture.request(id).await.state, "fetching");
    let cache_attempt = cache_fixture.attempt().await;
    assert!(cache_attempt.valid_for_ms() > 0 && cache_attempt.valid_for_ms() <= 30000);
    let mut writer = cache_fixture
        .run
        .root()
        .try_lock_blob(cache_attempt.id())
        .unwrap()
        .begin_write(cache_attempt.content())
        .unwrap();
    writer.append(&DATA[..8]).unwrap();
    let renewed = cache_fixture
        .store
        .renew(
            &cache_fixture.run,
            &cache_fixture.node,
            &cache_attempt,
            &writer,
        )
        .await
        .unwrap();
    assert_eq!(renewed.id(), cache_attempt.id());
    let view = cache_fixture
        .store
        .list_managed(
            &cache_fixture.run,
            &cache_fixture.base_fixture.admin,
            None,
            100,
        )
        .await
        .unwrap();
    assert_eq!(view[0].received_bytes, 8);
    assert_eq!(view[0].state, "fetching");
    writer.append(&DATA[8..]).unwrap();
    let proof = writer.finish().unwrap();
    let ready = cache_fixture
        .store
        .publish(&cache_fixture.run, &cache_fixture.node, &renewed, &proof)
        .await
        .unwrap();
    assert_eq!(ready.state, "ready");
    assert_eq!(
        ready,
        cache_fixture
            .store
            .publish(&cache_fixture.run, &cache_fixture.node, &renewed, &proof)
            .await
            .unwrap()
    );
    assert!(cache_fixture
        .store
        .abandon(&cache_fixture.run, &cache_fixture.node, &cache_attempt)
        .await
        .is_err());
    drop(proof);
    let reader = cache_fixture
        .run
        .root()
        .try_read(cache_attempt.id(), cache_attempt.content())
        .unwrap();
    assert_eq!(
        ready,
        cache_fixture
            .store
            .verify_cached(&cache_fixture.run, id, &reader)
            .await
            .unwrap()
    );
    drop(reader);
    assert!(cache_fixture
        .store
        .pending(&cache_fixture.run, &cache_fixture.node, None, 100)
        .await
        .unwrap()
        .is_empty());
    cache_fixture.close().await;
}
#[tokio::test]
async fn concurrent_requests_share_one_attempt_and_reserved_capacity_is_bounded() {
    let cache_fixture = CacheFixture::new().await;
    let id = cache_fixture.record(700_000).await;
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (cache_store, cache_runtime, admin_token, start_barrier) = (
            cache_fixture.store.clone(),
            cache_fixture.run.clone(),
            cache_fixture.base_fixture.admin.clone(),
            barrier.clone(),
        );
        tasks.push(tokio::spawn(async move {
            start_barrier.wait().await;
            cache_store
                .request(&cache_runtime, CacheCredential::Managed(&admin_token), id)
                .await
                .unwrap()
        }));
    }
    let mut revisions = Vec::new();
    for task_handle in tasks {
        revisions.push(task_handle.await.unwrap().revision);
    }
    assert!(revisions.iter().all(|revision| *revision == 2));
    let count: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.cache_blobs")
        .fetch_one(&cache_fixture.base_fixture.owner)
        .await
        .unwrap();
    assert_eq!(count, 1);
    let other = cache_fixture.record(700_000).await;
    assert!(cache_fixture
        .store
        .request(
            &cache_fixture.run,
            CacheCredential::Managed(&cache_fixture.base_fixture.admin),
            other
        )
        .await
        .is_err());
    let cache_attempt = cache_fixture.attempt().await;
    cache_fixture
        .store
        .abandon(&cache_fixture.run, &cache_fixture.node, &cache_attempt)
        .await
        .unwrap();
    cache_fixture
        .store
        .abandon(&cache_fixture.run, &cache_fixture.node, &cache_attempt)
        .await
        .unwrap();
    // Retired work may have partial files: it still consumes its full reservation.
    assert!(cache_fixture
        .store
        .request(
            &cache_fixture.run,
            CacheCredential::Managed(&cache_fixture.base_fixture.admin),
            id
        )
        .await
        .is_err());
    cache_fixture.close().await;
}
#[tokio::test]
async fn timeout_rejects_late_publisher_and_new_attempt_never_reuses_old_blob() {
    let cache_fixture = CacheFixture::new().await;
    let id = cache_fixture.record(DATA.len() as u64).await;
    cache_fixture.request(id).await;
    let cache_attempt = cache_fixture.attempt().await;
    let mut writer = cache_fixture
        .run
        .root()
        .try_lock_blob(cache_attempt.id())
        .unwrap()
        .begin_write(cache_attempt.content())
        .unwrap();
    writer.append(DATA).unwrap();
    let proof = writer.finish().unwrap();
    sqlx::query("UPDATE pixels.cache_blobs SET lease_until=clock_timestamp()-interval '1 second' WHERE id=$1")
  .bind(cache_attempt.id()).execute(&cache_fixture.base_fixture.owner).await.unwrap();
    assert!(cache_fixture
        .store
        .publish(
            &cache_fixture.run,
            &cache_fixture.node,
            &cache_attempt,
            &proof
        )
        .await
        .is_err());
    assert_eq!(
        cache_fixture
            .store
            .expire(&cache_fixture.run, 100)
            .await
            .unwrap(),
        1
    );
    assert_eq!(
        cache_fixture
            .store
            .expire(&cache_fixture.run, 100)
            .await
            .unwrap(),
        0
    );
    cache_fixture.request(id).await;
    let next = cache_fixture.attempt().await;
    assert_ne!(cache_attempt.id(), next.id());
    assert!(cache_fixture
        .store
        .publish(&cache_fixture.run, &cache_fixture.node, &next, &proof)
        .await
        .is_err());
    cache_fixture
        .store
        .abandon(&cache_fixture.run, &cache_fixture.node, &cache_attempt)
        .await
        .unwrap();
    assert_eq!(cache_fixture.attempt().await.id(), next.id());
    drop(proof);
    cache_fixture.close().await;
}
#[tokio::test]
async fn restart_requires_reverification_and_rejects_old_runtime_or_new_root_adoption() {
    let cache_fixture = CacheFixture::new().await;
    let id = cache_fixture.record(DATA.len() as u64).await;
    cache_fixture.request(id).await;
    let cache_attempt = cache_fixture.attempt().await;
    let mut writer = cache_fixture
        .run
        .root()
        .try_lock_blob(cache_attempt.id())
        .unwrap()
        .begin_write(cache_attempt.content())
        .unwrap();
    writer.append(DATA).unwrap();
    let proof = writer.finish().unwrap();
    cache_fixture
        .store
        .publish(
            &cache_fixture.run,
            &cache_fixture.node,
            &cache_attempt,
            &proof,
        )
        .await
        .unwrap();
    drop(proof);
    let run = cache_fixture
        .store
        .begin_runtime(
            cache_fixture.run.root().clone(),
            cache_fixture.node.epoch(),
            options(),
        )
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .list_managed(
            &cache_fixture.run,
            &cache_fixture.base_fixture.admin,
            None,
            100
        )
        .await
        .is_err());
    assert_eq!(
        cache_fixture
            .store
            .list_managed(&run, &cache_fixture.base_fixture.admin, None, 100)
            .await
            .unwrap()[0]
            .state,
        "verifying"
    );
    cache_fixture
        .base_fixture
        .nodes
        .close_connection(&cache_fixture.node)
        .await
        .unwrap();
    let reader = run
        .root()
        .try_read(cache_attempt.id(), cache_attempt.content())
        .unwrap();
    assert_eq!(
        cache_fixture
            .store
            .verify_cached(&run, id, &reader)
            .await
            .unwrap()
            .state,
        "ready"
    );
    drop(reader);
    let other = tempfile::tempdir().unwrap();
    private(other.path());
    let root = CacheRoot::initialize(other.path(), run.root().deployment()).unwrap();
    assert!(matches!(
        cache_fixture
            .store
            .begin_runtime(root, cache_fixture.node.epoch(), options())
            .await,
        Err(StoreError::CacheRecoveryRequired)
    ));
    other.close().unwrap();
    drop(run);
    cache_fixture.close().await;
}
#[tokio::test]
async fn original_login_revocation_cannot_be_repaired_by_another_requester() {
    let cache_fixture = CacheFixture::new().await;
    let id = cache_fixture.record(DATA.len() as u64).await;
    let viewer = cache_fixture
        .base_fixture
        .session("viewer", ClientType::AdminWeb)
        .await;
    cache_fixture
        .store
        .request(&cache_fixture.run, CacheCredential::Managed(&viewer), id)
        .await
        .unwrap();
    let cache_attempt = cache_fixture.attempt().await;
    let mut writer = cache_fixture
        .run
        .root()
        .try_lock_blob(cache_attempt.id())
        .unwrap()
        .begin_write(cache_attempt.content())
        .unwrap();
    writer.append(DATA).unwrap();
    let proof = writer.finish().unwrap();
    let identity = cache_fixture
        .base_fixture
        .identity
        .authenticate(&viewer, ClientType::AdminWeb)
        .await
        .unwrap();
    cache_fixture
        .base_fixture
        .identity
        .revoke_session(identity.user_id, identity.session_id)
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .publish(
            &cache_fixture.run,
            &cache_fixture.node,
            &cache_attempt,
            &proof
        )
        .await
        .is_err());
    assert!(cache_fixture
        .store
        .pending(&cache_fixture.run, &cache_fixture.node, None, 100)
        .await
        .unwrap()
        .is_empty());
    cache_fixture.request(id).await;
    let next = cache_fixture.attempt().await;
    assert_ne!(cache_attempt.id(), next.id());
    assert!(cache_fixture
        .store
        .publish(&cache_fixture.run, &cache_fixture.node, &next, &proof)
        .await
        .is_err());
    drop(proof);
    cache_fixture.close().await;
}
#[tokio::test]
async fn audit_failure_rolls_back_request_and_publication_without_claiming_file_atomicity() {
    let cache_fixture = CacheFixture::new().await;
    let id = cache_fixture.record(DATA.len() as u64).await;
    sqlx::query("REVOKE INSERT ON pixels.cache_events FROM pixels_console_runtime")
        .execute(&cache_fixture.base_fixture.owner)
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .request(
            &cache_fixture.run,
            CacheCredential::Managed(&cache_fixture.base_fixture.admin),
            id
        )
        .await
        .is_err());
    let count: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.cache_blobs")
        .fetch_one(&cache_fixture.base_fixture.owner)
        .await
        .unwrap();
    assert_eq!(count, 0);
    sqlx::query("GRANT INSERT ON pixels.cache_events TO pixels_console_runtime")
        .execute(&cache_fixture.base_fixture.owner)
        .await
        .unwrap();
    cache_fixture.request(id).await;
    let cache_attempt = cache_fixture.attempt().await;
    let mut writer = cache_fixture
        .run
        .root()
        .try_lock_blob(cache_attempt.id())
        .unwrap()
        .begin_write(cache_attempt.content())
        .unwrap();
    writer.append(DATA).unwrap();
    let proof = writer.finish().unwrap();
    sqlx::query("REVOKE INSERT ON pixels.cache_events FROM pixels_console_runtime")
        .execute(&cache_fixture.base_fixture.owner)
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .publish(
            &cache_fixture.run,
            &cache_fixture.node,
            &cache_attempt,
            &proof
        )
        .await
        .is_err());
    assert_eq!(
        cache_fixture
            .store
            .list_managed(
                &cache_fixture.run,
                &cache_fixture.base_fixture.admin,
                None,
                100
            )
            .await
            .unwrap()[0]
            .state,
        "fetching"
    );
    sqlx::query("GRANT INSERT ON pixels.cache_events TO pixels_console_runtime")
        .execute(&cache_fixture.base_fixture.owner)
        .await
        .unwrap();
    assert_eq!(
        cache_fixture
            .store
            .publish(
                &cache_fixture.run,
                &cache_fixture.node,
                &cache_attempt,
                &proof
            )
            .await
            .unwrap()
            .state,
        "ready"
    );
    let runtime = config("RUNTIME").connect().await.unwrap();
    assert!(sqlx::query("DELETE FROM pixels.cache_blobs")
        .execute(&runtime)
        .await
        .is_err());
    assert!(
        sqlx::query("UPDATE pixels.cache_blobs SET lease_id=gen_random_uuid()")
            .execute(&runtime)
            .await
            .is_err()
    );
    runtime.close().await;
    drop(proof);
    cache_fixture.close().await;
}
#[tokio::test]
async fn client_acl_node_generation_and_epoch_are_independent_admission_gates() {
    let cache_fixture = CacheFixture::new().await;
    let id = cache_fixture.record(DATA.len() as u64).await;
    let android = cache_fixture
        .base_fixture
        .session("user", ClientType::Android)
        .await;
    assert!(cache_fixture
        .store
        .request(
            &cache_fixture.run,
            CacheCredential::User {
                token: &android,
                client: ClientType::Android
            },
            id
        )
        .await
        .is_err());
    assert!(cache_fixture
        .store
        .request(&cache_fixture.run, CacheCredential::Managed(&android), id)
        .await
        .is_err());
    assert!(cache_fixture
        .store
        .request(
            &cache_fixture.run,
            CacheCredential::User {
                token: &cache_fixture.base_fixture.admin,
                client: ClientType::AdminWeb
            },
            id
        )
        .await
        .is_err());
    cache_fixture.request(id).await;
    let cache_attempt = cache_fixture.attempt().await;
    let mut writer = cache_fixture
        .run
        .root()
        .try_lock_blob(cache_attempt.id())
        .unwrap()
        .begin_write(cache_attempt.content())
        .unwrap();
    writer.append(DATA).unwrap();
    let proof = writer.finish().unwrap();
    let current = cache_fixture
        .base_fixture
        .nodes
        .open_connection(cache_fixture.node.epoch(), &cache_fixture.key, &token())
        .await
        .unwrap();
    cache_fixture
        .base_fixture
        .nodes
        .report(&current, &node_report(1))
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .publish(
            &cache_fixture.run,
            &cache_fixture.node,
            &cache_attempt,
            &proof
        )
        .await
        .is_err());
    assert!(cache_fixture
        .store
        .publish(&cache_fixture.run, &current, &cache_attempt, &proof)
        .await
        .is_err());
    assert!(cache_fixture
        .store
        .request(
            &cache_fixture.run,
            CacheCredential::Managed(&cache_fixture.base_fixture.admin),
            id
        )
        .await
        .is_err());
    let epoch = cache_fixture
        .base_fixture
        .nodes
        .begin_runtime()
        .await
        .unwrap();
    assert!(cache_fixture
        .store
        .expire(&cache_fixture.run, 100)
        .await
        .is_err());
    let run = cache_fixture
        .store
        .begin_runtime(cache_fixture.run.root().clone(), epoch, options())
        .await
        .unwrap();
    assert_eq!(cache_fixture.store.expire(&run, 100).await.unwrap(), 1);
    drop(proof);
    drop(run);
    cache_fixture.close().await;
}
#[tokio::test]
async fn download_concurrency_bounds_and_wrong_file_proofs_are_rejected() {
    let cache_fixture = CacheFixture::new().await;
    let first = cache_fixture.record(DATA.len() as u64).await;
    cache_fixture.request(first).await;
    let second = cache_fixture.record(DATA.len() as u64).await;
    cache_fixture.request(second).await;
    let third = cache_fixture.record(DATA.len() as u64).await;
    assert!(cache_fixture
        .store
        .request(
            &cache_fixture.run,
            CacheCredential::Managed(&cache_fixture.base_fixture.admin),
            third
        )
        .await
        .is_err());
    let cache_attempt = cache_fixture.attempt().await;
    let other = Uuid::new_v4();
    let mut writer = cache_fixture
        .run
        .root()
        .try_lock_blob(other)
        .unwrap()
        .begin_write(ContentIdentity::new(DATA.len() as u64, Sha256::digest(DATA).into()).unwrap())
        .unwrap();
    writer.append(DATA).unwrap();
    assert!(cache_fixture
        .store
        .renew(
            &cache_fixture.run,
            &cache_fixture.node,
            &cache_attempt,
            &writer
        )
        .await
        .is_err());
    let proof = writer.finish().unwrap();
    assert!(cache_fixture
        .store
        .publish(
            &cache_fixture.run,
            &cache_fixture.node,
            &cache_attempt,
            &proof
        )
        .await
        .is_err());
    drop(proof);
    let reader = cache_fixture
        .run
        .root()
        .try_read(other, cache_attempt.content())
        .unwrap();
    assert!(cache_fixture
        .store
        .verify_cached(&cache_fixture.run, cache_attempt.recording_id(), &reader)
        .await
        .is_err());
    drop(reader);
    cache_fixture
        .store
        .abandon(&cache_fixture.run, &cache_fixture.node, &cache_attempt)
        .await
        .unwrap();
    cache_fixture.request(third).await;
    assert!(cache_fixture
        .store
        .pending(&cache_fixture.run, &cache_fixture.node, None, 0)
        .await
        .is_err());
    assert!(cache_fixture
        .store
        .list_managed(
            &cache_fixture.run,
            &cache_fixture.base_fixture.admin,
            None,
            101
        )
        .await
        .is_err());
    cache_fixture.close().await;
}
