use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine};
use px_license::{
    Distribution, LicenseError, LicensePayload, LicenseSigner, LicenseVerifier, Product,
    VerifyContext,
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
fn context(payload: &LicensePayload) -> VerifyContext<'_> {
    VerifyContext {
        deployment_id: payload.deployment_id,
        product: payload.product,
        distribution: payload.distribution,
        machine_sha256: &payload.machine_sha256,
        now: 1750000000,
        minimum_revision: 7,
        last_trusted_time: 1700000000,
    }
}
fn signed_raw(bytes: &[u8]) -> String {
    let message = [b"Pixels-License-v1\0".as_slice(), bytes].concat();
    format!(
        "PXLIC1.{}.{}",
        URL_SAFE_NO_PAD.encode(bytes),
        URL_SAFE_NO_PAD.encode(pair().sign(&message).as_ref())
    )
}

#[test]
fn fixed_openssl_vector_matches_signer_and_verifier() {
    let v = vector();
    let public: [u8; 32] = hex::decode(&v.public_key).unwrap().try_into().unwrap();
    let verifier = LicenseVerifier::new(public).unwrap();
    assert_eq!(signer().public_key(), public);
    assert_eq!(signer().sign(&v.payload).unwrap(), v.wire);
    assert_eq!(
        verifier.verify(&v.wire, &context(&v.payload)).unwrap(),
        v.payload
    );
    let bytes = v.payload.canonical_bytes().unwrap();
    for name in ["password", "app_secret", "username", "token", "private_key"] {
        assert!(!std::str::from_utf8(&bytes)
            .unwrap()
            .contains(&format!("\"{name}\"")));
    }
}
#[test]
fn target_time_revision_and_rollback_boundaries_reject() {
    let v = vector();
    let verifier =
        LicenseVerifier::new(hex::decode(&v.public_key).unwrap().try_into().unwrap()).unwrap();
    let base = context(&v.payload);
    let mutations = [
        VerifyContext {
            deployment_id: Uuid::new_v4(),
            ..context(&v.payload)
        },
        VerifyContext {
            product: Product::Gopico,
            ..context(&v.payload)
        },
        VerifyContext {
            distribution: Distribution::Official,
            ..context(&v.payload)
        },
        VerifyContext {
            machine_sha256: &"b".repeat(64),
            ..context(&v.payload)
        },
        VerifyContext {
            now: v.payload.not_before - 1,
            ..context(&v.payload)
        },
        VerifyContext {
            now: v.payload.expires_at,
            ..context(&v.payload)
        },
        VerifyContext {
            minimum_revision: 8,
            ..context(&v.payload)
        },
        VerifyContext {
            last_trusted_time: base.now + 1,
            ..context(&v.payload)
        },
        VerifyContext {
            minimum_revision: 0,
            ..context(&v.payload)
        },
    ];
    for context in mutations {
        assert_eq!(
            verifier.verify(&v.wire, &context),
            Err(LicenseError::Rejected)
        );
    }
    assert!(verifier
        .verify(
            &v.wire,
            &VerifyContext {
                now: v.payload.not_before,
                ..context(&v.payload)
            }
        )
        .is_ok());
    assert!(verifier
        .verify(
            &v.wire,
            &VerifyContext {
                now: v.payload.expires_at - 1,
                ..context(&v.payload)
            }
        )
        .is_ok());
}
#[test]
fn valid_signature_does_not_authorize_unknown_or_noncanonical_payloads() {
    let v = vector();
    let verifier =
        LicenseVerifier::new(hex::decode(&v.public_key).unwrap().try_into().unwrap()).unwrap();
    let canonical = String::from_utf8(v.payload.canonical_bytes().unwrap()).unwrap();
    let extra = canonical.replacen("{", "{\"unexpected\":true,", 1);
    let duplicate = canonical.replacen("{", "{\"schema\":1,", 1);
    let no_product = canonical.replace("\"product\":\"pixels_console\",", "");
    let alias = canonical.replace("\"pixels_console\"", "\"Pixels_cms\"");
    let unknown_schema = canonical.replace("\"schema\":1", "\"schema\":2");
    let extra_space = canonical.replacen("{", "{ ", 1);
    let wrong_key = canonical.replace(&v.payload.key_id, &"b".repeat(64));
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
            .verify(&signed_raw(value.as_bytes()), &context(&v.payload))
            .is_err());
    }
}
#[test]
fn malformed_wire_tampering_and_wrong_trust_root_reject() {
    let v = vector();
    let verifier =
        LicenseVerifier::new(hex::decode(&v.public_key).unwrap().try_into().unwrap()).unwrap();
    for wire in [
        "".into(),
        v.wire.replacen("PXLIC1.", "", 1),
        format!("{}.extra", v.wire),
        format!("{}=", v.wire),
        v.wire.replacen("PXLIC1.", "PXLIC2.", 1),
        "x".repeat(8193),
    ] {
        assert!(verifier.verify(&wire, &context(&v.payload)).is_err());
    }
    let mut tampered = v.wire.clone().into_bytes();
    tampered[20] = if tampered[20] == b'A' { b'B' } else { b'A' };
    assert!(verifier
        .verify(
            std::str::from_utf8(&tampered).unwrap(),
            &context(&v.payload)
        )
        .is_err());
    assert!(LicenseVerifier::new([42; 32])
        .unwrap()
        .verify(&v.wire, &context(&v.payload))
        .is_err());
    assert!(LicenseVerifier::new([0; 32]).is_err());
    assert!(LicenseSigner::from_pkcs8(b"not a private key").is_err());
}
#[test]
fn issuance_rejects_invalid_limits_features_and_keys() {
    let original = vector().payload;
    for invalid in [
        LicensePayload {
            schema: 2,
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
