use crate::app_schedule::manager::{AppAccessMode, AppInstance, InstanceState};
use std::collections::BTreeSet;

/// Single authorization rule shared by catalog, start and ticket issuance.
/// Keeping this pure makes the identity matrix deterministic and prevents the
/// three HTTP paths from drifting apart as ACL behavior evolves.
pub fn user_can_access_app(
    access_mode: &AppAccessMode,
    app_id: &str,
    authorized_app_ids: &BTreeSet<String>,
) -> bool {
    *access_mode == AppAccessMode::Public || authorized_app_ids.contains(app_id)
}

pub fn guest_can_access_app(access_mode: &AppAccessMode) -> bool {
    *access_mode == AppAccessMode::Public
}

pub fn subject_owns_instance(instance: &AppInstance, subject_type: &str, subject_id: &str) -> bool {
    instance.owner_type == subject_type && instance.owner_id == subject_id
}

pub fn subject_owns_running_instance(
    instance: &AppInstance,
    subject_type: &str,
    subject_id: &str,
) -> bool {
    subject_owns_instance(instance, subject_type, subject_id)
        && instance.state == InstanceState::Running
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn app_access_matrix_keeps_public_acl_and_guest_semantics_separate() {
        let mut u1_apps = BTreeSet::new();
        u1_apps.insert("acl-a".to_string());
        let no_apps = BTreeSet::new();

        assert!(user_can_access_app(
            &AppAccessMode::Public,
            "public",
            &no_apps
        ));
        assert!(user_can_access_app(&AppAccessMode::Acl, "acl-a", &u1_apps));
        assert!(!user_can_access_app(&AppAccessMode::Acl, "acl-b", &u1_apps));
        assert!(guest_can_access_app(&AppAccessMode::Public));
        assert!(!guest_can_access_app(&AppAccessMode::Acl));
    }

    #[test]
    fn instance_owner_matrix_rejects_cross_user_and_cross_subject_access() {
        let instance = AppInstance {
            owner_type: "user".into(),
            owner_id: "u1".into(),
            state: InstanceState::Running,
            ..Default::default()
        };

        assert!(subject_owns_instance(&instance, "user", "u1"));
        assert!(subject_owns_running_instance(&instance, "user", "u1"));
        assert!(!subject_owns_instance(&instance, "user", "u2"));
        assert!(!subject_owns_instance(&instance, "guest", "u1"));

        let stopped = AppInstance {
            state: InstanceState::Stopped,
            ..instance
        };
        assert!(subject_owns_instance(&stopped, "user", "u1"));
        assert!(!subject_owns_running_instance(&stopped, "user", "u1"));
    }
}
