use px_pg::{DatabaseConfig, DatabaseError, Service, ServiceLease, Transport};
use std::{env, time::Duration};
use uuid::Uuid;
fn deployment() -> Uuid {
    env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap()
}
fn config(role: &str) -> DatabaseConfig {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    DatabaseConfig::parse(
        &env::var(format!("PIXELS_TEST_CONSOLE_{role}_URL")).unwrap(),
        Transport::LocalDevelopment,
    )
    .unwrap()
}
#[tokio::test]
async fn competing_active_processes_have_one_dedicated_session_lock() {
    let mut jobs = tokio::task::JoinSet::new();
    let barrier = std::sync::Arc::new(tokio::sync::Barrier::new(20));
    for _ in 0..20 {
        let barrier = barrier.clone();
        jobs.spawn(async move {
            barrier.wait().await;
            ServiceLease::acquire(&config("RUNTIME"), Service::Console, deployment()).await
        });
    }
    let mut winners = Vec::new();
    while let Some(result) = jobs.join_next().await {
        match result.unwrap() {
            Ok(lease) => winners.push(lease),
            Err(e) => assert_eq!(e, DatabaseError::Conflict),
        }
    }
    assert_eq!(winners.len(), 1);
    winners[0].renew().await.unwrap();
    let status = winners[0].status();
    status.check().unwrap();
    drop(winners);
    assert_eq!(status.check(), Err(DatabaseError::Unavailable));
    ServiceLease::acquire(&config("RUNTIME"), Service::Console, deployment())
        .await
        .unwrap();
}
#[tokio::test]
async fn wrong_owner_deployment_and_service_cannot_acquire_or_poison_the_lock() {
    assert!(matches!(
        ServiceLease::acquire(&config("OWNER"), Service::Console, deployment()).await,
        Err(DatabaseError::Permission)
    ));
    assert!(matches!(
        ServiceLease::acquire(&config("RUNTIME"), Service::Console, Uuid::new_v4()).await,
        Err(DatabaseError::Identity)
    ));
    assert!(matches!(
        ServiceLease::acquire(&config("RUNTIME"), Service::Desk, deployment()).await,
        Err(DatabaseError::Identity)
    ));
    let mut lease = ServiceLease::acquire(&config("RUNTIME"), Service::Console, deployment())
        .await
        .unwrap();
    lease.renew().await.unwrap();
}
#[tokio::test]
async fn expired_status_is_terminal_and_renewal_never_resurrects_it() {
    let mut lease = ServiceLease::acquire(&config("RUNTIME"), Service::Console, deployment())
        .await
        .unwrap();
    let status = lease.status();
    tokio::time::sleep(Duration::from_millis(5100)).await;
    assert_eq!(status.check(), Err(DatabaseError::Unavailable));
    assert_eq!(lease.renew().await, Err(DatabaseError::Unavailable));
    assert!(matches!(
        ServiceLease::acquire(&config("RUNTIME"), Service::Console, deployment()).await,
        Err(DatabaseError::Conflict)
    ));
    drop(lease);
    let next = ServiceLease::acquire(&config("RUNTIME"), Service::Console, deployment())
        .await
        .unwrap();
    next.status().check().unwrap();
    assert_eq!(status.check(), Err(DatabaseError::Unavailable));
}
#[tokio::test]
async fn backend_termination_fails_closed_and_never_reacquires_in_place() {
    let mut lease = ServiceLease::acquire(&config("RUNTIME"), Service::Console, deployment())
        .await
        .unwrap();
    let status = lease.status();
    let killer = config("RUNTIME").connect().await.unwrap();
    let killed: bool = sqlx::query_scalar("SELECT pg_terminate_backend($1,1000)")
        .bind(lease.test_backend_pid())
        .fetch_one(&killer)
        .await
        .unwrap();
    assert!(killed);
    // Do not renew/check the old status before takeover: this exercises its cached window.
    let started = std::time::Instant::now();
    let mut next = ServiceLease::acquire(&config("RUNTIME"), Service::Console, deployment())
        .await
        .unwrap();
    assert!(started.elapsed() >= Duration::from_secs(5));
    assert!(status.check().is_err());
    next.renew().await.unwrap();
    assert!(lease.renew().await.is_err());
    assert!(status.check().is_err());
    killer.close().await;
}
#[tokio::test]
async fn privilege_expansion_invalidates_lease_and_cannot_be_cured_by_late_renewal() {
    let mut lease = ServiceLease::acquire(&config("RUNTIME"), Service::Console, deployment())
        .await
        .unwrap();
    let owner = config("OWNER").connect().await.unwrap();
    sqlx::query("GRANT CREATE ON SCHEMA pixels TO pixels_console_runtime")
        .execute(&owner)
        .await
        .unwrap();
    let result = lease.renew().await;
    sqlx::query("REVOKE CREATE ON SCHEMA pixels FROM pixels_console_runtime")
        .execute(&owner)
        .await
        .unwrap();
    assert_eq!(result, Err(DatabaseError::Permission));
    assert!(lease.status().check().is_err());
    assert!(lease.renew().await.is_err());
    drop(lease);
    ServiceLease::acquire(&config("RUNTIME"), Service::Console, deployment())
        .await
        .unwrap();
    owner.close().await;
}
struct Child(std::process::Child);
impl Drop for Child {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}
#[tokio::test]
async fn an_independent_process_holds_the_lock_until_forced_os_exit() {
    use std::{
        io::{BufRead, BufReader},
        process::{Command, Stdio},
    };
    let mut child = Child(
        Command::new(env!("CARGO_BIN_EXE_px_pg_lease_probe"))
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()
            .unwrap(),
    );
    let out = child.0.stdout.take().unwrap();
    let (sender, receiver) = std::sync::mpsc::channel();
    let reader = std::thread::spawn(move || {
        let _ = sender.send(BufReader::new(out).lines().next());
    });
    let line = receiver
        .recv_timeout(Duration::from_secs(10))
        .unwrap()
        .unwrap()
        .unwrap();
    reader.join().unwrap();
    assert_eq!(line, "LEASED");
    assert!(matches!(
        ServiceLease::acquire(&config("RUNTIME"), Service::Console, deployment()).await,
        Err(DatabaseError::Conflict)
    ));
    drop(child);
    let deadline = std::time::Instant::now() + Duration::from_secs(3);
    loop {
        match ServiceLease::acquire(&config("RUNTIME"), Service::Console, deployment()).await {
            Ok(lease) => {
                lease.status().check().unwrap();
                break;
            }
            Err(DatabaseError::Conflict) => assert!(std::time::Instant::now() < deadline),
            Err(e) => panic!("unexpected {e:?}"),
        }
        tokio::time::sleep(Duration::from_millis(10)).await;
    }
}
