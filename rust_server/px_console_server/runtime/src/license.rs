use px_license::{LicensePayload, LicenseTrustStore, VerifyContext};
use px_private_files::private::{create_private, read_private_bounded, replace_private};
use serde::Serialize;
use std::{
    path::PathBuf,
    time::{SystemTime, UNIX_EPOCH},
};
use uuid::Uuid;

const LICENSE_WIRE_LIMIT: u64 = 4096;

#[derive(Debug, thiserror::Error)]
#[error("Console license admission failed")]
pub struct LicenseAdmissionError;

#[derive(Clone)]
pub struct LicenseLaunchConfig {
    trust_store_file: PathBuf,
    license_file: PathBuf,
}

#[derive(Clone)]
pub struct LicenseEntitlement {
    pub payload: LicensePayload,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct LicenseStatus {
    pub license_id: Uuid,
    pub revision: i64,
    pub expires_at: i64,
    pub max_streams: u32,
    pub services: Vec<px_license::LicensedService>,
}

impl LicenseLaunchConfig {
    pub fn new(
        trust_store_file: PathBuf,
        license_file: PathBuf,
    ) -> Result<Self, LicenseAdmissionError> {
        if trust_store_file == license_file {
            return Err(LicenseAdmissionError);
        }
        Ok(Self {
            trust_store_file,
            license_file,
        })
    }

    pub async fn admit(
        self,
        deployment_id: Uuid,
    ) -> Result<LicenseEntitlement, LicenseAdmissionError> {
        let trust_bytes = read_private_bounded(&self.trust_store_file, 65536)
            .map_err(|_| LicenseAdmissionError)?;
        let trust_store = LicenseTrustStore::from_canonical_bytes(&trust_bytes)
            .map_err(|_| LicenseAdmissionError)?;
        let verifier = trust_store
            .verifier_set()
            .map_err(|_| LicenseAdmissionError)?;
        let wire_bytes = read_private_bounded(&self.license_file, LICENSE_WIRE_LIMIT)
            .map_err(|_| LicenseAdmissionError)?;
        let wire = std::str::from_utf8(&wire_bytes).map_err(|_| LicenseAdmissionError)?;
        let payload = verifier
            .verify(
                wire,
                &VerifyContext::new(deployment_id, current_unix_time()?),
            )
            .map_err(|_| LicenseAdmissionError)?;
        Ok(LicenseEntitlement { payload })
    }

    pub async fn admit_if_present(
        self,
        deployment_id: Uuid,
    ) -> Result<Option<LicenseEntitlement>, LicenseAdmissionError> {
        if !self.license_file.exists() {
            return Ok(None);
        }
        self.admit(deployment_id).await.map(Some)
    }

    pub fn install(
        &self,
        deployment_id: Uuid,
        wire: &str,
    ) -> Result<LicenseEntitlement, LicenseAdmissionError> {
        if wire.is_empty() || wire.len() as u64 > LICENSE_WIRE_LIMIT {
            return Err(LicenseAdmissionError);
        }
        let trust_bytes = read_private_bounded(&self.trust_store_file, 65536)
            .map_err(|_| LicenseAdmissionError)?;
        let trust_store = LicenseTrustStore::from_canonical_bytes(&trust_bytes)
            .map_err(|_| LicenseAdmissionError)?;
        let verifier = trust_store
            .verifier_set()
            .map_err(|_| LicenseAdmissionError)?;
        let payload = verifier
            .verify(
                wire,
                &VerifyContext::new(deployment_id, current_unix_time()?),
            )
            .map_err(|_| LicenseAdmissionError)?;
        let persist_result = if self.license_file.exists() {
            replace_private(&self.license_file, wire.as_bytes())
        } else {
            create_private(&self.license_file, wire.as_bytes())
        };
        persist_result.map_err(|_| LicenseAdmissionError)?;
        let stored_bytes = read_private_bounded(&self.license_file, LICENSE_WIRE_LIMIT)
            .map_err(|_| LicenseAdmissionError)?;
        if stored_bytes.as_slice() != wire.as_bytes() {
            return Err(LicenseAdmissionError);
        }
        Ok(LicenseEntitlement { payload })
    }
}

impl LicenseEntitlement {
    pub fn validate_now(&self) -> Result<(), LicenseAdmissionError> {
        let now = current_unix_time()?;
        if now < self.payload.issued_at || now >= self.payload.expires_at {
            return Err(LicenseAdmissionError);
        }
        Ok(())
    }

    pub fn status(&self) -> LicenseStatus {
        LicenseStatus {
            license_id: self.payload.license_id,
            revision: self.payload.revision,
            expires_at: self.payload.expires_at,
            max_streams: self.payload.max_streams,
            services: self.payload.services.clone(),
        }
    }

    #[cfg(feature = "pg-integration")]
    pub fn synthetic_for_integration(deployment_id: Uuid) -> Self {
        use px_license::LicensedService;
        Self {
            payload: LicensePayload {
                schema: 2,
                license_id: Uuid::new_v4(),
                deployment_id,
                revision: 1,
                issued_at: 0,
                expires_at: 253402300799,
                max_streams: u32::MAX,
                services: vec![
                    LicensedService::CloudApplications,
                    LicensedService::Desktop,
                    LicensedService::Rdp,
                ],
                key_id: "f".repeat(64),
            },
        }
    }
}

fn current_unix_time() -> Result<i64, LicenseAdmissionError> {
    let seconds = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| LicenseAdmissionError)?
        .as_secs();
    i64::try_from(seconds).map_err(|_| LicenseAdmissionError)
}

#[cfg(test)]
mod tests {
    use super::*;
    use px_license::{LicenseSigner, LicensedService};
    use px_private_files::private::create_private;

    fn signer() -> LicenseSigner {
        LicenseSigner::from_pkcs8(&hex::decode(
            "3053020101300506032b6570042204209d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60a123032100d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
        ).unwrap()).unwrap()
    }

    #[tokio::test]
    async fn signed_license_is_bound_only_to_deployment_and_expiration() {
        let temporary = tempfile::tempdir().unwrap();
        make_private(temporary.path());
        let signing_key = signer();
        let deployment_id = Uuid::new_v4();
        let now = current_unix_time().unwrap();
        let payload = LicensePayload {
            schema: 2,
            license_id: Uuid::new_v4(),
            deployment_id,
            revision: 1,
            issued_at: now - 1,
            expires_at: now + 60,
            max_streams: 4,
            services: vec![LicensedService::Desktop],
            key_id: signing_key.key_id(),
        };
        let trust_store =
            LicenseTrustStore::new(signing_key.public_key().try_into().unwrap(), []).unwrap();
        let trust_path = temporary.path().join("trust.json");
        let license_path = temporary.path().join("console.license");
        create_private(&trust_path, &trust_store.canonical_bytes().unwrap()).unwrap();
        create_private(
            &license_path,
            signing_key.sign(&payload).unwrap().as_bytes(),
        )
        .unwrap();

        let entitlement = LicenseLaunchConfig::new(trust_path.clone(), license_path.clone())
            .unwrap()
            .admit(deployment_id)
            .await
            .unwrap();
        assert_eq!(entitlement.status().max_streams, 4);
        assert!(LicenseLaunchConfig::new(trust_path, license_path)
            .unwrap()
            .admit(Uuid::new_v4())
            .await
            .is_err());
    }

    #[tokio::test]
    async fn administrator_can_install_and_replace_only_a_valid_deployment_license() {
        let temporary = tempfile::tempdir().unwrap();
        make_private(temporary.path());
        let signing_key = signer();
        let deployment_id = Uuid::new_v4();
        let now = current_unix_time().unwrap();
        let trust_store =
            LicenseTrustStore::new(signing_key.public_key().try_into().unwrap(), []).unwrap();
        let trust_path = temporary.path().join("trust.json");
        let license_path = temporary.path().join("console.license");
        create_private(&trust_path, &trust_store.canonical_bytes().unwrap()).unwrap();
        let configuration = LicenseLaunchConfig::new(trust_path, license_path.clone()).unwrap();
        assert!(configuration
            .clone()
            .admit_if_present(deployment_id)
            .await
            .unwrap()
            .is_none());

        let make_wire = |license_deployment: Uuid, revision: i64| {
            signing_key
                .sign(&LicensePayload {
                    schema: 2,
                    license_id: Uuid::new_v4(),
                    deployment_id: license_deployment,
                    revision,
                    issued_at: now - 1,
                    expires_at: now + 60,
                    max_streams: 4,
                    services: vec![LicensedService::Desktop],
                    key_id: signing_key.key_id(),
                })
                .unwrap()
        };
        let first_wire = make_wire(deployment_id, 1);
        assert_eq!(
            configuration
                .install(deployment_id, &first_wire)
                .unwrap()
                .status()
                .revision,
            1
        );
        assert!(configuration
            .install(deployment_id, &make_wire(Uuid::new_v4(), 2))
            .is_err());
        assert_eq!(
            read_private_bounded(&license_path, LICENSE_WIRE_LIMIT)
                .unwrap()
                .as_slice(),
            first_wire.as_bytes()
        );

        let replacement_wire = make_wire(deployment_id, 2);
        assert_eq!(
            configuration
                .install(deployment_id, &replacement_wire)
                .unwrap()
                .status()
                .revision,
            2
        );
        assert_eq!(
            configuration
                .clone()
                .admit(deployment_id)
                .await
                .unwrap()
                .status()
                .revision,
            2
        );
    }

    fn make_private(path: &std::path::Path) {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o700)).unwrap();
        }
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            let current_identity = std::process::Command::new("whoami")
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(current_identity.status.success());
            let identity_access = format!(
                "{}:(OI)(CI)F",
                String::from_utf8(current_identity.stdout).unwrap().trim()
            );
            let access_result = std::process::Command::new("icacls")
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
}
