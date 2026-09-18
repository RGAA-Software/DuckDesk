use px_pg::{DatabaseConfig, Transport};
use std::{
    env,
    path::{Path, PathBuf},
    process::{Command, Stdio},
};
use tempfile::TempDir;
use uuid::Uuid;

struct PrivateDirectory {
    _temporary: TempDir,
    path: PathBuf,
}

impl PrivateDirectory {
    fn new() -> Self {
        let temporary = tempfile::tempdir().unwrap();
        restrict_private_directory(temporary.path());
        Self {
            path: temporary.path().to_path_buf(),
            _temporary: temporary,
        }
    }
}

fn restrict_private_directory(path: &Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o700)).unwrap();
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        let current_identity = Command::new("whoami")
            .creation_flags(0x08000000)
            .output()
            .unwrap();
        assert!(current_identity.status.success());
        let identity_access = format!(
            "{}:(OI)(CI)F",
            String::from_utf8(current_identity.stdout).unwrap().trim()
        );
        let access_result = Command::new("icacls")
            .arg(path)
            .args([
                "/inheritance:r",
                "/grant:r",
                &identity_access,
                "*S-1-5-18:(OI)(CI)F",
            ])
            .creation_flags(0x08000000)
            .output()
            .unwrap();
        assert!(access_result.status.success());
    }
}

fn database_url(role: &str) -> String {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let mut url =
        url::Url::parse(&env::var(format!("PIXELS_TEST_CONSOLE_{role}_URL")).unwrap()).unwrap();
    let platform = if cfg!(windows) { "windows" } else { "linux" };
    url.set_path(&format!("/pixels_console_admin_{platform}"));
    url.to_string()
}

fn admin_command(role: &str, password_path: &Path) -> Command {
    let mut command = Command::new(env!("CARGO_BIN_EXE_px_console_admin"));
    command
        .arg("bootstrap")
        .env("PIXELS_DATABASE_URL", database_url(role))
        .env("PIXELS_CONSOLE_LOCAL_DEVELOPMENT", "1")
        .env("PIXELS_CONSOLE_INITIAL_USERNAME", "bootstrap-admin")
        .env("PIXELS_CONSOLE_INITIAL_PASSWORD_FILE", password_path)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        command.creation_flags(0x08000000);
    }
    command
}

#[test]
fn explicit_secret_generation_is_private_distinct_and_never_overwrites() {
    let private_directory = PrivateDirectory::new();
    let guest_source_path = private_directory.path.join("guest-source.key");
    let workspace_key_path = private_directory.path.join("workspace.key");
    let workspace_key_id = Uuid::new_v4();
    let command = || {
        let mut command = Command::new(env!("CARGO_BIN_EXE_px_console_admin"));
        command
            .arg("generate-secrets")
            .env("PIXELS_CONSOLE_GUEST_SOURCE_KEY", &guest_source_path)
            .env("PIXELS_CONSOLE_WORKSPACE_KEY", &workspace_key_path)
            .env(
                "PIXELS_CONSOLE_WORKSPACE_KEY_ID",
                workspace_key_id.to_string(),
            )
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000);
        }
        command
    };
    let first = command().output().unwrap();
    assert!(first.status.success());
    let guest_source_key = px_private_files::private::read_private(&guest_source_path).unwrap();
    let workspace_key = px_private_files::private::read_private(&workspace_key_path).unwrap();
    assert_eq!(guest_source_key.len(), 32);
    assert_eq!(workspace_key.len(), 32);
    assert_ne!(guest_source_key, workspace_key);
    assert!(String::from_utf8(first.stdout)
        .unwrap()
        .contains(&workspace_key_id.to_string()));
    assert!(!command().output().unwrap().status.success());
    assert_eq!(
        px_private_files::private::read_private(&guest_source_path).unwrap(),
        guest_source_key
    );
    assert_eq!(
        px_private_files::private::read_private(&workspace_key_path).unwrap(),
        workspace_key
    );
}

#[test]
fn recording_cache_initialization_is_explicit_deployment_bound_and_never_overwrites() {
    let private_directory = PrivateDirectory::new();
    let cache_directory = private_directory.path.join("recording-cache");
    std::fs::create_dir(&cache_directory).unwrap();
    let deployment = Uuid::new_v4();
    let command = || {
        let mut command = Command::new(env!("CARGO_BIN_EXE_px_console_admin"));
        command
            .arg("initialize-recording-cache")
            .env("PIXELS_DEPLOYMENT_ID", deployment.to_string())
            .env("PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY", &cache_directory)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000);
        }
        command
    };
    assert!(command().output().unwrap().status.success());
    assert!(px_private_files::CacheRoot::open(&cache_directory, deployment).is_ok());
    assert!(!command().output().unwrap().status.success());
    assert!(px_private_files::CacheRoot::open(&cache_directory, Uuid::new_v4()).is_err());
}

#[tokio::test]
async fn bootstrap_is_owner_only_empty_only_and_concurrent_safe() {
    let private_directory = PrivateDirectory::new();
    let password_path = private_directory.path.join("password.txt");
    px_private_files::private::create_private(&password_path, b"synthetic-bootstrap-password")
        .unwrap();
    assert!(!admin_command("RUNTIME", &password_path)
        .output()
        .unwrap()
        .status
        .success());
    let first = admin_command("OWNER", &password_path).spawn().unwrap();
    let second = admin_command("OWNER", &password_path).spawn().unwrap();
    let first_output = first.wait_with_output().unwrap();
    let second_output = second.wait_with_output().unwrap();
    assert_eq!(
        usize::from(first_output.status.success()) + usize::from(second_output.status.success()),
        1
    );
    let runtime_config =
        DatabaseConfig::parse(&database_url("RUNTIME"), Transport::LocalDevelopment).unwrap();
    let runtime_pool = runtime_config.connect().await.unwrap();
    let administrator_count: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels.users WHERE role='admin' AND deleted_at IS NULL",
    )
    .fetch_one(&runtime_pool)
    .await
    .unwrap();
    assert_eq!(administrator_count, 1);
    runtime_pool.close().await;
    assert!(!admin_command("OWNER", &password_path)
        .output()
        .unwrap()
        .status
        .success());
}
