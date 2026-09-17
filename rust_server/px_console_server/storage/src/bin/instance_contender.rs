//! Test-only independent reservation process. No product startup or migration behavior.
use px_console_store::{
    ClientType, InstanceStore, ResourceCredential, StartApplication, StoreError, TokenDigest,
};
use px_pg::{DatabaseConfig, Transport};
use std::io::Read;

#[derive(serde::Deserialize)]
#[serde(deny_unknown_fields)]
struct Input {
    token_hash: [u8; 32],
    request: StartApplication,
}

#[tokio::main]
async fn main() {
    let code = match run().await {
        Ok(()) => 0,
        Err(()) => {
            eprintln!("isolated reservation failed");
            1
        }
    };
    std::process::exit(code);
}
async fn run() -> Result<(), ()> {
    if std::env::var("PIXELS_PG_ISOLATED_TEST").as_deref() != Ok("1") {
        return Err(());
    }
    let mut input = String::new();
    std::io::stdin()
        .take(8193)
        .read_to_string(&mut input)
        .map_err(|_| ())?;
    if input.len() > 8192 {
        return Err(());
    }
    let input: Input = serde_json::from_str(&input).map_err(|_| ())?;
    let config = DatabaseConfig::parse(
        &std::env::var("PIXELS_TEST_CONSOLE_RUNTIME_URL").map_err(|_| ())?,
        Transport::LocalDevelopment,
    )
    .map_err(|_| ())?;
    let deployment = std::env::var("PIXELS_DEPLOYMENT_ID")
        .map_err(|_| ())?
        .parse()
        .map_err(|_| ())?;
    let store = InstanceStore::connect(&config, deployment)
        .await
        .map_err(|_| ())?;
    let key = TokenDigest::from_sha256(input.token_hash);
    let outcome = store
        .reserve_in_isolated_test_runtime(
            ResourceCredential::User(&key),
            ClientType::Android,
            &input.request,
        )
        .await;
    store.close().await;
    match outcome {
        Ok(instance) => println!("RESERVED {}", instance.id),
        Err(StoreError::NoCapacity) => println!("NO_CAPACITY"),
        Err(_) => return Err(()),
    }
    Ok(())
}
