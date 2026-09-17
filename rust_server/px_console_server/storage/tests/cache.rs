#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, node_report, token, Fixture};
use px_console_store::{
    CacheCredential, CacheOptions, CacheRuntime, ClientType, RecordingCacheStore, RecordingCodec,
    RecordingReport, RecordingStore, StoreError,
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
        let a = self.attempt().await;
        let mut writer = self
            .run
            .root()
            .try_lock_blob(a.id())
            .unwrap()
            .begin_write(a.content())
            .unwrap();
        writer.append(DATA).unwrap();
        let proof = writer.finish().unwrap();
        self.store
            .publish(&self.run, &self.node, &a, &proof)
            .await
            .unwrap();
        drop(proof);
        let file = self
            .store
            .cached_file(&self.run, CacheCredential::Managed(&self.f.admin), id)
            .await
            .unwrap();
        (id, file)
    }
    async fn age(&self) {
        sqlx::query("UPDATE pixels.recording_cache SET last_access_at=clock_timestamp()-interval '2 minutes'").execute(&self.f.owner).await.unwrap();
    }
    async fn revision(&self) -> i64 {
        self.store
            .list_managed(&self.run, &self.f.admin, None, 100)
            .await
            .unwrap()[0]
            .revision
    }
}
#[tokio::test]
async fn read_leases_are_bounded_login_bound_revocable_and_not_blob_bearer_tokens() {
    let c = CacheFixture::new().await;
    let (id, file) = c.ready().await;
    let reader = c.run.root().try_read(file.id, file.content).unwrap();
    let viewer = c.f.session("viewer", ClientType::AdminWeb).await;
    let android = c.f.session("user", ClientType::Android).await;
    assert!(c
        .store
        .open_read(
            &c.run,
            CacheCredential::DeviceUser {
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
            c.store
                .open_read(&c.run, CacheCredential::Managed(&viewer), id, &reader)
                .await
                .unwrap(),
        );
    }
    assert!(leases
        .iter()
        .all(|l| l.valid_for_ms() > 0 && l.valid_for_ms() <= 30000));
    assert!(c
        .store
        .open_read(&c.run, CacheCredential::Managed(&viewer), id, &reader)
        .await
        .is_err());
    c.store.close_read(&c.run, &leases[0]).await.unwrap();
    c.store.close_read(&c.run, &leases[0]).await.unwrap();
    assert!(c
        .store
        .renew_read(&c.run, &leases[0], &reader)
        .await
        .is_err());
    let fresh = c
        .store
        .open_read(&c.run, CacheCredential::Managed(&viewer), id, &reader)
        .await
        .unwrap();
    c.store.renew_read(&c.run, &fresh, &reader).await.unwrap();
    let identity =
        c.f.identity
            .authenticate(&viewer, ClientType::AdminWeb)
            .await
            .unwrap();
    c.f.identity
        .revoke_session(identity.user_id, identity.session_id)
        .await
        .unwrap();
    assert!(c.store.renew_read(&c.run, &fresh, &reader).await.is_err());
    assert!(c
        .store
        .open_read(&c.run, CacheCredential::Managed(&viewer), id, &reader)
        .await
        .is_err());
    drop(reader);
    c.close().await;
}
#[tokio::test]
async fn read_lease_and_physical_lock_each_independently_prevent_collection() {
    let c = CacheFixture::new().await;
    let (id, file) = c.ready().await;
    let reader = c.run.root().try_read(file.id, file.content).unwrap();
    let lease = c
        .store
        .open_read(&c.run, CacheCredential::Managed(&c.f.admin), id, &reader)
        .await
        .unwrap();
    c.age().await;
    assert!(c
        .store
        .collection_candidates(&c.run, None, 100)
        .await
        .unwrap()
        .is_empty());
    drop(reader);
    let guard = c.run.root().try_lock_blob(file.id).unwrap();
    assert!(c.store.begin_collection(&c.run, &guard).await.is_err());
    drop(guard);
    let reader = c.run.root().try_read(file.id, file.content).unwrap();
    sqlx::query(
        "UPDATE pixels.cache_read_leases SET expires_at=clock_timestamp()-interval '1 second'",
    )
    .execute(&c.f.owner)
    .await
    .unwrap();
    assert!(c.store.renew_read(&c.run, &lease, &reader).await.is_err());
    assert_eq!(
        c.store
            .collection_candidates(&c.run, None, 100)
            .await
            .unwrap(),
        vec![file.id]
    );
    assert!(matches!(
        c.run.root().try_lock_blob(file.id),
        Err(px_private_files::FileError::Busy)
    ));
    drop(reader);
    let guard = c.run.root().try_lock_blob(file.id).unwrap();
    c.store.begin_collection(&c.run, &guard).await.unwrap();
    assert!(c
        .store
        .cached_file(&c.run, CacheCredential::Managed(&c.f.admin), id)
        .await
        .is_err());
    let proof = guard.delete().unwrap();
    c.store.finish_collection(&c.run, &proof).await.unwrap();
    c.store.finish_collection(&c.run, &proof).await.unwrap();
    drop(proof);
    assert!(c.run.root().try_read(file.id, file.content).is_err());
    c.close().await;
}
#[tokio::test]
async fn retention_is_admin_cas_and_survives_ttl_offline_and_capacity_pressure() {
    let c = CacheFixture::new().await;
    let (id, file) = c.ready().await;
    let viewer = c.f.session("viewer", ClientType::AdminWeb).await;
    let revision = c.revision().await;
    assert!(c
        .store
        .retain(&c.run, &viewer, id, revision, true)
        .await
        .is_err());
    let retained = c
        .store
        .retain(&c.run, &c.f.admin, id, revision, true)
        .await
        .unwrap();
    assert!(retained.pinned);
    assert!(c
        .store
        .retain(&c.run, &c.f.admin, id, revision, false)
        .await
        .is_err());
    c.age().await;
    assert!(c
        .store
        .collection_candidates(&c.run, None, 100)
        .await
        .unwrap()
        .is_empty());
    let guard = c.run.root().try_lock_blob(file.id).unwrap();
    assert!(c
        .store
        .evict(&c.run, &c.f.admin, retained.revision, &guard)
        .await
        .is_err());
    let large = c.record(1_048_576).await;
    assert!(c
        .store
        .request(&c.run, CacheCredential::Managed(&c.f.admin), large)
        .await
        .is_err());
    c.f.nodes.close_connection(&c.node).await.unwrap();
    assert_eq!(
        c.store
            .cached_file(&c.run, CacheCredential::Managed(&viewer), id)
            .await
            .unwrap()
            .id,
        file.id
    );
    let released = c
        .store
        .retain(&c.run, &c.f.admin, id, retained.revision, false)
        .await
        .unwrap();
    assert!(!released.pinned);
    assert!(c
        .store
        .evict(&c.run, &viewer, released.revision, &guard)
        .await
        .is_err());
    c.store
        .evict(&c.run, &c.f.admin, released.revision, &guard)
        .await
        .unwrap();
    assert!(c
        .store
        .retain(&c.run, &c.f.admin, id, released.revision, true)
        .await
        .is_err());
    let proof = guard.delete().unwrap();
    c.store.finish_collection(&c.run, &proof).await.unwrap();
    drop(proof);
    c.close().await;
}
#[tokio::test]
async fn collection_audit_failures_preserve_reference_then_reservation_until_actual_delete_ack() {
    let c = CacheFixture::new().await;
    let (id, file) = c.ready().await;
    c.age().await;
    let guard = c.run.root().try_lock_blob(file.id).unwrap();
    sqlx::query("REVOKE INSERT ON pixels.cache_events FROM pixels_console_runtime")
        .execute(&c.f.owner)
        .await
        .unwrap();
    assert!(c.store.begin_collection(&c.run, &guard).await.is_err());
    assert_eq!(
        c.store
            .cached_file(&c.run, CacheCredential::Managed(&c.f.admin), id)
            .await
            .unwrap()
            .id,
        file.id
    );
    sqlx::query("GRANT INSERT ON pixels.cache_events TO pixels_console_runtime")
        .execute(&c.f.owner)
        .await
        .unwrap();
    c.store.begin_collection(&c.run, &guard).await.unwrap();
    let proof = guard.delete().unwrap();
    sqlx::query("REVOKE INSERT ON pixels.cache_events FROM pixels_console_runtime")
        .execute(&c.f.owner)
        .await
        .unwrap();
    assert!(c.store.finish_collection(&c.run, &proof).await.is_err());
    let state: String = sqlx::query_scalar("SELECT state FROM pixels.cache_blobs WHERE id=$1")
        .bind(file.id)
        .fetch_one(&c.f.owner)
        .await
        .unwrap();
    assert_eq!(state, "deleting");
    sqlx::query("GRANT INSERT ON pixels.cache_events TO pixels_console_runtime")
        .execute(&c.f.owner)
        .await
        .unwrap();
    c.store.finish_collection(&c.run, &proof).await.unwrap();
    drop(proof);
    // A response lost after commit is safe to retry with a fresh exact-object deletion proof.
    let guard = c.run.root().try_lock_blob(file.id).unwrap();
    c.store.begin_collection(&c.run, &guard).await.unwrap();
    let proof = guard.delete().unwrap();
    c.store.finish_collection(&c.run, &proof).await.unwrap();
    drop(proof);
    let large = c.record(1_048_576).await;
    c.request(large).await;
    c.close().await;
}
#[tokio::test]
async fn never_started_attempt_is_tombstoned_by_collection_and_stale_worker_cannot_recreate_it() {
    let c = CacheFixture::new().await;
    let id = c.record(DATA.len() as u64).await;
    c.request(id).await;
    let a = c.attempt().await;
    let guard = c.run.root().try_lock_blob(a.id()).unwrap();
    assert!(c.store.begin_collection(&c.run, &guard).await.is_err());
    assert!(c
        .store
        .evict(&c.run, &c.f.admin, c.revision().await, &guard)
        .await
        .is_err());
    c.store.abandon(&c.run, &c.node, &a).await.unwrap();
    c.store.begin_collection(&c.run, &guard).await.unwrap();
    let proof = guard.delete().unwrap();
    c.store.finish_collection(&c.run, &proof).await.unwrap();
    drop(proof);
    assert!(c
        .run
        .root()
        .try_lock_blob(a.id())
        .unwrap()
        .begin_write(a.content())
        .is_err());
    c.request(id).await;
    assert_ne!(c.attempt().await.id(), a.id());
    // Unknown objects are not GC candidates, even with a legitimate root's physical lock.
    let unknown = c.run.root().try_lock_blob(Uuid::new_v4()).unwrap();
    assert!(c.store.begin_collection(&c.run, &unknown).await.is_err());
    drop(unknown);
    c.close().await;
}
#[tokio::test]
async fn restart_fences_media_leases_until_current_file_revalidation_and_new_authorization() {
    let c = CacheFixture::new().await;
    let (id, file) = c.ready().await;
    let reader = c.run.root().try_read(file.id, file.content).unwrap();
    let lease = c
        .store
        .open_read(&c.run, CacheCredential::Managed(&c.f.admin), id, &reader)
        .await
        .unwrap();
    let run = c
        .store
        .begin_runtime(c.run.root().clone(), c.node.epoch(), options())
        .await
        .unwrap();
    assert!(c.store.renew_read(&c.run, &lease, &reader).await.is_err());
    assert!(c.store.renew_read(&run, &lease, &reader).await.is_err());
    assert!(c
        .store
        .open_read(&run, CacheCredential::Managed(&c.f.admin), id, &reader)
        .await
        .is_err());
    c.store.verify_cached(&run, id, &reader).await.unwrap();
    c.store
        .open_read(&run, CacheCredential::Managed(&c.f.admin), id, &reader)
        .await
        .unwrap();
    drop(reader);
    drop(run);
    c.close().await;
}
#[tokio::test]
async fn android_media_requires_current_device_acl_not_application_or_other_client_identity() {
    let c = CacheFixture::new().await;
    let (id, file) = c.ready().await;
    let android = c.f.session("user", ClientType::Android).await;
    let identity =
        c.f.identity
            .authenticate(&android, ClientType::Android)
            .await
            .unwrap();
    let device: Uuid = sqlx::query_scalar("SELECT device_id FROM pixels.nodes WHERE id=$1")
        .bind(c.node.id())
        .fetch_one(&c.f.owner)
        .await
        .unwrap();
    let access =
        c.f.devices
            .replace_access(
                &c.f.admin,
                device,
                1,
                &px_console_store::DeviceAccess {
                    users: vec![identity.user_id],
                    groups: vec![],
                },
            )
            .await
            .unwrap();
    // Device ACL replacement revokes the previous authorization revision; issue a fresh login.
    let revision: i64 =
        sqlx::query_scalar("SELECT authorization_revision FROM pixels.users WHERE id=$1")
            .bind(identity.user_id)
            .fetch_one(&c.f.owner)
            .await
            .unwrap();
    let login = token();
    c.f.identity
        .issue_session(
            identity.user_id,
            revision,
            &login,
            ClientType::Android,
            std::time::Duration::from_secs(3600),
        )
        .await
        .unwrap();
    let reader = c.run.root().try_read(file.id, file.content).unwrap();
    let lease = c
        .store
        .open_read(
            &c.run,
            CacheCredential::DeviceUser {
                token: &login,
                client: ClientType::Android,
            },
            id,
            &reader,
        )
        .await
        .unwrap();
    assert!(c
        .store
        .open_read(
            &c.run,
            CacheCredential::DeviceUser {
                token: &login,
                client: ClientType::Panel
            },
            id,
            &reader
        )
        .await
        .is_err());
    c.f.devices
        .replace_access(
            &c.f.admin,
            device,
            access.revision,
            &px_console_store::DeviceAccess {
                users: vec![],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    assert!(c.store.renew_read(&c.run, &lease, &reader).await.is_err());
    drop(reader);
    c.close().await;
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
    let c = CacheFixture::new().await;
    for _ in 0..20 {
        let (id, file) = c.ready().await;
        let revision: i64 =
            sqlx::query_scalar("SELECT revision FROM pixels.recording_cache WHERE recording_id=$1")
                .bind(id)
                .fetch_one(&c.f.owner)
                .await
                .unwrap();
        let guard = c.run.root().try_lock_blob(file.id).unwrap();
        let (pin, evict) = tokio::join!(
            c.store.retain(&c.run, &c.f.admin, id, revision, true),
            c.store.evict(&c.run, &c.f.admin, revision, &guard)
        );
        assert_ne!(pin.is_ok(), evict.is_ok());
        if let Ok(pinned) = pin {
            assert_eq!(
                c.store
                    .cached_file(&c.run, CacheCredential::Managed(&c.f.admin), id)
                    .await
                    .unwrap()
                    .id,
                file.id
            );
            let unpinned = c
                .store
                .retain(&c.run, &c.f.admin, id, pinned.revision, false)
                .await
                .unwrap();
            c.store
                .evict(&c.run, &c.f.admin, unpinned.revision, &guard)
                .await
                .unwrap();
        } else {
            assert!(c
                .store
                .cached_file(&c.run, CacheCredential::Managed(&c.f.admin), id)
                .await
                .is_err());
        }
        let proof = guard.delete().unwrap();
        c.store.finish_collection(&c.run, &proof).await.unwrap();
        drop(proof);
    }
    c.close().await;
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
    f: Fixture,
    store: RecordingCacheStore,
    recordings: RecordingStore,
    run: CacheRuntime,
    node: px_console_store::NodeConnection,
    key: px_console_store::TokenDigest,
    directory: tempfile::TempDir,
}
impl CacheFixture {
    async fn new() -> Self {
        let f = Fixture::new().await;
        // Test-only owner cleanup within the harness's disposable database. No runtime DELETE grant.
        sqlx::query("TRUNCATE pixels.cache_roots,pixels.cache_runs,pixels.cache_runtime,pixels.recording_cache,pixels.cache_blobs,pixels.cache_events CASCADE")
   .execute(&f.owner).await.unwrap();
        let deployment = env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap();
        let store = RecordingCacheStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let recordings = RecordingStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let (node, key) = f.connected().await;
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
            f,
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
            .request(&self.run, CacheCredential::Managed(&self.f.admin), id)
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
        self.f.close().await;
        drop(self.run);
        self.directory.close().unwrap();
    }
}
#[tokio::test]
async fn publication_requires_real_hash_proof_and_retries_are_idempotent() {
    let c = CacheFixture::new().await;
    let id = c.record(DATA.len() as u64).await;
    assert_eq!(c.request(id).await.state, "fetching");
    let a = c.attempt().await;
    assert!(a.valid_for_ms() > 0 && a.valid_for_ms() <= 30000);
    let mut writer = c
        .run
        .root()
        .try_lock_blob(a.id())
        .unwrap()
        .begin_write(a.content())
        .unwrap();
    writer.append(&DATA[..8]).unwrap();
    let renewed = c.store.renew(&c.run, &c.node, &a, &writer).await.unwrap();
    assert_eq!(renewed.id(), a.id());
    let view = c
        .store
        .list_managed(&c.run, &c.f.admin, None, 100)
        .await
        .unwrap();
    assert_eq!(view[0].received_bytes, 8);
    assert_eq!(view[0].state, "fetching");
    writer.append(&DATA[8..]).unwrap();
    let proof = writer.finish().unwrap();
    let ready = c
        .store
        .publish(&c.run, &c.node, &renewed, &proof)
        .await
        .unwrap();
    assert_eq!(ready.state, "ready");
    assert_eq!(
        ready,
        c.store
            .publish(&c.run, &c.node, &renewed, &proof)
            .await
            .unwrap()
    );
    assert!(c.store.abandon(&c.run, &c.node, &a).await.is_err());
    drop(proof);
    let reader = c.run.root().try_read(a.id(), a.content()).unwrap();
    assert_eq!(
        ready,
        c.store.verify_cached(&c.run, id, &reader).await.unwrap()
    );
    drop(reader);
    assert!(c
        .store
        .pending(&c.run, &c.node, None, 100)
        .await
        .unwrap()
        .is_empty());
    c.close().await;
}
#[tokio::test]
async fn concurrent_requests_share_one_attempt_and_reserved_capacity_is_bounded() {
    let c = CacheFixture::new().await;
    let id = c.record(700_000).await;
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (s, r, t, b) = (
            c.store.clone(),
            c.run.clone(),
            c.f.admin.clone(),
            barrier.clone(),
        );
        tasks.push(tokio::spawn(async move {
            b.wait().await;
            s.request(&r, CacheCredential::Managed(&t), id)
                .await
                .unwrap()
        }));
    }
    let mut revisions = Vec::new();
    for t in tasks {
        revisions.push(t.await.unwrap().revision);
    }
    assert!(revisions.iter().all(|r| *r == 2));
    let count: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.cache_blobs")
        .fetch_one(&c.f.owner)
        .await
        .unwrap();
    assert_eq!(count, 1);
    let other = c.record(700_000).await;
    assert!(c
        .store
        .request(&c.run, CacheCredential::Managed(&c.f.admin), other)
        .await
        .is_err());
    let a = c.attempt().await;
    c.store.abandon(&c.run, &c.node, &a).await.unwrap();
    c.store.abandon(&c.run, &c.node, &a).await.unwrap();
    // Retired work may have partial files: it still consumes its full reservation.
    assert!(c
        .store
        .request(&c.run, CacheCredential::Managed(&c.f.admin), id)
        .await
        .is_err());
    c.close().await;
}
#[tokio::test]
async fn timeout_rejects_late_publisher_and_new_attempt_never_reuses_old_blob() {
    let c = CacheFixture::new().await;
    let id = c.record(DATA.len() as u64).await;
    c.request(id).await;
    let a = c.attempt().await;
    let mut writer = c
        .run
        .root()
        .try_lock_blob(a.id())
        .unwrap()
        .begin_write(a.content())
        .unwrap();
    writer.append(DATA).unwrap();
    let proof = writer.finish().unwrap();
    sqlx::query("UPDATE pixels.cache_blobs SET lease_until=clock_timestamp()-interval '1 second' WHERE id=$1")
  .bind(a.id()).execute(&c.f.owner).await.unwrap();
    assert!(c.store.publish(&c.run, &c.node, &a, &proof).await.is_err());
    assert_eq!(c.store.expire(&c.run, 100).await.unwrap(), 1);
    assert_eq!(c.store.expire(&c.run, 100).await.unwrap(), 0);
    c.request(id).await;
    let next = c.attempt().await;
    assert_ne!(a.id(), next.id());
    assert!(c
        .store
        .publish(&c.run, &c.node, &next, &proof)
        .await
        .is_err());
    c.store.abandon(&c.run, &c.node, &a).await.unwrap();
    assert_eq!(c.attempt().await.id(), next.id());
    drop(proof);
    c.close().await;
}
#[tokio::test]
async fn restart_requires_reverification_and_rejects_old_runtime_or_new_root_adoption() {
    let c = CacheFixture::new().await;
    let id = c.record(DATA.len() as u64).await;
    c.request(id).await;
    let a = c.attempt().await;
    let mut writer = c
        .run
        .root()
        .try_lock_blob(a.id())
        .unwrap()
        .begin_write(a.content())
        .unwrap();
    writer.append(DATA).unwrap();
    let proof = writer.finish().unwrap();
    c.store.publish(&c.run, &c.node, &a, &proof).await.unwrap();
    drop(proof);
    let run = c
        .store
        .begin_runtime(c.run.root().clone(), c.node.epoch(), options())
        .await
        .unwrap();
    assert!(c
        .store
        .list_managed(&c.run, &c.f.admin, None, 100)
        .await
        .is_err());
    assert_eq!(
        c.store
            .list_managed(&run, &c.f.admin, None, 100)
            .await
            .unwrap()[0]
            .state,
        "verifying"
    );
    c.f.nodes.close_connection(&c.node).await.unwrap();
    let reader = run.root().try_read(a.id(), a.content()).unwrap();
    assert_eq!(
        c.store
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
        c.store.begin_runtime(root, c.node.epoch(), options()).await,
        Err(StoreError::CacheRecoveryRequired)
    ));
    other.close().unwrap();
    drop(run);
    c.close().await;
}
#[tokio::test]
async fn original_login_revocation_cannot_be_repaired_by_another_requester() {
    let c = CacheFixture::new().await;
    let id = c.record(DATA.len() as u64).await;
    let viewer = c.f.session("viewer", ClientType::AdminWeb).await;
    c.store
        .request(&c.run, CacheCredential::Managed(&viewer), id)
        .await
        .unwrap();
    let a = c.attempt().await;
    let mut writer = c
        .run
        .root()
        .try_lock_blob(a.id())
        .unwrap()
        .begin_write(a.content())
        .unwrap();
    writer.append(DATA).unwrap();
    let proof = writer.finish().unwrap();
    let identity =
        c.f.identity
            .authenticate(&viewer, ClientType::AdminWeb)
            .await
            .unwrap();
    c.f.identity
        .revoke_session(identity.user_id, identity.session_id)
        .await
        .unwrap();
    assert!(c.store.publish(&c.run, &c.node, &a, &proof).await.is_err());
    assert!(c
        .store
        .pending(&c.run, &c.node, None, 100)
        .await
        .unwrap()
        .is_empty());
    c.request(id).await;
    let next = c.attempt().await;
    assert_ne!(a.id(), next.id());
    assert!(c
        .store
        .publish(&c.run, &c.node, &next, &proof)
        .await
        .is_err());
    drop(proof);
    c.close().await;
}
#[tokio::test]
async fn audit_failure_rolls_back_request_and_publication_without_claiming_file_atomicity() {
    let c = CacheFixture::new().await;
    let id = c.record(DATA.len() as u64).await;
    sqlx::query("REVOKE INSERT ON pixels.cache_events FROM pixels_console_runtime")
        .execute(&c.f.owner)
        .await
        .unwrap();
    assert!(c
        .store
        .request(&c.run, CacheCredential::Managed(&c.f.admin), id)
        .await
        .is_err());
    let count: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.cache_blobs")
        .fetch_one(&c.f.owner)
        .await
        .unwrap();
    assert_eq!(count, 0);
    sqlx::query("GRANT INSERT ON pixels.cache_events TO pixels_console_runtime")
        .execute(&c.f.owner)
        .await
        .unwrap();
    c.request(id).await;
    let a = c.attempt().await;
    let mut writer = c
        .run
        .root()
        .try_lock_blob(a.id())
        .unwrap()
        .begin_write(a.content())
        .unwrap();
    writer.append(DATA).unwrap();
    let proof = writer.finish().unwrap();
    sqlx::query("REVOKE INSERT ON pixels.cache_events FROM pixels_console_runtime")
        .execute(&c.f.owner)
        .await
        .unwrap();
    assert!(c.store.publish(&c.run, &c.node, &a, &proof).await.is_err());
    assert_eq!(
        c.store
            .list_managed(&c.run, &c.f.admin, None, 100)
            .await
            .unwrap()[0]
            .state,
        "fetching"
    );
    sqlx::query("GRANT INSERT ON pixels.cache_events TO pixels_console_runtime")
        .execute(&c.f.owner)
        .await
        .unwrap();
    assert_eq!(
        c.store
            .publish(&c.run, &c.node, &a, &proof)
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
    c.close().await;
}
#[tokio::test]
async fn client_acl_node_generation_and_epoch_are_independent_admission_gates() {
    let c = CacheFixture::new().await;
    let id = c.record(DATA.len() as u64).await;
    let android = c.f.session("user", ClientType::Android).await;
    assert!(c
        .store
        .request(
            &c.run,
            CacheCredential::DeviceUser {
                token: &android,
                client: ClientType::Android
            },
            id
        )
        .await
        .is_err());
    assert!(c
        .store
        .request(&c.run, CacheCredential::Managed(&android), id)
        .await
        .is_err());
    assert!(c
        .store
        .request(
            &c.run,
            CacheCredential::DeviceUser {
                token: &c.f.admin,
                client: ClientType::AdminWeb
            },
            id
        )
        .await
        .is_err());
    c.request(id).await;
    let a = c.attempt().await;
    let mut writer = c
        .run
        .root()
        .try_lock_blob(a.id())
        .unwrap()
        .begin_write(a.content())
        .unwrap();
    writer.append(DATA).unwrap();
    let proof = writer.finish().unwrap();
    let current =
        c.f.nodes
            .open_connection(c.node.epoch(), &c.key, &token())
            .await
            .unwrap();
    c.f.nodes.report(&current, &node_report(1)).await.unwrap();
    assert!(c.store.publish(&c.run, &c.node, &a, &proof).await.is_err());
    assert!(c.store.publish(&c.run, &current, &a, &proof).await.is_err());
    assert!(c
        .store
        .request(&c.run, CacheCredential::Managed(&c.f.admin), id)
        .await
        .is_err());
    let epoch = c.f.nodes.begin_runtime().await.unwrap();
    assert!(c.store.expire(&c.run, 100).await.is_err());
    let run = c
        .store
        .begin_runtime(c.run.root().clone(), epoch, options())
        .await
        .unwrap();
    assert_eq!(c.store.expire(&run, 100).await.unwrap(), 1);
    drop(proof);
    drop(run);
    c.close().await;
}
#[tokio::test]
async fn download_concurrency_bounds_and_wrong_file_proofs_are_rejected() {
    let c = CacheFixture::new().await;
    let first = c.record(DATA.len() as u64).await;
    c.request(first).await;
    let second = c.record(DATA.len() as u64).await;
    c.request(second).await;
    let third = c.record(DATA.len() as u64).await;
    assert!(c
        .store
        .request(&c.run, CacheCredential::Managed(&c.f.admin), third)
        .await
        .is_err());
    let a = c.attempt().await;
    let other = Uuid::new_v4();
    let mut writer = c
        .run
        .root()
        .try_lock_blob(other)
        .unwrap()
        .begin_write(ContentIdentity::new(DATA.len() as u64, Sha256::digest(DATA).into()).unwrap())
        .unwrap();
    writer.append(DATA).unwrap();
    assert!(c.store.renew(&c.run, &c.node, &a, &writer).await.is_err());
    let proof = writer.finish().unwrap();
    assert!(c.store.publish(&c.run, &c.node, &a, &proof).await.is_err());
    drop(proof);
    let reader = c.run.root().try_read(other, a.content()).unwrap();
    assert!(c
        .store
        .verify_cached(&c.run, a.recording_id(), &reader)
        .await
        .is_err());
    drop(reader);
    c.store.abandon(&c.run, &c.node, &a).await.unwrap();
    c.request(third).await;
    assert!(c.store.pending(&c.run, &c.node, None, 0).await.is_err());
    assert!(c
        .store
        .list_managed(&c.run, &c.f.admin, None, 101)
        .await
        .is_err());
    c.close().await;
}
