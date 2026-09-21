use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine};
use px_license::{
    Distribution, LicenseError, LicensePayload, LicenseSigner, LicenseTrustStore,
    LicenseVerifierSet, Product, VerifyContext,
};
use ring::signature::{Ed25519KeyPair, KeyPair};
use serde::Deserialize;
use uuid::Uuid;

#[derive(Deserialize)]
struct Vector {
    public_key: String,
    payload: LicensePayload,
    wire: String,
}
fn vector() -> Vector {
    serde_json::from_str(include_str!("vector.json")).unwrap()
}
// RFC 8032 test-only seed. It is public test material, never used by a service.
fn pair() -> Ed25519KeyPair {
    Ed25519KeyPair::from_seed_unchecked(
        &hex::decode("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60").unwrap(),
    )
    .unwrap()
}
fn signer() -> LicenseSigner {
    let key = pair();
    let mut pkcs8=hex::decode("3053020101300506032b6570042204209d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60a123032100").unwrap();
    pkcs8.extend_from_slice(key.public_key().as_ref());
    LicenseSigner::from_pkcs8(&pkcs8).unwrap()
}
fn signer_from_seed(seed: [u8; 32]) -> LicenseSigner {
    let key_pair = Ed25519KeyPair::from_seed_unchecked(&seed).unwrap();
    let mut pkcs8 = hex::decode("3053020101300506032b657004220420").unwrap();
    pkcs8.extend_from_slice(&seed);
    pkcs8.extend_from_slice(&hex::decode("a123032100").unwrap());
    pkcs8.extend_from_slice(key_pair.public_key().as_ref());
    LicenseSigner::from_pkcs8(&pkcs8).unwrap()
}
fn context(payload: &LicensePayload) -> VerifyContext<'_> {
    VerifyContext {
        deployment_id: payload.deployment_id,
        product: payload.product,
        distribution: payload.distribution,
        release_namespace: &payload.release_namespace,
        oem_id: payload.oem_id.as_deref(),
        machine_sha256: &payload.machine_sha256,
        now: 1750000000,
        minimum_revision: 7,
        last_trusted_time: 1700000000,
    }
}
fn signed_raw(bytes: &[u8]) -> String {
    let message = [b"Pixels-License-v2\0".as_slice(), bytes].concat();
    format!(
        "PXLIC2.{}.{}",
        URL_SAFE_NO_PAD.encode(bytes),
        URL_SAFE_NO_PAD.encode(pair().sign(&message).as_ref())
    )
}

#[test]
fn fixed_openssl_vector_matches_signer_and_verifier() {
    let contract_vector = vector();
    let public: [u8; 32] = hex::decode(&contract_vector.public_key)
        .unwrap()
        .try_into()
        .unwrap();
    let verifier = LicenseVerifierSet::new([public]).unwrap();
    assert_eq!(signer().public_key(), public);
    assert_eq!(
        signer().sign(&contract_vector.payload).unwrap(),
        contract_vector.wire
    );
    assert_eq!(
        verifier
            .verify(&contract_vector.wire, &context(&contract_vector.payload))
            .unwrap(),
        contract_vector.payload
    );
    let bytes = contract_vector.payload.canonical_bytes().unwrap();
    for name in ["password", "app_secret", "username", "token", "private_key"] {
        assert!(!std::str::from_utf8(&bytes)
            .unwrap()
            .contains(&format!("\"{name}\"")));
    }
}
#[test]
fn target_time_revision_and_rollback_boundaries_reject() {
    let contract_vector = vector();
    let verifier = LicenseVerifierSet::new([hex::decode(&contract_vector.public_key)
        .unwrap()
        .try_into()
        .unwrap()])
    .unwrap();
    let base = context(&contract_vector.payload);
    let mutations = [
        VerifyContext {
            deployment_id: Uuid::new_v4(),
            ..context(&contract_vector.payload)
        },
        VerifyContext {
            product: Product::Gopico,
            ..context(&contract_vector.payload)
        },
        VerifyContext {
            distribution: Distribution::Official,
            ..context(&contract_vector.payload)
        },
        VerifyContext {
            release_namespace: "pixels.official",
            ..context(&contract_vector.payload)
        },
        VerifyContext {
            oem_id: Some("acme-cloud"),
            ..context(&contract_vector.payload)
        },
        VerifyContext {
            machine_sha256: &"b".repeat(64),
            ..context(&contract_vector.payload)
        },
        VerifyContext {
            now: contract_vector.payload.not_before - 1,
            ..context(&contract_vector.payload)
        },
        VerifyContext {
            now: contract_vector.payload.expires_at,
            ..context(&contract_vector.payload)
        },
        VerifyContext {
            minimum_revision: 8,
            ..context(&contract_vector.payload)
        },
        VerifyContext {
            last_trusted_time: base.now + 1,
            ..context(&contract_vector.payload)
        },
        VerifyContext {
            minimum_revision: 0,
            ..context(&contract_vector.payload)
        },
    ];
    for context in mutations {
        assert_eq!(
            verifier.verify(&contract_vector.wire, &context),
            Err(LicenseError::Rejected)
        );
    }
    assert!(verifier
        .verify(
            &contract_vector.wire,
            &VerifyContext {
                now: contract_vector.payload.not_before,
                ..context(&contract_vector.payload)
            }
        )
        .is_ok());
    assert!(verifier
        .verify(
            &contract_vector.wire,
            &VerifyContext {
                now: contract_vector.payload.expires_at - 1,
                ..context(&contract_vector.payload)
            }
        )
        .is_ok());
}

#[test]
fn oem_release_domain_is_signed_and_cannot_be_substituted() {
    let mut payload = vector().payload;
    payload.distribution = Distribution::Oem;
    payload.release_namespace = "oem.acme-cloud".into();
    payload.oem_id = Some("acme-cloud".into());
    let wire = signer().sign(&payload).unwrap();
    let public_key: [u8; 32] = signer().public_key().try_into().unwrap();
    let verifier = LicenseVerifierSet::new([public_key]).unwrap();
    verifier.verify(&wire, &context(&payload)).unwrap();
    assert_eq!(
        verifier.verify(
            &wire,
            &VerifyContext {
                release_namespace: "oem.north-star",
                oem_id: Some("north-star"),
                ..context(&payload)
            },
        ),
        Err(LicenseError::Rejected),
    );
}
#[test]
fn valid_signature_does_not_authorize_unknown_or_noncanonical_payloads() {
    let contract_vector = vector();
    let verifier = LicenseVerifierSet::new([hex::decode(&contract_vector.public_key)
        .unwrap()
        .try_into()
        .unwrap()])
    .unwrap();
    let canonical = String::from_utf8(contract_vector.payload.canonical_bytes().unwrap()).unwrap();
    let extra = canonical.replacen("{", "{\"unexpected\":true,", 1);
    let duplicate = canonical.replacen("{", "{\"schema\":2,", 1);
    let no_product = canonical.replace("\"product\":\"pixels_console\",", "");
    let alias = canonical.replace("\"pixels_console\"", "\"Pixels_cms\"");
    let unknown_schema = canonical.replace("\"schema\":2", "\"schema\":1");
    let extra_space = canonical.replacen("{", "{ ", 1);
    let wrong_key = canonical.replace(&contract_vector.payload.key_id, &"b".repeat(64));
    for value in [
        extra,
        duplicate,
        no_product,
        alias,
        unknown_schema,
        extra_space,
        wrong_key,
    ] {
        assert!(verifier
            .verify(
                &signed_raw(value.as_bytes()),
                &context(&contract_vector.payload)
            )
            .is_err());
    }
}
#[test]
fn malformed_wire_tampering_and_wrong_trust_root_reject() {
    let contract_vector = vector();
    let verifier = LicenseVerifierSet::new([hex::decode(&contract_vector.public_key)
        .unwrap()
        .try_into()
        .unwrap()])
    .unwrap();
    for wire in [
        "".into(),
        contract_vector.wire.replacen("PXLIC2.", "", 1),
        format!("{}.extra", contract_vector.wire),
        format!("{}=", contract_vector.wire),
        contract_vector.wire.replacen("PXLIC2.", "PXLIC1.", 1),
        "x".repeat(8193),
    ] {
        assert!(verifier
            .verify(&wire, &context(&contract_vector.payload))
            .is_err());
    }
    let mut tampered = contract_vector.wire.clone().into_bytes();
    tampered[20] = if tampered[20] == b'A' { b'B' } else { b'A' };
    assert!(verifier
        .verify(
            std::str::from_utf8(&tampered).unwrap(),
            &context(&contract_vector.payload)
        )
        .is_err());
    assert!(LicenseVerifierSet::new([[42; 32]])
        .unwrap()
        .verify(&contract_vector.wire, &context(&contract_vector.payload))
        .is_err());
    assert!(LicenseVerifierSet::new([[0; 32]]).is_err());
    assert!(LicenseSigner::from_pkcs8(b"not a private key").is_err());
}
#[test]
fn issuance_rejects_invalid_limits_features_and_keys() {
    let original = vector().payload;
    for invalid in [
        LicensePayload {
            schema: 1,
            ..original.clone()
        },
        LicensePayload {
            release_namespace: "pixels.official".into(),
            ..original.clone()
        },
        LicensePayload {
            oem_id: Some("acme-cloud".into()),
            ..original.clone()
        },
        LicensePayload {
            license_id: Uuid::nil(),
            ..original.clone()
        },
        LicensePayload {
            deployment_id: Uuid::nil(),
            ..original.clone()
        },
        LicensePayload {
            machine_sha256: "a".repeat(63),
            ..original.clone()
        },
        LicensePayload {
            revision: 0,
            ..original.clone()
        },
        LicensePayload {
            issued_at: -1,
            ..original.clone()
        },
        LicensePayload {
            max_devices: 0,
            ..original.clone()
        },
        LicensePayload {
            max_sessions: 0,
            ..original.clone()
        },
        LicensePayload {
            expires_at: original.not_before,
            ..original.clone()
        },
        LicensePayload {
            expires_at: i64::MAX,
            ..original.clone()
        },
        LicensePayload {
            features: vec![],
            ..original.clone()
        },
        LicensePayload {
            features: vec![px_license::Feature::Desktop, px_license::Feature::Desktop],
            ..original.clone()
        },
        LicensePayload {
            key_id: "b".repeat(64),
            ..original.clone()
        },
    ] {
        assert!(signer().sign(&invalid).is_err());
    }
}

#[test]
fn trust_store_rotation_verifies_retained_key_and_withdrawal_rejects_it() {
    let previous_signer = signer();
    let active_signer = signer_from_seed([7; 32]);
    let authority_deployment_id = Uuid::new_v4();
    let recovery_generation = Uuid::new_v4();
    let rotating_store = LicenseTrustStore::new(
        authority_deployment_id,
        recovery_generation,
        active_signer.public_key().try_into().unwrap(),
        [previous_signer.public_key().try_into().unwrap()],
    )
    .unwrap();
    rotating_store.verify_active_signer(&active_signer).unwrap();
    assert!(rotating_store
        .verify_active_signer(&previous_signer)
        .is_err());
    let canonical_bytes = rotating_store.canonical_bytes().unwrap();
    assert_eq!(
        LicenseTrustStore::from_canonical_bytes(&canonical_bytes).unwrap(),
        rotating_store
    );

    let contract_vector = vector();
    rotating_store
        .verifier_set()
        .unwrap()
        .verify(&contract_vector.wire, &context(&contract_vector.payload))
        .unwrap();

    let mut active_payload = contract_vector.payload.clone();
    active_payload.key_id = active_signer.key_id();
    let active_wire = active_signer.sign(&active_payload).unwrap();
    rotating_store
        .verifier_set()
        .unwrap()
        .verify(&active_wire, &context(&active_payload))
        .unwrap();

    let withdrawn_store = LicenseTrustStore::new(
        authority_deployment_id,
        Uuid::new_v4(),
        active_signer.public_key().try_into().unwrap(),
        [],
    )
    .unwrap();
    assert_eq!(
        withdrawn_store
            .verifier_set()
            .unwrap()
            .verify(&contract_vector.wire, &context(&contract_vector.payload)),
        Err(LicenseError::Key)
    );
    withdrawn_store
        .verifier_set()
        .unwrap()
        .verify(&active_wire, &context(&active_payload))
        .unwrap();
}

#[test]
fn trust_store_rejects_duplicate_substituted_and_noncanonical_roots() {
    let active_signer = signer_from_seed([7; 32]);
    let public_key: [u8; 32] = active_signer.public_key().try_into().unwrap();
    assert!(
        LicenseTrustStore::new(Uuid::new_v4(), Uuid::new_v4(), public_key, [public_key]).is_err()
    );
    assert!(LicenseVerifierSet::new([public_key, public_key]).is_err());

    let trust_store =
        LicenseTrustStore::new(Uuid::new_v4(), Uuid::new_v4(), public_key, []).unwrap();
    let canonical_bytes = trust_store.canonical_bytes().unwrap();
    let mut substituted = serde_json::to_value(&trust_store).unwrap();
    substituted["trusted_keys"][0]["public_key_hex"] = serde_json::json!("11".repeat(32));
    assert!(
        LicenseTrustStore::from_canonical_bytes(&serde_json::to_vec(&substituted).unwrap())
            .is_err()
    );

    let mut unknown_field = serde_json::to_value(&trust_store).unwrap();
    unknown_field["legacy_key"] = serde_json::json!(true);
    assert!(
        LicenseTrustStore::from_canonical_bytes(&serde_json::to_vec(&unknown_field).unwrap())
            .is_err()
    );

    let mut noncanonical_bytes = canonical_bytes.clone();
    noncanonical_bytes.push(b'\n');
    assert!(LicenseTrustStore::from_canonical_bytes(&noncanonical_bytes).is_err());
}
