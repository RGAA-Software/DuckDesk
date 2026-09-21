use px_node_protocol::{NodeUpdateOffer, NodeUpdateRepository, NodeUpdateTrustObservation};
use px_release_catalog::{ReleaseQuery, ReleaseSpec};
use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::path::{Path, PathBuf};
use tokio::io::AsyncReadExt;
use tough::{ExpirationEnforcement, Limits, Prefix, RepositoryLoader, TargetName};
use url::Url;

const TRUSTED_ROOT_LIMIT: u64 = 1024 * 1024;
const COPY_BUFFER_BYTES: usize = 128 * 1024;
const PIXELS_TARGET_METADATA_KEY: &str = "pixels";

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct PreparedUpdate {
    pub release_id: uuid::Uuid,
    pub policy_revision: i64,
    pub build_number: i64,
    pub version: String,
    pub platform_signer_sha256: String,
    pub artifact_path: PathBuf,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct RepositorySynchronization {
    pub trust_observation: NodeUpdateTrustObservation,
    pub prepared_update: Option<PreparedUpdate>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct SignedPixelsTarget {
    schema_version: u32,
    target: ReleaseQuery,
    build_number: i64,
    version: String,
    platform_signer_sha256: String,
}

pub(crate) async fn synchronize(
    repository_descriptor: &NodeUpdateRepository,
    offer: Option<&NodeUpdateOffer>,
    trusted_root_path: &Path,
    update_data_root: &Path,
) -> Result<RepositorySynchronization, String> {
    validate_repository(repository_descriptor)?;
    if let Some(offer) = offer {
        validate_offer(offer)?;
        if offer.release_id != repository_descriptor.release_id
            || offer.artifact.metadata_base_url != repository_descriptor.metadata_base_url
            || offer.artifact.targets_base_url != repository_descriptor.targets_base_url
        {
            return Err("update offer does not belong to the approved TUF repository".into());
        }
    }
    let trusted_root = read_trusted_root(trusted_root_path).await?;
    crate::node_control_store::platform::ensure_private_directory(update_data_root)?;
    let metadata_base_url = Url::parse(&repository_descriptor.metadata_base_url)
        .map_err(|_| "update metadata base URL is invalid".to_string())?;
    let targets_base_url = Url::parse(&repository_descriptor.targets_base_url)
        .map_err(|_| "update targets base URL is invalid".to_string())?;
    synchronize_from_repository_urls(
        repository_descriptor,
        offer,
        trusted_root,
        update_data_root,
        metadata_base_url,
        targets_base_url,
    )
    .await
}

async fn synchronize_from_repository_urls(
    repository_descriptor: &NodeUpdateRepository,
    offer: Option<&NodeUpdateOffer>,
    trusted_root: Vec<u8>,
    update_data_root: &Path,
    metadata_base_url: Url,
    targets_base_url: Url,
) -> Result<RepositorySynchronization, String> {
    let repository_identity = hex::encode(Sha256::digest(
        repository_descriptor.metadata_base_url.as_bytes(),
    ));
    let datastore = update_data_root
        .join("tuf")
        .join(&repository_identity[..32]);
    ensure_directory(&datastore).await?;
    let repository = RepositoryLoader::new(&trusted_root, metadata_base_url, targets_base_url)
        .datastore(datastore)
        .expiration_enforcement(ExpirationEnforcement::Safe)
        .limits(Limits {
            max_root_size: TRUSTED_ROOT_LIMIT,
            max_targets_size: 16 * 1024 * 1024,
            max_timestamp_size: 1024 * 1024,
            max_snapshot_size: 4 * 1024 * 1024,
            max_root_updates: 64,
        })
        .load()
        .await
        .map_err(|error| format!("TUF metadata verification failed: {error}"))?;
    if repository.root().signed.version.get() < repository_descriptor.root_version {
        return Err("verified TUF repository has not reached the approved root version".into());
    }
    let trust_observation = NodeUpdateTrustObservation {
        release_id: repository_descriptor.release_id,
        repository_publication_sha256: repository_descriptor.repository_publication_sha256.clone(),
        root_version: repository_descriptor.root_version,
    };
    let Some(offer) = offer else {
        return Ok(RepositorySynchronization {
            trust_observation,
            prepared_update: None,
        });
    };
    let target_name = TargetName::new(offer.artifact.target_name.clone())
        .map_err(|_| "update target name is invalid".to_string())?;
    let signed_target = repository
        .targets()
        .signed
        .find_target(&target_name, false)
        .map_err(|_| "update target is absent from verified TUF metadata".to_string())?;
    validate_signed_target(&offer.artifact, signed_target)?;

    let prepared_root = update_data_root.join("prepared");
    ensure_directory(&prepared_root).await?;
    let release_directory_name = format!("{}-{}", offer.release_id, offer.artifact.build_number);
    let final_directory = prepared_root.join(&release_directory_name);
    let final_artifact = final_directory.join(target_name.resolved());
    if final_directory.exists() {
        verify_file(&final_artifact, &offer.artifact).await?;
        return Ok(RepositorySynchronization {
            trust_observation,
            prepared_update: Some(prepared_update(offer, final_artifact)),
        });
    }
    let pending_directory = prepared_root.join(format!(".{release_directory_name}.pending"));
    if pending_directory.exists() {
        return Err("an incomplete update preparation requires explicit cleanup".into());
    }
    tokio::fs::create_dir(&pending_directory)
        .await
        .map_err(|_| "cannot create update staging directory".to_string())?;
    repository
        .save_target(&target_name, &pending_directory, Prefix::None)
        .await
        .map_err(|error| format!("TUF target download failed: {error}"))?;
    let pending_artifact = pending_directory.join(target_name.resolved());
    verify_file(&pending_artifact, &offer.artifact).await?;
    tokio::fs::rename(&pending_directory, &final_directory)
        .await
        .map_err(|_| "cannot commit verified update staging directory".to_string())?;
    verify_file(&final_artifact, &offer.artifact).await?;
    Ok(RepositorySynchronization {
        trust_observation,
        prepared_update: Some(prepared_update(offer, final_artifact)),
    })
}

fn validate_repository(repository: &NodeUpdateRepository) -> Result<(), String> {
    if repository.release_id.is_nil()
        || repository.root_version == 0
        || repository.repository_publication_sha256.len() != 64
        || !repository
            .repository_publication_sha256
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        return Err("update repository descriptor is invalid".into());
    }
    Ok(())
}

fn validate_offer(offer: &NodeUpdateOffer) -> Result<(), String> {
    if offer.release_id.is_nil() || offer.policy_revision < 2 || offer.artifact.validate().is_err()
    {
        return Err("update offer is invalid".into());
    }
    Ok(())
}

fn validate_signed_target(
    release: &ReleaseSpec,
    target: &tough::schema::Target,
) -> Result<(), String> {
    let expected_size = u64::try_from(release.size_bytes)
        .map_err(|_| "update target size is invalid".to_string())?;
    let expected_digest =
        hex::decode(&release.sha256).map_err(|_| "update target digest is invalid".to_string())?;
    if target.length != expected_size || target.hashes.sha256.as_ref() != expected_digest {
        return Err("TUF target content identity does not match the approved release".into());
    }
    let signed = target
        .custom
        .get(PIXELS_TARGET_METADATA_KEY)
        .cloned()
        .ok_or_else(|| "TUF target lacks Pixels release identity".to_string())?;
    let signed: SignedPixelsTarget = serde_json::from_value(signed)
        .map_err(|_| "TUF Pixels release identity is invalid".to_string())?;
    if signed.schema_version != 1
        || signed.target != release.target
        || signed.build_number != release.build_number
        || signed.version != release.version
        || Some(signed.platform_signer_sha256.as_str()) != release.platform_signer_sha256.as_deref()
    {
        return Err("TUF Pixels release identity does not match the approved release".into());
    }
    Ok(())
}

async fn read_trusted_root(path: &Path) -> Result<Vec<u8>, String> {
    reject_reparse_path_components(path)?;
    let metadata = tokio::fs::metadata(path)
        .await
        .map_err(|_| "cannot inspect the installed TUF trusted root".to_string())?;
    if !metadata.is_file() || metadata.len() == 0 || metadata.len() > TRUSTED_ROOT_LIMIT {
        return Err("installed TUF trusted root size is invalid".into());
    }
    tokio::fs::read(path)
        .await
        .map_err(|_| "cannot read the installed TUF trusted root".to_string())
}

async fn ensure_directory(path: &Path) -> Result<(), String> {
    tokio::fs::create_dir_all(path)
        .await
        .map_err(|_| "cannot create update state directory".to_string())?;
    reject_reparse_path_components(path)?;
    let metadata = tokio::fs::metadata(path)
        .await
        .map_err(|_| "cannot inspect update state directory".to_string())?;
    if !metadata.is_dir() {
        return Err("update state path is not a directory".into());
    }
    Ok(())
}

fn reject_reparse_path_components(path: &Path) -> Result<(), String> {
    for component in path.ancestors() {
        let metadata = std::fs::symlink_metadata(component)
            .map_err(|_| "cannot inspect update path identity".to_string())?;
        if metadata.file_type().is_symlink() {
            return Err("update path cannot contain a symbolic link".into());
        }
        #[cfg(windows)]
        {
            use std::os::windows::fs::MetadataExt;
            const FILE_ATTRIBUTE_REPARSE_POINT: u32 = 0x0400;
            if metadata.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
                return Err("update path cannot contain a reparse point".into());
            }
        }
    }
    Ok(())
}

async fn verify_file(path: &Path, release: &ReleaseSpec) -> Result<(), String> {
    reject_reparse_path_components(path)?;
    let metadata = tokio::fs::metadata(path)
        .await
        .map_err(|_| "cannot inspect prepared update artifact".to_string())?;
    if !metadata.is_file() || metadata.len() != u64::try_from(release.size_bytes).unwrap_or(0) {
        return Err("prepared update artifact size does not match the approved release".into());
    }
    let mut file = tokio::fs::File::open(path)
        .await
        .map_err(|_| "cannot read prepared update artifact".to_string())?;
    let mut digest = Sha256::new();
    let mut buffer = vec![0_u8; COPY_BUFFER_BYTES];
    loop {
        let read = file
            .read(&mut buffer)
            .await
            .map_err(|_| "cannot hash prepared update artifact".to_string())?;
        if read == 0 {
            break;
        }
        digest.update(&buffer[..read]);
    }
    if hex::encode(digest.finalize()) != release.sha256 {
        return Err("prepared update artifact digest does not match the approved release".into());
    }
    Ok(())
}

fn prepared_update(offer: &NodeUpdateOffer, artifact_path: PathBuf) -> PreparedUpdate {
    PreparedUpdate {
        release_id: offer.release_id,
        policy_revision: offer.policy_revision,
        build_number: offer.artifact.build_number,
        version: offer.artifact.version.clone(),
        platform_signer_sha256: offer
            .artifact
            .platform_signer_sha256
            .clone()
            .expect("validated Windows release has a signer pin"),
        artifact_path,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use aws_lc_rs::rand::SystemRandom;
    use jiff::{SignedDuration, Timestamp};
    use px_release_catalog::{
        Architecture, Channel, Distribution, OperatingSystem, Product, ReleaseQuery,
    };
    use std::collections::HashMap;
    use std::num::NonZeroU64;
    use tough::editor::signed::SignedRole;
    use tough::editor::RepositoryEditor;
    use tough::key_source::{KeySource, LocalKeySource};
    use tough::schema::decoded::{Decoded, Hex};
    use tough::schema::{Hashes, KeyHolder, RoleKeys, RoleType, Root, Signed, Target};

    fn release() -> ReleaseSpec {
        ReleaseSpec {
            target: ReleaseQuery {
                product: Product::CloudNode,
                distribution: Distribution::Official,
                release_namespace: "pixels.official".into(),
                oem_id: None,
                channel: Channel::Stable,
                os: OperatingSystem::Windows,
                architecture: Architecture::X86_64,
            },
            build_number: 30368,
            version: "3.3.68".into(),
            metadata_base_url: "https://updates.example.test/metadata/".into(),
            targets_base_url: "https://updates.example.test/targets/".into(),
            target_name: "cloud-node/PixelsCloudNode.exe".into(),
            sha256: "a".repeat(64),
            platform_signer_sha256: Some("b".repeat(64)),
            size_bytes: 4096,
        }
    }

    fn signed_target(release: &ReleaseSpec) -> Target {
        let mut custom = HashMap::new();
        custom.insert(
            PIXELS_TARGET_METADATA_KEY.into(),
            serde_json::json!({
                "schema_version":1,
                "target":release.target,
                "build_number":release.build_number,
                "version":release.version,
                "platform_signer_sha256":release.platform_signer_sha256,
            }),
        );
        Target {
            length: u64::try_from(release.size_bytes).unwrap(),
            hashes: Hashes {
                sha256: Decoded::<Hex>::from(hex::decode(&release.sha256).unwrap()),
                _extra: HashMap::new(),
            },
            custom,
            _extra: HashMap::new(),
        }
    }

    #[test]
    fn signed_tuf_target_must_match_every_approved_release_dimension() {
        let release = release();
        let target = signed_target(&release);
        validate_signed_target(&release, &target).unwrap();
        let mut wrong_build = release.clone();
        wrong_build.build_number += 1;
        assert!(validate_signed_target(&wrong_build, &target).is_err());
        let mut wrong_distribution = release.clone();
        wrong_distribution.target.distribution = Distribution::Customer;
        assert!(validate_signed_target(&wrong_distribution, &target).is_err());
        let mut wrong_digest = release.clone();
        wrong_digest.sha256 = "b".repeat(64);
        assert!(validate_signed_target(&wrong_digest, &target).is_err());
        let mut wrong_signer = release.clone();
        wrong_signer.platform_signer_sha256 = Some("c".repeat(64));
        assert!(validate_signed_target(&wrong_signer, &target).is_err());
    }

    #[tokio::test]
    async fn prepared_artifact_is_rehashed_before_it_can_be_returned() {
        let directory = tempfile::tempdir().unwrap();
        let artifact = directory.path().join("artifact.bin");
        let bytes = b"verified update bytes";
        tokio::fs::write(&artifact, bytes).await.unwrap();
        let mut release = release();
        release.size_bytes = i64::try_from(bytes.len()).unwrap();
        release.sha256 = hex::encode(Sha256::digest(bytes));
        verify_file(&artifact, &release).await.unwrap();
        tokio::fs::write(&artifact, b"tampered update bytes")
            .await
            .unwrap();
        assert!(verify_file(&artifact, &release).await.is_err());
    }

    #[tokio::test]
    async fn signed_tuf_repository_is_verified_before_update_is_prepared() {
        let repository_root = tempfile::tempdir().unwrap();
        let root_path = repository_root.path().join("root.json");
        let key_path = repository_root.path().join("test-signing-key.pk8");
        let metadata_directory = repository_root.path().join("metadata");
        let targets_directory = repository_root.path().join("targets");
        let target_name = TargetName::new("cloud-node/PixelsCloudNode.exe").unwrap();
        let target_path = targets_directory.join(target_name.raw());
        let target_bytes = b"signed Pixels Cloud Node update";
        tokio::fs::create_dir_all(target_path.parent().unwrap())
            .await
            .unwrap();
        tokio::fs::write(&target_path, target_bytes).await.unwrap();

        let generated_key =
            ring::signature::Ed25519KeyPair::generate_pkcs8(&ring::rand::SystemRandom::new())
                .unwrap();
        tokio::fs::write(&key_path, generated_key.as_ref())
            .await
            .unwrap();
        let keys: Vec<Box<dyn KeySource>> = vec![Box::new(LocalKeySource { path: key_path })];
        write_test_root(&root_path, &keys).await;

        let mut approved_release = release();
        approved_release.size_bytes = i64::try_from(target_bytes.len()).unwrap();
        approved_release.sha256 = hex::encode(Sha256::digest(target_bytes));
        let mut target = Target::from_path(&target_path).await.unwrap();
        target.custom = signed_target(&approved_release).custom;
        let version = NonZeroU64::new(1).unwrap();
        let expiration = Timestamp::now() + SignedDuration::from_hours(24);
        let mut editor = RepositoryEditor::new(&root_path).await.unwrap();
        editor
            .targets_version(version)
            .unwrap()
            .targets_expires(expiration)
            .unwrap()
            .snapshot_version(version)
            .snapshot_expires(expiration)
            .timestamp_version(version)
            .timestamp_expires(expiration)
            .add_target(target_name.clone(), target)
            .unwrap();
        editor
            .sign(&keys)
            .await
            .unwrap()
            .write(&metadata_directory)
            .await
            .unwrap();

        let offer = NodeUpdateOffer {
            release_id: uuid::Uuid::new_v4(),
            policy_revision: 2,
            artifact: approved_release.clone(),
        };
        let repository_descriptor = NodeUpdateRepository {
            release_id: offer.release_id,
            repository_publication_sha256: "c".repeat(64),
            root_version: 1,
            metadata_base_url: approved_release.metadata_base_url.clone(),
            targets_base_url: approved_release.targets_base_url.clone(),
        };
        let update_data_root = repository_root.path().join("service-update-state");
        let synchronized = synchronize_from_repository_urls(
            &repository_descriptor,
            Some(&offer),
            tokio::fs::read(&root_path).await.unwrap(),
            &update_data_root,
            Url::from_directory_path(&metadata_directory).unwrap(),
            Url::from_directory_path(&targets_directory).unwrap(),
        )
        .await
        .unwrap();
        let prepared = synchronized.prepared_update.unwrap();
        assert_eq!(synchronized.trust_observation.root_version, 1);
        assert_eq!(prepared.release_id, offer.release_id);
        assert_eq!(
            tokio::fs::read(&prepared.artifact_path).await.unwrap(),
            target_bytes
        );
        let metadata_only = synchronize_from_repository_urls(
            &repository_descriptor,
            None,
            tokio::fs::read(&root_path).await.unwrap(),
            &repository_root.path().join("metadata-only-state"),
            Url::from_directory_path(&metadata_directory).unwrap(),
            Url::from_directory_path(&targets_directory).unwrap(),
        )
        .await
        .unwrap();
        assert!(metadata_only.prepared_update.is_none());
        assert_eq!(metadata_only.trust_observation.release_id, offer.release_id);

        let expired_metadata_directory = repository_root.path().join("expired-metadata");
        let mut expired_target = Target::from_path(&target_path).await.unwrap();
        expired_target.custom = signed_target(&approved_release).custom;
        let expired_at = Timestamp::now() - SignedDuration::from_hours(1);
        let mut expired_editor = RepositoryEditor::new(&root_path).await.unwrap();
        expired_editor
            .targets_version(version)
            .unwrap()
            .targets_expires(expired_at)
            .unwrap()
            .snapshot_version(version)
            .snapshot_expires(expired_at)
            .timestamp_version(version)
            .timestamp_expires(expired_at)
            .add_target(target_name.clone(), expired_target)
            .unwrap();
        expired_editor
            .sign(&keys)
            .await
            .unwrap()
            .write(&expired_metadata_directory)
            .await
            .unwrap();
        let expiration_error = synchronize_from_repository_urls(
            &repository_descriptor,
            Some(&offer),
            tokio::fs::read(&root_path).await.unwrap(),
            &repository_root.path().join("expiration-check-state"),
            Url::from_directory_path(&expired_metadata_directory).unwrap(),
            Url::from_directory_path(&targets_directory).unwrap(),
        )
        .await
        .unwrap_err();
        assert!(expiration_error.contains("TUF metadata verification failed"));

        tokio::fs::write(&target_path, b"tampered Pixels Cloud Node update")
            .await
            .unwrap();
        let second_data_root = repository_root.path().join("tamper-check-state");
        let error = synchronize_from_repository_urls(
            &repository_descriptor,
            Some(&offer),
            tokio::fs::read(&root_path).await.unwrap(),
            &second_data_root,
            Url::from_directory_path(&metadata_directory).unwrap(),
            Url::from_directory_path(&targets_directory).unwrap(),
        )
        .await
        .unwrap_err();
        assert!(error.contains("TUF target download failed"));
    }

    async fn write_test_root(root_path: &Path, keys: &[Box<dyn KeySource>]) {
        let public_key = keys[0].as_sign().await.unwrap().tuf_key();
        let key_id = public_key.key_id().unwrap();
        let role_keys = RoleKeys {
            keyids: vec![key_id.clone()],
            threshold: NonZeroU64::new(1).unwrap(),
            _extra: HashMap::new(),
        };
        let mut root = Signed {
            signed: Root {
                spec_version: "1.0.0".into(),
                consistent_snapshot: false,
                version: NonZeroU64::new(1).unwrap(),
                expires: Timestamp::now() + SignedDuration::from_hours(24),
                keys: HashMap::new(),
                roles: [
                    (RoleType::Root, role_keys.clone()),
                    (RoleType::Snapshot, role_keys.clone()),
                    (RoleType::Targets, role_keys.clone()),
                    (RoleType::Timestamp, role_keys),
                ]
                .into_iter()
                .collect(),
                _extra: HashMap::new(),
            },
            signatures: Vec::new(),
        };
        root.signed.keys.insert(key_id, public_key);
        let signed_root = SignedRole::new(
            root.signed.clone(),
            &KeyHolder::Root(root.signed),
            keys,
            &SystemRandom::new(),
        )
        .await
        .unwrap();
        tokio::fs::write(root_path, signed_root.buffer())
            .await
            .unwrap();
    }
}
