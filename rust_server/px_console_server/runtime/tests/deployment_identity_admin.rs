use ring::signature::{Ed25519KeyPair, KeyPair};
use std::{
    path::{Path, PathBuf},
    process::{Command, Stdio},
};
use tempfile::TempDir;

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

#[test]
fn deployment_key_generation_is_explicit_private_and_never_overwrites() {
    let private_directory = PrivateDirectory::new();
    let deployment_key_path = private_directory.path.join("deployment.pk8");
    let command = || {
        let mut command = Command::new(env!("CARGO_BIN_EXE_px_console_admin"));
        command
            .arg("generate-deployment-key")
            .env(
                "PIXELS_CONSOLE_DEPLOYMENT_SIGNING_KEY",
                &deployment_key_path,
            )
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000);
        }
        command
    };
    let first = command().output().unwrap();
    assert!(first.status.success());
    let signing_key_material =
        px_private_files::private::read_private(&deployment_key_path).unwrap();
    let signing_key = Ed25519KeyPair::from_pkcs8(&signing_key_material).unwrap();
    let expected_output = format!(
        "deployment_public_key_hex={}",
        hex::encode(signing_key.public_key().as_ref())
    );
    assert!(String::from_utf8(first.stdout)
        .unwrap()
        .contains(&expected_output));
    assert!(!command().output().unwrap().status.success());
    assert_eq!(
        px_private_files::private::read_private(&deployment_key_path).unwrap(),
        signing_key_material
    );
}
