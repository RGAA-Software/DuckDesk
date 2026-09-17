//! Test-only process, excluded from normal builds and not a production fault endpoint.
use px_pg::{DatabaseConfig, Service, Transport};
use sqlx::migrate::{Migration, MigrationType, Migrator};
use std::{borrow::Cow, env};
use uuid::Uuid;

#[path = "../../src/catalog.rs"]
mod catalog;

#[tokio::main]
async fn main() {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let config = DatabaseConfig::parse(
        &env::var("PIXELS_TEST_DESK_OWNER_URL").expect("isolated owner required"),
        Transport::LocalDevelopment,
    )
    .unwrap();
    let deployment = Uuid::parse_str(&env::var("PIXELS_DEPLOYMENT_ID").unwrap()).unwrap();
    let mut migrations: Vec<_> = catalog::migrations(Service::Desk).iter().cloned().collect();
    let probe_version = migrations.iter().map(|m| m.version).max().unwrap() + 1;
    migrations.push(Migration::new(
        probe_version, Cow::Borrowed("isolated crash probe"), MigrationType::Simple,
        Cow::Borrowed("CREATE TABLE pixels.crash_probe(id INTEGER PRIMARY KEY); SELECT pg_advisory_xact_lock(912340567); INSERT INTO pixels.crash_probe VALUES(1);"), false,
    ));
    let migrator = Migrator {
        migrations: Cow::Owned(migrations),
        ..Migrator::DEFAULT
    };
    match px_pg::migrate(&config, Service::Desk, deployment, &migrator).await {
        Ok(()) => println!("COMMITTED"),
        Err(error) => {
            eprintln!("{error}");
            std::process::exit(1);
        }
    }
}
