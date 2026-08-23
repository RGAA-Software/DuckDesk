use std::collections::HashMap;
use std::future::Future;

use crate::event::console_event::ConsoleEvent;

#[derive(Clone)]
struct AuditActor {
    actor_type: String,
    actor_id: String,
}

tokio::task_local! {
    static CURRENT_ACTOR: AuditActor;
}

/// Keep the authenticated management actor available to nested handlers and
/// manager code without forcing every internal function to carry request
/// context parameters.
pub async fn scope_actor<F, T>(actor_type: &str, actor_id: &str, future: F) -> T
where
    F: Future<Output = T>,
{
    CURRENT_ACTOR
        .scope(
            AuditActor {
                actor_type: actor_type.to_string(),
                actor_id: actor_id.to_string(),
            },
            future,
        )
        .await
}

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
    let scoped_actor = CURRENT_ACTOR.try_with(Clone::clone).ok();
    let (actor_type, actor_id) = scoped_actor
        .as_ref()
        .map(|actor| (actor.actor_type.as_str(), actor.actor_id.as_str()))
        .unwrap_or((actor_type, actor_id));
    let event = ConsoleEvent::new_audit(
        actor_type,
        actor_id,
        action,
        result,
        target_type,
        target_id,
        reason,
        HashMap::new(),
    );
    if let Err(error) = crate::gConsoleEventMgr.add_event(event).await {
        tracing::warn!(action, result, ?error, "security audit write failed");
    }
}
