use crate::RecoverySetManifest;
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};
use uuid::Uuid;

const SECONDS_PER_DAY: u64 = 86_400;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RetentionClass {
    Hourly,
    Daily,
    Weekly,
    Monthly,
    PreUpgrade,
    Manual,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RetentionPolicy {
    pub hourly: usize,
    pub daily: usize,
    pub weekly: usize,
    pub monthly: usize,
    pub pre_upgrade: usize,
    pub manual_days: u64,
}

impl Default for RetentionPolicy {
    fn default() -> Self {
        Self {
            hourly: 24,
            daily: 7,
            weekly: 4,
            monthly: 6,
            pre_upgrade: 5,
            manual_days: 30,
        }
    }
}

pub fn retained_set_ids(
    manifests: &[RecoverySetManifest],
    policy: RetentionPolicy,
    now_unix: u64,
) -> BTreeSet<Uuid> {
    let mut retained = BTreeSet::new();
    let verified = manifests
        .iter()
        .filter(|manifest| manifest.status.is_verified())
        .collect::<Vec<_>>();

    if let Some(last_valid) = verified
        .iter()
        .max_by_key(|manifest| (manifest.completed_at_unix, manifest.recovery_set_id))
    {
        retained.insert(last_valid.recovery_set_id);
    }
    for manifest in manifests {
        if manifest.locked || manifest.restoring {
            retained.insert(manifest.recovery_set_id);
        }
    }

    for (class, limit) in [
        (RetentionClass::Hourly, policy.hourly),
        (RetentionClass::Daily, policy.daily),
        (RetentionClass::Weekly, policy.weekly),
        (RetentionClass::Monthly, policy.monthly),
        (RetentionClass::PreUpgrade, policy.pre_upgrade),
    ] {
        let mut matching = verified
            .iter()
            .filter(|manifest| manifest.retention.contains(&class))
            .copied()
            .collect::<Vec<_>>();
        matching.sort_by_key(|manifest| (manifest.completed_at_unix, manifest.recovery_set_id));
        retained.extend(
            matching
                .into_iter()
                .rev()
                .take(limit)
                .map(|manifest| manifest.recovery_set_id),
        );
    }

    retained.extend(
        verified
            .iter()
            .filter(|manifest| {
                manifest.retention.contains(&RetentionClass::Manual)
                    && now_unix.saturating_sub(manifest.created_at_unix)
                        < policy.manual_days.saturating_mul(SECONDS_PER_DAY)
            })
            .map(|manifest| manifest.recovery_set_id),
    );

    let by_id = manifests
        .iter()
        .map(|manifest| (manifest.recovery_set_id, manifest))
        .collect::<BTreeMap<_, _>>();
    let mut pending = retained.iter().copied().collect::<Vec<_>>();
    while let Some(id) = pending.pop() {
        if let Some(dependency) = by_id
            .get(&id)
            .and_then(|manifest| manifest.previous_recovery_set_id)
        {
            if retained.insert(dependency) {
                pending.push(dependency);
            }
        }
    }
    retained
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        BackupMember, BackupMemberState, BackupService, RecoverySetKind, RecoverySetStatus,
        MANIFEST_SCHEMA_VERSION,
    };

    fn set(index: u64, retention: &[RetentionClass]) -> RecoverySetManifest {
        RecoverySetManifest {
            schema_version: MANIFEST_SCHEMA_VERSION,
            recovery_set_id: Uuid::from_u128(index as u128 + 1),
            deployment_id: Uuid::from_u128(999),
            kind: RecoverySetKind::WriteBarrier,
            status: RecoverySetStatus::Verified,
            created_at_unix: index * 3_600 + 1,
            completed_at_unix: Some(index * 3_600 + 2),
            locked: false,
            restoring: false,
            retention: retention.iter().copied().collect(),
            previous_recovery_set_id: None,
            members: [
                BackupService::Console,
                BackupService::Auth,
                BackupService::Desk,
            ]
            .into_iter()
            .map(|service| BackupMember {
                service,
                member: BackupMemberState::Required {
                    database: "pixels_test".into(),
                    schema_version: 1,
                    archive_file: format!("{service:?}.dump").to_ascii_lowercase(),
                    archive_sha256: "1".repeat(64),
                    started_at_unix: index * 3_600 + 1,
                    completed_at_unix: index * 3_600 + 2,
                },
            })
            .collect(),
            failure_code: None,
        }
    }

    #[test]
    fn seven_month_policy_keeps_exact_independent_tier_windows() {
        let mut manifests = Vec::new();
        for hour in 0..(31 * 7 * 24) {
            let mut classes = vec![RetentionClass::Hourly];
            if hour % 24 == 0 {
                classes.push(RetentionClass::Daily);
            }
            if hour % (24 * 7) == 0 {
                classes.push(RetentionClass::Weekly);
            }
            if hour % (24 * 31) == 0 {
                classes.push(RetentionClass::Monthly);
            }
            manifests.push(set(hour, &classes));
        }
        let retained =
            retained_set_ids(&manifests, RetentionPolicy::default(), 31 * 7 * 24 * 3_600);
        let latest = manifests.last().unwrap().recovery_set_id;
        assert!(retained.contains(&latest));
        assert!(retained.len() <= 24 + 7 + 4 + 6);
        assert!(retained.len() >= 24);
    }

    #[test]
    fn one_live_reference_lock_restore_last_valid_and_dependencies_all_protect_a_set() {
        let mut old = set(1, &[RetentionClass::Hourly, RetentionClass::Monthly]);
        let mut dependency = set(2, &[RetentionClass::Hourly]);
        let mut locked = set(3, &[RetentionClass::Hourly]);
        let mut restoring = set(4, &[RetentionClass::Hourly]);
        let latest = set(5, &[RetentionClass::Hourly]);
        locked.locked = true;
        restoring.restoring = true;
        dependency.previous_recovery_set_id = Some(old.recovery_set_id);
        old.status = RecoverySetStatus::Verified;
        let policy = RetentionPolicy {
            hourly: 1,
            daily: 0,
            weekly: 0,
            monthly: 1,
            pre_upgrade: 0,
            manual_days: 0,
        };
        let retained = retained_set_ids(
            &[
                old.clone(),
                dependency.clone(),
                locked.clone(),
                restoring.clone(),
                latest.clone(),
            ],
            policy,
            1_000_000,
        );
        for id in [
            old.recovery_set_id,
            locked.recovery_set_id,
            restoring.recovery_set_id,
            latest.recovery_set_id,
        ] {
            assert!(retained.contains(&id));
        }
    }

    #[test]
    fn failed_sets_do_not_consume_success_slots_and_manual_expiry_is_bounded() {
        let old_manual = set(1, &[RetentionClass::Manual]);
        let mut failed = set(2, &[RetentionClass::Hourly]);
        failed.status = RecoverySetStatus::Failed;
        failed.failure_code = Some("ARCHIVE_FAILED".into());
        let current = set(3, &[RetentionClass::Hourly]);
        let retained = retained_set_ids(
            &[old_manual.clone(), failed.clone(), current.clone()],
            RetentionPolicy {
                hourly: 1,
                ..RetentionPolicy::default()
            },
            old_manual.created_at_unix + 31 * SECONDS_PER_DAY,
        );
        assert!(!retained.contains(&old_manual.recovery_set_id));
        assert!(!retained.contains(&failed.recovery_set_id));
        assert!(retained.contains(&current.recovery_set_id));
    }
}
