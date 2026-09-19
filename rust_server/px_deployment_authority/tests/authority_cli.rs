use px_deployment_identity::{DeploymentIdentityVerifier, DeploymentKind, DeploymentTrustStore};
use ring::signature::{Ed25519KeyPair, KeyPair};
use std::{
    path::{Path, PathBuf},
    process::{Command, Stdio},
};
use tempfile::TempDir;
use uuid::Uuid;

struct PrivateDirectory {
    _temporary: TempDir,
    path: PathBuf,
}

impl PrivateDirectory {
    fn new() -> Self {
        let temporary = tempfile::tempdir().unwrap();
        restrict_private_directory(temporary.path());
        Self {
            path: temporary.path().to_path_buf(),
            _temporary: temporary,
        }
    }
}

fn restrict_private_directory(path: &Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o700)).unwrap();
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        let current_identity = Command::new("whoami")
            .creation_flags(0x08000000)
            .output()
            .unwrap();
        assert!(current_identity.status.success());
        let identity_access = format!(
            "{}:(OI)(CI)F",
            String::from_utf8(current_identity.stdout).unwrap().trim()
        );
        let access_result = Command::new("icacls")
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

fn authority_command(command_name: &str) -> Command {
    let mut command = Command::new(env!("CARGO_BIN_EXE_px_deployment_authority"));
    command
        .arg(command_name)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        command.creation_flags(0x08000000);
    }
    command
}

#[test]
fn offline_authority_generates_rotatable_trust_and_signed_certificate_without_overwrite() {
    let private_directory = PrivateDirectory::new();
    let vendor_key_path = private_directory.path.join("vendor.pk8");
    let trust_store_path = private_directory.path.join("trust.json");
    let certificate_path = private_directory.path.join("deployment.cert");

    let mut generate_vendor_key = authority_command("generate-vendor-key");
    generate_vendor_key.env("PIXELS_DEPLOYMENT_VENDOR_SIGNING_KEY", &vendor_key_path);
    assert!(generate_vendor_key.output().unwrap().status.success());

    let vendor_key_material = px_private_files::private::read_private(&vendor_key_path).unwrap();
    let vendor_key = Ed25519KeyPair::from_pkcs8(&vendor_key_material).unwrap();
    let vendor_public_key_hex = hex::encode(vendor_key.public_key().as_ref());
    let mut create_trust_store = authority_command("create-trust-store");
    create_trust_store
        .env("PIXELS_DEPLOYMENT_VENDOR_SIGNING_KEY", &vendor_key_path)
        .env("PIXELS_DEPLOYMENT_TRUST_STORE_OUTPUT", &trust_store_path)
        .env("PIXELS_DEPLOYMENT_TRUST_EPOCH", "7")
        .env(
            "PIXELS_DEPLOYMENT_ADDITIONAL_VENDOR_PUBLIC_KEYS",
            &vendor_public_key_hex,
        );
    assert!(!create_trust_store.output().unwrap().status.success());

    let mut create_trust_store = authority_command("create-trust-store");
    create_trust_store
        .env("PIXELS_DEPLOYMENT_VENDOR_SIGNING_KEY", &vendor_key_path)
        .env("PIXELS_DEPLOYMENT_TRUST_STORE_OUTPUT", &trust_store_path)
        .env("PIXELS_DEPLOYMENT_TRUST_EPOCH", "7");
    assert!(create_trust_store.output().unwrap().status.success());
    let trust_store_bytes = px_private_files::private::read_private(&trust_store_path).unwrap();
    let trust_store = DeploymentTrustStore::from_canonical_bytes(&trust_store_bytes).unwrap();
    assert_eq!(trust_store.trust_epoch, 7);

    let deployment_key_document =
        Ed25519KeyPair::generate_pkcs8(&ring::rand::SystemRandom::new()).unwrap();
    let deployment_key = Ed25519KeyPair::from_pkcs8(deployment_key_document.as_ref()).unwrap();
    let deployment_id = Uuid::new_v4();
    let mut sign_certificate = authority_command("sign-certificate");
    sign_certificate
        .env("PIXELS_DEPLOYMENT_VENDOR_SIGNING_KEY", &vendor_key_path)
        .env("PIXELS_DEPLOYMENT_CERTIFICATE_OUTPUT", &certificate_path)
        .env("PIXELS_DEPLOYMENT_ID", deployment_id.to_string())
        .env("PIXELS_DEPLOYMENT_KIND", "private")
        .env(
            "PIXELS_DEPLOYMENT_PUBLIC_KEY_HEX",
            hex::encode(deployment_key.public_key().as_ref()),
        )
        .env("PIXELS_DEPLOYMENT_CERTIFICATE_VERSION", "4")
        .env("PIXELS_DEPLOYMENT_NOT_BEFORE", "1700000000")
        .env("PIXELS_DEPLOYMENT_EXPIRES_AT", "1800000000");
    assert!(sign_certificate.output().unwrap().status.success());
    let certificate_wire = px_private_files::private::read_private(&certificate_path).unwrap();
    let verifier = DeploymentIdentityVerifier::new(&trust_store).unwrap();
    let certificate = verifier
        .verify_certificate(
            std::str::from_utf8(&certificate_wire).unwrap(),
            Some(deployment_id),
            DeploymentKind::Private,
            1_750_000_000,
            4,
        )
        .unwrap();
    assert_eq!(
        certificate.deployment_public_key_hex,
        hex::encode(deployment_key.public_key().as_ref())
    );

    let certificate_before = certificate_wire.to_vec();
    let mut overwrite_certificate = authority_command("sign-certificate");
    overwrite_certificate
        .env("PIXELS_DEPLOYMENT_VENDOR_SIGNING_KEY", &vendor_key_path)
        .env("PIXELS_DEPLOYMENT_CERTIFICATE_OUTPUT", &certificate_path)
        .env("PIXELS_DEPLOYMENT_ID", deployment_id.to_string())
        .env("PIXELS_DEPLOYMENT_KIND", "private")
        .env(
            "PIXELS_DEPLOYMENT_PUBLIC_KEY_HEX",
            hex::encode(deployment_key.public_key().as_ref()),
        )
        .env("PIXELS_DEPLOYMENT_CERTIFICATE_VERSION", "5")
        .env("PIXELS_DEPLOYMENT_NOT_BEFORE", "1700000000")
        .env("PIXELS_DEPLOYMENT_EXPIRES_AT", "1800000000");
    assert!(!overwrite_certificate.output().unwrap().status.success());
    assert_eq!(
        px_private_files::private::read_private(&certificate_path)
            .unwrap()
            .as_slice(),
        certificate_before
    );
}
