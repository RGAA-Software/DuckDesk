use px_backup::{
    BackupError, BackupPlan, BackupRepository, BackupRunner, BackupService, BackupTarget,
    DatabaseTarget, ExternalRecoveryWitness, LogicalBackupTool, RecoverySealPlan,
    RecoverySealReport, RecoverySecurityEvidence, RecoverySetKind, RecoverySetStatus,
    RestoreAdmissionRecord, RestoreAdmissionState, RestoreDatabaseTarget, RestoreExecutionPlan,
    RestoreExecutionReport, RestoreOperationalCheck, RestoreOperatorProvisionPlan, RetentionClass,
    WriteBarrierCoordinatorPlan, WriteBarrierDatabaseTarget, WriteBarrierProof,
    RECOVERY_WITNESS_SCHEMA_VERSION,
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

#[derive(Serialize)]
struct WriteBarrierCommandFixture {
    schema_version: u32,
    plan: WriteBarrierCoordinatorPlan,
    psql_path: PathBuf,
    psql_sha256: String,
    command_timeout_seconds: u64,
}

#[derive(Serialize)]
struct RecoverySealCommandFixture {
    schema_version: u32,
    repository_root: PathBuf,
    plan: RecoverySealPlan,
    psql_path: PathBuf,
    psql_sha256: String,
    command_timeout_seconds: u64,
}

#[derive(Serialize)]
struct RestoreAdmissionCommandFixture {
    schema_version: u32,
    deployment_id: Uuid,
    recovery_set_id: Uuid,
    target_environment_id: Uuid,
    repository_root: PathBuf,
    admission_root: PathBuf,
    witness_path: PathBuf,
    recovery_seal_report_path: PathBuf,
    completed_checks: BTreeSet<RestoreOperationalCheck>,
}

#[derive(Serialize)]
struct RestoreApprovalCommandFixture {
    schema_version: u32,
    approval_id: Uuid,
    administrator_id: Uuid,
    expected_revision: u64,
    expected_evidence_sha256: String,
}

#[derive(Serialize)]
struct WitnessRecordCommandFixture {
    schema_version: u32,
    deployment_id: Uuid,
    recovery_set_id: Uuid,
    repository_root: PathBuf,
    witness_root: PathBuf,
    generation_transition: Option<serde_json::Value>,
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
        BackupService::Console => 26,
        BackupService::Auth => 4,
        BackupService::Desk => 3,
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

fn seed_recovery_security_records(tool: &DockerPgTool) {
    let password_salt = "A".repeat(22);
    let password_digest = "B".repeat(42);
    let password_hash =
        format!("$argon2id$v=19$m=19456,t=2,p=1${password_salt}${password_digest}A");
    assert_eq!(password_hash.len(), 97);
    let console_seed = format!(
        "INSERT INTO pixels.users(id,username,username_normalized,password_hash,role) VALUES('10000000-0000-0000-0000-000000000001','Recovery User','recovery user','{password_hash}','admin');\
         INSERT INTO pixels.login_sessions(id,user_id,token_hash,client_type,authorization_revision,expires_at,absolute_expires_at) VALUES('10000000-0000-0000-0000-000000000002','10000000-0000-0000-0000-000000000001',decode(repeat('11',32),'hex'),'admin_web',1,clock_timestamp()+interval '1 hour',clock_timestamp()+interval '2 hours');\
         INSERT INTO pixels.user_groups(id,name,name_normalized) VALUES('10000000-0000-0000-0000-000000000003','Recovery Group','recovery group');\
         INSERT INTO pixels.group_members(group_id,user_id) VALUES('10000000-0000-0000-0000-000000000003','10000000-0000-0000-0000-000000000001');\
         INSERT INTO pixels.devices(id,public_code,name,platform,enrollment_hash) VALUES('10000000-0000-0000-0000-000000000004','123456789012','Recovery Device','windows',decode(repeat('22',32),'hex'));\
         INSERT INTO pixels.user_devices(user_id,device_id) VALUES('10000000-0000-0000-0000-000000000001','10000000-0000-0000-0000-000000000004');\
         INSERT INTO pixels.group_device_grants(group_id,device_id) VALUES('10000000-0000-0000-0000-000000000003','10000000-0000-0000-0000-000000000004');\
         INSERT INTO pixels.applications(id,name,kind,access_mode,entry_url,bitrate_kbps,codec,allow_observer,allow_takeover,disabled) VALUES('10000000-0000-0000-0000-000000000005','Recovery App','webview','acl','https://example.test',1000,'h264',false,false,false);\
         INSERT INTO pixels.group_app_grants(group_id,application_id) VALUES('10000000-0000-0000-0000-000000000003','10000000-0000-0000-0000-000000000005');\
         INSERT INTO pixels.application_events(id,application_id,actor_id,revision,access_revision,kind) VALUES('10000000-0000-0000-0000-000000000006','10000000-0000-0000-0000-000000000005','10000000-0000-0000-0000-000000000001',1,1,'created');\
         INSERT INTO pixels.authorization_outbox(id,user_id,authorization_revision,reason) VALUES('10000000-0000-0000-0000-000000000007','10000000-0000-0000-0000-000000000001',1,'permissions_changed');\
         INSERT INTO pixels.nodes(id,device_id,product,credential_hash,max_instances) VALUES('10000000-0000-0000-0000-000000000008','10000000-0000-0000-0000-000000000004','cloud_node',decode(repeat('33',32),'hex'),4);"
    );
    tool.execute(&[
        "psql",
        "-X",
        "--username",
        "pixels_admin",
        "--dbname",
        "pixels_console",
        "--set=ON_ERROR_STOP=1",
        "--command",
        &console_seed,
    ]);
    let auth_seed = format!(
        "INSERT INTO pixels.authors(id,username_normalized,password_hash,role) VALUES('20000000-0000-0000-0000-000000000001','recovery-author','{password_hash}','admin');\
         INSERT INTO pixels.author_sessions(id,author_id,token_hash,authorization_revision,expires_at) VALUES('20000000-0000-0000-0000-000000000002','20000000-0000-0000-0000-000000000001',decode(repeat('44',32),'hex'),1,clock_timestamp()+interval '1 hour');\
         INSERT INTO pixels.customers(id,name,name_normalized,remark) VALUES('20000000-0000-0000-0000-000000000003','Recovery Customer','recovery customer','');\
         INSERT INTO pixels.licenses(id,customer_id,target_deployment,product,distribution,machine_sha256,revision,mode,not_before,expires_at,max_devices,max_sessions,features) VALUES('20000000-0000-0000-0000-000000000004','20000000-0000-0000-0000-000000000003','20000000-0000-0000-0000-000000000005','pixels_console','official',repeat('a',64),1,'licensed',clock_timestamp()-interval '1 hour',clock_timestamp()+interval '1 day',4,4,ARRAY['desktop']);\
         INSERT INTO pixels.license_requests(author_id,request_id,body_sha256) VALUES('20000000-0000-0000-0000-000000000001','20000000-0000-0000-0000-000000000006',decode(repeat('55',32),'hex'));"
    );
    tool.execute(&[
        "psql",
        "-X",
        "--username",
        "pixels_admin",
        "--dbname",
        "pixels_auth",
        "--set=ON_ERROR_STOP=1",
        "--command",
        &auth_seed,
    ]);
    tool.execute(&[
        "psql",
        "-X",
        "--username",
        "pixels_admin",
        "--dbname",
        "pixels_desk",
        "--set=ON_ERROR_STOP=1",
        "--command",
        "INSERT INTO pixels.admin_sessions(id,token_hash,credential_fingerprint,expires_at) VALUES('30000000-0000-0000-0000-000000000001',decode(repeat('66',32),'hex'),decode(repeat('77',32),'hex'),clock_timestamp()+interval '1 hour')",
    ]);
}

#[test]
fn real_three_database_archives_restore_from_offsite_after_source_loss_and_detect_tampering() {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let container = env::var("PIXELS_TEST_CONTAINER").unwrap();
    let deployment_id = env::var("PIXELS_DEPLOYMENT_ID")
        .unwrap()
        .parse::<Uuid>()
        .unwrap();
    let fixture = Fixture::new();
    let repository = BackupRepository::open(&fixture.root, deployment_id).unwrap();
    let tool = DockerPgTool::new(container.clone());
    let admin_password = env::var("PIXELS_TEST_PG_ADMIN_PASSWORD").unwrap();
    assert_eq!(admin_password.len(), 64);
    assert!(admin_password
        .bytes()
        .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte)));
    let admin_container_password_command = format!(
        "umask 077; printf '%s\\n' '127.0.0.1:5432:*:pixels_admin:{admin_password}' > /tmp/pixels-admin.pgpass"
    );
    tool.execute(&["sh", "-c", &admin_container_password_command]);
    let admin_password_file = fixture._base.path().join("admin.pgpass");
    px_private_files::private::create_private(
        &admin_password_file,
        format!("127.0.0.1:5432:*:pixels_admin:{admin_password}\n").as_bytes(),
    )
    .unwrap();
    seed_recovery_security_records(&tool);
    let barrier_tool_directory = fixture._base.path().join("barrier-tools");
    fs::create_dir(&barrier_tool_directory).unwrap();
    make_private(&barrier_tool_directory);
    let barrier_psql = create_docker_tool_proxy(
        &barrier_tool_directory,
        &container,
        "psql",
        "/tmp/pixels-admin.pgpass",
    );
    let barrier_proof_file = fixture._base.path().join("write-barrier-proof.json");
    let barrier_marker_file = fixture._base.path().join("write-barrier-marker.json");
    let barrier_config = WriteBarrierCommandFixture {
        schema_version: 1,
        plan: WriteBarrierCoordinatorPlan {
            deployment_id,
            proof_file: barrier_proof_file.clone(),
            marker_file: barrier_marker_file.clone(),
            lease_seconds: 120,
            external_key_ids: BTreeSet::new(),
            targets: [
                BackupService::Console,
                BackupService::Auth,
                BackupService::Desk,
            ]
            .into_iter()
            .map(|service| WriteBarrierDatabaseTarget {
                service,
                host: "127.0.0.1".to_string(),
                port: 5432,
                database: format!("pixels_{}", service_name(service)),
                admin_username: "pixels_admin".to_string(),
                admin_password_file: admin_password_file.clone(),
            })
            .collect(),
        },
        psql_path: barrier_psql.clone(),
        psql_sha256: file_sha256(&barrier_psql),
        command_timeout_seconds: 30,
    };
    let barrier_config_path = fixture._base.path().join("write-barrier-config.json");
    px_private_files::private::create_private(
        &barrier_config_path,
        &serde_json::to_vec(&barrier_config).unwrap(),
    )
    .unwrap();
    let barrier_acquire_output = Command::new(env!("CARGO_BIN_EXE_px_backup"))
        .args(["barrier-acquire", barrier_config_path.to_str().unwrap()])
        .stdin(Stdio::null())
        .output()
        .unwrap();
    assert!(
        barrier_acquire_output.status.success(),
        "write barrier acquisition failed: {}",
        String::from_utf8_lossy(&barrier_acquire_output.stderr)
    );
    let duplicate_barrier_acquire = Command::new(env!("CARGO_BIN_EXE_px_backup"))
        .args(["barrier-acquire", barrier_config_path.to_str().unwrap()])
        .stdin(Stdio::null())
        .output()
        .unwrap();
    assert!(!duplicate_barrier_acquire.status.success());
    let barrier_proof = serde_json::from_slice::<WriteBarrierProof>(
        &px_private_files::private::read_private(&barrier_proof_file).unwrap(),
    )
    .unwrap();
    assert_eq!(barrier_proof.attestations.len(), 3);
    let console_connect_during_barrier = tool.output(&[
        "psql",
        "-X",
        "--tuples-only",
        "--no-align",
        "--username",
        "pixels_admin",
        "--dbname",
        "postgres",
        "--command",
        "SELECT has_database_privilege('pixels_console_runtime','pixels_console','CONNECT')",
    ]);
    assert_eq!(console_connect_during_barrier, "f");
    let plan = BackupPlan {
        deployment_id,
        kind: RecoverySetKind::WriteBarrier,
        write_barrier_proof_file: Some(barrier_proof_file.clone()),
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
    assert_eq!(manifest.kind, RecoverySetKind::WriteBarrier);
    let barrier_release_output = Command::new(env!("CARGO_BIN_EXE_px_backup"))
        .args(["barrier-release", barrier_config_path.to_str().unwrap()])
        .stdin(Stdio::null())
        .output()
        .unwrap();
    assert!(
        barrier_release_output.status.success(),
        "write barrier release failed: {}",
        String::from_utf8_lossy(&barrier_release_output.stderr)
    );
    assert!(!barrier_proof_file.exists());
    assert!(!barrier_marker_file.exists());
    let repeated_barrier_release = Command::new(env!("CARGO_BIN_EXE_px_backup"))
        .args(["barrier-release", barrier_config_path.to_str().unwrap()])
        .stdin(Stdio::null())
        .output()
        .unwrap();
    assert!(repeated_barrier_release.status.success());
    let console_connect_after_release = tool.output(&[
        "psql",
        "-X",
        "--tuples-only",
        "--no-align",
        "--username",
        "pixels_admin",
        "--dbname",
        "postgres",
        "--command",
        "SELECT has_database_privilege('pixels_console_runtime','pixels_console','CONNECT')",
    ]);
    assert_eq!(console_connect_after_release, "t");
    assert_eq!(repository.manifests().unwrap(), vec![manifest.clone()]);
    let offsite_root = fixture._base.path().join("offsite-sets");
    fs::create_dir(&offsite_root).unwrap();
    make_private(&offsite_root);
    let offsite_repository = BackupRepository::open(&offsite_root, deployment_id).unwrap();
    let offsite_manifest = repository
        .replicate_verified_to(&offsite_repository, manifest.recovery_set_id)
        .unwrap();
    assert_eq!(offsite_manifest.status, RecoverySetStatus::OffsiteVerified);
    assert_eq!(
        offsite_repository.manifests().unwrap(),
        vec![offsite_manifest]
    );
    drop(offsite_repository);
    let witness_root = fixture._base.path().join("recovery-witness-store");
    fs::create_dir(&witness_root).unwrap();
    make_private(&witness_root);
    let witness_config = WitnessRecordCommandFixture {
        schema_version: 2,
        deployment_id,
        recovery_set_id: manifest.recovery_set_id,
        repository_root: offsite_root.clone(),
        witness_root: witness_root.clone(),
        generation_transition: None,
    };
    let witness_config_path = fixture._base.path().join("recovery-witness-config.json");
    px_private_files::private::create_private(
        &witness_config_path,
        &serde_json::to_vec(&witness_config).unwrap(),
    )
    .unwrap();
    for _ in 0..2 {
        let witness_output = Command::new(env!("CARGO_BIN_EXE_px_backup"))
            .args(["witness-record", witness_config_path.to_str().unwrap()])
            .stdin(Stdio::null())
            .output()
            .unwrap();
        assert!(
            witness_output.status.success(),
            "recovery witness recording failed: {}",
            String::from_utf8_lossy(&witness_output.stderr)
        );
    }
    let witness_path = witness_root
        .join("witnesses")
        .join(format!("{}.json", manifest.recovery_set_id));
    assert!(witness_path.is_file());
    drop(repository);
    let unavailable_source_root = fixture._base.path().join("source-host-unavailable");
    fs::rename(&fixture.root, &unavailable_source_root).unwrap();
    assert!(!fixture.root.exists());
    let set_directory = offsite_root.join(manifest.recovery_set_id.to_string());
    let mut restore_password = "restore_Test-Password_0123456789abcdef";
    let restore_container_password_command = format!(
        "umask 077; printf '%s\\n' '127.0.0.1:5432:*:pixels_restore_operator:{restore_password}' > /tmp/pixels-restore.pgpass"
    );
    tool.execute(&["sh", "-c", &restore_container_password_command]);
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
        repository_root: offsite_root.clone(),
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

    let seal_marker_path = fixture._base.path().join("recovery-seal-marker.json");
    let seal_report_path = fixture._base.path().join("recovery-seal-report.json");
    let seal_config = RecoverySealCommandFixture {
        schema_version: 1,
        repository_root: offsite_root.clone(),
        plan: RecoverySealPlan {
            deployment_id,
            recovery_set_id: manifest.recovery_set_id,
            target_environment_id,
            lock_file: fixture._base.path().join("recovery-seal.lock"),
            marker_file: seal_marker_path.clone(),
            report_file: seal_report_path.clone(),
            targets: targets.clone(),
        },
        psql_path: psql.clone(),
        psql_sha256: file_sha256(&psql),
        command_timeout_seconds: 30,
    };
    let seal_config_path = fixture._base.path().join("recovery-seal-config.json");
    px_private_files::private::create_private(
        &seal_config_path,
        &serde_json::to_vec(&seal_config).unwrap(),
    )
    .unwrap();
    let seal_output = Command::new(env!("CARGO_BIN_EXE_px_backup"))
        .args(["restore-seal", seal_config_path.to_str().unwrap()])
        .stdin(Stdio::null())
        .output()
        .unwrap();
    if !seal_output.status.success() {
        let proxy_log = fs::read_to_string(tool_directory.join("proxy.log"))
            .unwrap_or_else(|_| "proxy log unavailable".to_string());
        panic!(
            "recovery seal failed: status={} stdout={} stderr={}\n{proxy_log}",
            seal_output.status,
            String::from_utf8_lossy(&seal_output.stdout),
            String::from_utf8_lossy(&seal_output.stderr)
        );
    }
    let seal_report = serde_json::from_slice::<RecoverySealReport>(
        &px_private_files::private::read_private(&seal_report_path).unwrap(),
    )
    .unwrap();
    assert!(seal_report.old_sessions_revoked);
    assert!(seal_report.old_grants_invalidated);
    assert!(seal_report.pending_control_invalidated);
    assert!(seal_report.admission_required);
    assert_eq!(seal_report.services.len(), 3);
    let console_seal = seal_report
        .services
        .iter()
        .find(|service| service.service == BackupService::Console)
        .unwrap();
    assert_eq!(console_seal.revoked_session_records, 1);
    assert_eq!(console_seal.invalidated_grant_records, 3);
    assert_eq!(console_seal.invalidated_control_records, 1);
    let auth_seal = seal_report
        .services
        .iter()
        .find(|service| service.service == BackupService::Auth)
        .unwrap();
    assert_eq!(auth_seal.revoked_session_records, 1);
    assert_eq!(auth_seal.invalidated_grant_records, 1);
    assert_eq!(auth_seal.invalidated_control_records, 1);
    let desk_seal = seal_report
        .services
        .iter()
        .find(|service| service.service == BackupService::Desk)
        .unwrap();
    assert_eq!(desk_seal.revoked_session_records, 1);
    assert!(!seal_marker_path.exists());
    for target in &targets {
        let sealed_state = tool.output(&[
            "psql",
            "-X",
            "--tuples-only",
            "--no-align",
            "--username",
            "pixels_admin",
            "--dbname",
            &target.database,
            "--command",
            "SELECT recovery_generation::text || '|' || (write_barrier_id IS NULL)::text FROM pixels.recovery_security_state WHERE singleton",
        ]);
        assert_eq!(
            sealed_state,
            format!("{}|true", seal_report.recovery_generation)
        );
    }
    let repeated_seal_output = Command::new(env!("CARGO_BIN_EXE_px_backup"))
        .args(["restore-seal", seal_config_path.to_str().unwrap()])
        .stdin(Stdio::null())
        .output()
        .unwrap();
    assert!(repeated_seal_output.status.success());

    let witness = ExternalRecoveryWitness::load_private(&witness_path).unwrap();
    assert_eq!(witness.schema_version, RECOVERY_WITNESS_SCHEMA_VERSION);
    let RecoverySecurityEvidence::Captured { watermarks, .. } = &manifest.security_evidence else {
        panic!("coordinated backup must carry security evidence");
    };
    assert_eq!(witness.watermarks, *watermarks);
    let admission_root = fixture._base.path().join("restore-admission");
    fs::create_dir(&admission_root).unwrap();
    make_private(&admission_root);
    let admission_config = RestoreAdmissionCommandFixture {
        schema_version: 2,
        deployment_id,
        recovery_set_id: manifest.recovery_set_id,
        target_environment_id,
        repository_root: offsite_root.clone(),
        admission_root,
        witness_path,
        recovery_seal_report_path: seal_report_path.clone(),
        completed_checks: BTreeSet::from([
            RestoreOperationalCheck::TargetNetworkIsolated,
            RestoreOperationalCheck::SideEffectsDisabled,
            RestoreOperationalCheck::RestoredIntoNewDatabases,
            RestoreOperationalCheck::DeploymentIdentityMatched,
            RestoreOperationalCheck::SchemaAndConstraintsVerified,
            RestoreOperationalCheck::BusinessSummariesVerified,
            RestoreOperationalCheck::PendingCommandsReconciled,
            RestoreOperationalCheck::ApplicationArtifactsVerified,
            RestoreOperationalCheck::NodeWorkspaceFactsReconciled,
        ]),
    };
    let admission_config_path = fixture._base.path().join("restore-admission-config.json");
    px_private_files::private::create_private(
        &admission_config_path,
        &serde_json::to_vec(&admission_config).unwrap(),
    )
    .unwrap();
    let evaluate_output = Command::new(env!("CARGO_BIN_EXE_px_backup"))
        .args(["restore-evaluate", admission_config_path.to_str().unwrap()])
        .stdin(Stdio::null())
        .output()
        .unwrap();
    assert!(
        evaluate_output.status.success(),
        "restore evaluation failed: {}",
        String::from_utf8_lossy(&evaluate_output.stderr)
    );
    let ready_record = serde_json::from_slice::<RestoreAdmissionRecord>(
        String::from_utf8_lossy(&evaluate_output.stdout)
            .trim()
            .as_bytes(),
    )
    .unwrap();
    assert_eq!(
        ready_record.state,
        RestoreAdmissionState::ReadyForManualApproval
    );
    let approval_request = RestoreApprovalCommandFixture {
        schema_version: 1,
        approval_id: Uuid::new_v4(),
        administrator_id: Uuid::new_v4(),
        expected_revision: ready_record.revision,
        expected_evidence_sha256: ready_record.evidence_sha256.unwrap(),
    };
    let approval_request_path = fixture._base.path().join("restore-approval.json");
    px_private_files::private::create_private(
        &approval_request_path,
        &serde_json::to_vec(&approval_request).unwrap(),
    )
    .unwrap();
    let approval_output = Command::new(env!("CARGO_BIN_EXE_px_backup"))
        .args([
            "restore-approve",
            admission_config_path.to_str().unwrap(),
            approval_request_path.to_str().unwrap(),
        ])
        .stdin(Stdio::null())
        .output()
        .unwrap();
    assert!(
        approval_output.status.success(),
        "restore approval failed: {}",
        String::from_utf8_lossy(&approval_output.stderr)
    );
    let admitted_record = serde_json::from_slice::<RestoreAdmissionRecord>(
        String::from_utf8_lossy(&approval_output.stdout)
            .trim()
            .as_bytes(),
    )
    .unwrap();
    assert_eq!(admitted_record.state, RestoreAdmissionState::Admitted);

    let console_archive = set_directory.join("console.dump");
    OpenOptions::new()
        .append(true)
        .open(console_archive)
        .unwrap()
        .write_all(b"tampered")
        .unwrap();
    assert!(!fixture.root.exists());
    let repository = BackupRepository::open(&offsite_root, deployment_id).unwrap();
    assert!(repository.manifests().is_err());
}
