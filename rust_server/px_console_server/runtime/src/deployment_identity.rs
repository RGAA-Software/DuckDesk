use crate::{error::ApiError, StateData};
use axum::{
    extract::State,
    http::{header, Method},
    routing::{get, post},
    Json, Router,
};
use px_deployment_identity::{
    AuthenticationMethod, ChallengePayload, DeploymentCertificate, DeploymentIdentitySigner,
    DeploymentIdentityVerifier, DeploymentKind, DeploymentTrustStore, PlatformDescriptor,
    RegistrationPolicy, SignedDeploymentIdentity,
};
use px_license::Distribution;
use px_private_files::private::read_private_bounded;
use serde::{Deserialize, Serialize};
use std::{
    path::PathBuf,
    sync::Arc,
    time::{SystemTime, UNIX_EPOCH},
};
use tower_http::cors::{Any, CorsLayer};
use uuid::Uuid;

const CERTIFICATE_LIMIT: u64 = 16 * 1024;
const SIGNING_KEY_LIMIT: u64 = 8 * 1024;
const TRUST_STORE_LIMIT: u64 = 64 * 1024;
const DESCRIPTOR_LIFETIME_SECONDS: i64 = 300;
const CHALLENGE_LIFETIME_SECONDS: i64 = 30;

#[derive(Debug, thiserror::Error)]
#[error("invalid deployment identity runtime configuration")]
pub struct DeploymentIdentityRuntimeError;

pub struct DeploymentIdentityLaunchConfig {
    certificate_path: PathBuf,
    signing_key_path: PathBuf,
    trust_store_path: PathBuf,
    minimum_certificate_version: u64,
    descriptor_revision: u64,
    trust_epoch: u64,
    minimum_client_build: u64,
    registration_enabled: bool,
    guests_enabled: bool,
}

pub struct DeploymentIdentityRuntime {
    certificate_wire: String,
    certificate: DeploymentCertificate,
    signer: DeploymentIdentitySigner,
    descriptor_revision: u64,
    trust_epoch: u64,
    minimum_client_build: u64,
    authentication_methods: Vec<AuthenticationMethod>,
    registration_policy: RegistrationPolicy,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct ChallengeRequest {
    nonce: String,
    descriptor_revision: u64,
}

#[derive(Serialize)]
struct ChallengeResponse {
    proof_wire: String,
}

impl DeploymentIdentityLaunchConfig {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        certificate_path: PathBuf,
        signing_key_path: PathBuf,
        trust_store_path: PathBuf,
        minimum_certificate_version: u64,
        descriptor_revision: u64,
        trust_epoch: u64,
        minimum_client_build: u64,
        registration_enabled: bool,
        guests_enabled: bool,
    ) -> Result<Self, DeploymentIdentityRuntimeError> {
        if certificate_path.as_os_str().is_empty()
            || signing_key_path.as_os_str().is_empty()
            || trust_store_path.as_os_str().is_empty()
            || minimum_certificate_version == 0
            || descriptor_revision == 0
            || trust_epoch == 0
            || minimum_client_build == 0
        {
            return Err(DeploymentIdentityRuntimeError);
        }
        Ok(Self {
            certificate_path,
            signing_key_path,
            trust_store_path,
            minimum_certificate_version,
            descriptor_revision,
            trust_epoch,
            minimum_client_build,
            registration_enabled,
            guests_enabled,
        })
    }

    pub async fn load(
        self,
        expected_deployment_id: Uuid,
        distribution: Distribution,
    ) -> Result<DeploymentIdentityRuntime, DeploymentIdentityRuntimeError> {
        let certificate_path = self.certificate_path.clone();
        let signing_key_path = self.signing_key_path.clone();
        let trust_store_path = self.trust_store_path.clone();
        let (certificate_bytes, signing_key_bytes, trust_store_bytes) =
            tokio::task::spawn_blocking(move || {
                Ok::<_, DeploymentIdentityRuntimeError>((
                    read_private_bounded(&certificate_path, CERTIFICATE_LIMIT)
                        .map_err(|_| DeploymentIdentityRuntimeError)?,
                    read_private_bounded(&signing_key_path, SIGNING_KEY_LIMIT)
                        .map_err(|_| DeploymentIdentityRuntimeError)?,
                    read_private_bounded(&trust_store_path, TRUST_STORE_LIMIT)
                        .map_err(|_| DeploymentIdentityRuntimeError)?,
                ))
            })
            .await
            .map_err(|_| DeploymentIdentityRuntimeError)??;
        let certificate_wire = String::from_utf8(certificate_bytes.to_vec())
            .map_err(|_| DeploymentIdentityRuntimeError)?;
        let trust_store = DeploymentTrustStore::from_canonical_bytes(&trust_store_bytes)
            .map_err(|_| DeploymentIdentityRuntimeError)?;
        if trust_store.trust_epoch != self.trust_epoch {
            return Err(DeploymentIdentityRuntimeError);
        }
        let expected_kind = match distribution {
            Distribution::Official => DeploymentKind::Official,
            Distribution::Customer => DeploymentKind::Private,
        };
        let verifier = DeploymentIdentityVerifier::new(&trust_store)
            .map_err(|_| DeploymentIdentityRuntimeError)?;
        let now = current_unix_time()?;
        let certificate = verifier
            .verify_certificate(
                &certificate_wire,
                Some(expected_deployment_id),
                expected_kind,
                now,
                self.minimum_certificate_version,
            )
            .map_err(|_| DeploymentIdentityRuntimeError)?;
        let signer = DeploymentIdentitySigner::from_pkcs8(&signing_key_bytes)
            .map_err(|_| DeploymentIdentityRuntimeError)?;
        let mut authentication_methods = vec![AuthenticationMethod::Password];
        if self.guests_enabled {
            authentication_methods.push(AuthenticationMethod::Guest);
            authentication_methods.sort();
        }
        let runtime = DeploymentIdentityRuntime {
            certificate_wire,
            certificate,
            signer,
            descriptor_revision: self.descriptor_revision,
            trust_epoch: self.trust_epoch,
            minimum_client_build: self.minimum_client_build,
            authentication_methods,
            registration_policy: if self.registration_enabled {
                RegistrationPolicy::Open
            } else {
                RegistrationPolicy::Closed
            },
        };
        runtime.signed_identity(now)?;
        Ok(runtime)
    }
}

impl DeploymentIdentityRuntime {
    fn descriptor(&self, now: i64) -> PlatformDescriptor {
        PlatformDescriptor {
            schema_version: 1,
            deployment_id: self.certificate.deployment_id,
            deployment_kind: self.certificate.deployment_kind,
            descriptor_revision: self.descriptor_revision,
            trust_epoch: self.trust_epoch,
            issued_at: now,
            expires_at: now + DESCRIPTOR_LIFETIME_SECONDS,
            minimum_client_build: self.minimum_client_build,
            api_versions: vec!["console.v1".to_string(), "node.v1".to_string()],
            minimum_protocol_version: 1,
            maximum_protocol_version: 1,
            authentication_methods: self.authentication_methods.clone(),
            registration_policy: self.registration_policy,
            console_api_path: "/api/console".to_string(),
            node_control_path: "/api/console/node-control".to_string(),
        }
    }

    fn signed_identity(
        &self,
        now: i64,
    ) -> Result<SignedDeploymentIdentity, DeploymentIdentityRuntimeError> {
        let descriptor = self.descriptor(now);
        let descriptor_wire = self
            .signer
            .sign_descriptor(&self.certificate, &descriptor)
            .map_err(|_| DeploymentIdentityRuntimeError)?;
        Ok(SignedDeploymentIdentity {
            certificate_wire: self.certificate_wire.clone(),
            descriptor_wire,
        })
    }

    fn challenge(
        &self,
        request: ChallengeRequest,
        now: i64,
    ) -> Result<ChallengeResponse, DeploymentIdentityRuntimeError> {
        if request.descriptor_revision != self.descriptor_revision {
            return Err(DeploymentIdentityRuntimeError);
        }
        let payload = ChallengePayload {
            schema_version: 1,
            deployment_id: self.certificate.deployment_id,
            descriptor_revision: self.descriptor_revision,
            nonce: request.nonce,
            issued_at: now,
            expires_at: now + CHALLENGE_LIFETIME_SECONDS,
        };
        let proof_wire = self
            .signer
            .sign_challenge(&self.certificate, &payload)
            .map_err(|_| DeploymentIdentityRuntimeError)?;
        Ok(ChallengeResponse { proof_wire })
    }
}

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route("/.well-known/pixels", get(identity))
        .route("/.well-known/pixels/challenge", post(challenge))
        .layer(
            CorsLayer::new()
                .allow_origin(Any)
                .allow_methods([Method::GET, Method::POST])
                .allow_headers([header::CONTENT_TYPE]),
        )
}

async fn identity(
    State(state): State<Arc<StateData>>,
) -> Result<Json<SignedDeploymentIdentity>, ApiError> {
    let runtime = state
        .deployment_identity
        .as_ref()
        .ok_or(ApiError::Unavailable)?;
    Ok(Json(
        runtime
            .signed_identity(current_unix_time().map_err(|_| ApiError::Unavailable)?)
            .map_err(|_| ApiError::Unavailable)?,
    ))
}

async fn challenge(
    State(state): State<Arc<StateData>>,
    Json(request): Json<ChallengeRequest>,
) -> Result<Json<ChallengeResponse>, ApiError> {
    let runtime = state
        .deployment_identity
        .as_ref()
        .ok_or(ApiError::Unavailable)?;
    runtime
        .challenge(
            request,
            current_unix_time().map_err(|_| ApiError::Unavailable)?,
        )
        .map(Json)
        .map_err(|_| ApiError::Invalid)
}

fn current_unix_time() -> Result<i64, DeploymentIdentityRuntimeError> {
    let seconds = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| DeploymentIdentityRuntimeError)?
        .as_secs();
    seconds
        .try_into()
        .map_err(|_| DeploymentIdentityRuntimeError)
}

#[cfg(test)]
mod tests {
    use super::*;
    use px_deployment_identity::{
        sign_certificate, DeploymentIdentityVerifier, DeploymentVerificationContext,
    };
    use px_private_files::private::create_private;
    use ring::{
        rand::SystemRandom,
        signature::{Ed25519KeyPair, KeyPair},
    };
    use sha2::{Digest, Sha256};

    const NOW: i64 = 1_700_000_000;

    fn key_material() -> (Vec<u8>, [u8; 32]) {
        let document = Ed25519KeyPair::generate_pkcs8(&SystemRandom::new()).unwrap();
        let pair = Ed25519KeyPair::from_pkcs8(document.as_ref()).unwrap();
        (
            document.as_ref().to_vec(),
            pair.public_key().as_ref().try_into().unwrap(),
        )
    }

    #[test]
    fn runtime_issues_short_lived_identity_and_nonce_bound_proof() {
        let deployment_id = Uuid::parse_str("8f9cbade-f2c1-47d4-a92e-109675684b21").unwrap();
        let (vendor_key, vendor_public_key) = key_material();
        let (deployment_key, deployment_public_key) = key_material();
        let certificate = DeploymentCertificate {
            schema_version: 1,
            deployment_id,
            deployment_kind: DeploymentKind::Private,
            deployment_public_key_hex: hex::encode(deployment_public_key),
            certificate_version: 2,
            not_before: NOW - 60,
            expires_at: NOW + 86_400,
            issuer_key_id: hex::encode(Sha256::digest(vendor_public_key)),
        };
        let certificate_wire = sign_certificate(&vendor_key, &certificate).unwrap();
        let runtime = DeploymentIdentityRuntime {
            certificate_wire,
            certificate,
            signer: DeploymentIdentitySigner::from_pkcs8(&deployment_key).unwrap(),
            descriptor_revision: 4,
            trust_epoch: 3,
            minimum_client_build: 20,
            authentication_methods: vec![
                AuthenticationMethod::Guest,
                AuthenticationMethod::Password,
            ],
            registration_policy: RegistrationPolicy::Closed,
        };
        let trust_store = DeploymentTrustStore::new(3, [vendor_public_key]).unwrap();
        let verifier = DeploymentIdentityVerifier::new(&trust_store).unwrap();
        let identity = runtime.signed_identity(NOW).unwrap();
        let verified = verifier
            .verify_identity(
                &identity,
                &DeploymentVerificationContext {
                    expected_deployment_id: Some(deployment_id),
                    expected_kind: DeploymentKind::Private,
                    now: NOW,
                    minimum_certificate_version: 2,
                    minimum_descriptor_revision: 4,
                    minimum_trust_epoch: 3,
                    client_build: 20,
                    protocol_version: 1,
                },
            )
            .unwrap();
        assert_eq!(
            verified.descriptor.expires_at,
            NOW + DESCRIPTOR_LIFETIME_SECONDS
        );

        let nonce = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA".to_string();
        let proof = runtime
            .challenge(
                ChallengeRequest {
                    nonce: nonce.clone(),
                    descriptor_revision: 4,
                },
                NOW,
            )
            .unwrap();
        verifier
            .verify_challenge(&verified, &proof.proof_wire, &nonce, NOW + 1)
            .unwrap();
        assert!(runtime
            .challenge(
                ChallengeRequest {
                    nonce,
                    descriptor_revision: 3,
                },
                NOW,
            )
            .is_err());
    }

    #[tokio::test]
    async fn launch_material_must_match_deployment_distribution_trust_and_private_key() {
        let temporary = tempfile::Builder::new()
            .prefix("pixels-deployment-identity-")
            .tempdir()
            .unwrap();
        make_private(temporary.path());
        let deployment_id = Uuid::parse_str("9c08feb1-af71-4fab-a6b8-bbd99b3552ba").unwrap();
        let (vendor_key, vendor_public_key) = key_material();
        let (deployment_key, deployment_public_key) = key_material();
        let now = current_unix_time().unwrap();
        let certificate = DeploymentCertificate {
            schema_version: 1,
            deployment_id,
            deployment_kind: DeploymentKind::Private,
            deployment_public_key_hex: hex::encode(deployment_public_key),
            certificate_version: 2,
            not_before: now - 60,
            expires_at: now + 86_400,
            issuer_key_id: hex::encode(Sha256::digest(vendor_public_key)),
        };
        let certificate_path = temporary.path().join("deployment.cert");
        let signing_key_path = temporary.path().join("deployment.pk8");
        let trust_store_path = temporary.path().join("trust.json");
        create_private(
            &certificate_path,
            sign_certificate(&vendor_key, &certificate)
                .unwrap()
                .as_bytes(),
        )
        .unwrap();
        create_private(&signing_key_path, &deployment_key).unwrap();
        create_private(
            &trust_store_path,
            &DeploymentTrustStore::new(3, [vendor_public_key])
                .unwrap()
                .canonical_bytes()
                .unwrap(),
        )
        .unwrap();

        let configuration = || {
            DeploymentIdentityLaunchConfig::new(
                certificate_path.clone(),
                signing_key_path.clone(),
                trust_store_path.clone(),
                2,
                4,
                3,
                20,
                false,
                true,
            )
            .unwrap()
        };
        assert!(configuration()
            .load(deployment_id, Distribution::Customer)
            .await
            .is_ok());
        assert!(configuration()
            .load(deployment_id, Distribution::Official)
            .await
            .is_err());

        let (_, unrelated_public_key) = key_material();
        let wrong_certificate = DeploymentCertificate {
            deployment_public_key_hex: hex::encode(unrelated_public_key),
            ..certificate
        };
        let wrong_certificate_path = temporary.path().join("wrong.cert");
        create_private(
            &wrong_certificate_path,
            sign_certificate(&vendor_key, &wrong_certificate)
                .unwrap()
                .as_bytes(),
        )
        .unwrap();
        let wrong_configuration = DeploymentIdentityLaunchConfig::new(
            wrong_certificate_path,
            signing_key_path,
            trust_store_path,
            2,
            4,
            3,
            20,
            false,
            true,
        )
        .unwrap();
        assert!(wrong_configuration
            .load(deployment_id, Distribution::Customer)
            .await
            .is_err());
    }

    fn make_private(path: &std::path::Path) {
        #[cfg(unix)]
        {
            use std::{fs, os::unix::fs::PermissionsExt};
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
