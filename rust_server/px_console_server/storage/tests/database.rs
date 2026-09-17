#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, Fixture};
use px_console_store::{ConsoleDatabase, StoreError, WorkspaceKey, WorkspaceVault};
use std::{
    env,
    sync::Arc,
    time::{Duration, Instant},
};
use uuid::Uuid;
use zeroize::Zeroizing;
fn vault() -> Arc<WorkspaceVault> {
    let id = Uuid::new_v4();
    Arc::new(
        WorkspaceVault::new(
            id,
            vec![WorkspaceKey {
                id,
                bytes: Zeroizing::new([61; 32]),
            }],
        )
        .unwrap(),
    )
}
fn deployment() -> Uuid {
    env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap()
}

#[tokio::test]
async fn all_repository_handles_share_one_bounded_pool_and_owner_shutdown() {
    let f = Fixture::new().await;
    let admin = f.admin.clone();
    let user = f
        .session("user", px_console_store::ClientType::Android)
        .await;
    f.close().await;
    let owner = config("OWNER").connect().await.unwrap();
    let database = Arc::new(
        ConsoleDatabase::connect(
            &config("RUNTIME")
                .with_pool_limits(2, Duration::from_secs(5))
                .unwrap(),
            deployment(),
            vault(),
        )
        .await
        .unwrap(),
    );
    database.ready().await.unwrap();
    let mut blocker = owner.begin().await.unwrap();
    sqlx::query("SELECT pg_advisory_xact_lock(22091401)")
        .execute(&mut *blocker)
        .await
        .unwrap();
    let mut tasks = tokio::task::JoinSet::new();
    for domain in 0..13 {
        let database = database.clone();
        let admin = admin.clone();
        let user = user.clone();
        tasks.spawn(async move {
            match domain {
                0 => database
                    .control()
                    .list_users(&admin, None, 1)
                    .await
                    .map(|_| ()),
                1 => database
                    .devices()
                    .list_managed(&admin, None, 1)
                    .await
                    .map(|_| ()),
                2 => database
                    .applications()
                    .list_managed(&admin, None, 1)
                    .await
                    .map(|_| ()),
                3 => database
                    .nodes()
                    .list_managed(&admin, None, 1)
                    .await
                    .map(|_| ()),
                4 => database
                    .deployments()
                    .list_managed(&admin, None, 1)
                    .await
                    .map(|_| ()),
                5 => database
                    .workspaces()
                    .list_managed(&admin, None, 1)
                    .await
                    .map(|_| ()),
                6 => database
                    .resource_sessions()
                    .list_managed(&admin, None, 1)
                    .await
                    .map(|_| ()),
                7 => database
                    .file_transfers()
                    .list_managed(&admin, None, None, 1)
                    .await
                    .map(|_| ()),
                8 => database
                    .recordings()
                    .list_managed(&admin, None, None, 1)
                    .await
                    .map(|_| ()),
                9 => database
                    .saved_connections()
                    .list(&user, px_console_store::ClientType::Android, None, 1)
                    .await
                    .map(|_| ()),
                10 => database
                    .activity()
                    .visits_managed(&admin, None, 1)
                    .await
                    .map(|_| ()),
                11 => database
                    .updates()
                    .list_managed(&admin, None, 1)
                    .await
                    .map(|_| ()),
                _ => database
                    .groups()
                    .get(&admin, Uuid::new_v4())
                    .await
                    .map(|_| ()),
            }
        });
    }
    let deadline = Instant::now() + Duration::from_secs(4);
    loop {
        // pg_stat_activity hides another role's wait_event from an ordinary owner.
        // pg_locks exposes lock identities without granting pg_read_all_stats.
        let waiting:i64=sqlx::query_scalar("SELECT count(*) FROM pg_locks WHERE locktype='advisory' AND NOT granted AND classid=0 AND objid=22091401 AND database=(SELECT oid FROM pg_database WHERE datname=current_database())")
            .fetch_one(&owner).await.unwrap();
        if waiting >= 2 {
            break;
        }
        assert!(
            Instant::now() < deadline,
            "shared pool never reached two waiting operations"
        );
        tokio::time::sleep(Duration::from_millis(10)).await;
    }
    let connections:i64=sqlx::query_scalar("SELECT count(*) FROM pg_stat_activity WHERE datname=current_database() AND usename='pixels_console_runtime'")
        .fetch_one(&owner).await.unwrap();
    assert_eq!(connections, 2);
    assert_eq!(database.pool_status().connections, 2);
    blocker.rollback().await.unwrap();
    let mut successful = 0;
    while let Some(result) = tasks.join_next().await {
        match result.unwrap() {
            Ok(()) => successful += 1,
            Err(StoreError::Rejected) => {}
            other => panic!("unexpected shared-pool result: {other:?}"),
        }
    }
    assert_eq!(successful, 12); // nonexistent group is still an authorized read, then NotFound/Rejected
    let retained = database.control();
    let retained_sessions = database.resource_sessions();
    let retained_preferences = database.saved_connections();
    let retained_activity = database.activity();
    let retained_updates = database.updates();
    database.close().await;
    assert!(database.pool_status().closed);
    assert!(retained_updates
        .list_managed(&admin, None, 1)
        .await
        .is_err());
    assert!(retained.list_users(&admin, None, 1).await.is_err());
    assert!(retained_sessions
        .list_managed(&admin, None, 1)
        .await
        .is_err());
    assert!(database.ready().await.is_err());
    assert!(retained_activity
        .visits_managed(&admin, None, 1)
        .await
        .is_err());
    assert!(retained_preferences
        .list(&user, px_console_store::ClientType::Android, None, 1)
        .await
        .is_err());
    owner.close().await;
}
#[tokio::test]
async fn composition_refuses_wrong_deployment_or_owner_account() {
    assert!(
        ConsoleDatabase::connect(&config("RUNTIME"), Uuid::new_v4(), vault())
            .await
            .is_err()
    );
    assert!(matches!(
        ConsoleDatabase::connect(&config("OWNER"), deployment(), vault()).await,
        Err(StoreError::Database(px_pg::DatabaseError::Permission))
    ));
    let database = ConsoleDatabase::connect(&config("RUNTIME"), deployment(), vault())
        .await
        .unwrap();
    database.ready().await.unwrap();
    database.close().await;
}
