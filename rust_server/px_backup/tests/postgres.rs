use px_backup::{
    BackupError, BackupPlan, BackupRepository, BackupRunner, BackupService, BackupTarget,
    DatabaseTarget, LogicalBackupTool, RecoverySetKind, RestoreDatabaseTarget,
    RestoreExecutionPlan, RestoreExecutionReport, RestoreOperatorProvisionPlan, RetentionClass,
};
use serde::Serialize;
use sha2::{Digest, Sha256};
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

    fn execute(&self, arguments: &[&str]) {
        let status = self
            .docker()
            .args(arguments)
            .stdin(Stdio::null())
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

    fn register(&mut self, name: String) {
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

#[derive(Serialize)]
struct RestoreExecutionCommandFixture {
    schema_version: u32,
    repository_root: PathBuf,
    report_path: PathBuf,
    plan: RestoreExecutionPlan,
    createdb_path: PathBuf,
    createdb_sha256: String,
    pg_restore_path: PathBuf,
    pg_restore_sha256: String,
    psql_path: PathBuf,
    psql_sha256: String,
    command_timeout_seconds: u64,
}

#[derive(Serialize)]
struct RestoreProvisionCommandFixture {
    schema_version: u32,
    plan: RestoreOperatorProvisionPlan,
    psql_path: PathBuf,
    psql_sha256: String,
    command_timeout_seconds: u64,
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
            schema_version: expected_schema_version(service),
        },
    }
}

fn expected_schema_version(service: BackupService) -> u32 {
    match service {
        BackupService::Console => 22,
        BackupService::Auth => 3,
        BackupService::Desk => 2,
    }
}

fn valid_container_id(value: &str) -> bool {
    (12..=64).contains(&value.len())
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn service_name(service: BackupService) -> &'static str {
    match service {
        BackupService::Console => "console",
        BackupService::Auth => "auth",
        BackupService::Desk => "desk",
    }
}

fn isolated_database_name(target_environment_id: Uuid, service: BackupService) -> String {
    let environment_text = target_environment_id.simple().to_string();
    format!(
        "pixels_restore_{}_{}",
        &environment_text[..12],
        service_name(service)
    )
}

fn create_docker_tool_proxy(
    directory: &Path,
    container: &str,
    tool_name: &str,
    container_password_file: &str,
) -> PathBuf {
    #[cfg(windows)]
    let proxy_path = directory.join(format!("{tool_name}.cmd"));
    #[cfg(unix)]
    let proxy_path = directory.join(tool_name);
    let forwarded_restore_password = if container_password_file == "/tmp/pixels-admin.pgpass" {
        "-e PIXELS_RESTORE_OPERATOR_PASSWORD"
    } else {
        ""
    };
    #[cfg(windows)]
    let proxy_script = format!(
        "@echo off\r\necho {tool_name} %*>>\"{}\"\r\ndocker exec -i -e PGPASSFILE={container_password_file} {forwarded_restore_password} {container} {tool_name} %* 2>>\"{}\"\r\nexit /b %ERRORLEVEL%\r\n",
        directory.join("proxy.log").display(),
        directory.join("proxy.log").display()
    );
    #[cfg(unix)]
    let proxy_script = format!(
        "#!/bin/sh\nprintf '%s\\n' \"{tool_name} $*\" >> '{}'\nexec docker exec -i -e PGPASSFILE={container_password_file} {forwarded_restore_password} {container} {tool_name} \"$@\" 2>> '{}'\n",
        directory.join("proxy.log").display(),
        directory.join("proxy.log").display()
    );
    fs::write(&proxy_path, proxy_script).unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&proxy_path, fs::Permissions::from_mode(0o700)).unwrap();
    }
    proxy_path
}

fn file_sha256(path: &Path) -> String {
    format!("{:x}", Sha256::digest(fs::read(path).unwrap()))
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
    let tool = DockerPgTool::new(container.clone());
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
    let admin_password = env::var("PIXELS_TEST_PG_ADMIN_PASSWORD").unwrap();
    assert_eq!(admin_password.len(), 64);
    assert!(admin_password
        .bytes()
        .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte)));
    let mut restore_password = "restore_Test-Password_0123456789abcdef";
    let admin_container_password_command = format!(
        "umask 077; printf '%s\\n' '127.0.0.1:5432:*:pixels_admin:{admin_password}' > /tmp/pixels-admin.pgpass"
    );
    tool.execute(&["sh", "-c", &admin_container_password_command]);
    let restore_container_password_command = format!(
        "umask 077; printf '%s\\n' '127.0.0.1:5432:*:pixels_restore_operator:{restore_password}' > /tmp/pixels-restore.pgpass"
    );
    tool.execute(&["sh", "-c", &restore_container_password_command]);
    let admin_password_file = fixture._base.path().join("admin.pgpass");
    px_private_files::private::create_private(
        &admin_password_file,
        format!("127.0.0.1:5432:*:pixels_admin:{admin_password}\n").as_bytes(),
    )
    .unwrap();
    let restore_password_file = fixture._base.path().join("restore-password.secret");
    px_private_files::private::create_private(&restore_password_file, restore_password.as_bytes())
        .unwrap();
    let provision_tool_directory = fixture._base.path().join("provision-tools");
    fs::create_dir(&provision_tool_directory).unwrap();
    make_private(&provision_tool_directory);
    let provision_psql = create_docker_tool_proxy(
        &provision_tool_directory,
        &container,
        "psql",
        "/tmp/pixels-admin.pgpass",
    );
    let mut provision_config = RestoreProvisionCommandFixture {
        schema_version: 1,
        plan: RestoreOperatorProvisionPlan {
            host: "127.0.0.1".to_string(),
            port: 5432,
            admin_username: "pixels_admin".to_string(),
            admin_password_file,
            restore_password_file,
            services: BTreeSet::from([
                BackupService::Console,
                BackupService::Auth,
                BackupService::Desk,
            ]),
        },
        psql_path: provision_psql.clone(),
        psql_sha256: file_sha256(&provision_psql),
        command_timeout_seconds: 30,
    };
    let provision_config_path = fixture._base.path().join("restore-provision-config.json");
    px_private_files::private::create_private(
        &provision_config_path,
        &serde_json::to_vec(&provision_config).unwrap(),
    )
    .unwrap();
    let provision_output = Command::new(env!("CARGO_BIN_EXE_px_backup"))
        .args(["restore-provision", provision_config_path.to_str().unwrap()])
        .stdin(Stdio::null())
        .output()
        .unwrap();
    if !provision_output.status.success() {
        let proxy_log = fs::read_to_string(provision_tool_directory.join("proxy.log"))
            .unwrap_or_else(|_| "provision proxy log unavailable".to_string());
        panic!(
            "restore provision failed: status={} stdout={} stderr={}\n{proxy_log}",
            provision_output.status,
            String::from_utf8_lossy(&provision_output.stdout),
            String::from_utf8_lossy(&provision_output.stderr)
        );
    }
    tool.execute(&[
        "sh",
        "-c",
        "cp /tmp/pixels-restore.pgpass /tmp/pixels-old-restore.pgpass; chmod 600 /tmp/pixels-old-restore.pgpass",
    ]);
    restore_password = "restore_Rotated-Password_abcdef0123456789";
    let rotated_restore_password_file =
        fixture._base.path().join("rotated-restore-password.secret");
    px_private_files::private::create_private(
        &rotated_restore_password_file,
        restore_password.as_bytes(),
    )
    .unwrap();
    provision_config.plan.restore_password_file = rotated_restore_password_file;
    let rotated_provision_config_path = fixture
        ._base
        .path()
        .join("rotated-restore-provision-config.json");
    px_private_files::private::create_private(
        &rotated_provision_config_path,
        &serde_json::to_vec(&provision_config).unwrap(),
    )
    .unwrap();
    let rotated_provision_output = Command::new(env!("CARGO_BIN_EXE_px_backup"))
        .args([
            "restore-provision",
            rotated_provision_config_path.to_str().unwrap(),
        ])
        .stdin(Stdio::null())
        .output()
        .unwrap();
    assert!(
        rotated_provision_output.status.success(),
        "restore password rotation failed: {}",
        String::from_utf8_lossy(&rotated_provision_output.stderr)
    );
    let old_password_status = tool
        .docker()
        .args([
            "exec",
            "-i",
            "-e",
            "PGPASSFILE=/tmp/pixels-old-restore.pgpass",
            &container,
            "psql",
            "-X",
            "--no-password",
            "--host",
            "127.0.0.1",
            "--port",
            "5432",
            "--username",
            "pixels_restore_operator",
            "--dbname",
            "postgres",
            "--command",
            "SELECT 1",
        ])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .unwrap();
    assert!(!old_password_status.success());
    let rotated_container_password_command = format!(
        "umask 077; printf '%s\\n' '127.0.0.1:5432:*:pixels_restore_operator:{restore_password}' > /tmp/pixels-restore.pgpass"
    );
    tool.execute(&["sh", "-c", &rotated_container_password_command]);
    let tool_directory = fixture._base.path().join("restore-tools");
    fs::create_dir(&tool_directory).unwrap();
    make_private(&tool_directory);
    let createdb = create_docker_tool_proxy(
        &tool_directory,
        &container,
        "createdb",
        "/tmp/pixels-restore.pgpass",
    );
    let pg_restore = create_docker_tool_proxy(
        &tool_directory,
        &container,
        "pg_restore",
        "/tmp/pixels-restore.pgpass",
    );
    let psql = create_docker_tool_proxy(
        &tool_directory,
        &container,
        "psql",
        "/tmp/pixels-restore.pgpass",
    );
    let password_file = fixture._base.path().join("restore.pgpass");
    px_private_files::private::create_private(
        &password_file,
        format!("127.0.0.1:5432:*:pixels_restore_operator:{restore_password}\n").as_bytes(),
    )
    .unwrap();
    let target_environment_id = Uuid::new_v4();
    let targets = [
        BackupService::Console,
        BackupService::Auth,
        BackupService::Desk,
    ]
    .into_iter()
    .map(|service| RestoreDatabaseTarget {
        service,
        host: "127.0.0.1".to_string(),
        port: 5432,
        database: isolated_database_name(target_environment_id, service),
        owner: format!("pixels_{}_owner", service_name(service)),
        username: "pixels_restore_operator".to_string(),
        password_file: password_file.clone(),
    })
    .collect::<Vec<_>>();
    let mut restored = RestoredDatabases::new(&tool);
    for target in &targets {
        restored.register(target.database.clone());
    }
    let report_path = fixture._base.path().join("restore-report.json");
    let restore_config = RestoreExecutionCommandFixture {
        schema_version: 1,
        repository_root: fixture.root.clone(),
        report_path: report_path.clone(),
        plan: RestoreExecutionPlan {
            deployment_id,
            recovery_set_id: manifest.recovery_set_id,
            target_environment_id,
            targets: targets.clone(),
        },
        createdb_path: createdb.clone(),
        createdb_sha256: file_sha256(&createdb),
        pg_restore_path: pg_restore.clone(),
        pg_restore_sha256: file_sha256(&pg_restore),
        psql_path: psql.clone(),
        psql_sha256: file_sha256(&psql),
        command_timeout_seconds: 30,
    };
    let restore_config_path = fixture._base.path().join("restore-config.json");
    px_private_files::private::create_private(
        &restore_config_path,
        &serde_json::to_vec(&restore_config).unwrap(),
    )
    .unwrap();
    drop(repository);
    let restore_output = Command::new(env!("CARGO_BIN_EXE_px_backup"))
        .args(["restore-execute", restore_config_path.to_str().unwrap()])
        .stdin(Stdio::null())
        .output()
        .unwrap();
    if !restore_output.status.success() {
        let proxy_log = fs::read_to_string(tool_directory.join("proxy.log"))
            .unwrap_or_else(|_| "proxy log unavailable".to_string());
        let failed_target = &targets[0];
        let database_marker = tool.output(&[
            "psql",
            "-X",
            "--tuples-only",
            "--no-align",
            "--username",
            "pixels_admin",
            "--dbname",
            &failed_target.database,
            "--command",
            "SELECT identity.service || '|' || identity.deployment_id::text || '|' || current_database() || '|' || pg_catalog.pg_get_userbyid(database_record.datdba) || '|' || COUNT(migration.version)::text || '|' || COALESCE(BOOL_AND(migration.success), FALSE)::text FROM pixels.deployment_identity AS identity CROSS JOIN pg_catalog.pg_database AS database_record LEFT JOIN pixels._sqlx_migrations AS migration ON TRUE WHERE database_record.datname = current_database() GROUP BY identity.service, identity.deployment_id, database_record.datdba",
        ]);
        panic!(
            "production restore command failed: status={} stderr={} marker={database_marker}\n{proxy_log}",
            restore_output.status,
            String::from_utf8_lossy(&restore_output.stderr)
        );
    }
    let restore_report = serde_json::from_slice::<RestoreExecutionReport>(
        &px_private_files::private::read_private(&report_path).unwrap(),
    )
    .unwrap();
    assert!(restore_report.admission_required);
    assert_eq!(restore_report.members.len(), 3);
    for target in &targets {
        let runtime_role = format!("pixels_{}_runtime", service_name(target.service));
        let privilege_marker = tool.output(&[
            "psql",
            "-X",
            "--tuples-only",
            "--no-align",
            "--username",
            "pixels_admin",
            "--dbname",
            &target.database,
            "--command",
            &format!("SELECT has_schema_privilege('{runtime_role}','pixels','USAGE')"),
        ]);
        assert_eq!(privilege_marker, "t");
    }

    let console_archive = set_directory.join("console.dump");
    OpenOptions::new()
        .append(true)
        .open(console_archive)
        .unwrap()
        .write_all(b"tampered")
        .unwrap();
    let repository = BackupRepository::open(&fixture.root, deployment_id).unwrap();
    assert!(repository.manifests().is_err());
}
