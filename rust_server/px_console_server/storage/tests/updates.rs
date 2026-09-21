#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, token, Fixture};
use px_console_store::{
    ClientType, DeploymentTarget, UpdateDecision, UpdateRelease, UpdateStore,
    UpdateTrustObservation,
};
use px_release_catalog::*;
use std::env;
use uuid::Uuid;

const REPOSITORY_PUBLICATION_SHA256: &str =
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
const REPOSITORY_ROOT_VERSION: i64 = 1;

fn spec() -> ReleaseSpec {
    ReleaseSpec {
        target: ReleaseQuery {
            product: Product::Server,
            distribution: Distribution::Customer,
            channel: Channel::Stable,
            os: OperatingSystem::Windows,
            architecture: Architecture::X86_64,
        },
        build_number: chrono::Utc::now().timestamp_micros(),
        version: "3.2.9".into(),
        metadata_base_url: "https://example.invalid/metadata/".into(),
        targets_base_url: "https://example.invalid/targets/".into(),
        target_name: "pixels.exe".into(),
        sha256: "a".repeat(64),
        platform_signer_sha256: Some("b".repeat(64)),
        size_bytes: 12345,
    }
}
async fn store() -> UpdateStore {
    UpdateStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap()
}

async fn approved_cloud_node_release(
    update_store: &UpdateStore,
    fixture: &Fixture,
) -> UpdateRelease {
    let mut release_spec = spec();
    release_spec.target = ReleaseQuery {
        product: Product::CloudNode,
        distribution: Distribution::Customer,
        channel: Channel::Stable,
        os: OperatingSystem::Windows,
        architecture: Architecture::X86_64,
    };
    release_spec.build_number =
        1_000 + i64::try_from(Uuid::new_v4().as_u128() % 1_000_000).unwrap();
    let release = update_store
        .register(
            &fixture.admin,
            Uuid::new_v4(),
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .unwrap();
    update_store
        .decide(
            &fixture.admin,
            release.id,
            release.revision,
            UpdateDecision::Approve,
        )
        .await
        .unwrap()
}
#[tokio::test]
async fn all_product_platform_flavor_channel_dimensions_are_independent() {
    let fixture = Fixture::new().await;
    let update_store = store().await;
    let base = spec();
    for (product, os, architecture) in [
        (
            Product::Client,
            OperatingSystem::Windows,
            Architecture::X86_64,
        ),
        (
            Product::CloudNode,
            OperatingSystem::Windows,
            Architecture::X86_64,
        ),
        (
            Product::Remote,
            OperatingSystem::Windows,
            Architecture::X86_64,
        ),
        (
            Product::Server,
            OperatingSystem::Windows,
            Architecture::X86_64,
        ),
        (
            Product::Server,
            OperatingSystem::Linux,
            Architecture::X86_64,
        ),
        (
            Product::Android,
            OperatingSystem::Android,
            Architecture::Aarch64,
        ),
    ] {
        for distribution in [Distribution::Official, Distribution::Customer] {
            for channel in [Channel::Stable, Channel::Preview] {
                let mut release_spec = base.clone();
                release_spec.target = ReleaseQuery {
                    product,
                    distribution,
                    channel,
                    os,
                    architecture,
                };
                release_spec.platform_signer_sha256 = match os {
                    OperatingSystem::Linux => None,
                    OperatingSystem::Windows | OperatingSystem::Android => Some("b".repeat(64)),
                };
                let row = update_store
                    .register(
                        &fixture.admin,
                        Uuid::new_v4(),
                        REPOSITORY_PUBLICATION_SHA256,
                        REPOSITORY_ROOT_VERSION,
                        &release_spec,
                    )
                    .await
                    .unwrap();
                assert_eq!(row.state, "pending");
                assert_eq!(row.artifact, release_spec);
                assert!(update_store
                    .latest(&fixture.admin, ClientType::AdminWeb, &release_spec.target)
                    .await
                    .is_err());
                update_store
                    .decide(&fixture.admin, row.id, 1, UpdateDecision::Approve)
                    .await
                    .unwrap();
                let found = update_store
                    .latest(&fixture.admin, ClientType::AdminWeb, &release_spec.target)
                    .await
                    .unwrap();
                assert_eq!(found.id, row.id);
                assert_eq!(found.artifact, release_spec);
            }
        }
    }
    update_store.close().await;
    fixture.close().await;
}

#[tokio::test]
async fn authenticated_node_receives_the_approved_repository_and_records_real_root_trust() {
    let fixture = Fixture::new().await;
    let update_store = store().await;
    let (connection, _) = fixture.connected().await;
    let mut release_spec = spec();
    release_spec.target = ReleaseQuery {
        product: Product::CloudNode,
        distribution: Distribution::Customer,
        channel: Channel::Stable,
        os: OperatingSystem::Windows,
        architecture: Architecture::X86_64,
    };
    let release = update_store
        .register(
            &fixture.admin,
            Uuid::new_v4(),
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .unwrap();
    assert!(update_store
        .check_for_node(
            &connection,
            &release_spec.target,
            release_spec.build_number - 1,
            None,
        )
        .await
        .unwrap()
        .is_none());
    let premature_observation = UpdateTrustObservation {
        release_id: release.id,
        repository_publication_sha256: REPOSITORY_PUBLICATION_SHA256.into(),
        root_version: REPOSITORY_ROOT_VERSION,
    };
    assert!(update_store
        .check_for_node(
            &connection,
            &release_spec.target,
            release_spec.build_number,
            Some(&premature_observation),
        )
        .await
        .is_err());
    let approved = update_store
        .decide(
            &fixture.admin,
            release.id,
            release.revision,
            UpdateDecision::Approve,
        )
        .await
        .unwrap();
    let offer = update_store
        .check_for_node(
            &connection,
            &release_spec.target,
            release_spec.build_number - 1,
            None,
        )
        .await
        .unwrap()
        .unwrap();
    assert_eq!(offer.id, approved.id);
    assert_eq!(offer.artifact, release_spec);
    assert_eq!(
        update_store
            .check_for_node(
                &connection,
                &release_spec.target,
                release_spec.build_number,
                None,
            )
            .await
            .unwrap()
            .unwrap()
            .id,
        approved.id
    );
    let observation = UpdateTrustObservation {
        release_id: approved.id,
        repository_publication_sha256: REPOSITORY_PUBLICATION_SHA256.into(),
        root_version: REPOSITORY_ROOT_VERSION,
    };
    update_store
        .check_for_node(
            &connection,
            &release_spec.target,
            release_spec.build_number,
            Some(&observation),
        )
        .await
        .unwrap();
    let trusted_root_version: i64 = sqlx::query_scalar(
        "SELECT trusted_root_version FROM pixels.node_update_trust WHERE node_id=$1",
    )
    .bind(connection.id())
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(trusted_root_version, REPOSITORY_ROOT_VERSION);
    let trust_summary = update_store
        .node_trust_summary(&fixture.admin, approved.id, Distribution::Customer)
        .await
        .unwrap();
    assert_eq!(trust_summary.release_id, approved.id);
    assert_eq!(trust_summary.required_root_version, REPOSITORY_ROOT_VERSION);
    assert!(trust_summary.eligible_node_count >= 1);
    assert_eq!(trust_summary.confirmed_node_count, 1);
    assert_eq!(
        trust_summary.unknown_or_behind_node_count,
        trust_summary.eligible_node_count - 1
    );
    assert!(update_store
        .node_trust_summary(&fixture.admin, approved.id, Distribution::Official)
        .await
        .is_err());
    let mut wrong_observation = observation.clone();
    wrong_observation.repository_publication_sha256 = "d".repeat(64);
    assert!(update_store
        .check_for_node(
            &connection,
            &release_spec.target,
            release_spec.build_number,
            Some(&wrong_observation),
        )
        .await
        .is_err());
    let mut next_release_spec = release_spec.clone();
    next_release_spec.build_number += 1;
    next_release_spec.version = "3.2.10".into();
    let next_publication_sha256 = "e".repeat(64);
    let next_release = update_store
        .register(
            &fixture.admin,
            Uuid::new_v4(),
            &next_publication_sha256,
            2,
            &next_release_spec,
        )
        .await
        .unwrap();
    let next_release = update_store
        .decide(
            &fixture.admin,
            next_release.id,
            next_release.revision,
            UpdateDecision::Approve,
        )
        .await
        .unwrap();
    let next_observation = UpdateTrustObservation {
        release_id: next_release.id,
        repository_publication_sha256: next_publication_sha256,
        root_version: 2,
    };
    update_store
        .check_for_node(
            &connection,
            &next_release_spec.target,
            next_release_spec.build_number,
            Some(&next_observation),
        )
        .await
        .unwrap();
    assert!(update_store
        .check_for_node(
            &connection,
            &release_spec.target,
            release_spec.build_number,
            Some(&observation),
        )
        .await
        .is_err());
    let mut wrong_product = release_spec.target;
    wrong_product.product = Product::Remote;
    assert!(update_store
        .check_for_node(
            &connection,
            &wrong_product,
            release_spec.build_number - 1,
            None,
        )
        .await
        .is_err());
    update_store
        .decide(
            &fixture.admin,
            next_release.id,
            next_release.revision,
            UpdateDecision::Withdraw,
        )
        .await
        .unwrap();
    assert!(update_store
        .check_for_node(
            &connection,
            &release_spec.target,
            release_spec.build_number - 1,
            None,
        )
        .await
        .unwrap()
        .is_none());
    update_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn concurrent_registration_retries_bind_body_and_never_reverse_withdrawal() {
    let fixture = Fixture::new().await;
    let update_store = store().await;
    let release_spec = spec();
    let request = Uuid::new_v4();
    let mut tasks = tokio::task::JoinSet::new();
    for _ in 0..20 {
        let update_store = update_store.clone();
        let token = fixture.admin.clone();
        let release_spec = release_spec.clone();
        tasks.spawn(async move {
            update_store
                .register(
                    &token,
                    request,
                    REPOSITORY_PUBLICATION_SHA256,
                    REPOSITORY_ROOT_VERSION,
                    &release_spec,
                )
                .await
                .unwrap()
        });
    }
    let mut id = None;
    while let Some(row) = tasks.join_next().await {
        let row = row.unwrap();
        assert_eq!(row.revision, 1);
        if let Some(id) = id {
            assert_eq!(row.id, id);
        } else {
            id = Some(row.id);
        }
    }
    let id = id.unwrap();
    let count: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.update_release_events WHERE release_id=$1")
            .bind(id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(count, 1);
    let mut changed = release_spec.clone();
    changed.target_name = "pixels-other.exe".into();
    assert!(update_store
        .register(
            &fixture.admin,
            request,
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &changed,
        )
        .await
        .is_err());
    let different_publication_sha256 = "d".repeat(64);
    assert!(update_store
        .register(
            &fixture.admin,
            request,
            &different_publication_sha256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .is_err());
    assert!(update_store
        .register(
            &fixture.admin,
            request,
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION + 1,
            &release_spec,
        )
        .await
        .is_err());
    assert!(update_store
        .register(
            &fixture.admin,
            Uuid::new_v4(),
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .is_err());
    update_store
        .decide(&fixture.admin, id, 1, UpdateDecision::Withdraw)
        .await
        .unwrap();
    let retry = update_store
        .register(
            &fixture.admin,
            request,
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .unwrap();
    assert_eq!(retry.id, id);
    assert_eq!(retry.state, "withdrawn");
    assert_eq!(retry.revision, 2);
    update_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn concurrent_policy_cas_and_admin_identity_are_checked_on_every_write() {
    let fixture = Fixture::new().await;
    let update_store = store().await;
    let release_spec = spec();
    let row = update_store
        .register(
            &fixture.admin,
            Uuid::new_v4(),
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .unwrap();
    let viewer = fixture.session("viewer", ClientType::AdminWeb).await;
    let panel_admin = fixture.session("admin", ClientType::Panel).await;
    let user = fixture.session("user", ClientType::AdminWeb).await;
    for denied in [&viewer, &panel_admin, &user, &token()] {
        assert!(update_store
            .register(
                denied,
                Uuid::new_v4(),
                REPOSITORY_PUBLICATION_SHA256,
                REPOSITORY_ROOT_VERSION,
                &spec(),
            )
            .await
            .is_err());
        assert!(update_store
            .decide(denied, row.id, 1, UpdateDecision::Approve)
            .await
            .is_err());
    }
    update_store.list_managed(&viewer, None, 1).await.unwrap();
    assert!(update_store
        .list_managed(&panel_admin, None, 1)
        .await
        .is_err());
    let mut tasks = tokio::task::JoinSet::new();
    for decision_index in 0..20 {
        let update_store = update_store.clone();
        let token = fixture.admin.clone();
        let id = row.id;
        tasks.spawn(async move {
            update_store
                .decide(
                    &token,
                    id,
                    1,
                    if decision_index % 2 == 0 {
                        UpdateDecision::Approve
                    } else {
                        UpdateDecision::Withdraw
                    },
                )
                .await
        });
    }
    let mut wins = 0;
    while let Some(task_result) = tasks.join_next().await {
        if task_result.unwrap().is_ok() {
            wins += 1;
        }
    }
    assert_eq!(wins, 1);
    let actor = fixture
        .identity
        .authenticate(&fixture.admin, ClientType::AdminWeb)
        .await
        .unwrap();
    fixture
        .identity
        .revoke_session(actor.user_id, actor.session_id)
        .await
        .unwrap();
    assert!(update_store
        .decide(&fixture.admin, row.id, 2, UpdateDecision::Approve)
        .await
        .is_err());
    update_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn event_failure_rolls_back_registration_and_approval_and_runtime_cannot_rewrite_content() {
    let fixture = Fixture::new().await;
    let update_store = store().await;
    let release_spec = spec();
    let request = Uuid::new_v4();
    sqlx::query("REVOKE INSERT ON pixels.update_release_events FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let failed = update_store
        .register(
            &fixture.admin,
            request,
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await;
    sqlx::query("GRANT INSERT ON pixels.update_release_events TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    let count: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.update_releases WHERE request_id=$1")
            .bind(request)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(count, 0);
    let row = update_store
        .register(
            &fixture.admin,
            request,
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .unwrap();
    sqlx::query("REVOKE INSERT ON pixels.update_release_events FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let failed = update_store
        .decide(&fixture.admin, row.id, 1, UpdateDecision::Approve)
        .await;
    sqlx::query("GRANT INSERT ON pixels.update_release_events TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    let row = update_store
        .register(
            &fixture.admin,
            request,
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .unwrap();
    assert_eq!(row.state, "pending");
    assert_eq!(row.revision, 1);
    let runtime = config("RUNTIME").connect().await.unwrap();
    for sql in [
        "UPDATE pixels.update_releases SET sha256=repeat('c',64)",
        "UPDATE pixels.update_releases SET repository_publication_sha256=repeat('d',64)",
        "UPDATE pixels.update_releases SET os='linux'",
        "DELETE FROM pixels.update_releases",
        "UPDATE pixels.update_release_events SET state='approved'",
        "DELETE FROM pixels.update_release_events",
    ] {
        assert!(sqlx::query(sql).execute(&runtime).await.is_err());
    }
    runtime.close().await;
    update_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn newest_unapproved_or_withdrawn_build_never_falls_back_and_pages_are_bounded() {
    let fixture = Fixture::new().await;
    let update_store = store().await;
    let mut release_spec = spec();
    let older = update_store
        .register(
            &fixture.admin,
            Uuid::new_v4(),
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .unwrap();
    update_store
        .decide(&fixture.admin, older.id, 1, UpdateDecision::Approve)
        .await
        .unwrap();
    let user = fixture.session("user", ClientType::Android).await;
    assert_eq!(
        update_store
            .latest(&user, ClientType::Android, &release_spec.target)
            .await
            .unwrap()
            .id,
        older.id
    );
    assert!(update_store
        .latest(&user, ClientType::Panel, &release_spec.target)
        .await
        .is_err());
    release_spec.build_number += 1;
    let newer = update_store
        .register(
            &fixture.admin,
            Uuid::new_v4(),
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .unwrap();
    assert!(update_store
        .latest(&user, ClientType::Android, &release_spec.target)
        .await
        .is_err());
    update_store
        .decide(&fixture.admin, newer.id, 1, UpdateDecision::Approve)
        .await
        .unwrap();
    update_store
        .decide(&fixture.admin, newer.id, 2, UpdateDecision::Withdraw)
        .await
        .unwrap();
    assert!(update_store
        .latest(&user, ClientType::Android, &release_spec.target)
        .await
        .is_err());
    let again = update_store
        .decide(&fixture.admin, newer.id, 3, UpdateDecision::Withdraw)
        .await
        .unwrap();
    assert_eq!(again.revision, 3);
    update_store
        .decide(&fixture.admin, newer.id, 3, UpdateDecision::Approve)
        .await
        .unwrap();
    assert_eq!(
        update_store
            .latest(&user, ClientType::Android, &release_spec.target)
            .await
            .unwrap()
            .id,
        newer.id
    );
    for limit in [0, 101] {
        assert!(update_store
            .list_managed(&fixture.admin, None, limit)
            .await
            .is_err());
    }
    let mut after = None;
    let mut ids = std::collections::HashSet::new();
    loop {
        let page = update_store
            .list_managed(&fixture.admin, after, 3)
            .await
            .unwrap();
        if page.is_empty() {
            break;
        }
        for row in page {
            if let Some(previous) = after {
                assert!(row.id > previous);
            }
            after = Some(row.id);
            assert!(ids.insert(row.id));
        }
    }
    assert!(ids.contains(&newer.id));
    assert!(ids.contains(&older.id));
    update_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn malformed_release_metadata_has_no_side_effects_and_database_enforces_platforms() {
    let fixture = Fixture::new().await;
    let update_store = store().await;
    let release_spec = spec();
    let before: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.update_releases")
        .fetch_one(&fixture.owner)
        .await
        .unwrap();
    for invalid_case_index in 0..6 {
        let mut bad = release_spec.clone();
        match invalid_case_index {
            0 => bad.size_bytes = 0,
            1 => bad.target.os = OperatingSystem::Android,
            2 => bad.target.architecture = Architecture::Aarch64,
            3 => bad.target_name = "../escape.exe".into(),
            4 => bad.targets_base_url = "https://example.invalid/a?secret=x".into(),
            _ => bad.build_number = 0,
        }
        assert!(update_store
            .register(
                &fixture.admin,
                Uuid::new_v4(),
                REPOSITORY_PUBLICATION_SHA256,
                REPOSITORY_ROOT_VERSION,
                &bad,
            )
            .await
            .is_err());
    }
    assert!(update_store
        .register(
            &fixture.admin,
            Uuid::nil(),
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .is_err());
    for invalid_publication_sha256 in ["short".to_owned(), "C".repeat(64)] {
        assert!(update_store
            .register(
                &fixture.admin,
                Uuid::new_v4(),
                &invalid_publication_sha256,
                REPOSITORY_ROOT_VERSION,
                &release_spec,
            )
            .await
            .is_err());
    }
    assert!(update_store
        .register(
            &fixture.admin,
            Uuid::new_v4(),
            REPOSITORY_PUBLICATION_SHA256,
            0,
            &release_spec,
        )
        .await
        .is_err());
    let after: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.update_releases")
        .fetch_one(&fixture.owner)
        .await
        .unwrap();
    assert_eq!(before, after);
    let row = update_store
        .register(
            &fixture.admin,
            Uuid::new_v4(),
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .unwrap();
    assert_eq!(
        row.repository_publication_sha256,
        REPOSITORY_PUBLICATION_SHA256
    );
    assert!(
        sqlx::query("UPDATE pixels.update_releases SET os='android' WHERE id=$1")
            .bind(row.id)
            .execute(&fixture.owner)
            .await
            .is_err()
    );
    assert!(
        sqlx::query("UPDATE pixels.update_releases SET size_bytes=0 WHERE id=$1")
            .bind(row.id)
            .execute(&fixture.owner)
            .await
            .is_err()
    );
    update_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn restarts_preserve_policy_and_closed_pool_never_reports_success() {
    let fixture = Fixture::new().await;
    let update_store = store().await;
    let release_spec = spec();
    let request = Uuid::new_v4();
    let row = update_store
        .register(
            &fixture.admin,
            request,
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .unwrap();
    update_store
        .decide(&fixture.admin, row.id, 1, UpdateDecision::Approve)
        .await
        .unwrap();
    update_store.close().await;
    assert!(update_store
        .register(
            &fixture.admin,
            request,
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .is_err());
    assert!(update_store
        .latest(&fixture.admin, ClientType::AdminWeb, &release_spec.target)
        .await
        .is_err());
    let resumed = store().await;
    let retry = resumed
        .register(
            &fixture.admin,
            request,
            REPOSITORY_PUBLICATION_SHA256,
            REPOSITORY_ROOT_VERSION,
            &release_spec,
        )
        .await
        .unwrap();
    assert_eq!(retry.id, row.id);
    assert_eq!(retry.revision, 2);
    assert_eq!(retry.state, "approved");
    let json = serde_json::to_string(&retry).unwrap();
    for secret in ["request_hash", "registered_by", "token_hash"] {
        assert!(!json.contains(secret));
    }
    resumed.close().await;
    fixture.close().await;
}

#[tokio::test]
async fn activation_is_console_serialized_idempotent_and_blocks_new_work() {
    let fixture = Fixture::new().await;
    let update_store = store().await;
    let (connection, application, _) = fixture.prepared(DeploymentTarget::Rdp, 1).await;
    let release = approved_cloud_node_release(&update_store, &fixture).await;

    let first = update_store
        .begin_activation(
            &connection,
            &release.artifact.target,
            release.id,
            release.revision,
            &release.artifact.sha256,
        )
        .await
        .unwrap();
    let retry = update_store
        .begin_activation(
            &connection,
            &release.artifact.target,
            release.id,
            release.revision,
            &release.artifact.sha256,
        )
        .await
        .unwrap();
    assert_eq!(retry.task_id, first.task_id);
    assert_eq!(retry.lease_id, first.lease_id);
    assert_eq!(retry.lease_until, first.lease_until);

    let user = fixture.session("user", ClientType::Android).await;
    assert!(fixture
        .instances
        .reserve(
            px_console_store::ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &fixture::request(application.id),
        )
        .await
        .is_err());

    let count: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels.node_update_tasks WHERE node_id=$1 AND state='activating'",
    )
    .bind(connection.id())
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(count, 1);

    assert!(update_store
        .finish_activation(
            &connection,
            first.task_id,
            first.lease_id,
            &px_console_store::UpdateActivationOutcome::Installed,
        )
        .await
        .is_err());
    sqlx::query("UPDATE pixels.nodes SET product_version_code=$2 WHERE id=$1")
        .bind(connection.id())
        .bind(release.artifact.build_number)
        .execute(&fixture.owner)
        .await
        .unwrap();
    let completed = update_store
        .finish_activation(
            &connection,
            first.task_id,
            first.lease_id,
            &px_console_store::UpdateActivationOutcome::Installed,
        )
        .await
        .unwrap();
    assert_eq!(completed.state, "installed");
    assert_eq!(completed.revision, 2);
    assert!(completed.error_code.is_none());
    let completion_retry = update_store
        .finish_activation(
            &connection,
            first.task_id,
            first.lease_id,
            &px_console_store::UpdateActivationOutcome::Installed,
        )
        .await
        .unwrap();
    assert_eq!(completion_retry.revision, completed.revision);
    assert!(fixture
        .instances
        .reserve(
            px_console_store::ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &fixture::request(application.id),
        )
        .await
        .is_ok());
    update_store.close().await;
    fixture.close().await;
}

#[tokio::test]
async fn activation_rejects_busy_draining_or_mismatched_release_state() {
    let fixture = Fixture::new().await;
    let update_store = store().await;
    let (connection, application, _) = fixture.prepared(DeploymentTarget::Rdp, 1).await;
    let release = approved_cloud_node_release(&update_store, &fixture).await;
    let (_user, _instance, _command) = fixture.started(&connection, application.id).await;

    assert!(update_store
        .begin_activation(
            &connection,
            &release.artifact.target,
            release.id,
            release.revision,
            &release.artifact.sha256,
        )
        .await
        .is_err());

    sqlx::query(
        "UPDATE pixels.instances SET ended_at=clock_timestamp(),state='stopped' WHERE node_id=$1",
    )
    .bind(connection.id())
    .execute(&fixture.owner)
    .await
    .unwrap();
    sqlx::query("UPDATE pixels.nodes SET draining=true WHERE id=$1")
        .bind(connection.id())
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(update_store
        .begin_activation(
            &connection,
            &release.artifact.target,
            release.id,
            release.revision,
            &release.artifact.sha256,
        )
        .await
        .is_err());
    assert!(update_store
        .begin_activation(
            &connection,
            &release.artifact.target,
            release.id,
            release.revision + 1,
            &release.artifact.sha256,
        )
        .await
        .is_err());
    assert!(update_store
        .begin_activation(
            &connection,
            &release.artifact.target,
            release.id,
            release.revision,
            &"b".repeat(64),
        )
        .await
        .is_err());

    let count: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.node_update_tasks WHERE node_id=$1")
            .bind(connection.id())
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(count, 0);
    update_store.close().await;
    fixture.close().await;
}

#[tokio::test]
async fn node_report_expires_an_abandoned_activation_lease() {
    let fixture = Fixture::new().await;
    let update_store = store().await;
    let (connection, _, _) = fixture.prepared(DeploymentTarget::Rdp, 1).await;
    let release = approved_cloud_node_release(&update_store, &fixture).await;
    let activation = update_store
        .begin_activation(
            &connection,
            &release.artifact.target,
            release.id,
            release.revision,
            &release.artifact.sha256,
        )
        .await
        .unwrap();
    sqlx::query(
        "UPDATE pixels.node_update_tasks SET created_at=clock_timestamp()-interval '11 minutes',lease_until=clock_timestamp()-interval '1 minute' WHERE id=$1",
    )
    .bind(activation.task_id)
    .execute(&fixture.owner)
    .await
    .unwrap();

    fixture
        .nodes
        .report(&connection, &fixture::node_report(2))
        .await
        .unwrap();
    let terminal: (String, Option<String>) =
        sqlx::query_as("SELECT state,error_code FROM pixels.node_update_tasks WHERE id=$1")
            .bind(activation.task_id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(terminal.0, "failed");
    assert_eq!(terminal.1.as_deref(), Some("activation_lease_expired"));

    update_store.close().await;
    fixture.close().await;
}
