#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, token, Fixture};
use px_console_store::{ClientType, UpdateDecision, UpdateStore};
use px_release_catalog::*;
use std::env;
use uuid::Uuid;

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
        artifact_url: "https://example.invalid/pixels.exe".into(),
        sha256: "a".repeat(64),
        size_bytes: 12345,
        metadata_url: "https://example.invalid/targets.json".into(),
        metadata_sha256: "b".repeat(64),
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
#[tokio::test]
async fn all_product_platform_flavor_channel_dimensions_are_independent() {
    let f = Fixture::new().await;
    let s = store().await;
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
                let mut a = base.clone();
                a.target = ReleaseQuery {
                    product,
                    distribution,
                    channel,
                    os,
                    architecture,
                };
                let row = s.register(&f.admin, Uuid::new_v4(), &a).await.unwrap();
                assert_eq!(row.state, "pending");
                assert_eq!(row.artifact, a);
                assert!(s
                    .latest(&f.admin, ClientType::AdminWeb, &a.target)
                    .await
                    .is_err());
                s.decide(&f.admin, row.id, 1, UpdateDecision::Approve)
                    .await
                    .unwrap();
                let found = s
                    .latest(&f.admin, ClientType::AdminWeb, &a.target)
                    .await
                    .unwrap();
                assert_eq!(found.id, row.id);
                assert_eq!(found.artifact, a);
            }
        }
    }
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn concurrent_registration_retries_bind_body_and_never_reverse_withdrawal() {
    let f = Fixture::new().await;
    let s = store().await;
    let a = spec();
    let request = Uuid::new_v4();
    let mut tasks = tokio::task::JoinSet::new();
    for _ in 0..20 {
        let s = s.clone();
        let token = f.admin.clone();
        let a = a.clone();
        tasks.spawn(async move { s.register(&token, request, &a).await.unwrap() });
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
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(count, 1);
    let mut changed = a.clone();
    changed.metadata_sha256 = "c".repeat(64);
    assert!(s.register(&f.admin, request, &changed).await.is_err());
    assert!(s.register(&f.admin, Uuid::new_v4(), &a).await.is_err());
    s.decide(&f.admin, id, 1, UpdateDecision::Withdraw)
        .await
        .unwrap();
    let retry = s.register(&f.admin, request, &a).await.unwrap();
    assert_eq!(retry.id, id);
    assert_eq!(retry.state, "withdrawn");
    assert_eq!(retry.revision, 2);
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn concurrent_policy_cas_and_admin_identity_are_checked_on_every_write() {
    let f = Fixture::new().await;
    let s = store().await;
    let a = spec();
    let row = s.register(&f.admin, Uuid::new_v4(), &a).await.unwrap();
    let viewer = f.session("viewer", ClientType::AdminWeb).await;
    let panel_admin = f.session("admin", ClientType::Panel).await;
    let user = f.session("user", ClientType::AdminWeb).await;
    for denied in [&viewer, &panel_admin, &user, &token()] {
        assert!(s.register(denied, Uuid::new_v4(), &spec()).await.is_err());
        assert!(s
            .decide(denied, row.id, 1, UpdateDecision::Approve)
            .await
            .is_err());
    }
    s.list_managed(&viewer, None, 1).await.unwrap();
    assert!(s.list_managed(&panel_admin, None, 1).await.is_err());
    let mut tasks = tokio::task::JoinSet::new();
    for n in 0..20 {
        let s = s.clone();
        let token = f.admin.clone();
        let id = row.id;
        tasks.spawn(async move {
            s.decide(
                &token,
                id,
                1,
                if n % 2 == 0 {
                    UpdateDecision::Approve
                } else {
                    UpdateDecision::Withdraw
                },
            )
            .await
        });
    }
    let mut wins = 0;
    while let Some(r) = tasks.join_next().await {
        if r.unwrap().is_ok() {
            wins += 1;
        }
    }
    assert_eq!(wins, 1);
    let actor = f
        .identity
        .authenticate(&f.admin, ClientType::AdminWeb)
        .await
        .unwrap();
    f.identity
        .revoke_session(actor.user_id, actor.session_id)
        .await
        .unwrap();
    assert!(s
        .decide(&f.admin, row.id, 2, UpdateDecision::Approve)
        .await
        .is_err());
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn event_failure_rolls_back_registration_and_approval_and_runtime_cannot_rewrite_content() {
    let f = Fixture::new().await;
    let s = store().await;
    let a = spec();
    let request = Uuid::new_v4();
    sqlx::query("REVOKE INSERT ON pixels.update_release_events FROM pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    let failed = s.register(&f.admin, request, &a).await;
    sqlx::query("GRANT INSERT ON pixels.update_release_events TO pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    let count: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.update_releases WHERE request_id=$1")
            .bind(request)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(count, 0);
    let row = s.register(&f.admin, request, &a).await.unwrap();
    sqlx::query("REVOKE INSERT ON pixels.update_release_events FROM pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    let failed = s.decide(&f.admin, row.id, 1, UpdateDecision::Approve).await;
    sqlx::query("GRANT INSERT ON pixels.update_release_events TO pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    let row = s.register(&f.admin, request, &a).await.unwrap();
    assert_eq!(row.state, "pending");
    assert_eq!(row.revision, 1);
    let runtime = config("RUNTIME").connect().await.unwrap();
    for sql in [
        "UPDATE pixels.update_releases SET sha256=repeat('c',64)",
        "UPDATE pixels.update_releases SET os='linux'",
        "DELETE FROM pixels.update_releases",
        "UPDATE pixels.update_release_events SET state='approved'",
        "DELETE FROM pixels.update_release_events",
    ] {
        assert!(sqlx::query(sql).execute(&runtime).await.is_err());
    }
    runtime.close().await;
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn newest_unapproved_or_withdrawn_build_never_falls_back_and_pages_are_bounded() {
    let f = Fixture::new().await;
    let s = store().await;
    let mut a = spec();
    let older = s.register(&f.admin, Uuid::new_v4(), &a).await.unwrap();
    s.decide(&f.admin, older.id, 1, UpdateDecision::Approve)
        .await
        .unwrap();
    let user = f.session("user", ClientType::Android).await;
    assert_eq!(
        s.latest(&user, ClientType::Android, &a.target)
            .await
            .unwrap()
            .id,
        older.id
    );
    assert!(s.latest(&user, ClientType::Panel, &a.target).await.is_err());
    a.build_number += 1;
    let newer = s.register(&f.admin, Uuid::new_v4(), &a).await.unwrap();
    assert!(s
        .latest(&user, ClientType::Android, &a.target)
        .await
        .is_err());
    s.decide(&f.admin, newer.id, 1, UpdateDecision::Approve)
        .await
        .unwrap();
    s.decide(&f.admin, newer.id, 2, UpdateDecision::Withdraw)
        .await
        .unwrap();
    assert!(s
        .latest(&user, ClientType::Android, &a.target)
        .await
        .is_err());
    let again = s
        .decide(&f.admin, newer.id, 3, UpdateDecision::Withdraw)
        .await
        .unwrap();
    assert_eq!(again.revision, 3);
    s.decide(&f.admin, newer.id, 3, UpdateDecision::Approve)
        .await
        .unwrap();
    assert_eq!(
        s.latest(&user, ClientType::Android, &a.target)
            .await
            .unwrap()
            .id,
        newer.id
    );
    for limit in [0, 101] {
        assert!(s.list_managed(&f.admin, None, limit).await.is_err());
    }
    let mut after = None;
    let mut ids = std::collections::HashSet::new();
    loop {
        let page = s.list_managed(&f.admin, after, 3).await.unwrap();
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
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn malformed_release_metadata_has_no_side_effects_and_database_enforces_platforms() {
    let f = Fixture::new().await;
    let s = store().await;
    let a = spec();
    let before: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.update_releases")
        .fetch_one(&f.owner)
        .await
        .unwrap();
    for n in 0..6 {
        let mut bad = a.clone();
        match n {
            0 => bad.size_bytes = 0,
            1 => bad.target.os = OperatingSystem::Android,
            2 => bad.target.architecture = Architecture::Aarch64,
            3 => bad.metadata_sha256 = "bad".into(),
            4 => bad.artifact_url = "https://example.invalid/a?secret=x".into(),
            _ => bad.build_number = 0,
        }
        assert!(s.register(&f.admin, Uuid::new_v4(), &bad).await.is_err());
    }
    assert!(s.register(&f.admin, Uuid::nil(), &a).await.is_err());
    let after: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.update_releases")
        .fetch_one(&f.owner)
        .await
        .unwrap();
    assert_eq!(before, after);
    let row = s.register(&f.admin, Uuid::new_v4(), &a).await.unwrap();
    assert!(
        sqlx::query("UPDATE pixels.update_releases SET os='android' WHERE id=$1")
            .bind(row.id)
            .execute(&f.owner)
            .await
            .is_err()
    );
    assert!(
        sqlx::query("UPDATE pixels.update_releases SET size_bytes=0 WHERE id=$1")
            .bind(row.id)
            .execute(&f.owner)
            .await
            .is_err()
    );
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn restarts_preserve_policy_and_closed_pool_never_reports_success() {
    let f = Fixture::new().await;
    let s = store().await;
    let a = spec();
    let request = Uuid::new_v4();
    let row = s.register(&f.admin, request, &a).await.unwrap();
    s.decide(&f.admin, row.id, 1, UpdateDecision::Approve)
        .await
        .unwrap();
    s.close().await;
    assert!(s.register(&f.admin, request, &a).await.is_err());
    assert!(s
        .latest(&f.admin, ClientType::AdminWeb, &a.target)
        .await
        .is_err());
    let resumed = store().await;
    let retry = resumed.register(&f.admin, request, &a).await.unwrap();
    assert_eq!(retry.id, row.id);
    assert_eq!(retry.revision, 2);
    assert_eq!(retry.state, "approved");
    let json = serde_json::to_string(&retry).unwrap();
    for secret in ["request_hash", "registered_by", "token_hash"] {
        assert!(!json.contains(secret));
    }
    resumed.close().await;
    f.close().await;
}
