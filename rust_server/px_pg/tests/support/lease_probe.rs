//! Test-only independent process. No production fault or lease endpoint.
use px_pg::{DatabaseConfig, Service, ServiceLease, Transport};
use std::{env, io::Write, time::Duration};
#[tokio::main]
async fn main() {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let config = DatabaseConfig::parse(
        &env::var("PIXELS_TEST_CONSOLE_RUNTIME_URL").unwrap(),
        Transport::LocalDevelopment,
    )
    .unwrap();
    let deployment = env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap();
    let mut lease = ServiceLease::acquire(&config, Service::Console, deployment)
        .await
        .unwrap();
    println!("LEASED");
    std::io::stdout().flush().unwrap();
    loop {
        tokio::time::sleep(Duration::from_millis(500)).await;
        lease.renew().await.unwrap();
    }
}
