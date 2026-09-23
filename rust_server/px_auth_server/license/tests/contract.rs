use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine};
use px_license::{
    LicenseError, LicensePayload, LicenseSigner, LicenseTrustStore, LicenseVerifierSet,
    LicensedService, VerifyContext,
};
use serde::Deserialize;
use uuid::Uuid;

const PRIVATE_KEY: &str = "3053020101300506032b6570042204209d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60a123032100d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";

#[derive(Deserialize)]
struct ContractVector {
    public_key: String,
    payload: LicensePayload,
    wire: String,
}

fn signer() -> LicenseSigner {
    LicenseSigner::from_pkcs8(&hex::decode(PRIVATE_KEY).unwrap()).unwrap()
}

fn vector() -> ContractVector {
    serde_json::from_str(include_str!("vector.json")).unwrap()
}

#[test]
fn fixed_openssl_vector_matches_signer_and_verifier() {
    let contract_vector = vector();
    let signing_key = signer();
    assert_eq!(
        hex::encode(signing_key.public_key()),
        contract_vector.public_key
    );
    assert_eq!(
        signing_key.sign(&contract_vector.payload).unwrap(),
        contract_vector.wire
    );
    let verifier = LicenseVerifierSet::new([signing_key.public_key().try_into().unwrap()]).unwrap();
    assert_eq!(
        verifier
            .verify(
                &contract_vector.wire,
                &VerifyContext::new(contract_vector.payload.deployment_id, 1_750_000_000),
            )
            .unwrap(),
        contract_vector.payload
    );
}

#[test]
fn deployment_and_expiration_are_the_only_runtime_bindings() {
    let contract_vector = vector();
    let verifier = LicenseVerifierSet::new([signer().public_key().try_into().unwrap()]).unwrap();
    assert_eq!(
        verifier.verify(
            &contract_vector.wire,
            &VerifyContext::new(Uuid::new_v4(), 1_750_000_000),
        ),
        Err(LicenseError::Rejected)
    );
    assert_eq!(
        verifier.verify(
            &contract_vector.wire,
            &VerifyContext::new(contract_vector.payload.deployment_id, 1_699_999_999),
        ),
        Err(LicenseError::Rejected)
    );
    assert_eq!(
        verifier.verify(
            &contract_vector.wire,
            &VerifyContext::new(contract_vector.payload.deployment_id, 1_800_000_000),
        ),
        Err(LicenseError::Rejected)
    );
}

#[test]
fn invalid_streams_services_and_key_are_rejected() {
    let original = vector().payload;
    for invalid_payload in [
        LicensePayload {
            max_streams: 0,
            ..original.clone()
        },
        LicensePayload {
            services: vec![],
            ..original.clone()
        },
        LicensePayload {
            services: vec![LicensedService::Rdp, LicensedService::Desktop],
            ..original.clone()
        },
        LicensePayload {
            expires_at: original.issued_at,
            ..original.clone()
        },
        LicensePayload {
            key_id: "A".repeat(64),
            ..original.clone()
        },
    ] {
        assert_eq!(invalid_payload.validate(), Err(LicenseError::Invalid));
    }
}

#[test]
fn malformed_wire_tampering_and_unknown_fields_are_rejected() {
    let contract_vector = vector();
    let verifier = LicenseVerifierSet::new([signer().public_key().try_into().unwrap()]).unwrap();
    let context = VerifyContext::new(contract_vector.payload.deployment_id, 1_750_000_000);
    for malformed in [
        "PXLIC1.payload.signature".to_string(),
        contract_vector.wire.replace("PXLIC2", "PXLIC2.extra"),
        format!("{}x", contract_vector.wire),
    ] {
        assert!(verifier.verify(&malformed, &context).is_err());
    }
    let mut parts = contract_vector.wire.split('.');
    let prefix = parts.next().unwrap();
    let payload = URL_SAFE_NO_PAD.decode(parts.next().unwrap()).unwrap();
    let signature = parts.next().unwrap();
    let mut value: serde_json::Value = serde_json::from_slice(&payload).unwrap();
    value["unexpected"] = serde_json::json!(true);
    let unknown = format!(
        "{prefix}.{}.{signature}",
        URL_SAFE_NO_PAD.encode(serde_json::to_vec(&value).unwrap())
    );
    assert!(verifier.verify(&unknown, &context).is_err());
}

#[test]
fn trust_store_rotation_accepts_retained_key_and_rejects_withdrawn_key() {
    let original_signer = signer();
    let replacement_material =
        ring::signature::Ed25519KeyPair::generate_pkcs8(&ring::rand::SystemRandom::new()).unwrap();
    let replacement_signer = LicenseSigner::from_pkcs8(replacement_material.as_ref()).unwrap();
    let rotating = LicenseTrustStore::new(
        replacement_signer.public_key().try_into().unwrap(),
        [original_signer.public_key().try_into().unwrap()],
    )
    .unwrap();
    let contract_vector = vector();
    assert!(rotating
        .verifier_set()
        .unwrap()
        .verify(
            &contract_vector.wire,
            &VerifyContext::new(contract_vector.payload.deployment_id, 1_750_000_000),
        )
        .is_ok());
    let withdrawn =
        LicenseTrustStore::new(replacement_signer.public_key().try_into().unwrap(), []).unwrap();
    assert!(withdrawn
        .verifier_set()
        .unwrap()
        .verify(
            &contract_vector.wire,
            &VerifyContext::new(contract_vector.payload.deployment_id, 1_750_000_000),
        )
        .is_err());
}
