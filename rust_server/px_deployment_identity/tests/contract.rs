use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine};
use px_deployment_identity::{
    sign_certificate, AuthenticationMethod, ChallengePayload, DeploymentCertificate,
    DeploymentIdentityError, DeploymentIdentitySigner, DeploymentIdentityVerifier, DeploymentKind,
    DeploymentTrustStore, DeploymentVerificationContext, Distribution, PlatformDescriptor,
    RegistrationPolicy, SignedDeploymentIdentity,
};
use ring::{
    rand::SystemRandom,
    signature::{Ed25519KeyPair, KeyPair},
};
use sha2::{Digest, Sha256};
use uuid::Uuid;

const NOW: i64 = 1_700_000_000;

struct Fixture {
    certificate: DeploymentCertificate,
    descriptor: PlatformDescriptor,
    identity: SignedDeploymentIdentity,
    signer: DeploymentIdentitySigner,
    verifier: DeploymentIdentityVerifier,
}

fn key_material() -> (Vec<u8>, [u8; 32]) {
    let document = Ed25519KeyPair::generate_pkcs8(&SystemRandom::new()).unwrap();
    let pair = Ed25519KeyPair::from_pkcs8(document.as_ref()).unwrap();
    (
        document.as_ref().to_vec(),
        pair.public_key().as_ref().try_into().unwrap(),
    )
}

fn key_id(public_key: &[u8]) -> String {
    hex::encode(Sha256::digest(public_key))
}

fn fixture(kind: DeploymentKind) -> Fixture {
    match kind {
        DeploymentKind::Official => {
            fixture_for_domain(kind, Distribution::Official, "pixels.official", None)
        }
        DeploymentKind::Private => {
            fixture_for_domain(kind, Distribution::Customer, "pixels.customer", None)
        }
    }
}

fn fixture_for_domain(
    kind: DeploymentKind,
    distribution: Distribution,
    release_namespace: &str,
    oem_id: Option<&str>,
) -> Fixture {
    let deployment_id = Uuid::parse_str("8f9cbade-f2c1-47d4-a92e-109675684b21").unwrap();
    let (vendor_pkcs8, vendor_public_key) = key_material();
    let (deployment_pkcs8, deployment_public_key) = key_material();
    let certificate = DeploymentCertificate {
        schema_version: 2,
        deployment_id,
        deployment_kind: kind,
        distribution,
        release_namespace: release_namespace.into(),
        oem_id: oem_id.map(str::to_owned),
        deployment_public_key_hex: hex::encode(deployment_public_key),
        certificate_version: 4,
        not_before: NOW - 60,
        expires_at: NOW + 86_400,
        issuer_key_id: key_id(&vendor_public_key),
    };
    let descriptor = PlatformDescriptor {
        schema_version: 2,
        deployment_id,
        deployment_kind: kind,
        distribution,
        release_namespace: release_namespace.into(),
        oem_id: oem_id.map(str::to_owned),
        descriptor_revision: 7,
        trust_epoch: 3,
        issued_at: NOW - 10,
        expires_at: NOW + 600,
        minimum_client_build: 25,
        api_versions: vec!["console.v1".to_string(), "node.v1".to_string()],
        minimum_protocol_version: 1,
        maximum_protocol_version: 2,
        authentication_methods: vec![AuthenticationMethod::Guest, AuthenticationMethod::Password],
        registration_policy: RegistrationPolicy::Closed,
        console_api_path: "/api/console".to_string(),
        node_control_path: "/api/console/node-control".to_string(),
    };
    let signer = DeploymentIdentitySigner::from_pkcs8(&deployment_pkcs8).unwrap();
    let identity = SignedDeploymentIdentity {
        certificate_wire: sign_certificate(&vendor_pkcs8, &certificate).unwrap(),
        descriptor_wire: signer.sign_descriptor(&certificate, &descriptor).unwrap(),
    };
    let trust_store = DeploymentTrustStore::new(3, [vendor_public_key]).unwrap();
    let verifier = DeploymentIdentityVerifier::new(&trust_store).unwrap();
    Fixture {
        certificate,
        descriptor,
        identity,
        signer,
        verifier,
    }
}

fn context(kind: DeploymentKind) -> DeploymentVerificationContext {
    let (expected_distribution, expected_release_namespace) = match kind {
        DeploymentKind::Official => (Distribution::Official, "pixels.official"),
        DeploymentKind::Private => (Distribution::Customer, "pixels.customer"),
    };
    DeploymentVerificationContext {
        expected_deployment_id: None,
        expected_kind: kind,
        expected_distribution,
        expected_release_namespace: expected_release_namespace.into(),
        expected_oem_id: None,
        now: NOW,
        minimum_certificate_version: 4,
        minimum_descriptor_revision: 7,
        minimum_trust_epoch: 3,
        client_build: 25,
        protocol_version: 2,
    }
}

#[test]
fn matching_private_identity_and_online_challenge_are_accepted() {
    let fixture = fixture(DeploymentKind::Private);
    let verified = fixture
        .verifier
        .verify_identity(&fixture.identity, &context(DeploymentKind::Private))
        .unwrap();
    assert_eq!(verified.certificate, fixture.certificate);
    assert_eq!(verified.descriptor, fixture.descriptor);

    let nonce = URL_SAFE_NO_PAD.encode([42_u8; 32]);
    let challenge = ChallengePayload {
        schema_version: 1,
        deployment_id: fixture.certificate.deployment_id,
        descriptor_revision: fixture.descriptor.descriptor_revision,
        nonce: nonce.clone(),
        issued_at: NOW,
        expires_at: NOW + 30,
    };
    let wire = fixture
        .signer
        .sign_challenge(&fixture.certificate, &challenge)
        .unwrap();
    assert_eq!(
        fixture
            .verifier
            .verify_challenge(&verified, &wire, &nonce, NOW + 1)
            .unwrap(),
        challenge
    );
}

#[test]
fn official_and_private_distributions_are_cryptographically_disjoint() {
    let official = fixture(DeploymentKind::Official);
    assert_eq!(
        official
            .verifier
            .verify_identity(&official.identity, &context(DeploymentKind::Private)),
        Err(DeploymentIdentityError::Rejected)
    );
    let private = fixture(DeploymentKind::Private);
    assert_eq!(
        private
            .verifier
            .verify_identity(&private.identity, &context(DeploymentKind::Official)),
        Err(DeploymentIdentityError::Rejected)
    );
}

#[test]
fn oem_identity_accepts_only_its_signed_release_domain() {
    let fixture = fixture_for_domain(
        DeploymentKind::Private,
        Distribution::Oem,
        "oem.acme-cloud",
        Some("acme-cloud"),
    );
    let context = DeploymentVerificationContext {
        expected_deployment_id: None,
        expected_kind: DeploymentKind::Private,
        expected_distribution: Distribution::Oem,
        expected_release_namespace: "oem.acme-cloud".into(),
        expected_oem_id: Some("acme-cloud".into()),
        now: NOW,
        minimum_certificate_version: 4,
        minimum_descriptor_revision: 7,
        minimum_trust_epoch: 3,
        client_build: 25,
        protocol_version: 2,
    };
    fixture
        .verifier
        .verify_identity(&fixture.identity, &context)
        .unwrap();
    assert_eq!(
        fixture.verifier.verify_identity(
            &fixture.identity,
            &DeploymentVerificationContext {
                expected_release_namespace: "oem.north-star".into(),
                expected_oem_id: Some("north-star".into()),
                ..context
            },
        ),
        Err(DeploymentIdentityError::Rejected),
    );
}

#[test]
fn tampering_stale_watermarks_and_incompatible_clients_are_rejected() {
    let fixture = fixture(DeploymentKind::Private);
    let mut tampered = fixture.identity.clone();
    tampered.descriptor_wire.push('A');
    assert!(matches!(
        fixture
            .verifier
            .verify_identity(&tampered, &context(DeploymentKind::Private)),
        Err(DeploymentIdentityError::Invalid | DeploymentIdentityError::Signature)
    ));

    let mut stale_revision = context(DeploymentKind::Private);
    stale_revision.minimum_descriptor_revision = 8;
    assert_eq!(
        fixture
            .verifier
            .verify_identity(&fixture.identity, &stale_revision),
        Err(DeploymentIdentityError::Rejected)
    );
    let mut old_client = context(DeploymentKind::Private);
    old_client.client_build = 24;
    assert_eq!(
        fixture
            .verifier
            .verify_identity(&fixture.identity, &old_client),
        Err(DeploymentIdentityError::Rejected)
    );
    let mut unsupported_protocol = context(DeploymentKind::Private);
    unsupported_protocol.protocol_version = 3;
    assert_eq!(
        fixture
            .verifier
            .verify_identity(&fixture.identity, &unsupported_protocol),
        Err(DeploymentIdentityError::Rejected)
    );
}

#[test]
fn challenge_nonce_revision_and_expiry_prevent_replay() {
    let fixture = fixture(DeploymentKind::Private);
    let verified = fixture
        .verifier
        .verify_identity(&fixture.identity, &context(DeploymentKind::Private))
        .unwrap();
    let nonce = URL_SAFE_NO_PAD.encode([7_u8; 32]);
    let challenge = ChallengePayload {
        schema_version: 1,
        deployment_id: fixture.certificate.deployment_id,
        descriptor_revision: fixture.descriptor.descriptor_revision,
        nonce: nonce.clone(),
        issued_at: NOW,
        expires_at: NOW + 30,
    };
    let wire = fixture
        .signer
        .sign_challenge(&fixture.certificate, &challenge)
        .unwrap();
    assert_eq!(
        fixture.verifier.verify_challenge(
            &verified,
            &wire,
            &URL_SAFE_NO_PAD.encode([8_u8; 32]),
            NOW + 1
        ),
        Err(DeploymentIdentityError::Rejected)
    );
    assert_eq!(
        fixture
            .verifier
            .verify_challenge(&verified, &wire, &nonce, NOW + 30),
        Err(DeploymentIdentityError::Rejected)
    );
}

#[test]
fn trust_store_is_canonical_and_rejects_duplicate_keys() {
    let (_, public_key) = key_material();
    let store = DeploymentTrustStore::new(1, [public_key]).unwrap();
    let bytes = store.canonical_bytes().unwrap();
    assert_eq!(
        DeploymentTrustStore::from_canonical_bytes(&bytes).unwrap(),
        store
    );
    assert_eq!(
        DeploymentTrustStore::new(1, [public_key, public_key]),
        Err(DeploymentIdentityError::Key)
    );
}
