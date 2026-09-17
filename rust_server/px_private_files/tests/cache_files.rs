use px_private_files::{CacheRoot, ContentIdentity, FileError};
use sha2::{Digest, Sha256};
use std::{
    fs,
    io::{BufRead, BufReader, Read, Seek, SeekFrom},
    path::{Path, PathBuf},
    process::{Child, Command, Stdio},
    time::Duration,
};
use uuid::Uuid;
struct Fixture {
    base: tempfile::TempDir,
    root: PathBuf,
    deployment: Uuid,
}
fn private(path: &Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(
            path,
            fs::Permissions::from_mode(if path.is_dir() { 0o700 } else { 0o600 }),
        )
        .unwrap();
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
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
impl Fixture {
    fn new() -> Self {
        let base = tempfile::Builder::new()
            .prefix("pixels-cache-fixture-")
            .tempdir()
            .unwrap();
        private(base.path());
        let root = base.path().join("cache");
        fs::create_dir(&root).unwrap();
        private(&root);
        Self {
            base,
            root,
            deployment: Uuid::new_v4(),
        }
    }
    fn initialize(&self) -> std::sync::Arc<CacheRoot> {
        CacheRoot::initialize(&self.root, self.deployment).unwrap()
    }
    fn write_private(&self, name: &str, bytes: &[u8]) {
        let path = self.root.join(name);
        fs::write(&path, bytes).unwrap();
        #[cfg(unix)]
        private(&path);
    }
}
fn content(bytes: &[u8]) -> ContentIdentity {
    ContentIdentity::new(bytes.len() as u64, Sha256::digest(bytes).into()).unwrap()
}
#[test]
fn provisioning_is_explicit_empty_private_and_deployment_bound() {
    let fixture = Fixture::new();
    assert!(CacheRoot::open(&fixture.root, fixture.deployment).is_err());
    assert!(CacheRoot::initialize(Path::new("relative-root"), fixture.deployment).is_err());
    assert!(CacheRoot::initialize(&fixture.root, Uuid::nil()).is_err());
    fixture.write_private("do-not-delete", b"unrelated");
    assert!(matches!(
        CacheRoot::initialize(&fixture.root, fixture.deployment),
        Err(FileError::Exists)
    ));
    assert_eq!(
        fs::read(fixture.root.join("do-not-delete")).unwrap(),
        b"unrelated"
    );
    fs::remove_file(fixture.root.join("do-not-delete")).unwrap();
    let root = fixture.initialize();
    let id = root.id();
    assert_eq!(root.deployment(), fixture.deployment);
    assert!(matches!(
        CacheRoot::open(&fixture.root, fixture.deployment),
        Err(FileError::Busy)
    ));
    assert!(CacheRoot::initialize(&fixture.root, fixture.deployment).is_err());
    drop(root);
    assert!(CacheRoot::open(&fixture.root, Uuid::new_v4()).is_err());
    let reopened = CacheRoot::open(&fixture.root, fixture.deployment).unwrap();
    assert_eq!(id, reopened.id());
    drop(reopened);
    fixture.write_private("root.identity", b"broken");
    assert!(matches!(
        CacheRoot::open(&fixture.root, fixture.deployment),
        Err(FileError::Corrupt)
    ));
}
#[test]
fn complete_hash_verified_publication_and_shared_reads_hold_exclusive_cleanup_out() {
    let fixture = Fixture::new();
    let root = fixture.initialize();
    let id = Uuid::new_v4();
    let bytes = b"synthetic MP4 payload";
    let mut writer = root
        .try_lock_blob(id)
        .unwrap()
        .begin_write(content(bytes))
        .unwrap();
    writer.append(&bytes[..7]).unwrap();
    assert_eq!(writer.written(), 7);
    writer.append(&bytes[7..]).unwrap();
    let published = writer.finish().unwrap();
    assert_eq!(published.id(), id);
    assert_eq!(published.root_id(), root.id());
    assert_eq!(published.content(), content(bytes));
    assert!(!fixture.root.join(format!("{id}.part")).exists());
    assert!(matches!(
        root.try_read(id, content(bytes)),
        Err(FileError::Busy)
    ));
    drop(published);
    let mut first = root.try_read(id, content(bytes)).unwrap();
    let second = root.try_read(id, content(bytes)).unwrap();
    assert_eq!(first.id(), id);
    assert_eq!(second.content(), content(bytes));
    assert!(matches!(root.try_lock_blob(id), Err(FileError::Busy)));
    let mut received = Vec::new();
    first.read_to_end(&mut received).unwrap();
    assert_eq!(received, bytes);
    first.seek(SeekFrom::Start(10)).unwrap();
    received.clear();
    first.read_to_end(&mut received).unwrap();
    assert_eq!(received, &bytes[10..]);
    drop(root);
    assert!(matches!(
        CacheRoot::open(&fixture.root, fixture.deployment),
        Err(FileError::Busy)
    ));
    drop(first);
    drop(second);
    let root = CacheRoot::open(&fixture.root, fixture.deployment).unwrap();
    let guard = root.try_lock_blob(id).unwrap();
    assert!(guard.remove_data().unwrap());
    assert!(!guard.remove_data().unwrap());
    assert!(fixture.root.join(format!("{id}.lock")).exists());
    assert!(matches!(
        guard.begin_write(content(bytes)),
        Err(FileError::Exists)
    ));
}
#[test]
fn cancelled_oversized_short_or_wrong_hash_writes_never_publish_and_cleanup_is_exact() {
    let fixture = Fixture::new();
    let root = fixture.initialize();
    let bytes = b"12345";
    fixture.write_private("untracked.mp4", b"keep");
    for scenario in 0..5 {
        let id = Uuid::new_v4();
        let mut writer = root
            .try_lock_blob(id)
            .unwrap()
            .begin_write(content(bytes))
            .unwrap();
        match scenario {
            0 => {
                writer.append(b"12").unwrap();
                drop(writer);
            }
            1 => {
                assert!(writer.append(b"123456").is_err());
                assert!(writer.append(bytes).is_err());
                assert!(writer.finish().is_err());
            }
            2 => {
                writer.append(b"12").unwrap();
                assert!(writer.finish().is_err());
            }
            3 => {
                writer.append(b"abcde").unwrap();
                assert!(writer.finish().is_err());
            }
            _ => {
                assert!(writer.append(&[]).is_err());
                assert!(writer.finish().is_err());
            }
        }
        assert!(!fixture.root.join(format!("{id}.blob")).exists());
        let guard = root.try_lock_blob(id).unwrap();
        assert!(guard.remove_data().unwrap());
        assert!(matches!(
            guard.begin_write(content(bytes)),
            Err(FileError::Exists)
        ));
    }
    assert_eq!(
        fs::read(fixture.root.join("untracked.mp4")).unwrap(),
        b"keep"
    );
    assert!(ContentIdentity::new(0, [0; 32]).is_err());
    assert!(ContentIdentity::new(u64::MAX, [0; 32]).is_err());
    assert!(root.try_lock_blob(Uuid::nil()).is_err());
}
#[test]
fn existing_destination_is_never_overwritten_even_without_a_known_attempt_tombstone() {
    let fixture = Fixture::new();
    let root = fixture.initialize();
    let id = Uuid::new_v4();
    fixture.write_private(&format!("{id}.blob"), b"original");
    let bytes = b"replacement";
    let mut writer = root
        .try_lock_blob(id)
        .unwrap()
        .begin_write(content(bytes))
        .unwrap();
    writer.append(bytes).unwrap();
    assert!(writer.finish().is_err());
    assert_eq!(
        fs::read(fixture.root.join(format!("{id}.blob"))).unwrap(),
        b"original"
    );
    let guard = root.try_lock_blob(id).unwrap();
    // Only this synthetic object, explicitly authorized by this test, is removed.
    assert!(guard.remove_data().unwrap());
}
#[test]
fn symlinks_hardlinks_and_reparse_roots_cannot_escape_the_private_root() {
    let fixture = Fixture::new();
    let root = fixture.initialize();
    let id = Uuid::new_v4();
    let outside = fixture.base.path().join("outside.bin");
    fs::write(&outside, b"outside").unwrap();
    #[cfg(unix)]
    private(&outside);
    let blob = fixture.root.join(format!("{id}.blob"));
    fs::hard_link(&outside, &blob).unwrap();
    assert!(root.try_read(id, content(b"outside")).is_err());
    assert!(root.try_lock_blob(id).unwrap().remove_data().is_err());
    fs::remove_file(&blob).unwrap();
    #[cfg(unix)]
    std::os::unix::fs::symlink(&outside, &blob).unwrap();
    #[cfg(windows)]
    std::os::windows::fs::symlink_file(&outside, &blob).unwrap();
    assert!(root.try_read(id, content(b"outside")).is_err());
    assert!(root.try_lock_blob(id).unwrap().remove_data().is_err());
    assert_eq!(fs::read(&outside).unwrap(), b"outside");
    let alias = fixture.base.path().join("alias");
    #[cfg(unix)]
    std::os::unix::fs::symlink(&fixture.root, &alias).unwrap();
    #[cfg(windows)]
    std::os::windows::fs::symlink_dir(&fixture.root, &alias).unwrap();
    assert!(CacheRoot::open(&alias, fixture.deployment).is_err());
    drop(root);
}
struct Process(Child);
impl Drop for Process {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}
#[test]
fn independent_process_owner_is_exclusive_and_forced_exit_releases_without_manual_unlock() {
    let fixture = Fixture::new();
    drop(fixture.initialize());
    let mut command = Command::new(env!("CARGO_BIN_EXE_px_cache_probe"));
    command
        .arg(&fixture.root)
        .arg(fixture.deployment.to_string())
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        command.creation_flags(0x08000000);
    }
    let mut child = Process(command.spawn().unwrap());
    let stdout = child.0.stdout.take().unwrap();
    let (sender, receiver) = std::sync::mpsc::channel();
    let reader = std::thread::spawn(move || {
        let line = BufReader::new(stdout).lines().next();
        let _ = sender.send(line);
    });
    let ready = receiver.recv_timeout(Duration::from_secs(20));
    if ready.is_err() {
        child.0.kill().unwrap();
        child.0.wait().unwrap();
    }
    reader.join().unwrap();
    assert_eq!(ready.unwrap().unwrap().unwrap(), "READY");
    assert!(matches!(
        CacheRoot::open(&fixture.root, fixture.deployment),
        Err(FileError::Busy)
    ));
    child.0.kill().unwrap();
    child.0.wait().unwrap();
    let deadline = std::time::Instant::now() + Duration::from_secs(5);
    let root = loop {
        match CacheRoot::open(&fixture.root, fixture.deployment) {
            Ok(root) => break root,
            Err(FileError::Busy) if std::time::Instant::now() < deadline => {
                std::thread::sleep(Duration::from_millis(10))
            }
            Err(error) => panic!("owner did not release after forced process exit: {error}"),
        }
    };
    let id = Uuid::new_v4();
    let writer = root
        .try_lock_blob(id)
        .unwrap()
        .begin_write(content(b"cancelled"))
        .unwrap();
    drop(root);
    assert!(matches!(
        CacheRoot::open(&fixture.root, fixture.deployment),
        Err(FileError::Busy)
    ));
    drop(writer);
    assert!(CacheRoot::open(&fixture.root, fixture.deployment).is_ok());
}
#[test]
fn restored_metadata_or_correct_size_does_not_substitute_for_actual_content_hash() {
    let fixture = Fixture::new();
    let root = fixture.initialize();
    let id = Uuid::new_v4();
    let bytes = b"original";
    let mut writer = root
        .try_lock_blob(id)
        .unwrap()
        .begin_write(content(bytes))
        .unwrap();
    writer.append(bytes).unwrap();
    drop(writer.finish().unwrap());
    drop(root);
    for damaged in [&b"modified"[..], &b"short"[..], &b"extra bytes"[..]] {
        fixture.write_private(&format!("{id}.blob"), damaged);
        let root = CacheRoot::open(&fixture.root, fixture.deployment).unwrap();
        assert!(matches!(
            root.try_read(id, content(bytes)),
            Err(FileError::Corrupt)
        ));
    }
    fs::remove_file(fixture.root.join(format!("{id}.blob"))).unwrap();
    let root = CacheRoot::open(&fixture.root, fixture.deployment).unwrap();
    assert!(root.try_read(id, content(bytes)).is_err());
}
#[test]
fn untrusted_permissions_are_rejected_and_rename_cannot_redirect_live_directory_operations() {
    let fixture = Fixture::new();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(&fixture.root, fs::Permissions::from_mode(0o755)).unwrap();
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        let output = Command::new("icacls")
            .arg(&fixture.root)
            .args(["/grant", "*S-1-1-0:(OI)(CI)R"])
            .creation_flags(0x08000000)
            .output()
            .unwrap();
        assert!(output.status.success());
    }
    assert!(matches!(
        CacheRoot::initialize(&fixture.root, fixture.deployment),
        Err(FileError::Permission)
    ));
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        assert!(Command::new("icacls")
            .arg(&fixture.root)
            .args(["/remove:g", "*S-1-1-0"])
            .creation_flags(0x08000000)
            .output()
            .unwrap()
            .status
            .success());
    }
    private(&fixture.root);
    let root = fixture.initialize();
    let id = Uuid::new_v4();
    let bytes = b"anchored";
    let mut writer = root
        .try_lock_blob(id)
        .unwrap()
        .begin_write(content(bytes))
        .unwrap();
    let moved = fixture.base.path().join("moved-cache");
    #[cfg(unix)]
    {
        fs::rename(&fixture.root, &moved).unwrap();
        fs::create_dir(&fixture.root).unwrap();
        private(&fixture.root);
    }
    #[cfg(windows)]
    assert!(fs::rename(&fixture.root, &moved).is_err());
    writer.append(bytes).unwrap();
    drop(writer.finish().unwrap());
    let mut reader = root.try_read(id, content(bytes)).unwrap();
    let mut payload_bytes = Vec::new();
    reader.read_to_end(&mut payload_bytes).unwrap();
    assert_eq!(payload_bytes, bytes);
    #[cfg(unix)]
    {
        assert!(moved.join(format!("{id}.blob")).exists());
        assert!(!fixture.root.join(format!("{id}.blob")).exists());
    }
}
