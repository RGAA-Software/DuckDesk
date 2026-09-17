use px_backup::{
    BackupError, BackupPlan, BackupRepository, BackupRunner, BackupService, BackupTarget,
    DatabaseTarget, LogicalBackupTool, RecoverySetKind, RetentionClass,
};
use std::{
    collections::BTreeSet,
    env,
    fs::{self, File, OpenOptions},
    io::Write,
    path::{Path, PathBuf},
    process::{Command, Stdio},
};
use uuid::Uuid;

#[derive(Clone)]
struct DockerPgTool {
    container: String,
}

impl DockerPgTool {
    fn new(container: String) -> Self {
        assert!(valid_container_id(&container));
        Self { container }
    }

    fn docker(&self) -> Command {
        let mut command = Command::new("docker");
        command.args(["exec", "-i", &self.container]);
        command
    }

    fn restore(&self, archive: &Path, database: &str) {
        let input = File::open(archive).unwrap();
        let status = self
            .docker()
            .args([
                "pg_restore",
                "--exit-on-error",
                "--no-password",
                "--username",
                "pixels_admin",
                "--dbname",
                database,
            ])
            .stdin(Stdio::from(input))
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .unwrap();
        assert!(status.success());
    }

    fn output(&self, arguments: &[&str]) -> String {
        let output = self
            .docker()
            .args(arguments)
            .stdin(Stdio::null())
            .output()
            .unwrap();
        assert!(output.status.success());
        String::from_utf8(output.stdout).unwrap().trim().to_string()
    }
}

impl LogicalBackupTool for DockerPgTool {
    fn dump(&self, target: &DatabaseTarget, destination: &Path) -> Result<(), BackupError> {
        let output = OpenOptions::new()
            .write(true)
            .truncate(true)
            .open(destination)
            .map_err(|_| BackupError::ArchiveFailed)?;
        let status = self
            .docker()
            .args([
                "pg_dump",
                "--format=custom",
                "--no-password",
                "--username",
                "pixels_admin",
                "--dbname",
                &target.database,
            ])
            .stdin(Stdio::null())
            .stdout(Stdio::from(output))
            .stderr(Stdio::null())
            .status()
            .map_err(|_| BackupError::ArchiveFailed)?;
        if status.success() {
            Ok(())
        } else {
            Err(BackupError::ArchiveFailed)
        }
    }

    fn verify(&self, archive: &Path) -> Result<(), BackupError> {
        let input = File::open(archive).map_err(|_| BackupError::VerificationFailed)?;
        let status = self
            .docker()
            .args(["pg_restore", "--list"])
            .stdin(Stdio::from(input))
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .map_err(|_| BackupError::VerificationFailed)?;
        if status.success() {
            Ok(())
        } else {
            Err(BackupError::VerificationFailed)
        }
    }
}

struct RestoredDatabases<'a> {
    tool: &'a DockerPgTool,
    names: Vec<String>,
}

impl<'a> RestoredDatabases<'a> {
    fn new(tool: &'a DockerPgTool) -> Self {
        Self {
            tool,
            names: Vec::new(),
        }
    }

    fn create(&mut self, name: String) {
        let status = self
            .tool
            .docker()
            .args([
                "createdb",
                "--username",
                "pixels_admin",
                "--template",
                "template0",
                &name,
            ])
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .unwrap();
        assert!(status.success());
        self.names.push(name);
    }
}

impl Drop for RestoredDatabases<'_> {
    fn drop(&mut self) {
        for name in &self.names {
            let _ = self
                .tool
                .docker()
                .args(["dropdb", "--force", "--username", "pixels_admin", name])
                .stdin(Stdio::null())
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
        }
    }
}

struct Fixture {
    _base: tempfile::TempDir,
    root: PathBuf,
}

impl Fixture {
    fn new() -> Self {
        let base = tempfile::Builder::new()
            .prefix("pixels-backup-postgres-")
            .tempdir()
            .unwrap();
        make_private(base.path());
        let root = base.path().join("sets");
        fs::create_dir(&root).unwrap();
        make_private(&root);
        Self { _base: base, root }
    }
}

fn make_private(path: &Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(path, fs::Permissions::from_mode(0o700)).unwrap();
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        let identity = Command::new("whoami")
            .creation_flags(0x08000000)
            .output()
            .unwrap();
        assert!(identity.status.success());
        let grant = format!(
            "{}:(OI)(CI)F",
            String::from_utf8(identity.stdout).unwrap().trim()
        );
        let result = Command::new("icacls")
            .arg(path)
            .args(["/inheritance:r", "/grant:r", &grant, "*S-1-5-18:(OI)(CI)F"])
            .creation_flags(0x08000000)
            .output()
            .unwrap();
        assert!(result.status.success());
    }
}

fn target(fixture: &Fixture, service: BackupService, database: &str) -> BackupTarget {
    BackupTarget::Required {
        database: DatabaseTarget {
            service,
            host: "127.0.0.1".into(),
            port: 5432,
            database: database.into(),
            username: "pixels_admin".into(),
            password_file: fixture.root.join("unused-test-adapter.pgpass"),
            schema_version: 1,
        },
    }
}

fn valid_container_id(value: &str) -> bool {
    (12..=64).contains(&value.len())
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

#[test]
fn real_three_database_archives_publish_restore_and_detect_tampering() {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let container = env::var("PIXELS_TEST_CONTAINER").unwrap();
    let deployment_id = env::var("PIXELS_DEPLOYMENT_ID")
        .unwrap()
        .parse::<Uuid>()
        .unwrap();
    let fixture = Fixture::new();
    let repository = BackupRepository::open(&fixture.root, deployment_id).unwrap();
    let tool = DockerPgTool::new(container);
    let plan = BackupPlan {
        deployment_id,
        kind: RecoverySetKind::Independent,
        retention: BTreeSet::from([RetentionClass::Hourly]),
        previous_recovery_set_id: None,
        targets: vec![
            target(&fixture, BackupService::Console, "pixels_console"),
            target(&fixture, BackupService::Auth, "pixels_auth"),
            target(&fixture, BackupService::Desk, "pixels_desk"),
        ],
    };

    let manifest = BackupRunner::new(tool.clone())
        .run(&repository, &plan)
        .unwrap();
    assert_eq!(repository.manifests().unwrap(), vec![manifest.clone()]);
    let set_directory = fixture.root.join(manifest.recovery_set_id.to_string());
    let suffix = Uuid::new_v4().simple().to_string();
    let mut restored = RestoredDatabases::new(&tool);
    for (service, archive) in [
        ("console", "console.dump"),
        ("auth", "auth.dump"),
        ("desk", "desk.dump"),
    ] {
        let database = format!("pixels_restore_{service}_{suffix}");
        restored.create(database.clone());
        tool.restore(&set_directory.join(archive), &database);
        let marker = tool.output(&[
            "psql",
            "-X",
            "--tuples-only",
            "--no-align",
            "--username",
            "pixels_admin",
            "--dbname",
            &database,
            "--command",
            "SELECT service || ':' || deployment_id::text FROM pixels.deployment_identity",
        ]);
        assert_eq!(marker, format!("{service}:{deployment_id}"));
    }

    let console_archive = set_directory.join("console.dump");
    OpenOptions::new()
        .append(true)
        .open(console_archive)
        .unwrap()
        .write_all(b"tampered")
        .unwrap();
    assert!(repository.manifests().is_err());
}
