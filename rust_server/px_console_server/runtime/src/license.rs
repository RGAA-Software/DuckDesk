use fs2::FileExt;
use px_license::{Distribution, LicensePayload, LicenseTrustStore, Product, VerifyContext};
use px_private_files::private::{read_private, read_private_bounded, verify_private_directory};
use serde::{Deserialize, Serialize};
use std::{
    fs::{self, File, OpenOptions},
    io::Write,
    path::{Path, PathBuf},
    time::{Duration, SystemTime, UNIX_EPOCH},
};
use url::Url;
use uuid::Uuid;

const WATERMARK_SCHEMA_VERSION: u16 = 1;
const LICENSE_WIRE_LIMIT: u64 = 8192;

#[derive(Debug, thiserror::Error)]
#[error("Console license admission failed during {stage} validation")]
pub struct LicenseAdmissionError {
    stage: &'static str,
}

#[allow(non_upper_case_globals)]
const LicenseAdmissionError: LicenseAdmissionError = LicenseAdmissionError { stage: "general" };

pub struct LicenseLaunchConfig {
    distribution: Distribution,
    machine_sha256: String,
    authority_deployment_id: Uuid,
    trust_store_file: PathBuf,
    license_file: PathBuf,
    watermark_directory: PathBuf,
    auth_verify_url: Option<Url>,
}

#[derive(Clone)]
pub struct LicenseEntitlement {
    pub payload: LicensePayload,
    pub trusted_at: i64,
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
        machine_sha256: String,
        authority_deployment_id: Uuid,
        trust_store_file: PathBuf,
        license_file: PathBuf,
        watermark_directory: PathBuf,
        auth_verify_url: Option<String>,
        local_development: bool,
    ) -> Result<Self, LicenseAdmissionError> {
        let distribution = match distribution {
            "official" => Distribution::Official,
            "customer" => Distribution::Customer,
            _ => return Err(LicenseAdmissionError),
        };
        if authority_deployment_id.is_nil() || !valid_hash(&machine_sha256) {
            return Err(LicenseAdmissionError);
        }
        let auth_verify_url = match (distribution, auth_verify_url) {
            (Distribution::Official, Some(value)) => {
                let url = Url::parse(&value).map_err(|_| LicenseAdmissionError)?;
                validate_official_url(&url, local_development)?;
                Some(url)
            }
            (Distribution::Official, None) => return Err(LicenseAdmissionError),
            (Distribution::Customer, None) => None,
            (Distribution::Customer, Some(_)) => return Err(LicenseAdmissionError),
        };
        Ok(Self {
            distribution,
            machine_sha256,
            authority_deployment_id,
            trust_store_file,
            license_file,
            watermark_directory,
            auth_verify_url,
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
        let wire = std::str::from_utf8(&wire_bytes).map_err(|_| LicenseAdmissionError)?;
        verify_private_directory(&self.watermark_directory).map_err(|_| LicenseAdmissionError)?;
        let state = WatermarkStore::open(&self.watermark_directory)?;
        let previous = state.load(
            consumer_deployment_id,
            self.authority_deployment_id,
            trust_store.recovery_generation,
            self.distribution,
            &self.machine_sha256,
        )?;
        let minimum_revision = previous
            .as_ref()
            .map_or(1, |watermark| watermark.minimum_revision);
        let last_trusted_time = previous
            .as_ref()
            .map_or(0, |watermark| watermark.last_trusted_time);
        let online = match &self.auth_verify_url {
            Some(url) => Some(
                verify_online(
                    url,
                    wire,
                    consumer_deployment_id,
                    self.distribution,
                    &self.machine_sha256,
                )
                .await?,
            ),
            None => None,
        };
        let trusted_at = online
            .as_ref()
            .map_or_else(current_unix_time, |response| Ok(response.verified_at))?;
        let payload = verifier
            .verify(
                wire,
                &VerifyContext {
                    deployment_id: consumer_deployment_id,
                    product: Product::PixelsConsole,
                    distribution: self.distribution,
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
            machine_sha256: self.machine_sha256,
            license_id: payload.license_id,
            minimum_revision: payload.revision,
            last_trusted_time: trusted_at.max(last_trusted_time),
        };
        state.persist(&watermark)?;
        Ok(LicenseEntitlement {
            payload,
            trusted_at: watermark.last_trusted_time,
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
        Ok(())
    }

    #[cfg(feature = "pg-integration")]
    pub fn synthetic_for_integration(deployment_id: Uuid) -> Self {
        use px_license::{Feature, Mode};
        Self {
            payload: LicensePayload {
                schema: 1,
                license_id: Uuid::new_v4(),
                deployment_id,
                product: Product::PixelsConsole,
                distribution: Distribution::Customer,
                machine_sha256: "f".repeat(64),
                revision: 1,
                mode: Mode::Licensed,
                issued_at: 0,
                not_before: 0,
                expires_at: 253402300799,
                max_devices: u32::MAX,
                max_sessions: u32::MAX,
                features: vec![Feature::CloudApplications, Feature::Desktop, Feature::Rdp],
                key_id: "f".repeat(64),
            },
            trusted_at: 0,
        }
    }
}

async fn verify_online(
    url: &Url,
    wire: &str,
    deployment_id: Uuid,
    distribution: Distribution,
    machine_sha256: &str,
) -> Result<OnlineVerificationResponse, LicenseAdmissionError> {
    let client = reqwest::Client::builder()
        .redirect(reqwest::redirect::Policy::none())
        .timeout(Duration::from_secs(5))
        .build()
        .map_err(|_| LicenseAdmissionError)?;
    let response = client
        .post(url.clone())
        .json(&OnlineVerificationRequest {
            wire,
            deployment_id,
            product: Product::PixelsConsole,
            distribution,
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

fn validate_official_url(url: &Url, local_development: bool) -> Result<(), LicenseAdmissionError> {
    if url.username() != ""
        || url.password().is_some()
        || url.query().is_some()
        || url.fragment().is_some()
        || url.path() != "/api/auth/licenses/verify"
        || url.port() == Some(20371)
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
    use px_license::{Feature, LicenseSigner, Mode};
    use px_private_files::private::create_private;

    struct Fixture {
        _temporary: tempfile::TempDir,
        deployment_id: Uuid,
        authority_deployment_id: Uuid,
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
            let now = current_unix_time().unwrap();
            let payload = LicensePayload {
                schema: 1,
                license_id: self.license_id,
                deployment_id: self.deployment_id,
                product: Product::PixelsConsole,
                distribution,
                machine_sha256: self.machine_sha256.clone(),
                revision,
                mode: Mode::Licensed,
                issued_at: now - 10,
                not_before: now - 10,
                expires_at: now + 3600,
                max_devices: 4,
                max_sessions: 8,
                features: vec![Feature::CloudApplications, Feature::Desktop, Feature::Rdp],
                key_id: self.signer.key_id(),
            };
            let wire = self.signer.sign(&payload).unwrap();
            if self.license_file.exists() {
                fs::write(&self.license_file, wire).unwrap();
            } else {
                create_private(&self.license_file, wire.as_bytes()).unwrap();
            }
        }

        fn config(&self) -> LicenseLaunchConfig {
            LicenseLaunchConfig::new(
                "customer",
                self.machine_sha256.clone(),
                self.authority_deployment_id,
                self.trust_store_file.clone(),
                self.license_file.clone(),
                self.state_directory.clone(),
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
    async fn official_distribution_requires_matching_online_currentness_response() {
        use axum::{routing::post, Json, Router};
        use serde_json::json;

        let fixture = Fixture::new();
        fixture.write_license(1, Distribution::Official);
        let verified_at = current_unix_time().unwrap();
        let license_id = fixture.license_id;
        let application = Router::new().route(
            "/api/auth/licenses/verify",
            post(move || async move {
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
            fixture.machine_sha256.clone(),
            fixture.authority_deployment_id,
            fixture.trust_store_file.clone(),
            fixture.license_file.clone(),
            fixture.state_directory.clone(),
            Some(format!("http://{address}/api/auth/licenses/verify")),
            true,
        )
        .unwrap();
        let entitlement = config.admit(fixture.deployment_id).await.unwrap();
        assert_eq!(entitlement.payload.license_id, fixture.license_id);
        assert_eq!(entitlement.trusted_at, verified_at);
        server.abort();
        let _ = server.await;
    }

    #[test]
    fn official_and_customer_endpoint_policies_are_disjoint() {
        let fixture = Fixture::new();
        assert!(LicenseLaunchConfig::new(
            "official",
            fixture.machine_sha256.clone(),
            fixture.authority_deployment_id,
            fixture.trust_store_file.clone(),
            fixture.license_file.clone(),
            fixture.state_directory.clone(),
            Some("https://auth.example.test/api/auth/licenses/verify".into()),
            false,
        )
        .is_ok());
        assert!(LicenseLaunchConfig::new(
            "customer",
            fixture.machine_sha256.clone(),
            fixture.authority_deployment_id,
            fixture.trust_store_file.clone(),
            fixture.license_file.clone(),
            fixture.state_directory.clone(),
            Some("https://auth.example.test/api/auth/licenses/verify".into()),
            false,
        )
        .is_err());
        assert!(LicenseLaunchConfig::new(
            "official",
            fixture.machine_sha256.clone(),
            fixture.authority_deployment_id,
            fixture.trust_store_file.clone(),
            fixture.license_file.clone(),
            fixture.state_directory.clone(),
            Some("https://auth.example.test:20371/api/auth/licenses/verify".into()),
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
