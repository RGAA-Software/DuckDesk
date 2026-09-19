use crate::{
    executor::{hash_file, valid_sha256, valid_tool_path},
    BackupCancellation, BackupError, BackupMemberState, BackupRepository, BackupService,
    RepositoryError,
};
use serde::{Deserialize, Serialize};
use std::{
    collections::{BTreeMap, BTreeSet},
    env,
    fs::OpenOptions,
    io::{Read, Write},
    net::IpAddr,
    path::{Path, PathBuf},
    process::{Child, Command, Stdio},
    thread,
    time::{Duration, Instant},
};
use uuid::Uuid;

pub const RESTORE_EXECUTION_REPORT_SCHEMA_VERSION: u32 = 1;

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum RestoreExecutionError {
    #[error("restore execution plan is invalid")]
    InvalidPlan,
    #[error("verified recovery set is unavailable")]
    RecoverySetUnavailable,
    #[error("backup repository rejected restore execution")]
    Repository,
    #[error("fresh isolated target database creation failed")]
    TargetCreationFailed,
    #[error("database archive restore failed")]
    RestoreFailed,
    #[error("restored database identity or schema verification failed")]
    VerificationFailed,
    #[error("pinned PostgreSQL restore tool is unavailable or changed")]
    ToolIdentity,
    #[error("database restore credential file is unavailable")]
    Credential,
    #[error("database restore command timed out")]
    ToolTimeout,
    #[error("database restore operation cancelled")]
    Cancelled,
    #[error("system clock is unavailable")]
    Clock,
}

impl From<RepositoryError> for RestoreExecutionError {
    fn from(_: RepositoryError) -> Self {
        Self::Repository
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RestoreDatabaseTarget {
    pub service: BackupService,
    pub host: String,
    pub port: u16,
    pub database: String,
    pub owner: String,
    pub username: String,
    pub password_file: PathBuf,
}

impl RestoreDatabaseTarget {
    fn validate(&self, target_environment_id: Uuid) -> Result<(), RestoreExecutionError> {
        let expected_database = isolated_database_name(target_environment_id, self.service);
        let expected_owner = format!("pixels_{}_owner", service_name(self.service));
        if self.port == 0
            || !valid_host(&self.host)
            || self.database != expected_database
            || self.owner != expected_owner
            || self.username != "pixels_restore_operator"
            || !self.password_file.is_absolute()
        {
            return Err(RestoreExecutionError::InvalidPlan);
        }
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RestoreExecutionPlan {
    pub deployment_id: Uuid,
    pub recovery_set_id: Uuid,
    pub target_environment_id: Uuid,
    pub targets: Vec<RestoreDatabaseTarget>,
}

impl RestoreExecutionPlan {
    pub fn validate(&self) -> Result<(), RestoreExecutionError> {
        if self.deployment_id.is_nil()
            || self.recovery_set_id.is_nil()
            || self.target_environment_id.is_nil()
        {
            return Err(RestoreExecutionError::InvalidPlan);
        }
        let services = self
            .targets
            .iter()
            .map(|target| target.service)
            .collect::<BTreeSet<_>>();
        if services.len() != self.targets.len() {
            return Err(RestoreExecutionError::InvalidPlan);
        }
        for target in &self.targets {
            target.validate(self.target_environment_id)?;
        }
        Ok(())
    }
}

pub trait LogicalRestoreTool {
    fn create_fresh_database(
        &self,
        target: &RestoreDatabaseTarget,
    ) -> Result<(), RestoreExecutionError>;
    fn restore_archive(
        &self,
        target: &RestoreDatabaseTarget,
        archive_path: &Path,
    ) -> Result<(), RestoreExecutionError>;
    fn verify_restored_database(
        &self,
        target: &RestoreDatabaseTarget,
        expected_deployment_id: Uuid,
        expected_schema_version: u32,
    ) -> Result<(), RestoreExecutionError>;
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RestoreOperatorProvisionPlan {
    pub host: String,
    pub port: u16,
    pub admin_username: String,
    pub admin_password_file: PathBuf,
    pub restore_password_file: PathBuf,
    pub services: BTreeSet<BackupService>,
}

impl RestoreOperatorProvisionPlan {
    pub fn validate(&self) -> Result<(), RestoreExecutionError> {
        if self.port == 0
            || !valid_host(&self.host)
            || !valid_identifier(&self.admin_username)
            || !self.admin_password_file.is_absolute()
            || !self.restore_password_file.is_absolute()
            || !self.services.contains(&BackupService::Console)
            || self.services.is_empty()
            || self.services.len() > 3
        {
            return Err(RestoreExecutionError::InvalidPlan);
        }
        Ok(())
    }
}

#[derive(Debug, Clone)]
pub struct PinnedPgRestoreTools {
    createdb: PathBuf,
    createdb_sha256: String,
    pg_restore: PathBuf,
    pg_restore_sha256: String,
    psql: PathBuf,
    psql_sha256: String,
    command_timeout: Duration,
    cancellation: BackupCancellation,
}

impl PinnedPgRestoreTools {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        createdb: PathBuf,
        createdb_sha256: String,
        pg_restore: PathBuf,
        pg_restore_sha256: String,
        psql: PathBuf,
        psql_sha256: String,
        command_timeout: Duration,
        cancellation: BackupCancellation,
    ) -> Result<Self, RestoreExecutionError> {
        if !valid_tool_path(&createdb, "createdb")
            || !valid_tool_path(&pg_restore, "pg_restore")
            || !valid_tool_path(&psql, "psql")
            || !valid_sha256(&createdb_sha256)
            || !valid_sha256(&pg_restore_sha256)
            || !valid_sha256(&psql_sha256)
            || command_timeout < Duration::from_secs(1)
            || command_timeout > Duration::from_secs(24 * 60 * 60)
        {
            return Err(RestoreExecutionError::ToolIdentity);
        }
        let tools = Self {
            createdb,
            createdb_sha256,
            pg_restore,
            pg_restore_sha256,
            psql,
            psql_sha256,
            command_timeout,
            cancellation,
        };
        tools.verify_tools()?;
        Ok(tools)
    }

    fn verify_tools(&self) -> Result<(), RestoreExecutionError> {
        if hash_restore_tool(&self.createdb)? != self.createdb_sha256
            || hash_restore_tool(&self.pg_restore)? != self.pg_restore_sha256
            || hash_restore_tool(&self.psql)? != self.psql_sha256
        {
            return Err(RestoreExecutionError::ToolIdentity);
        }
        Ok(())
    }

    fn prepare_command(&self, executable: &Path, target: &RestoreDatabaseTarget) -> Command {
        let mut command = Command::new(executable);
        sanitize_postgres_environment(&mut command);
        command
            .env("PGPASSFILE", &target.password_file)
            .stdin(Stdio::null())
            .stderr(Stdio::null());
        command
    }

    fn validate_target(&self, target: &RestoreDatabaseTarget) -> Result<(), RestoreExecutionError> {
        self.verify_tools()?;
        drop(
            px_private_files::private::read_private(&target.password_file)
                .map_err(|_| RestoreExecutionError::Credential)?,
        );
        Ok(())
    }
}

impl LogicalRestoreTool for PinnedPgRestoreTools {
    fn create_fresh_database(
        &self,
        target: &RestoreDatabaseTarget,
    ) -> Result<(), RestoreExecutionError> {
        self.validate_target(target)?;
        let mut command = self.prepare_command(&self.createdb, target);
        command
            .args([
                "--no-password",
                "--template=template0",
                "--maintenance-db=postgres",
            ])
            .arg("--host")
            .arg(&target.host)
            .arg("--port")
            .arg(target.port.to_string())
            .arg("--username")
            .arg(&target.username)
            .arg("--owner")
            .arg(&target.owner)
            .arg(&target.database)
            .stdout(Stdio::null());
        run_restore_command(
            &mut command,
            self.command_timeout,
            &self.cancellation,
            RestoreExecutionError::TargetCreationFailed,
            false,
        )?;
        Ok(())
    }

    fn restore_archive(
        &self,
        target: &RestoreDatabaseTarget,
        archive_path: &Path,
    ) -> Result<(), RestoreExecutionError> {
        self.validate_target(target)?;
        let archive = open_restore_archive(archive_path)?;
        let mut command = self.prepare_command(&self.pg_restore, target);
        command
            .args(["--exit-on-error", "--no-password", "--no-owner"])
            .arg("--host")
            .arg(&target.host)
            .arg("--port")
            .arg(target.port.to_string())
            .arg("--username")
            .arg(&target.username)
            .arg("--role")
            .arg(&target.owner)
            .arg("--dbname")
            .arg(&target.database)
            .stdin(Stdio::from(archive))
            .stdout(Stdio::null());
        run_restore_command(
            &mut command,
            self.command_timeout,
            &self.cancellation,
            RestoreExecutionError::RestoreFailed,
            false,
        )?;
        Ok(())
    }

    fn verify_restored_database(
        &self,
        target: &RestoreDatabaseTarget,
        expected_deployment_id: Uuid,
        expected_schema_version: u32,
    ) -> Result<(), RestoreExecutionError> {
        self.validate_target(target)?;
        let verification_query = "SELECT identity.service || '|' || identity.deployment_id::text || '|' || current_database() || '|' || pg_catalog.pg_get_userbyid(database_record.datdba) || '|' || COUNT(migration.version)::text || '|' || COALESCE(BOOL_AND(migration.success), FALSE)::text FROM pixels.deployment_identity AS identity CROSS JOIN pg_catalog.pg_database AS database_record LEFT JOIN pixels._sqlx_migrations AS migration ON TRUE WHERE database_record.datname = current_database() GROUP BY identity.service, identity.deployment_id, database_record.datdba";
        let mut command = self.prepare_command(&self.psql, target);
        command
            .args([
                "-X",
                "--tuples-only",
                "--no-align",
                "--no-password",
                "--set=ON_ERROR_STOP=1",
            ])
            .arg("--host")
            .arg(&target.host)
            .arg("--port")
            .arg(target.port.to_string())
            .arg("--username")
            .arg(&target.username)
            .arg("--dbname")
            .arg(&target.database)
            .arg("--command")
            .arg(verification_query)
            .stdout(Stdio::piped());
        let output = run_restore_command(
            &mut command,
            self.command_timeout,
            &self.cancellation,
            RestoreExecutionError::VerificationFailed,
            true,
        )?
        .ok_or(RestoreExecutionError::VerificationFailed)?;
        let expected_output = format!(
            "{}|{}|{}|{}|{}|true",
            service_name(target.service),
            expected_deployment_id,
            target.database,
            target.owner,
            expected_schema_version
        );
        if output.trim() != expected_output {
            return Err(RestoreExecutionError::VerificationFailed);
        }
        Ok(())
    }
}

#[derive(Debug, Clone)]
pub struct PinnedPgRestoreProvisioner {
    psql: PathBuf,
    psql_sha256: String,
    command_timeout: Duration,
    cancellation: BackupCancellation,
}

impl PinnedPgRestoreProvisioner {
    pub fn new(
        psql: PathBuf,
        psql_sha256: String,
        command_timeout: Duration,
        cancellation: BackupCancellation,
    ) -> Result<Self, RestoreExecutionError> {
        if !valid_tool_path(&psql, "psql")
            || !valid_sha256(&psql_sha256)
            || command_timeout < Duration::from_secs(1)
            || command_timeout > Duration::from_secs(24 * 60 * 60)
        {
            return Err(RestoreExecutionError::ToolIdentity);
        }
        let provisioner = Self {
            psql,
            psql_sha256,
            command_timeout,
            cancellation,
        };
        provisioner.verify_tool()?;
        Ok(provisioner)
    }

    pub fn provision(
        &self,
        plan: &RestoreOperatorProvisionPlan,
    ) -> Result<(), RestoreExecutionError> {
        plan.validate()?;
        self.verify_tool()?;
        drop(
            px_private_files::private::read_private(&plan.admin_password_file)
                .map_err(|_| RestoreExecutionError::Credential)?,
        );
        let restore_password = px_private_files::private::read_private(&plan.restore_password_file)
            .map_err(|_| RestoreExecutionError::Credential)?;
        if !valid_restore_password(&restore_password) {
            return Err(RestoreExecutionError::Credential);
        }
        let restore_password_text = std::str::from_utf8(&restore_password)
            .map_err(|_| RestoreExecutionError::Credential)?;
        let mut provision_command = self.prepare_command(plan);
        provision_command
            .args(["--quiet", "--set=ON_ERROR_STOP=1", "--file=-"])
            .env("PIXELS_RESTORE_OPERATOR_PASSWORD", restore_password_text)
            .stdin(Stdio::piped())
            .stdout(Stdio::null());
        let provision_script = restore_operator_provision_script(&plan.services);
        run_restore_command_with_input(
            &mut provision_command,
            self.command_timeout,
            &self.cancellation,
            RestoreExecutionError::RestoreFailed,
            false,
            Some(provision_script.as_bytes()),
        )?;

        self.verify_tool()?;
        let mut verification_command = self.prepare_command(plan);
        verification_command
            .args(["--tuples-only", "--no-align", "--set=ON_ERROR_STOP=1"])
            .arg("--command")
            .arg(restore_operator_verification_query())
            .stdout(Stdio::piped());
        let verification_output = run_restore_command(
            &mut verification_command,
            self.command_timeout,
            &self.cancellation,
            RestoreExecutionError::VerificationFailed,
            true,
        )?
        .ok_or(RestoreExecutionError::VerificationFailed)?;
        let mut expected_memberships = plan
            .services
            .iter()
            .map(|service| format!("pixels_{}_owner", service_name(*service)))
            .collect::<Vec<_>>();
        expected_memberships.sort();
        let expected_memberships = expected_memberships.join(",");
        let expected_output =
            format!("true|true|false|false|false|false|{expected_memberships}|true");
        if verification_output.trim() != expected_output {
            return Err(RestoreExecutionError::VerificationFailed);
        }
        Ok(())
    }

    fn verify_tool(&self) -> Result<(), RestoreExecutionError> {
        if hash_restore_tool(&self.psql)? != self.psql_sha256 {
            return Err(RestoreExecutionError::ToolIdentity);
        }
        Ok(())
    }

    fn prepare_command(&self, plan: &RestoreOperatorProvisionPlan) -> Command {
        let mut command = Command::new(&self.psql);
        sanitize_postgres_environment(&mut command);
        command
            .args(["-X", "--no-password"])
            .arg("--host")
            .arg(&plan.host)
            .arg("--port")
            .arg(plan.port.to_string())
            .arg("--username")
            .arg(&plan.admin_username)
            .args(["--dbname", "postgres"])
            .env("PGPASSFILE", &plan.admin_password_file)
            .stdin(Stdio::null())
            .stderr(Stdio::null());
        command
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "state", rename_all = "snake_case", deny_unknown_fields)]
pub enum RestoredMember {
    Restored {
        service: BackupService,
        database: String,
        archive_sha256: String,
        schema_version: u32,
    },
    NotApplicable {
        service: BackupService,
        reason: String,
    },
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RestoreExecutionReport {
    pub schema_version: u32,
    pub deployment_id: Uuid,
    pub recovery_set_id: Uuid,
    pub target_environment_id: Uuid,
    pub started_at_unix: u64,
    pub completed_at_unix: u64,
    pub members: Vec<RestoredMember>,
    pub admission_required: bool,
}

pub struct RestoreRunner<T> {
    tool: T,
}

impl<T: LogicalRestoreTool> RestoreRunner<T> {
    pub fn new(tool: T) -> Self {
        Self { tool }
    }

    pub fn run(
        &self,
        repository: &BackupRepository,
        plan: &RestoreExecutionPlan,
    ) -> Result<RestoreExecutionReport, RestoreExecutionError> {
        plan.validate()?;
        if repository.deployment_id() != plan.deployment_id {
            return Err(RestoreExecutionError::InvalidPlan);
        }
        let manifest = repository
            .manifests()?
            .into_iter()
            .find(|manifest| {
                manifest.recovery_set_id == plan.recovery_set_id && manifest.status.is_verified()
            })
            .ok_or(RestoreExecutionError::RecoverySetUnavailable)?;
        let required_services = manifest
            .members
            .iter()
            .filter_map(|member| match member.member {
                BackupMemberState::Required { .. } => Some(member.service),
                BackupMemberState::NotApplicable { .. } => None,
            })
            .collect::<BTreeSet<_>>();
        let targets_by_service = plan
            .targets
            .iter()
            .map(|target| (target.service, target))
            .collect::<BTreeMap<_, _>>();
        if targets_by_service.keys().copied().collect::<BTreeSet<_>>() != required_services {
            return Err(RestoreExecutionError::InvalidPlan);
        }
        let started_at_unix = current_unix_time()?;
        let recovery_set_directory = repository.root().join(plan.recovery_set_id.to_string());
        let mut restored_members = Vec::with_capacity(manifest.members.len());
        for member in &manifest.members {
            match &member.member {
                BackupMemberState::Required {
                    database: source_database,
                    schema_version,
                    archive_file,
                    archive_sha256,
                    ..
                } => {
                    let target = targets_by_service
                        .get(&member.service)
                        .copied()
                        .ok_or(RestoreExecutionError::InvalidPlan)?;
                    if target.database == *source_database {
                        return Err(RestoreExecutionError::InvalidPlan);
                    }
                    self.tool.create_fresh_database(target)?;
                    self.tool
                        .restore_archive(target, &recovery_set_directory.join(archive_file))?;
                    self.tool.verify_restored_database(
                        target,
                        plan.deployment_id,
                        *schema_version,
                    )?;
                    restored_members.push(RestoredMember::Restored {
                        service: member.service,
                        database: target.database.clone(),
                        archive_sha256: archive_sha256.clone(),
                        schema_version: *schema_version,
                    });
                }
                BackupMemberState::NotApplicable { reason } => {
                    restored_members.push(RestoredMember::NotApplicable {
                        service: member.service,
                        reason: reason.clone(),
                    });
                }
            }
        }
        Ok(RestoreExecutionReport {
            schema_version: RESTORE_EXECUTION_REPORT_SCHEMA_VERSION,
            deployment_id: plan.deployment_id,
            recovery_set_id: plan.recovery_set_id,
            target_environment_id: plan.target_environment_id,
            started_at_unix,
            completed_at_unix: current_unix_time()?,
            members: restored_members,
            admission_required: true,
        })
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

fn service_name(service: BackupService) -> &'static str {
    match service {
        BackupService::Console => "console",
        BackupService::Auth => "auth",
        BackupService::Desk => "desk",
    }
}

fn valid_host(value: &str) -> bool {
    if value.is_empty() || value.len() > 253 || value.contains(['/', '\\', '?', '#', '@']) {
        return false;
    }
    if value.parse::<IpAddr>().is_ok() {
        return true;
    }
    value.split('.').all(|label| {
        !label.is_empty()
            && label.len() <= 63
            && !label.starts_with('-')
            && !label.ends_with('-')
            && label
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-')
    })
}

fn valid_identifier(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 63
        && value
            .bytes()
            .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'_')
}

fn valid_restore_password(password: &[u8]) -> bool {
    (32..=128).contains(&password.len())
        && password
            .iter()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'_' | b'-' | b'.' | b'~'))
}

fn sanitize_postgres_environment(command: &mut Command) {
    for (environment_name, _) in env::vars_os() {
        let normalized_name = environment_name.to_string_lossy().to_ascii_uppercase();
        if normalized_name.starts_with("PG")
            || normalized_name == "DATABASE_URL"
            || normalized_name == "PIXELS_RESTORE_OPERATOR_PASSWORD"
        {
            command.env_remove(environment_name);
        }
    }
}

fn restore_operator_provision_script(services: &BTreeSet<BackupService>) -> String {
    let mut intended_roles = services
        .iter()
        .map(|service| format!("pixels_{}_owner", service_name(*service)))
        .collect::<Vec<_>>();
    intended_roles.sort();
    let intended_role_list = intended_roles.join(",");
    let intended_role_literals = intended_roles
        .iter()
        .map(|role| format!("'{role}'"))
        .collect::<Vec<_>>()
        .join(",");
    format!(
        "\\getenv restore_password PIXELS_RESTORE_OPERATOR_PASSWORD\n\
SELECT format('CREATE ROLE pixels_restore_operator LOGIN PASSWORD %L CREATEDB NOSUPERUSER NOCREATEROLE NOREPLICATION NOBYPASSRLS', :'restore_password') WHERE NOT EXISTS (SELECT 1 FROM pg_catalog.pg_roles WHERE rolname='pixels_restore_operator') \\gexec\n\
SELECT format('ALTER ROLE pixels_restore_operator WITH LOGIN PASSWORD %L CREATEDB NOSUPERUSER NOCREATEROLE NOREPLICATION NOBYPASSRLS', :'restore_password') \\gexec\n\
ALTER ROLE pixels_restore_operator RESET ALL;\n\
SELECT format('REVOKE %I FROM pixels_restore_operator', role_record.rolname) FROM pg_catalog.pg_roles AS role_record WHERE role_record.rolname IN ('pixels_console_owner','pixels_auth_owner','pixels_desk_owner') AND role_record.rolname NOT IN ({intended_role_literals}) \\gexec\n\
GRANT {intended_role_list} TO pixels_restore_operator;\n\
GRANT CONNECT ON DATABASE postgres TO pixels_restore_operator;\n"
    )
}

fn restore_operator_verification_query() -> &'static str {
    "SELECT restore_role.rolcanlogin::text || '|' || restore_role.rolcreatedb::text || '|' || restore_role.rolsuper::text || '|' || restore_role.rolcreaterole::text || '|' || restore_role.rolreplication::text || '|' || restore_role.rolbypassrls::text || '|' || COALESCE((SELECT string_agg(parent_role.rolname, ',' ORDER BY parent_role.rolname) FROM pg_catalog.pg_auth_members AS membership JOIN pg_catalog.pg_roles AS parent_role ON parent_role.oid=membership.roleid WHERE membership.member=restore_role.oid), '') || '|' || pg_catalog.has_database_privilege(restore_role.rolname, 'postgres', 'CONNECT')::text FROM pg_catalog.pg_roles AS restore_role WHERE restore_role.rolname='pixels_restore_operator'"
}

fn current_unix_time() -> Result<u64, RestoreExecutionError> {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_secs())
        .map_err(|_| RestoreExecutionError::Clock)
}

fn hash_restore_tool(path: &Path) -> Result<String, RestoreExecutionError> {
    hash_file(path, BackupError::ToolIdentity).map_err(|_| RestoreExecutionError::ToolIdentity)
}

fn open_restore_archive(path: &Path) -> Result<std::fs::File, RestoreExecutionError> {
    let mut options = OpenOptions::new();
    options.read(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.custom_flags(libc::O_NOFOLLOW);
    }
    let archive = options
        .open(path)
        .map_err(|_| RestoreExecutionError::RestoreFailed)?;
    if !archive
        .metadata()
        .map_err(|_| RestoreExecutionError::RestoreFailed)?
        .is_file()
    {
        return Err(RestoreExecutionError::RestoreFailed);
    }
    Ok(archive)
}

fn run_restore_command(
    command: &mut Command,
    timeout: Duration,
    cancellation: &BackupCancellation,
    failure: RestoreExecutionError,
    capture_stdout: bool,
) -> Result<Option<String>, RestoreExecutionError> {
    run_restore_command_with_input(
        command,
        timeout,
        cancellation,
        failure,
        capture_stdout,
        None,
    )
}

fn run_restore_command_with_input(
    command: &mut Command,
    timeout: Duration,
    cancellation: &BackupCancellation,
    failure: RestoreExecutionError,
    capture_stdout: bool,
    standard_input: Option<&[u8]>,
) -> Result<Option<String>, RestoreExecutionError> {
    if cancellation.is_cancelled() {
        return Err(RestoreExecutionError::Cancelled);
    }
    let mut child = command.spawn().map_err(|_| failure)?;
    if let Some(standard_input) = standard_input {
        let write_result = child
            .stdin
            .take()
            .ok_or(failure)
            .and_then(|mut child_input| child_input.write_all(standard_input).map_err(|_| failure));
        if let Err(error) = write_result {
            stop_restore_child(&mut child);
            return Err(error);
        }
    }
    let started = Instant::now();
    loop {
        if cancellation.is_cancelled() {
            stop_restore_child(&mut child);
            return Err(RestoreExecutionError::Cancelled);
        }
        if started.elapsed() >= timeout {
            stop_restore_child(&mut child);
            return Err(RestoreExecutionError::ToolTimeout);
        }
        match child.try_wait() {
            Ok(Some(status)) if status.success() => {
                if !capture_stdout {
                    return Ok(None);
                }
                let mut stdout = child.stdout.take().ok_or(failure)?;
                let mut output = String::new();
                stdout.read_to_string(&mut output).map_err(|_| failure)?;
                if output.len() > 4096 {
                    return Err(failure);
                }
                return Ok(Some(output));
            }
            Ok(Some(_)) => return Err(failure),
            Ok(None) => thread::sleep(Duration::from_millis(100)),
            Err(_) => {
                stop_restore_child(&mut child);
                return Err(failure);
            }
        }
    }
}

fn stop_restore_child(child: &mut Child) {
    let _ = child.kill();
    let _ = child.wait();
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        BackupMember, RecoveryEvidenceUnavailableReason, RecoverySecurityEvidence, RecoverySetKind,
        RecoverySetManifest, RecoverySetStatus, RetentionClass, MANIFEST_SCHEMA_VERSION,
    };
    use sha2::{Digest, Sha256};
    use std::{fs, sync::Mutex, time::Duration};

    struct RestoreToolProbe {
        operations: Mutex<Vec<String>>,
        fail_restore_service: Option<BackupService>,
    }

    impl LogicalRestoreTool for RestoreToolProbe {
        fn create_fresh_database(
            &self,
            target: &RestoreDatabaseTarget,
        ) -> Result<(), RestoreExecutionError> {
            self.operations
                .lock()
                .unwrap()
                .push(format!("create:{}", service_name(target.service)));
            Ok(())
        }

        fn restore_archive(
            &self,
            target: &RestoreDatabaseTarget,
            archive_path: &Path,
        ) -> Result<(), RestoreExecutionError> {
            assert!(archive_path.is_file());
            self.operations
                .lock()
                .unwrap()
                .push(format!("restore:{}", service_name(target.service)));
            if self.fail_restore_service == Some(target.service) {
                Err(RestoreExecutionError::RestoreFailed)
            } else {
                Ok(())
            }
        }

        fn verify_restored_database(
            &self,
            target: &RestoreDatabaseTarget,
            expected_deployment_id: Uuid,
            expected_schema_version: u32,
        ) -> Result<(), RestoreExecutionError> {
            assert!(!expected_deployment_id.is_nil());
            assert_eq!(expected_schema_version, 1);
            self.operations
                .lock()
                .unwrap()
                .push(format!("verify:{}", service_name(target.service)));
            Ok(())
        }
    }

    struct RestoreFixture {
        _temporary_directory: tempfile::TempDir,
        repository: BackupRepository,
        manifest: RecoverySetManifest,
        password_file: PathBuf,
    }

    impl RestoreFixture {
        fn new() -> Self {
            let temporary_directory = tempfile::tempdir().unwrap();
            make_private_directory(temporary_directory.path());
            let repository_root = temporary_directory.path().join("repository");
            fs::create_dir(&repository_root).unwrap();
            make_private_directory(&repository_root);
            let deployment_id = Uuid::new_v4();
            let repository = BackupRepository::open(&repository_root, deployment_id).unwrap();
            let recovery_set_id = Uuid::new_v4();
            let staged_recovery_set = repository.begin_set(recovery_set_id).unwrap();
            let archive_sha256 = format!("{:x}", Sha256::digest(b"archive"));
            let services = [
                BackupService::Console,
                BackupService::Auth,
                BackupService::Desk,
            ];
            for service in services {
                let archive_path = staged_recovery_set.prepare_archive(service).unwrap();
                fs::write(archive_path, b"archive").unwrap();
            }
            let manifest = RecoverySetManifest {
                schema_version: MANIFEST_SCHEMA_VERSION,
                recovery_set_id,
                deployment_id,
                kind: RecoverySetKind::Independent,
                status: RecoverySetStatus::Verified,
                created_at_unix: 100,
                completed_at_unix: Some(200),
                locked: false,
                restoring: false,
                retention: BTreeSet::from([RetentionClass::Hourly]),
                previous_recovery_set_id: None,
                members: services
                    .into_iter()
                    .map(|service| BackupMember {
                        service,
                        member: BackupMemberState::Required {
                            database: format!("pixels_{}", service_name(service)),
                            schema_version: 1,
                            archive_file: format!("{}.dump", service_name(service)),
                            archive_sha256: archive_sha256.clone(),
                            started_at_unix: 100,
                            completed_at_unix: 200,
                        },
                    })
                    .collect(),
                security_evidence: RecoverySecurityEvidence::Unavailable {
                    reason: RecoveryEvidenceUnavailableReason::IndependentBackup,
                },
                failure_code: None,
            };
            staged_recovery_set.publish(&manifest).unwrap();
            let password_file = temporary_directory.path().join("restore.pgpass");
            px_private_files::private::create_private(&password_file, b"credential\n").unwrap();
            Self {
                _temporary_directory: temporary_directory,
                repository,
                manifest,
                password_file,
            }
        }

        fn plan(&self, target_environment_id: Uuid) -> RestoreExecutionPlan {
            RestoreExecutionPlan {
                deployment_id: self.manifest.deployment_id,
                recovery_set_id: self.manifest.recovery_set_id,
                target_environment_id,
                targets: [
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
                    password_file: self.password_file.clone(),
                })
                .collect(),
            }
        }
    }

    fn make_private_directory(path: &Path) {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(path, fs::Permissions::from_mode(0o700)).unwrap();
        }
        #[cfg(windows)]
        {
            use std::{os::windows::process::CommandExt, process::Command};
            let process_identity = Command::new("whoami")
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(process_identity.status.success());
            let access_grant = format!(
                "{}:(OI)(CI)F",
                String::from_utf8(process_identity.stdout).unwrap().trim()
            );
            let access_result = Command::new("icacls")
                .arg(path)
                .args([
                    "/inheritance:r",
                    "/grant:r",
                    &access_grant,
                    "*S-1-5-18:(OI)(CI)F",
                ])
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(access_result.status.success());
        }
    }

    fn create_fake_tool(
        directory: &Path,
        tool_name: &str,
        log_path: &Path,
        output: Option<&str>,
    ) -> PathBuf {
        #[cfg(windows)]
        let tool_path = directory.join(format!("{tool_name}.cmd"));
        #[cfg(unix)]
        let tool_path = directory.join(tool_name);
        #[cfg(windows)]
        let standard_input_capture = if tool_name == "psql" {
            format!("more >>\"{}\"\r\n", log_path.display())
        } else {
            String::new()
        };
        #[cfg(windows)]
        let script = format!(
            "@echo off\r\necho %*>>\"{}\"\r\n{}{}exit /b 0\r\n",
            log_path.display(),
            standard_input_capture,
            output
                .map(|line| format!("echo {}\r\n", line.replace('|', "^|")))
                .unwrap_or_default()
        );
        #[cfg(unix)]
        let standard_input_capture = if tool_name == "psql" {
            format!("cat >> '{}'\n", log_path.display())
        } else {
            String::new()
        };
        #[cfg(unix)]
        let script = format!(
            "#!/bin/sh\nprintf '%s\\n' \"$*\" >> '{}'\n{}{}exit 0\n",
            log_path.display(),
            standard_input_capture,
            output
                .map(|line| format!("printf '%s\\n' '{line}'\n"))
                .unwrap_or_default()
        );
        fs::write(&tool_path, script).unwrap();
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(&tool_path, fs::Permissions::from_mode(0o700)).unwrap();
        }
        tool_path
    }

    #[test]
    fn isolated_restore_uses_exact_derived_targets_and_remains_unadmitted() {
        let fixture = RestoreFixture::new();
        let target_environment_id = Uuid::new_v4();
        let plan = fixture.plan(target_environment_id);
        let tool = RestoreToolProbe {
            operations: Mutex::new(Vec::new()),
            fail_restore_service: None,
        };
        let report = RestoreRunner::new(tool)
            .run(&fixture.repository, &plan)
            .unwrap();
        assert_eq!(report.target_environment_id, target_environment_id);
        assert_eq!(report.members.len(), 3);
        assert!(report.admission_required);
        assert!(report.members.iter().all(|member| matches!(
            member,
            RestoredMember::Restored { database, .. } if database.starts_with("pixels_restore_")
        )));
    }

    #[test]
    fn restore_failure_stops_without_verifying_or_cleaning_the_failed_target() {
        let fixture = RestoreFixture::new();
        let plan = fixture.plan(Uuid::new_v4());
        let tool = RestoreToolProbe {
            operations: Mutex::new(Vec::new()),
            fail_restore_service: Some(BackupService::Auth),
        };
        let runner = RestoreRunner::new(tool);
        assert_eq!(
            runner.run(&fixture.repository, &plan),
            Err(RestoreExecutionError::RestoreFailed)
        );
        assert_eq!(
            runner.tool.operations.lock().unwrap().as_slice(),
            [
                "create:console",
                "restore:console",
                "verify:console",
                "create:auth",
                "restore:auth"
            ]
        );
    }

    #[test]
    fn source_database_and_arbitrary_target_names_are_rejected_before_tool_use() {
        let fixture = RestoreFixture::new();
        let mut plan = fixture.plan(Uuid::new_v4());
        plan.targets[0].database = "pixels_console".to_string();
        let tool = RestoreToolProbe {
            operations: Mutex::new(Vec::new()),
            fail_restore_service: None,
        };
        let runner = RestoreRunner::new(tool);
        assert_eq!(
            runner.run(&fixture.repository, &plan),
            Err(RestoreExecutionError::InvalidPlan)
        );
        assert!(runner.tool.operations.lock().unwrap().is_empty());
    }

    #[test]
    fn pinned_restore_tools_execute_fixed_commands_and_reject_post_start_tampering() {
        let temporary_directory = tempfile::tempdir().unwrap();
        make_private_directory(temporary_directory.path());
        let password_file = temporary_directory.path().join("restore.pgpass");
        px_private_files::private::create_private(&password_file, b"credential\n").unwrap();
        let log_path = temporary_directory.path().join("commands.log");
        let target_environment_id = Uuid::new_v4();
        let deployment_id = Uuid::new_v4();
        let target = RestoreDatabaseTarget {
            service: BackupService::Console,
            host: "127.0.0.1".to_string(),
            port: 5432,
            database: isolated_database_name(target_environment_id, BackupService::Console),
            owner: "pixels_console_owner".to_string(),
            username: "pixels_restore_operator".to_string(),
            password_file,
        };
        let verification_output = format!(
            "console|{}|{}|pixels_console_owner|27|true",
            deployment_id, target.database
        );
        let createdb = create_fake_tool(temporary_directory.path(), "createdb", &log_path, None);
        let pg_restore =
            create_fake_tool(temporary_directory.path(), "pg_restore", &log_path, None);
        let psql = create_fake_tool(
            temporary_directory.path(),
            "psql",
            &log_path,
            Some(&verification_output),
        );
        let tools = PinnedPgRestoreTools::new(
            createdb.clone(),
            hash_restore_tool(&createdb).unwrap(),
            pg_restore.clone(),
            hash_restore_tool(&pg_restore).unwrap(),
            psql.clone(),
            hash_restore_tool(&psql).unwrap(),
            Duration::from_secs(5),
            BackupCancellation::default(),
        )
        .unwrap();
        let archive_path = temporary_directory.path().join("console.dump");
        fs::write(&archive_path, b"archive").unwrap();
        tools.create_fresh_database(&target).unwrap();
        tools.restore_archive(&target, &archive_path).unwrap();
        tools
            .verify_restored_database(&target, deployment_id, 27)
            .unwrap();
        let command_log = fs::read_to_string(&log_path).unwrap();
        assert!(command_log.contains("--template=template0"));
        assert!(command_log.contains("--role pixels_console_owner"));
        assert!(command_log.contains("--set=ON_ERROR_STOP=1"));
        assert!(!command_log.contains("credential"));

        fs::write(&psql, b"tampered").unwrap();
        assert_eq!(
            tools.verify_restored_database(&target, deployment_id, 27),
            Err(RestoreExecutionError::ToolIdentity)
        );
    }

    #[test]
    fn restore_operator_provisioning_is_fixed_minimal_verified_and_secret_safe() {
        let temporary_directory = tempfile::tempdir().unwrap();
        make_private_directory(temporary_directory.path());
        let admin_password_file = temporary_directory.path().join("admin.pgpass");
        px_private_files::private::create_private(&admin_password_file, b"admin credential\n")
            .unwrap();
        let restore_password_file = temporary_directory.path().join("restore-password.secret");
        let restore_password = b"restore_Test-Password_0123456789abcdef";
        px_private_files::private::create_private(&restore_password_file, restore_password)
            .unwrap();
        let log_path = temporary_directory.path().join("provision.log");
        let verification_output = "true|true|false|false|false|false|pixels_auth_owner,pixels_console_owner,pixels_desk_owner|true";
        let psql = create_fake_tool(
            temporary_directory.path(),
            "psql",
            &log_path,
            Some(verification_output),
        );
        let provisioner = PinnedPgRestoreProvisioner::new(
            psql.clone(),
            hash_restore_tool(&psql).unwrap(),
            Duration::from_secs(5),
            BackupCancellation::default(),
        )
        .unwrap();
        let plan = RestoreOperatorProvisionPlan {
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
        };
        provisioner.provision(&plan).unwrap();
        let provision_log = fs::read_to_string(&log_path).unwrap();
        assert!(
            provision_log.contains("CREATEDB NOSUPERUSER NOCREATEROLE NOREPLICATION NOBYPASSRLS")
        );
        assert!(provision_log
            .contains("GRANT pixels_auth_owner,pixels_console_owner,pixels_desk_owner"));
        assert!(provision_log.contains("ALTER ROLE pixels_restore_operator RESET ALL"));
        assert!(!provision_log.contains(std::str::from_utf8(restore_password).unwrap()));

        let mut invalid_plan = plan;
        invalid_plan.services = BTreeSet::from([BackupService::Auth]);
        assert_eq!(
            provisioner.provision(&invalid_plan),
            Err(RestoreExecutionError::InvalidPlan)
        );
    }
}
