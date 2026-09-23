#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, token, Fixture};
use px_console_store::{ClientType, UpdateDecision, UpdateStore};
use px_release_catalog::*;
use std::env;
use uuid::Uuid;

const REPOSITORY_PUBLICATION_SHA256: &str =
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
const REPOSITORY_ROOT_VERSION: i64 = 1;

fn release_domain(distribution: Distribution) -> (String, Option<String>) {
    match distribution {
        Distribution::Official => ("pixels.official".into(), None),
        Distribution::Customer => ("pixels.customer".into(), None),
        Distribution::Oem => ("oem.acme-cloud".into(), Some("acme-cloud".into())),
    }
}

fn spec() -> ReleaseSpec {
    let build_number = chrono::Utc::now().timestamp_micros();
    ReleaseSpec {
        target: ReleaseQuery {
            product: Product::Server,
            distribution: Distribution::Customer,
            release_namespace: "pixels.customer".into(),
            oem_id: None,
            channel: Channel::Stable,
            os: OperatingSystem::Windows,
            architecture: Architecture::X86_64,
        },
        build_number,
        version: "3.2.9".into(),
        metadata_base_url: "https://example.invalid/metadata/".into(),
        targets_base_url: "https://example.invalid/targets/".into(),
        target_name: format!("windows/server/customer/stable/x86_64/{build_number}/pixels.exe"),
        sha256: "a".repeat(64),
        platform_signer_sha256: Some("b".repeat(64)),
        size_bytes: 12345,
    }
}

fn synchronize_target_name(release_spec: &mut ReleaseSpec) {
    let target = &release_spec.target;
    let mut components = vec![
        target.os.name().to_owned(),
        target.product.name().to_owned(),
        target.distribution.name().to_owned(),
    ];
    if let Some(oem_id) = &target.oem_id {
        components.push(oem_id.clone());
    }
    components.extend([
        target.channel.name().to_owned(),
        target.architecture.name().to_owned(),
        release_spec.build_number.to_string(),
        "pixels.exe".to_owned(),
    ]);
    release_spec.target_name = components.join("/");
}
async fn store() -> UpdateStore {
    UpdateStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
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
        for distribution in [
            Distribution::Official,
            Distribution::Customer,
            Distribution::Oem,
        ] {
            for channel in [Channel::Stable, Channel::Preview] {
                let mut release_spec = base.clone();
                let (release_namespace, oem_id) = release_domain(distribution);
                release_spec.target = ReleaseQuery {
                    product,
                    distribution,
                    release_namespace,
                    oem_id,
                    channel,
                    os,
                    architecture,
                };
                release_spec.platform_signer_sha256 = match os {
                    OperatingSystem::Linux => None,
                    OperatingSystem::Windows | OperatingSystem::Android => Some("b".repeat(64)),
                };
                synchronize_target_name(&mut release_spec);
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

    let shared_build_number = base.build_number.checked_add(1).unwrap();
    let mut acme_release = base.clone();
    acme_release.build_number = shared_build_number;
    acme_release.target.distribution = Distribution::Oem;
    acme_release.target.release_namespace = "oem.acme-cloud".into();
    acme_release.target.oem_id = Some("acme-cloud".into());
    synchronize_target_name(&mut acme_release);
    let mut north_star_release = acme_release.clone();
    north_star_release.target.release_namespace = "oem.north-star".into();
    north_star_release.target.oem_id = Some("north-star".into());
    synchronize_target_name(&mut north_star_release);
    let mut approved_releases = Vec::new();
    for release_spec in [&acme_release, &north_star_release] {
        let pending_release = update_store
            .register(
                &fixture.admin,
                Uuid::new_v4(),
                REPOSITORY_PUBLICATION_SHA256,
                REPOSITORY_ROOT_VERSION,
                release_spec,
            )
            .await
            .unwrap();
        approved_releases.push(
            update_store
                .decide(
                    &fixture.admin,
                    pending_release.id,
                    pending_release.revision,
                    UpdateDecision::Approve,
                )
                .await
                .unwrap(),
        );
    }
    assert_ne!(approved_releases[0].id, approved_releases[1].id);
    assert_eq!(
        update_store
            .latest(&fixture.admin, ClientType::AdminWeb, &acme_release.target)
            .await
            .unwrap()
            .id,
        approved_releases[0].id
    );
    assert_eq!(
        update_store
            .latest(
                &fixture.admin,
                ClientType::AdminWeb,
                &north_star_release.target,
            )
            .await
            .unwrap()
            .id,
        approved_releases[1].id
    );
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
    changed.version = "3.2.10-changed-request".into();
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
    synchronize_target_name(&mut release_spec);
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
    for invalid_case_index in 0..7 {
        let mut bad = release_spec.clone();
        match invalid_case_index {
            0 => bad.size_bytes = 0,
            1 => bad.target.os = OperatingSystem::Android,
            2 => bad.target.architecture = Architecture::Aarch64,
            3 => bad.target_name = "../escape.exe".into(),
            4 => {
                bad.target_name = format!(
                    "windows/server/official/stable/x86_64/{}/pixels.exe",
                    bad.build_number
                )
            }
            5 => bad.targets_base_url = "https://example.invalid/a?secret=x".into(),
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
