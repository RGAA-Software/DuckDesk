use jiff::Timestamp;
use px_update_authority::{
    create_initial_root, generate_signing_key, publish_repository, RepositoryPublication,
    RootCreation,
};
use std::env;
use std::path::PathBuf;

#[tokio::main]
async fn main() {
    if let Err(error) = run().await {
        eprintln!("Update authority operation failed: {error}");
        std::process::exit(1);
    }
}

async fn run() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let arguments: Vec<String> = env::args().skip(1).collect();
    match arguments.as_slice() {
        [command] if command == "generate-key" => {
            generate_signing_key(&required_path("PIXELS_TUF_KEY_OUTPUT")?)
        }
        [command] if command == "create-root" => {
            create_initial_root(&RootCreation {
                root_signing_key_paths: required_path_array("PIXELS_TUF_ROOT_SIGNING_KEYS")?,
                root_signature_threshold: required("PIXELS_TUF_ROOT_THRESHOLD")?.parse()?,
                targets_signing_key_path: required_path("PIXELS_TUF_TARGETS_SIGNING_KEY")?,
                snapshot_signing_key_path: required_path("PIXELS_TUF_SNAPSHOT_SIGNING_KEY")?,
                timestamp_signing_key_path: required_path("PIXELS_TUF_TIMESTAMP_SIGNING_KEY")?,
                version: required("PIXELS_TUF_ROOT_VERSION")?.parse()?,
                expires_at: required_timestamp("PIXELS_TUF_ROOT_EXPIRES_AT")?,
                output_path: required_path("PIXELS_TUF_ROOT_OUTPUT")?,
            })
            .await
        }
        [command] if command == "publish" => {
            publish_repository(&RepositoryPublication {
                root_path: required_path("PIXELS_TUF_ROOT_FILE")?,
                targets_signing_key_path: required_path("PIXELS_TUF_TARGETS_SIGNING_KEY")?,
                snapshot_signing_key_path: required_path("PIXELS_TUF_SNAPSHOT_SIGNING_KEY")?,
                timestamp_signing_key_path: required_path("PIXELS_TUF_TIMESTAMP_SIGNING_KEY")?,
                release_spec_path: required_path("PIXELS_RELEASE_SPEC_FILE")?,
                artifact_path: required_path("PIXELS_RELEASE_ARTIFACT")?,
                previous_repository_path: optional_path("PIXELS_TUF_PREVIOUS_REPOSITORY")?,
                output_path: required_path("PIXELS_TUF_REPOSITORY_OUTPUT")?,
                targets_expires_at: required_timestamp("PIXELS_TUF_TARGETS_EXPIRES_AT")?,
                snapshot_expires_at: required_timestamp("PIXELS_TUF_SNAPSHOT_EXPIRES_AT")?,
                timestamp_expires_at: required_timestamp("PIXELS_TUF_TIMESTAMP_EXPIRES_AT")?,
            })
            .await
        }
        _ => Err("usage: px_update_authority <generate-key|create-root|publish>; offline explicit provisioning only; configuration via environment".into()),
    }
}

fn required(name: &str) -> Result<String, Box<dyn std::error::Error + Send + Sync>> {
    let value = env::var(name)?;
    if value.is_empty() || value.trim() != value {
        return Err(format!("invalid {name}").into());
    }
    Ok(value)
}

fn required_path(name: &str) -> Result<PathBuf, Box<dyn std::error::Error + Send + Sync>> {
    Ok(PathBuf::from(required(name)?))
}

fn optional_path(name: &str) -> Result<Option<PathBuf>, Box<dyn std::error::Error + Send + Sync>> {
    match env::var(name) {
        Ok(value) if !value.is_empty() && value.trim() == value => Ok(Some(PathBuf::from(value))),
        Ok(_) => Err(format!("invalid {name}").into()),
        Err(env::VarError::NotPresent) => Ok(None),
        Err(error) => Err(error.into()),
    }
}

fn required_path_array(
    name: &str,
) -> Result<Vec<PathBuf>, Box<dyn std::error::Error + Send + Sync>> {
    let values: Vec<String> = serde_json::from_str(&required(name)?)?;
    if values
        .iter()
        .any(|value| value.is_empty() || value.trim() != value)
    {
        return Err(format!("invalid {name}").into());
    }
    Ok(values.into_iter().map(PathBuf::from).collect())
}

fn required_timestamp(name: &str) -> Result<Timestamp, Box<dyn std::error::Error + Send + Sync>> {
    Ok(required(name)?.parse()?)
}
