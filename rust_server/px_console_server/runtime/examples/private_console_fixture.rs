//! Isolated systemd test fixture. This fixed signer must never be packaged or used for a deployment.

use px_license::{LicensePayload, LicenseSigner, LicenseTrustStore, LicensedService};
use px_private_files::private;
use std::{env, path::PathBuf};
use uuid::Uuid;

const TEST_SIGNING_KEY: &str = "3053020101300506032b6570042204209d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60a123032100d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";

fn main() -> Result<(), Box<dyn std::error::Error>> {
    if env::var("PIXELS_PG_ISOLATED_TEST").as_deref() != Ok("1") {
        return Err("isolated PostgreSQL test environment is required".into());
    }
    let arguments: Vec<String> = env::args().skip(1).collect();
    let [deployment_text, private_directory_text] = arguments.as_slice() else {
        return Err(
            "usage: private_console_fixture <deployment-uuid> <existing-private-directory>".into(),
        );
    };
    let deployment_id = deployment_text.parse::<Uuid>()?;
    if deployment_id.is_nil() {
        return Err("deployment identifier must not be nil".into());
    }
    let private_directory = PathBuf::from(private_directory_text);
    if private_directory.is_symlink() || !private_directory.is_dir() {
        return Err("private directory must already exist".into());
    }
    let signer = LicenseSigner::from_pkcs8(&hex::decode(TEST_SIGNING_KEY)?)?;
    let trust_store = LicenseTrustStore::new(signer.public_key().try_into()?, [])?;
    let current_time = chrono::Utc::now().timestamp();
    let license = LicensePayload {
        schema: 2,
        license_id: Uuid::new_v4(),
        deployment_id,
        revision: 1,
        issued_at: current_time - 10,
        expires_at: current_time + 3600,
        max_streams: 8,
        services: vec![
            LicensedService::CloudApplications,
            LicensedService::Desktop,
            LicensedService::Rdp,
        ],
        key_id: signer.key_id(),
    };
    private::create_private(
        &private_directory.join("license-trust.json"),
        &trust_store.canonical_bytes()?,
    )?;
    private::create_private(
        &private_directory.join("console.license"),
        signer.sign(&license)?.as_bytes(),
    )?;
    println!("created isolated PXLIC2 fixture for deployment {deployment_id}");
    Ok(())
}
