use px_pg::{migrate, readiness, DatabaseConfig, DatabaseError, Service, Transport};
use std::env;
use uuid::Uuid;

#[path = "../catalog.rs"]
mod catalog;

async fn run() -> Result<(), DatabaseError> {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.len() != 2 || !matches!(args[0].as_str(), "migrate" | "check") {
        eprintln!("usage: px_db <migrate|check> <console|auth|desk>; credentials via PIXELS_DATABASE_URL only");
        return Err(DatabaseError::Configuration);
    }
    let service = Service::parse(&args[1])?;
    let dsn = env::var("PIXELS_DATABASE_URL").map_err(|_| DatabaseError::Configuration)?;
    let deployment = env::var("PIXELS_DEPLOYMENT_ID")
        .ok()
        .and_then(|deployment_text| Uuid::parse_str(&deployment_text).ok())
        .ok_or(DatabaseError::Configuration)?;
    let transport = match env::var("PIXELS_PG_LOCAL_DEVELOPMENT").as_deref() {
        Ok("1") => Transport::LocalDevelopment,
        Ok("0") | Err(_) => Transport::VerifyFull,
        _ => return Err(DatabaseError::Configuration),
    };
    let config = DatabaseConfig::parse(&dsn, transport)?;
    if args[0] == "migrate" {
        migrate(&config, service, deployment, catalog::migrations(service)).await?;
    }
    let pool = config.connect().await?;
    let result = readiness(&pool, service, deployment, catalog::migrations(service)).await;
    pool.close().await;
    result?;
    println!("READY service={}", service.name());
    Ok(())
}

#[tokio::main]
async fn main() {
    if let Err(error) = run().await {
        eprintln!("NOT_READY: {error}");
        std::process::exit(1);
    }
}
