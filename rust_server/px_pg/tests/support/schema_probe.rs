//! Isolated acceptance child; never a packaged service or production endpoint.
use px_pg::{DatabaseConfig, Service, Transport};
use std::env;
#[path = "../../src/catalog.rs"]
mod catalog;

#[tokio::main]
async fn main() {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let config = DatabaseConfig::parse(
        &env::var("PIXELS_TEST_DESK_RUNTIME_URL").unwrap(),
        Transport::LocalDevelopment,
    )
    .unwrap();
    let deployment = env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap();
    let pool = config
        .connect_runtime(
            Service::Desk,
            deployment,
            catalog::migrations(Service::Desk),
        )
        .await
        .unwrap();
    let mut connection = pool.acquire().await.unwrap();
    // Observable ready boundary on the exact pinned connection, not a startup sleep.
    sqlx::query("SELECT pg_advisory_lock(912340569)")
        .execute(&mut *connection)
        .await
        .unwrap();
    std::future::pending::<()>().await;
}
