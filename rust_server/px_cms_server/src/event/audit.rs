use std::collections::HashMap;

use crate::event::cms_event::CmsEvent;

/// Security audit is deliberately best-effort: an unavailable audit store must
/// not turn a successful logout/revocation into a misleading HTTP failure.
/// Callers must only pass identifiers and classified reasons, never secrets.
pub async fn record(
    actor_type: &str,
    actor_id: &str,
    action: &str,
    result: &str,
    target_type: &str,
    target_id: &str,
    reason: &str,
) {
    let event = CmsEvent::new_audit(
        actor_type,
        actor_id,
        action,
        result,
        target_type,
        target_id,
        reason,
        HashMap::new(),
    );
    if let Err(error) = crate::gCmsEventMgr.add_event(event).await {
        tracing::warn!(action, result, ?error, "security audit write failed");
    }
}
