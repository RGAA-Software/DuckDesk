use fs2::FileExt;
use px_license::{Distribution, LicensePayload, LicenseTrustStore, Product, VerifyContext};
use px_private_files::private::{read_private, read_private_bounded, verify_private_directory};
use serde::{Deserialize, Serialize};
use std::{
    fs::{self, File, OpenOptions},
    io::Write,
    path::{Path, PathBuf},
    sync::{
        atomic::{AtomicI64, Ordering},
        Arc, Mutex,
    },
    time::{Duration, SystemTime, UNIX_EPOCH},
};
use tokio::task::JoinHandle;
use tokio_util::sync::CancellationToken;
use url::Url;
use uuid::Uuid;

const WATERMARK_SCHEMA_VERSION: u16 = 2;
const LICENSE_WIRE_LIMIT: u64 = 8192;
const ONLINE_REFRESH_SECONDS: u64 = 30;
const ONLINE_FAILURE_LIMIT_SECONDS: i64 = 40;

#[derive(Debug, thiserror::Error)]
#[error("Console license admission failed during {stage} validation")]
pub struct LicenseAdmissionError {
    stage: &'static str,
}

#[allow(non_upper_case_globals)]
const LicenseAdmissionError: LicenseAdmissionError = LicenseAdmissionError { stage: "general" };

pub struct LicenseLaunchConfig {
    distribution: Distribution,
    release_namespace: String,
    oem_id: Option<String>,
    machine_sha256: String,
    authority_deployment_id: Uuid,
    trust_store_file: PathBuf,
    license_file: PathBuf,
    watermark_directory: PathBuf,
    auth_verify_url: Option<Url>,
    auth_verify_ca: Option<PathBuf>,
}

#[derive(Clone)]
pub struct LicenseEntitlement {
    pub payload: LicensePayload,
    pub trusted_at: i64,
    online: Option<Arc<OnlineLicenseMonitor>>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct LicenseStatus {
    pub license_id: Uuid,
    pub revision: i64,
    pub distribution: Distribution,
    pub release_namespace: String,
    pub oem_id: Option<String>,
    pub mode: px_license::Mode,
    pub expires_at: i64,
    pub max_streams: u32,
    pub services: Vec<px_license::LicensedService>,
    pub last_authoritative_time: i64,
    pub online_fresh_until: Option<i64>,
}

struct OnlineLicenseMonitor {
    client: reqwest::Client,
    url: Url,
    wire: String,
    consumer_deployment_id: Uuid,
    distribution: Distribution,
    release_namespace: String,
    oem_id: Option<String>,
    machine_sha256: String,
    license_id: Uuid,
    revision: i64,
    watermark_store: Arc<WatermarkStore>,
    watermark: Mutex<LicenseWatermark>,
    refresh_lock: tokio::sync::Mutex<()>,
    last_authoritative_time: AtomicI64,
    last_success_local_time: AtomicI64,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct LicenseWatermark {
    schema_version: u16,
    consumer_deployment_id: Uuid,
    authority_deployment_id: Uuid,
    authority_recovery_generation: Uuid,
    product: Product,
    distribution: Distribution,
    release_namespace: String,
    oem_id: Option<String>,
    machine_sha256: String,
    license_id: Uuid,
    minimum_revision: i64,
    last_trusted_time: i64,
}

#[derive(Serialize)]
struct OnlineVerificationRequest<'a> {
    wire: &'a str,
    deployment_id: Uuid,
    product: Product,
    distribution: Distribution,
    release_namespace: &'a str,
    oem_id: Option<&'a str>,
    machine_sha256: &'a str,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct OnlineVerificationResponse {
    license_id: Uuid,
    revision: i64,
    verified_at: i64,
}

impl LicenseLaunchConfig {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        distribution: &str,
        release_namespace: String,
        oem_id: Option<String>,
        machine_sha256: String,
        authority_deployment_id: Uuid,
        trust_store_file: PathBuf,
        license_file: PathBuf,
        watermark_directory: PathBuf,
        auth_verify_url: Option<String>,
        auth_verify_ca: Option<PathBuf>,
        local_development: bool,
    ) -> Result<Self, LicenseAdmissionError> {
        let distribution = distribution
            .parse::<Distribution>()
            .map_err(|_| LicenseAdmissionError)?;
        distribution
            .validate_release_domain(&release_namespace, oem_id.as_deref())
            .map_err(|_| LicenseAdmissionError)?;
        if authority_deployment_id.is_nil() || !valid_hash(&machine_sha256) {
            return Err(LicenseAdmissionError);
        }
        let (auth_verify_url, auth_verify_ca) =
            match (distribution, auth_verify_url, auth_verify_ca) {
                (Distribution::Official, Some(value), certificate_authority) => {
                    let url = Url::parse(&value).map_err(|_| LicenseAdmissionError)?;
                    validate_official_url(&url, local_development)?;
                    (Some(url), certificate_authority)
                }
                (Distribution::Official, None, _) => return Err(LicenseAdmissionError),
                (Distribution::Customer | Distribution::Oem, None, None) => (None, None),
                (Distribution::Customer | Distribution::Oem, _, _) => {
                    return Err(LicenseAdmissionError)
                }
            };
        Ok(Self {
            distribution,
            release_namespace,
            oem_id,
            machine_sha256,
            authority_deployment_id,
            trust_store_file,
            license_file,
            watermark_directory,
            auth_verify_url,
            auth_verify_ca,
        })
    }

    pub async fn admit(
        self,
        consumer_deployment_id: Uuid,
    ) -> Result<LicenseEntitlement, LicenseAdmissionError> {
        let trust_bytes = read_private_bounded(&self.trust_store_file, 65536)
            .map_err(|_| LicenseAdmissionError)?;
        let trust_store = LicenseTrustStore::from_canonical_bytes(&trust_bytes)
            .map_err(|_| LicenseAdmissionError)?;
        if trust_store.authority_deployment_id != self.authority_deployment_id {
            return Err(LicenseAdmissionError);
        }
        let verifier = trust_store
            .verifier_set()
            .map_err(|_| LicenseAdmissionError)?;
        let wire_bytes = read_private_bounded(&self.license_file, LICENSE_WIRE_LIMIT)
            .map_err(|_| LicenseAdmissionError)?;
        let wire = std::str::from_utf8(&wire_bytes)
            .map_err(|_| LicenseAdmissionError)?
            .to_owned();
        verify_private_directory(&self.watermark_directory).map_err(|_| LicenseAdmissionError)?;
        let state = Arc::new(WatermarkStore::open(&self.watermark_directory)?);
        let previous = state.load(
            consumer_deployment_id,
            self.authority_deployment_id,
            trust_store.recovery_generation,
            self.distribution,
            &self.release_namespace,
            self.oem_id.as_deref(),
            &self.machine_sha256,
        )?;
        let minimum_revision = previous
            .as_ref()
            .map_or(1, |watermark| watermark.minimum_revision);
        let last_trusted_time = previous
            .as_ref()
            .map_or(0, |watermark| watermark.last_trusted_time);
        let online_client = match &self.auth_verify_url {
            Some(_) => Some(build_online_client(self.auth_verify_ca.as_deref())?),
            None => None,
        };
        let online = match (&self.auth_verify_url, &online_client) {
            (Some(url), Some(client)) => Some(
                verify_online(
                    client,
                    url,
                    &wire,
                    consumer_deployment_id,
                    self.distribution,
                    &self.release_namespace,
                    self.oem_id.as_deref(),
                    &self.machine_sha256,
                )
                .await?,
            ),
            (None, None) => None,
            _ => return Err(LicenseAdmissionError),
        };
        let trusted_at = online
            .as_ref()
            .map_or_else(current_unix_time, |response| Ok(response.verified_at))?;
        let payload = verifier
            .verify(
                &wire,
                &VerifyContext {
                    deployment_id: consumer_deployment_id,
                    product: Product::PixelsConsole,
                    distribution: self.distribution,
                    release_namespace: &self.release_namespace,
                    oem_id: self.oem_id.as_deref(),
                    machine_sha256: &self.machine_sha256,
                    now: trusted_at,
                    minimum_revision,
                    last_trusted_time,
                },
            )
            .map_err(|_| LicenseAdmissionError)?;
        if previous
            .as_ref()
            .is_some_and(|watermark| watermark.license_id != payload.license_id)
            || online.as_ref().is_some_and(|response| {
                response.license_id != payload.license_id || response.revision != payload.revision
            })
        {
            return Err(LicenseAdmissionError);
        }
        let watermark = LicenseWatermark {
            schema_version: WATERMARK_SCHEMA_VERSION,
            consumer_deployment_id,
            authority_deployment_id: self.authority_deployment_id,
            authority_recovery_generation: trust_store.recovery_generation,
            product: Product::PixelsConsole,
            distribution: self.distribution,
            release_namespace: self.release_namespace.clone(),
            oem_id: self.oem_id.clone(),
            machine_sha256: self.machine_sha256,
            license_id: payload.license_id,
            minimum_revision: payload.revision,
            last_trusted_time: trusted_at.max(last_trusted_time),
        };
        state.persist(&watermark)?;
        let local_admission_time = current_unix_time()?;
        let online_monitor = self
            .auth_verify_url
            .zip(online_client)
            .map(|(url, client)| {
                Arc::new(OnlineLicenseMonitor {
                    client,
                    url,
                    wire,
                    consumer_deployment_id,
                    distribution: self.distribution,
                    release_namespace: watermark.release_namespace.clone(),
                    oem_id: watermark.oem_id.clone(),
                    machine_sha256: watermark.machine_sha256.clone(),
                    license_id: payload.license_id,
                    revision: payload.revision,
                    watermark_store: state,
                    watermark: Mutex::new(watermark.clone()),
                    refresh_lock: tokio::sync::Mutex::new(()),
                    last_authoritative_time: AtomicI64::new(watermark.last_trusted_time),
                    last_success_local_time: AtomicI64::new(local_admission_time),
                })
            });
        Ok(LicenseEntitlement {
            payload,
            trusted_at: watermark.last_trusted_time,
            online: online_monitor,
        })
    }
}

impl LicenseEntitlement {
    pub fn validate_now(&self) -> Result<(), LicenseAdmissionError> {
        let now = current_unix_time()?;
        if now < self.trusted_at || now < self.payload.not_before || now >= self.payload.expires_at
        {
            return Err(LicenseAdmissionError);
        }
        if let Some(online) = &self.online {
            online.validate(now)?;
        }
        Ok(())
    }

    pub fn online_refresh_interval(&self) -> Option<Duration> {
        self.online
            .as_ref()
            .map(|_| Duration::from_secs(ONLINE_REFRESH_SECONDS))
    }

    pub async fn refresh_online(&self) -> Result<(), LicenseAdmissionError> {
        match &self.online {
            Some(online) => online.refresh().await,
            None => Ok(()),
        }
    }

    pub(crate) fn spawn_online_supervisor(
        &self,
        cancellation: CancellationToken,
    ) -> Option<JoinHandle<()>> {
        let refresh_period = self.online_refresh_interval()?;
        let entitlement = self.clone();
        Some(tokio::spawn(async move {
            let mut freshness_check = tokio::time::interval(Duration::from_secs(1));
            freshness_check.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
            let mut refresh = tokio::time::interval_at(
                tokio::time::Instant::now() + refresh_period,
                refresh_period,
            );
            refresh.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
            loop {
                tokio::select! {
                    biased;
                    _=cancellation.cancelled()=>break,
                    _=freshness_check.tick()=>if let Err(error)=entitlement.validate_now(){
                        tracing::error!("official license authority freshness expired");
                        eprintln!("Official license authority freshness expired: {error}");
                        cancellation.cancel();
                        break;
                    },
                    _=refresh.tick()=>if let Err(error)=entitlement.refresh_online().await{
                        tracing::warn!(%error, "official license authority refresh failed");
                        eprintln!("Official license authority refresh failed: {error}");
                    },
                }
            }
        }))
    }

    pub fn status(&self) -> LicenseStatus {
        let (last_authoritative_time, online_fresh_until) =
            self.online
                .as_ref()
                .map_or((self.trusted_at, None), |online| {
                    let authoritative = online.last_authoritative_time.load(Ordering::Acquire);
                    let fresh_until = online
                        .last_success_local_time
                        .load(Ordering::Acquire)
                        .saturating_add(ONLINE_FAILURE_LIMIT_SECONDS);
                    (authoritative, Some(fresh_until))
                });
        LicenseStatus {
            license_id: self.payload.license_id,
            revision: self.payload.revision,
            distribution: self.payload.distribution,
            release_namespace: self.payload.release_namespace.clone(),
            oem_id: self.payload.oem_id.clone(),
            mode: self.payload.mode,
            expires_at: self.payload.expires_at,
            max_streams: self.payload.max_streams,
            services: self.payload.services.clone(),
            last_authoritative_time,
            online_fresh_until,
        }
    }

    #[cfg(feature = "pg-integration")]
    pub fn synthetic_for_integration(deployment_id: Uuid) -> Self {
        use px_license::{LicensedService, Mode};
        Self {
            payload: LicensePayload {
                schema: 2,
                license_id: Uuid::new_v4(),
                deployment_id,
                product: Product::PixelsConsole,
                distribution: Distribution::Customer,
                release_namespace: "pixels.customer".into(),
                oem_id: None,
                machine_sha256: "f".repeat(64),
                revision: 1,
                mode: Mode::Licensed,
                issued_at: 0,
                not_before: 0,
                expires_at: 253402300799,
                max_streams: u32::MAX,
                services: vec![
                    LicensedService::CloudApplications,
                    LicensedService::Desktop,
                    LicensedService::Rdp,
                ],
                key_id: "f".repeat(64),
            },
            trusted_at: 0,
            online: None,
        }
    }
}

impl OnlineLicenseMonitor {
    fn validate(&self, now: i64) -> Result<(), LicenseAdmissionError> {
        let authoritative = self.last_authoritative_time.load(Ordering::Acquire);
        let local_success = self.last_success_local_time.load(Ordering::Acquire);
        if now < authoritative
            || now < local_success
            || now.saturating_sub(local_success) > ONLINE_FAILURE_LIMIT_SECONDS
        {
            return Err(LicenseAdmissionError {
                stage: "online-freshness",
            });
        }
        Ok(())
    }

    async fn refresh(&self) -> Result<(), LicenseAdmissionError> {
        let _refresh = self.refresh_lock.lock().await;
        let response = verify_online(
            &self.client,
            &self.url,
            &self.wire,
            self.consumer_deployment_id,
            self.distribution,
            &self.release_namespace,
            self.oem_id.as_deref(),
            &self.machine_sha256,
        )
        .await?;
        let previous_time = self.last_authoritative_time.load(Ordering::Acquire);
        if response.license_id != self.license_id
            || response.revision != self.revision
            || response.verified_at < previous_time
        {
            return Err(LicenseAdmissionError {
                stage: "online-response",
            });
        }
        let mut next = self
            .watermark
            .lock()
            .map_err(|_| LicenseAdmissionError {
                stage: "watermark-lock",
            })?
            .clone();
        next.last_trusted_time = response.verified_at;
        let watermark_store = self.watermark_store.clone();
        let persisted = next.clone();
        tokio::task::spawn_blocking(move || watermark_store.persist(&persisted))
            .await
            .map_err(|_| LicenseAdmissionError {
                stage: "watermark-task",
            })??;
        *self.watermark.lock().map_err(|_| LicenseAdmissionError {
            stage: "watermark-lock",
        })? = next;
        let local_time = current_unix_time()?;
        self.last_authoritative_time
            .store(response.verified_at, Ordering::Release);
        self.last_success_local_time
            .store(local_time, Ordering::Release);
        Ok(())
    }
}

async fn verify_online(
    client: &reqwest::Client,
    url: &Url,
    wire: &str,
    deployment_id: Uuid,
    distribution: Distribution,
    release_namespace: &str,
    oem_id: Option<&str>,
    machine_sha256: &str,
) -> Result<OnlineVerificationResponse, LicenseAdmissionError> {
    let response = client
        .post(url.clone())
        .json(&OnlineVerificationRequest {
            wire,
            deployment_id,
            product: Product::PixelsConsole,
            distribution,
            release_namespace,
            oem_id,
            machine_sha256,
        })
        .send()
        .await
        .map_err(|_| LicenseAdmissionError)?;
    if !response.status().is_success() {
        return Err(LicenseAdmissionError);
    }
    if response
        .content_length()
        .is_some_and(|length| length > 4096)
    {
        return Err(LicenseAdmissionError);
    }
    let bytes = response.bytes().await.map_err(|_| LicenseAdmissionError)?;
    if bytes.is_empty() || bytes.len() > 4096 {
        return Err(LicenseAdmissionError);
    }
    serde_json::from_slice::<OnlineVerificationResponse>(&bytes).map_err(|_| LicenseAdmissionError)
}

fn build_online_client(
    certificate_authority_path: Option<&Path>,
) -> Result<reqwest::Client, LicenseAdmissionError> {
    let mut builder = reqwest::Client::builder()
        .redirect(reqwest::redirect::Policy::none())
        .timeout(Duration::from_secs(5));
    if let Some(path) = certificate_authority_path {
        let bytes = fs::read(path).map_err(|_| LicenseAdmissionError)?;
        if bytes.is_empty() || bytes.len() > 65536 {
            return Err(LicenseAdmissionError);
        }
        let certificate =
            reqwest::Certificate::from_pem(&bytes).map_err(|_| LicenseAdmissionError)?;
        builder = builder.add_root_certificate(certificate);
    }
    builder.build().map_err(|_| LicenseAdmissionError)
}

fn validate_official_url(url: &Url, local_development: bool) -> Result<(), LicenseAdmissionError> {
    if url.username() != ""
        || url.password().is_some()
        || url.query().is_some()
        || url.fragment().is_some()
        || url.path() != "/api/auth/licenses/verify"
    {
        return Err(LicenseAdmissionError);
    }
    if url.scheme() == "https" && url.host_str().is_some() {
        return Ok(());
    }
    let loopback = url.host_str().is_some_and(|host| {
        host == "localhost"
            || host
                .parse::<std::net::IpAddr>()
                .is_ok_and(|ip| ip.is_loopback())
    });
    if local_development && url.scheme() == "http" && loopback {
        return Ok(());
    }
    Err(LicenseAdmissionError)
}

fn current_unix_time() -> Result<i64, LicenseAdmissionError> {
    let seconds = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| LicenseAdmissionError)?
        .as_secs();
    i64::try_from(seconds).map_err(|_| LicenseAdmissionError)
}

fn valid_hash(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

struct WatermarkStore {
    root: PathBuf,
    _lock: File,
}

impl WatermarkStore {
    fn open(root: &Path) -> Result<Self, LicenseAdmissionError> {
        let state_error = |stage| LicenseAdmissionError { stage };
        for entry in fs::read_dir(root).map_err(|_| state_error("watermark-directory"))? {
            let entry = entry.map_err(|_| state_error("watermark-directory-entry"))?;
            let name = entry.file_name();
            let name = name
                .to_str()
                .ok_or_else(|| state_error("watermark-entry-name"))?;
            if !matches!(
                name,
                "watermark.lock" | "watermark.json" | "watermark.previous" | "watermark.next"
            ) || !entry
                .file_type()
                .map_err(|_| state_error("watermark-entry-type"))?
                .is_file()
            {
                return Err(state_error("watermark-unknown-entry"));
            }
        }
        let lock_path = root.join("watermark.lock");
        let mut lock_options = OpenOptions::new();
        lock_options
            .read(true)
            .write(true)
            .create(true)
            .truncate(false);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            lock_options.mode(0o600).custom_flags(libc::O_NOFOLLOW);
        }
        #[cfg(windows)]
        {
            use std::os::windows::fs::OpenOptionsExt;
            lock_options.share_mode(3).custom_flags(0x00200000);
        }
        let mut lock = lock_options
            .open(lock_path)
            .map_err(|_| state_error("watermark-lock-open"))?;
        let lock_metadata = lock
            .metadata()
            .map_err(|_| state_error("watermark-lock-metadata"))?;
        if !lock_metadata.is_file() {
            return Err(state_error("watermark-lock-type"));
        }
        #[cfg(windows)]
        {
            use std::os::windows::fs::MetadataExt;
            if lock_metadata.file_attributes() & 0x400 != 0 {
                return Err(state_error("watermark-lock-reparse"));
            }
        }
        if lock_metadata.len() == 0 {
            lock.write_all(b"1")
                .map_err(|_| state_error("watermark-lock-write"))?;
            lock.sync_all()
                .map_err(|_| state_error("watermark-lock-sync"))?;
        }
        lock.try_lock_exclusive()
            .map_err(|_| state_error("watermark-lock-acquire"))?;
        Ok(Self {
            root: root.to_path_buf(),
            _lock: lock,
        })
    }

    fn load(
        &self,
        consumer_deployment_id: Uuid,
        authority_deployment_id: Uuid,
        authority_recovery_generation: Uuid,
        distribution: Distribution,
        release_namespace: &str,
        oem_id: Option<&str>,
        machine_sha256: &str,
    ) -> Result<Option<LicenseWatermark>, LicenseAdmissionError> {
        let state_error = || LicenseAdmissionError {
            stage: "watermark-load",
        };
        let current_path = self.root.join("watermark.json");
        let previous_path = self.root.join("watermark.previous");
        let next_path = self.root.join("watermark.next");
        let current = read_watermark(&current_path)?;
        let previous = read_watermark(&previous_path)?;
        let next = read_watermark(&next_path)?;
        let recovered = match (current, previous, next) {
            (None, None, None) => None,
            (Some(current), None, None) => Some(current),
            (Some(current), Some(_), None) => {
                fs::remove_file(previous_path).map_err(|_| state_error())?;
                Some(current)
            }
            (Some(current), None, Some(_)) => {
                fs::remove_file(next_path).map_err(|_| state_error())?;
                Some(current)
            }
            (None, Some(previous), None) => {
                fs::rename(previous_path, current_path).map_err(|_| state_error())?;
                Some(previous)
            }
            (None, Some(_), Some(next)) => {
                fs::rename(next_path, current_path).map_err(|_| state_error())?;
                fs::remove_file(previous_path).map_err(|_| state_error())?;
                Some(next)
            }
            (None, None, Some(next)) => {
                fs::rename(next_path, current_path).map_err(|_| state_error())?;
                Some(next)
            }
            _ => return Err(state_error()),
        };
        if recovered.as_ref().is_some_and(|watermark| {
            watermark.consumer_deployment_id != consumer_deployment_id
                || watermark.authority_deployment_id != authority_deployment_id
                || watermark.authority_recovery_generation != authority_recovery_generation
                || watermark.product != Product::PixelsConsole
                || watermark.distribution != distribution
                || watermark.release_namespace != release_namespace
                || watermark.oem_id.as_deref() != oem_id
                || watermark.machine_sha256 != machine_sha256
        }) {
            return Err(state_error());
        }
        Ok(recovered)
    }

    fn persist(&self, watermark: &LicenseWatermark) -> Result<(), LicenseAdmissionError> {
        let state_error = || LicenseAdmissionError {
            stage: "watermark-persist",
        };
        let current_path = self.root.join("watermark.json");
        if read_watermark(&current_path)?.as_ref() == Some(watermark) {
            return Ok(());
        }
        let previous_path = self.root.join("watermark.previous");
        let next_path = self.root.join("watermark.next");
        if previous_path.exists() || next_path.exists() {
            return Err(state_error());
        }
        let bytes = serde_json::to_vec(watermark).map_err(|_| state_error())?;
        let mut options = OpenOptions::new();
        options.write(true).create_new(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            options.mode(0o600).custom_flags(libc::O_NOFOLLOW);
        }
        #[cfg(windows)]
        {
            use std::os::windows::fs::OpenOptionsExt;
            options.share_mode(0);
        }
        let mut next = options.open(&next_path).map_err(|_| state_error())?;
        next.write_all(&bytes).map_err(|_| state_error())?;
        next.sync_all().map_err(|_| state_error())?;
        drop(next);
        if current_path.exists() {
            fs::rename(&current_path, &previous_path).map_err(|_| state_error())?;
        }
        fs::rename(&next_path, &current_path).map_err(|_| state_error())?;
        sync_directory(&self.root)?;
        if read_watermark(&current_path)?.as_ref() != Some(watermark) {
            return Err(state_error());
        }
        if previous_path.exists() {
            fs::remove_file(previous_path).map_err(|_| state_error())?;
            sync_directory(&self.root)?;
        }
        Ok(())
    }
}

fn read_watermark(path: &Path) -> Result<Option<LicenseWatermark>, LicenseAdmissionError> {
    if !path.exists() {
        return Ok(None);
    }
    let bytes = read_private(path).map_err(|_| LicenseAdmissionError)?;
    let watermark =
        serde_json::from_slice::<LicenseWatermark>(&bytes).map_err(|_| LicenseAdmissionError)?;
    if watermark.schema_version != WATERMARK_SCHEMA_VERSION
        || watermark.consumer_deployment_id.is_nil()
        || watermark.authority_deployment_id.is_nil()
        || watermark.authority_recovery_generation.is_nil()
        || watermark
            .distribution
            .validate_release_domain(&watermark.release_namespace, watermark.oem_id.as_deref())
            .is_err()
        || !valid_hash(&watermark.machine_sha256)
        || watermark.license_id.is_nil()
        || watermark.minimum_revision < 1
        || watermark.last_trusted_time < 0
        || serde_json::to_vec(&watermark).map_err(|_| LicenseAdmissionError)? != bytes.as_slice()
    {
        return Err(LicenseAdmissionError);
    }
    Ok(Some(watermark))
}

fn sync_directory(path: &Path) -> Result<(), LicenseAdmissionError> {
    #[cfg(windows)]
    {
        let _ = path;
        Ok(())
    }
    #[cfg(unix)]
    {
        File::open(path)
            .and_then(|directory| directory.sync_all())
            .map_err(|_| LicenseAdmissionError)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use px_license::{LicenseSigner, LicensedService, Mode};
    use px_private_files::private::create_private;

    struct Fixture {
        _temporary: tempfile::TempDir,
        deployment_id: Uuid,
        authority_deployment_id: Uuid,
        recovery_generation: Uuid,
        machine_sha256: String,
        state_directory: PathBuf,
        trust_store_file: PathBuf,
        license_file: PathBuf,
        signer: LicenseSigner,
        license_id: Uuid,
    }

    impl Fixture {
        fn new() -> Self {
            let temporary = tempfile::Builder::new()
                .prefix("pixels-console-license-")
                .tempdir()
                .unwrap();
            make_private(temporary.path());
            let material_directory = temporary.path().join("material");
            let state_directory = temporary.path().join("state");
            fs::create_dir(&material_directory).unwrap();
            fs::create_dir(&state_directory).unwrap();
            make_private(&material_directory);
            make_private(&state_directory);
            let signer = LicenseSigner::from_pkcs8(
                &hex::decode("3053020101300506032b6570042204209d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60a123032100d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a").unwrap(),
            )
            .unwrap();
            let authority_deployment_id = Uuid::new_v4();
            let recovery_generation = Uuid::new_v4();
            let trust_store = LicenseTrustStore::new(
                authority_deployment_id,
                recovery_generation,
                signer.public_key().try_into().unwrap(),
                [],
            )
            .unwrap();
            let trust_store_file = material_directory.join("trust.json");
            let license_file = material_directory.join("console.license");
            create_private(&trust_store_file, &trust_store.canonical_bytes().unwrap()).unwrap();
            let fixture = Self {
                _temporary: temporary,
                deployment_id: Uuid::new_v4(),
                authority_deployment_id,
                recovery_generation,
                machine_sha256: "a".repeat(64),
                state_directory,
                trust_store_file,
                license_file,
                signer,
                license_id: Uuid::new_v4(),
            };
            fixture.write_license(1, Distribution::Customer);
            fixture
        }

        fn write_license(&self, revision: i64, distribution: Distribution) {
            self.write_license_with(revision, distribution, &self.signer, self.license_id);
        }

        fn write_license_with(
            &self,
            revision: i64,
            distribution: Distribution,
            signer: &LicenseSigner,
            license_id: Uuid,
        ) {
            let now = current_unix_time().unwrap();
            let payload = LicensePayload {
                schema: 2,
                license_id,
                deployment_id: self.deployment_id,
                product: Product::PixelsConsole,
                distribution,
                release_namespace: match distribution {
                    Distribution::Official => "pixels.official".into(),
                    Distribution::Customer => "pixels.customer".into(),
                    Distribution::Oem => "oem.acme-cloud".into(),
                },
                oem_id: (distribution == Distribution::Oem).then(|| "acme-cloud".into()),
                machine_sha256: self.machine_sha256.clone(),
                revision,
                mode: Mode::Licensed,
                issued_at: now - 10,
                not_before: now - 10,
                expires_at: now + 3600,
                max_streams: 8,
                services: vec![
                    LicensedService::CloudApplications,
                    LicensedService::Desktop,
                    LicensedService::Rdp,
                ],
                key_id: signer.key_id(),
            };
            let wire = signer.sign(&payload).unwrap();
            if self.license_file.exists() {
                fs::write(&self.license_file, wire).unwrap();
            } else {
                create_private(&self.license_file, wire.as_bytes()).unwrap();
            }
        }

        fn config(&self) -> LicenseLaunchConfig {
            self.config_with_state(self.state_directory.clone())
        }

        fn config_with_state(&self, state_directory: PathBuf) -> LicenseLaunchConfig {
            LicenseLaunchConfig::new(
                "customer",
                "pixels.customer".into(),
                None,
                self.machine_sha256.clone(),
                self.authority_deployment_id,
                self.trust_store_file.clone(),
                self.license_file.clone(),
                state_directory,
                None,
                None,
                true,
            )
            .unwrap()
        }
    }

    #[tokio::test]
    async fn customer_watermark_advances_and_rejects_license_rollback() {
        let fixture = Fixture::new();
        let first = fixture.config().admit(fixture.deployment_id).await.unwrap();
        assert_eq!(first.payload.revision, 1);
        fixture.write_license(2, Distribution::Customer);
        let second = fixture.config().admit(fixture.deployment_id).await.unwrap();
        assert_eq!(second.payload.revision, 2);
        assert!(second.trusted_at >= first.trusted_at);
        fixture.write_license(1, Distribution::Customer);
        assert!(fixture.config().admit(fixture.deployment_id).await.is_err());
    }

    #[tokio::test]
    async fn oem_license_is_offline_and_bound_to_one_release_namespace() {
        let fixture = Fixture::new();
        fixture.write_license(1, Distribution::Oem);
        let configuration = LicenseLaunchConfig::new(
            "oem",
            "oem.acme-cloud".into(),
            Some("acme-cloud".into()),
            fixture.machine_sha256.clone(),
            fixture.authority_deployment_id,
            fixture.trust_store_file.clone(),
            fixture.license_file.clone(),
            fixture.state_directory.clone(),
            None,
            None,
            true,
        )
        .unwrap();
        let entitlement = configuration.admit(fixture.deployment_id).await.unwrap();
        assert_eq!(entitlement.payload.distribution, Distribution::Oem);
        assert_eq!(entitlement.payload.release_namespace, "oem.acme-cloud");
        assert!(LicenseLaunchConfig::new(
            "oem",
            "oem.north-star".into(),
            Some("north-star".into()),
            fixture.machine_sha256.clone(),
            fixture.authority_deployment_id,
            fixture.trust_store_file.clone(),
            fixture.license_file.clone(),
            fixture.state_directory.clone(),
            None,
            None,
            true,
        )
        .unwrap()
        .admit(fixture.deployment_id)
        .await
        .is_err());
    }

    #[tokio::test]
    async fn watermark_binds_authority_recovery_generation_and_rejects_unknown_files() {
        let fixture = Fixture::new();
        fixture.config().admit(fixture.deployment_id).await.unwrap();
        let replacement = LicenseTrustStore::new(
            fixture.authority_deployment_id,
            Uuid::new_v4(),
            fixture.signer.public_key().try_into().unwrap(),
            [],
        )
        .unwrap();
        fs::write(
            &fixture.trust_store_file,
            replacement.canonical_bytes().unwrap(),
        )
        .unwrap();
        assert!(fixture.config().admit(fixture.deployment_id).await.is_err());
        fs::write(fixture.state_directory.join("unexpected"), b"x").unwrap();
        assert!(fixture.config().admit(fixture.deployment_id).await.is_err());
    }

    #[tokio::test]
    async fn keyring_rotation_keeps_watermark_and_withdraws_the_old_key() {
        let fixture = Fixture::new();
        fixture.config().admit(fixture.deployment_id).await.unwrap();
        let replacement_key =
            ring::signature::Ed25519KeyPair::generate_pkcs8(&ring::rand::SystemRandom::new())
                .unwrap();
        let replacement_signer = LicenseSigner::from_pkcs8(replacement_key.as_ref()).unwrap();
        let rotating_trust_store = LicenseTrustStore::new(
            fixture.authority_deployment_id,
            fixture.recovery_generation,
            replacement_signer.public_key().try_into().unwrap(),
            [fixture.signer.public_key().try_into().unwrap()],
        )
        .unwrap();
        fs::write(
            &fixture.trust_store_file,
            rotating_trust_store.canonical_bytes().unwrap(),
        )
        .unwrap();
        fixture.write_license_with(
            2,
            Distribution::Customer,
            &replacement_signer,
            fixture.license_id,
        );
        let rotated = fixture.config().admit(fixture.deployment_id).await.unwrap();
        assert_eq!(rotated.payload.revision, 2);
        assert_eq!(rotated.payload.key_id, replacement_signer.key_id());

        let withdrawn_trust_store = LicenseTrustStore::new(
            fixture.authority_deployment_id,
            fixture.recovery_generation,
            replacement_signer.public_key().try_into().unwrap(),
            [],
        )
        .unwrap();
        fs::write(
            &fixture.trust_store_file,
            withdrawn_trust_store.canonical_bytes().unwrap(),
        )
        .unwrap();
        fixture.write_license_with(
            3,
            Distribution::Customer,
            &fixture.signer,
            fixture.license_id,
        );
        assert!(fixture.config().admit(fixture.deployment_id).await.is_err());
        fixture.write_license_with(
            3,
            Distribution::Customer,
            &replacement_signer,
            fixture.license_id,
        );
        assert_eq!(
            fixture
                .config()
                .admit(fixture.deployment_id)
                .await
                .unwrap()
                .payload
                .revision,
            3
        );
    }

    #[tokio::test]
    async fn recovery_generation_requires_a_new_approved_watermark_root() {
        let fixture = Fixture::new();
        fixture.config().admit(fixture.deployment_id).await.unwrap();
        let recovered_key =
            ring::signature::Ed25519KeyPair::generate_pkcs8(&ring::rand::SystemRandom::new())
                .unwrap();
        let recovered_signer = LicenseSigner::from_pkcs8(recovered_key.as_ref()).unwrap();
        let recovered_trust_store = LicenseTrustStore::new(
            fixture.authority_deployment_id,
            Uuid::new_v4(),
            recovered_signer.public_key().try_into().unwrap(),
            [],
        )
        .unwrap();
        fs::write(
            &fixture.trust_store_file,
            recovered_trust_store.canonical_bytes().unwrap(),
        )
        .unwrap();
        let recovered_license_id = Uuid::new_v4();
        fixture.write_license_with(
            1,
            Distribution::Customer,
            &recovered_signer,
            recovered_license_id,
        );
        assert!(fixture.config().admit(fixture.deployment_id).await.is_err());

        let recovered_state_directory = fixture
            .state_directory
            .parent()
            .unwrap()
            .join("recovered-state");
        fs::create_dir(&recovered_state_directory).unwrap();
        make_private(&recovered_state_directory);
        let recovered = fixture
            .config_with_state(recovered_state_directory)
            .admit(fixture.deployment_id)
            .await
            .unwrap();
        assert_eq!(recovered.payload.license_id, recovered_license_id);
        assert_eq!(recovered.payload.revision, 1);
    }

    #[tokio::test]
    async fn official_distribution_requires_matching_online_currentness_response() {
        use axum::{routing::post, Json, Router};
        use serde_json::json;
        use std::sync::atomic::AtomicUsize;

        let fixture = Fixture::new();
        fixture.write_license(1, Distribution::Official);
        let verified_at = current_unix_time().unwrap();
        let license_id = fixture.license_id;
        let requests = Arc::new(AtomicUsize::new(0));
        let server_requests = requests.clone();
        let application = Router::new().route(
            "/api/auth/licenses/verify",
            post(move || async move {
                server_requests.fetch_add(1, Ordering::Relaxed);
                Json(json!({
                    "license_id": license_id,
                    "revision": 1,
                    "verified_at": verified_at
                }))
            }),
        );
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let server = tokio::spawn(async move { axum::serve(listener, application).await.unwrap() });
        let config = LicenseLaunchConfig::new(
            "official",
            "pixels.official".into(),
            None,
            fixture.machine_sha256.clone(),
            fixture.authority_deployment_id,
            fixture.trust_store_file.clone(),
            fixture.license_file.clone(),
            fixture.state_directory.clone(),
            Some(format!("http://{address}/api/auth/licenses/verify")),
            None,
            true,
        )
        .unwrap();
        let entitlement = config.admit(fixture.deployment_id).await.unwrap();
        assert_eq!(entitlement.payload.license_id, fixture.license_id);
        assert_eq!(entitlement.trusted_at, verified_at);
        assert_eq!(
            entitlement.online_refresh_interval(),
            Some(Duration::from_secs(30))
        );
        entitlement.refresh_online().await.unwrap();
        assert_eq!(requests.load(Ordering::Relaxed), 2);
        server.abort();
        let _ = server.await;
        assert!(entitlement.refresh_online().await.is_err());
        let monitor = entitlement.online.as_ref().unwrap();
        monitor.last_success_local_time.store(
            current_unix_time().unwrap() - ONLINE_FAILURE_LIMIT_SECONDS - 1,
            Ordering::Release,
        );
        assert!(entitlement.validate_now().is_err());
        let cancellation = CancellationToken::new();
        let supervisor = entitlement
            .spawn_online_supervisor(cancellation.clone())
            .unwrap();
        tokio::time::timeout(Duration::from_secs(1), cancellation.cancelled())
            .await
            .unwrap();
        supervisor.await.unwrap();
    }

    #[test]
    fn official_and_customer_endpoint_policies_are_disjoint() {
        let fixture = Fixture::new();
        assert!(LicenseLaunchConfig::new(
            "official",
            "pixels.official".into(),
            None,
            fixture.machine_sha256.clone(),
            fixture.authority_deployment_id,
            fixture.trust_store_file.clone(),
            fixture.license_file.clone(),
            fixture.state_directory.clone(),
            Some("https://auth.example.test/api/auth/licenses/verify".into()),
            None,
            false,
        )
        .is_ok());
        assert!(LicenseLaunchConfig::new(
            "customer",
            "pixels.customer".into(),
            None,
            fixture.machine_sha256.clone(),
            fixture.authority_deployment_id,
            fixture.trust_store_file.clone(),
            fixture.license_file.clone(),
            fixture.state_directory.clone(),
            Some("https://auth.example.test/api/auth/licenses/verify".into()),
            None,
            false,
        )
        .is_err());
    }

    fn make_private(path: &Path) {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(path, fs::Permissions::from_mode(0o700)).unwrap();
        }
        #[cfg(windows)]
        {
            use std::{os::windows::process::CommandExt, process::Command};
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
}
