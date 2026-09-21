#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, node_report, request, token, Fixture};
use px_console_store::*;
use std::{env, sync::Arc, time::Duration};
use tokio::sync::Barrier;
use uuid::Uuid;
struct Context {
    fixture: Fixture,
    activity_store: ActivityStore,
    session_store: ResourceSessionStore,
    node: NodeConnection,
    user: TokenDigest,
    client: ClientType,
    instance: ApplicationInstance,
    session: ResourceSession,
}
impl Context {
    async fn new(target: DeploymentTarget, admitted: bool) -> Self {
        let client = if target == DeploymentTarget::Rdp {
            ClientType::Panel
        } else {
            ClientType::Android
        };
        let deployment = env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap();
        let fixture = Fixture::new().await;
        let activity_store = ActivityStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let session_store = ResourceSessionStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let (node, application, _) = fixture.prepared(target, 1).await;
        let user = fixture.session("user", client).await;
        let instance = fixture
            .instances
            .reserve(
                ResourceCredential::User(&user),
                client,
                node.epoch(),
                &request(application.id),
            )
            .await
            .unwrap();
        let start_command = fixture
            .instances
            .next_command(&node)
            .await
            .unwrap()
            .unwrap();
        let port = match start_command.action {
            NodeCommandAction::Start { port, .. } => port,
            _ => panic!("start required"),
        };
        fixture
            .instances
            .acknowledge_command(
                &node,
                &CommandReceipt {
                    command_id: start_command.id,
                    lease_id: start_command.lease_id,
                    instance_id: instance.id,
                    launch_id: start_command.launch_id,
                    instance_revision: start_command.instance_revision,
                    outcome: CommandOutcome::Running { port },
                },
            )
            .await
            .unwrap();
        let session = session_store
            .open(
                ResourceCredential::User(&user),
                client,
                &OpenResourceSession {
                    request_id: Uuid::new_v4(),
                    target: SessionTarget::CloudApplication {
                        application_id: application.id,
                        instance_id: instance.id,
                    },
                    access: SessionAccess::Controller,
                },
            )
            .await
            .unwrap();
        let context = Self {
            fixture,
            activity_store,
            session_store,
            node,
            user,
            client,
            instance,
            session,
        };
        if admitted {
            context.admit(&context.session).await;
        }
        context
    }
    async fn admit(&self, session: &ResourceSession) {
        let secret = token();
        let descriptor = self
            .session_store
            .descriptor(
                ResourceCredential::User(&self.user),
                self.client,
                session.id,
                session.revision,
                &secret,
            )
            .await
            .unwrap();
        self.session_store
            .admit_frontend(&self.node, session.id, descriptor.session.revision, &secret)
            .await
            .unwrap();
    }
    fn open(&self, kind: ChannelKind) -> OpenChannel {
        OpenChannel {
            source_id: Uuid::new_v4(),
            session_id: self.session.id,
            kind,
        }
    }
    async fn channels(&self) -> Vec<ChannelRecord> {
        self.activity_store
            .channels_managed(&self.fixture.admin, Some(self.session.id), None, 100)
            .await
            .unwrap()
    }
    async fn visits(&self) -> Vec<VisitRecord> {
        self.activity_store
            .visits_owned(ResourceCredential::User(&self.user), self.client, None, 100)
            .await
            .unwrap()
    }
    async fn close(self) {
        self.activity_store.close().await;
        self.session_store.close().await;
        self.fixture.close().await;
    }
}
fn progress(sequence: u64, byte_count: u64) -> ChannelProgress {
    ChannelProgress {
        sequence,
        sent_bytes: byte_count,
        received_bytes: byte_count,
        elapsed_ms: byte_count,
        outcome: ChannelOutcome::Progress,
    }
}
async fn permission(context: &Context, allow: bool) {
    sqlx::query(if allow {
        "GRANT INSERT ON pixels.connection_observation_events TO pixels_console_runtime"
    } else {
        "REVOKE INSERT ON pixels.connection_observation_events FROM pixels_console_runtime"
    })
    .execute(&context.fixture.owner)
    .await
    .unwrap();
}
#[tokio::test]
async fn visit_confirmation_and_multiple_channels_never_create_more_occupants_or_close_the_app() {
    let context = Context::new(DeploymentTarget::Webview, false).await;
    assert!(context.visits().await[0].first_connected_at.is_none());
    assert!(context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Media))
        .await
        .is_err());
    context.admit(&context.session).await;
    let initial = context.visits().await.remove(0);
    assert!(initial.first_connected_at.is_some());
    assert_eq!(initial.channel_count, 0);
    let first = context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Control))
        .await
        .unwrap();
    context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Audio))
        .await
        .unwrap();
    let mut closed = progress(1, 10);
    closed.outcome = ChannelOutcome::Closed {
        reason: ChannelClose::PeerClosed,
    };
    let done = context
        .activity_store
        .report_channel(&context.node, first.id, &closed)
        .await
        .unwrap();
    assert!(done.ended_at.is_some());
    assert_eq!(
        context
            .activity_store
            .report_channel(&context.node, first.id, &closed)
            .await
            .unwrap(),
        done
    );
    let visit = context.visits().await.remove(0);
    assert_eq!(visit.channel_count, 2);
    assert_eq!(visit.first_connected_at, initial.first_connected_at);
    assert_eq!(visit.session.state, "connected");
    assert!(visit.session.closed_at.is_none());
    let count: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.resource_sessions WHERE instance_id=$1")
            .bind(context.instance.id)
            .fetch_one(&context.fixture.owner)
            .await
            .unwrap();
    assert_eq!(count, 1);
    let state: String = sqlx::query_scalar("SELECT state FROM pixels.instances WHERE id=$1")
        .bind(context.instance.id)
        .fetch_one(&context.fixture.owner)
        .await
        .unwrap();
    assert_eq!(state, "running");
    context.close().await;
}
#[tokio::test]
async fn concurrent_duplicate_channels_and_ordered_reports_have_one_identity_and_exact_body_retry()
{
    let context = Context::new(DeploymentTarget::Webview, true).await;
    let open_request = context.open(ChannelKind::Media);
    let start_barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (activity_store, node, open_request, start_barrier) = (
            context.activity_store.clone(),
            context.node.clone(),
            open_request.clone(),
            start_barrier.clone(),
        );
        tasks.push(tokio::spawn(async move {
            start_barrier.wait().await;
            activity_store
                .open_channel(&node, &open_request)
                .await
                .unwrap()
        }));
    }
    let mut ids = Vec::new();
    for task_handle in tasks {
        ids.push(task_handle.await.unwrap().id);
    }
    assert!(ids.iter().all(|id| *id == ids[0]));
    let id = ids[0];
    let mut changed = open_request.clone();
    changed.kind = ChannelKind::Control;
    assert!(context
        .activity_store
        .open_channel(&context.node, &changed)
        .await
        .is_err());
    let start_barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for contender_index in 0..20 {
        let (activity_store, node, start_barrier) = (
            context.activity_store.clone(),
            context.node.clone(),
            start_barrier.clone(),
        );
        tasks.push(tokio::spawn(async move {
            start_barrier.wait().await;
            activity_store
                .report_channel(&node, id, &progress(1, 10 + contender_index % 2))
                .await
        }));
    }
    let mut accepted = Vec::new();
    let mut denied = 0;
    for task_handle in tasks {
        match task_handle.await.unwrap() {
            Ok(channel_record) => accepted.push(channel_record),
            Err(_) => denied += 1,
        }
    }
    assert_eq!(accepted.len(), 10);
    assert_eq!(denied, 10);
    assert!(accepted
        .iter()
        .all(|accepted_record| *accepted_record == accepted[0]));
    assert!(context
        .activity_store
        .report_channel(&context.node, id, &progress(2, 1))
        .await
        .is_err());
    let mut fail = progress(2, 20);
    fail.outcome = ChannelOutcome::Failed {
        reason: ChannelFailure::TransportLost,
    };
    let failed = context
        .activity_store
        .report_channel(&context.node, id, &fail)
        .await
        .unwrap();
    assert_eq!(failed.state, "failed");
    assert!(context
        .activity_store
        .report_channel(&context.node, id, &progress(3, 30))
        .await
        .is_err());
    context.close().await;
}
#[tokio::test]
async fn channel_limit_is_atomic_under_twenty_contenders_and_pages_do_not_leak_producer_secrets() {
    let context = Context::new(DeploymentTarget::Webview, true).await;
    for _ in 0..15 {
        context
            .activity_store
            .open_channel(&context.node, &context.open(ChannelKind::Media))
            .await
            .unwrap();
    }
    let start_barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (activity_store, node, open_request, start_barrier) = (
            context.activity_store.clone(),
            context.node.clone(),
            context.open(ChannelKind::Media),
            start_barrier.clone(),
        );
        tasks.push(tokio::spawn(async move {
            start_barrier.wait().await;
            activity_store.open_channel(&node, &open_request).await
        }));
    }
    let mut won = 0;
    for task_handle in tasks {
        if task_handle.await.unwrap().is_ok() {
            won += 1;
        }
    }
    assert_eq!(won, 1);
    let mut after = None;
    let mut ids = Vec::new();
    loop {
        let rows = context
            .activity_store
            .channels_managed(&context.fixture.admin, Some(context.session.id), after, 3)
            .await
            .unwrap();
        if rows.is_empty() {
            break;
        }
        after = Some(rows.last().unwrap().id);
        ids.extend(rows.into_iter().map(|channel_record| channel_record.id));
    }
    assert_eq!(ids.len(), 16);
    let mut unique = ids.clone();
    unique.sort();
    unique.dedup();
    assert_eq!(unique.len(), 16);
    let body = serde_json::to_string(&context.channels().await).unwrap();
    for field in [
        "token",
        "request_hash",
        "source_id",
        "report_hash",
        "appkey",
        "login_session",
    ] {
        assert!(!body.contains(field));
    }
    assert!(context
        .activity_store
        .channels_managed(&context.fixture.admin, None, None, 101)
        .await
        .is_err());
    assert!(context
        .activity_store
        .visits_managed(&context.fixture.admin, None, 0)
        .await
        .is_err());
    context.close().await;
}
#[tokio::test]
async fn node_rotation_preserves_unknown_history_and_never_adopts_old_source_identity() {
    let context = Context::new(DeploymentTarget::Webview, true).await;
    let open_request = context.open(ChannelKind::Media);
    let row = context
        .activity_store
        .open_channel(&context.node, &open_request)
        .await
        .unwrap();
    context
        .activity_store
        .report_channel(&context.node, row.id, &progress(1, 20))
        .await
        .unwrap();
    let key = token();
    context
        .fixture
        .nodes
        .rotate_key(&context.fixture.admin, context.node.id(), 2, &key)
        .await
        .unwrap();
    let current = context
        .fixture
        .nodes
        .open_connection(context.node.epoch(), &key, &token())
        .await
        .unwrap();
    context
        .fixture
        .nodes
        .report(&current, &node_report(1))
        .await
        .unwrap();
    let unknown = context.channels().await.remove(0);
    assert_eq!(unknown.state, "unknown");
    assert!(unknown.ended_at.is_none());
    assert_eq!(unknown.sent_bytes, 20);
    assert!(context
        .activity_store
        .open_channel(&current, &open_request)
        .await
        .is_err());
    assert!(context
        .activity_store
        .report_channel(&current, row.id, &progress(2, 30))
        .await
        .is_err());
    assert!(context
        .activity_store
        .report_channel(&context.node, row.id, &progress(2, 30))
        .await
        .is_err());
    assert!(context.visits().await[0].session.closed_at.is_none());
    context.close().await;
}
#[tokio::test]
async fn audit_failures_roll_back_channel_mutations_node_invalidation_and_frontend_retirement() {
    let context = Context::new(DeploymentTarget::Webview, true).await;
    permission(&context, false).await;
    let failed = context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Media))
        .await;
    permission(&context, true).await;
    assert!(failed.is_err());
    assert!(context.channels().await.is_empty());
    let row = context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Media))
        .await
        .unwrap();
    permission(&context, false).await;
    let failed = context
        .activity_store
        .report_channel(&context.node, row.id, &progress(1, 1))
        .await;
    permission(&context, true).await;
    assert!(failed.is_err());
    assert_eq!(context.channels().await[0].revision, 1);
    permission(&context, false).await;
    let failed = context.fixture.nodes.close_connection(&context.node).await;
    permission(&context, true).await;
    assert!(failed.is_err());
    assert_eq!(context.channels().await[0].state, "active");
    let challenge = context
        .session_store
        .begin_retirement(&context.node, context.session.id)
        .await
        .unwrap();
    permission(&context, false).await;
    let failed = context
        .session_store
        .finish_retirement(&context.node, context.session.id, challenge.challenge_id)
        .await;
    permission(&context, true).await;
    assert!(failed.is_err());
    assert_eq!(context.channels().await[0].state, "active");
    assert_eq!(context.visits().await[0].session.state, "closing");
    context
        .session_store
        .finish_retirement(&context.node, context.session.id, challenge.challenge_id)
        .await
        .unwrap();
    context
        .session_store
        .finish_retirement(&context.node, context.session.id, challenge.challenge_id)
        .await
        .unwrap();
    let result = context.channels().await.remove(0);
    assert_eq!(result.state, "unknown");
    assert_eq!(result.revision, 2);
    assert!(result.ended_at.is_none());
    assert!(context.visits().await[0].session.closed_at.is_some());
    assert!(context
        .activity_store
        .report_channel(&context.node, row.id, &progress(1, 100))
        .await
        .is_err());
    let mut terminal = progress(1, 100);
    terminal.outcome = ChannelOutcome::Failed {
        reason: ChannelFailure::PolicyRevoked,
    };
    let finalized = context
        .activity_store
        .report_channel(&context.node, row.id, &terminal)
        .await
        .unwrap();
    assert_eq!(finalized.state, "failed");
    assert_eq!(finalized.reason.as_deref(), Some("policy_revoked"));
    assert_eq!(finalized.revision, 3);
    assert!(finalized.ended_at.is_some());
    let runtime = config("RUNTIME").connect().await.unwrap();
    assert!(sqlx::query("DELETE FROM pixels.connection_observations")
        .execute(&runtime)
        .await
        .is_err());
    assert!(sqlx::query(
        "UPDATE pixels.connection_observations SET node_generation=node_generation+1"
    )
    .execute(&runtime)
    .await
    .is_err());
    runtime.close().await;
    context.close().await;
}
#[tokio::test]
async fn revoked_origin_denies_progress_but_same_producer_can_close_and_fresh_login_can_read_history(
) {
    let context = Context::new(DeploymentTarget::Webview, true).await;
    let row = context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Media))
        .await
        .unwrap();
    let identity = context
        .fixture
        .identity
        .authenticate(&context.user, context.client)
        .await
        .unwrap();
    context
        .fixture
        .identity
        .revoke_session(identity.user_id, identity.session_id)
        .await
        .unwrap();
    assert!(context
        .activity_store
        .report_channel(&context.node, row.id, &progress(1, 1))
        .await
        .is_err());
    assert!(context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Audio))
        .await
        .is_err());
    let mut closed = progress(1, 1);
    closed.outcome = ChannelOutcome::Closed {
        reason: ChannelClose::UserStopped,
    };
    context
        .activity_store
        .report_channel(&context.node, row.id, &closed)
        .await
        .unwrap();
    let login = token();
    context
        .fixture
        .identity
        .issue_session(
            identity.user_id,
            1,
            &login,
            context.client,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    let history = context
        .activity_store
        .visits_owned(ResourceCredential::User(&login), context.client, None, 100)
        .await
        .unwrap();
    assert_eq!(history.len(), 1);
    assert!(history[0].first_connected_at.is_some());
    let other = context.fixture.session("user", context.client).await;
    let (guest, _) = context.fixture.guest().await;
    assert!(context
        .activity_store
        .channels_owned(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            Some(context.session.id),
            None,
            100
        )
        .await
        .unwrap()
        .is_empty());
    assert!(context
        .activity_store
        .visits_owned(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            None,
            100
        )
        .await
        .unwrap()
        .is_empty());
    assert!(context
        .activity_store
        .visits_owned(ResourceCredential::User(&other), context.client, None, 100)
        .await
        .unwrap()
        .is_empty());
    assert!(context
        .activity_store
        .channels_owned(
            ResourceCredential::User(&other),
            context.client,
            Some(context.session.id),
            None,
            100
        )
        .await
        .unwrap()
        .is_empty());
    assert!(context
        .activity_store
        .channels_owned(
            ResourceCredential::User(&login),
            ClientType::Panel,
            None,
            None,
            100
        )
        .await
        .is_err());
    let viewer = context
        .fixture
        .session("viewer", ClientType::AdminWeb)
        .await;
    assert!(context
        .activity_store
        .channels_managed(&viewer, Some(context.session.id), None, 100)
        .await
        .is_ok());
    assert!(context
        .activity_store
        .visits_managed(&login, None, 100)
        .await
        .is_err());
    context.close().await;
}
#[tokio::test]
async fn wrong_node_expired_lease_and_observer_file_channel_are_denied() {
    let context = Context::new(DeploymentTarget::Webview, true).await;
    let (_, key) = context.fixture.node().await;
    let other = context
        .fixture
        .nodes
        .open_connection(context.node.epoch(), &key, &token())
        .await
        .unwrap();
    context
        .fixture
        .nodes
        .report(&other, &node_report(1))
        .await
        .unwrap();
    assert!(context
        .activity_store
        .open_channel(&other, &context.open(ChannelKind::Media))
        .await
        .is_err());
    assert!(context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Rdp))
        .await
        .is_err());
    let row = context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Media))
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.resource_sessions SET descriptor_expires_at=clock_timestamp()-interval '1 second' WHERE id=$1").bind(context.session.id).execute(&context.fixture.owner).await.unwrap();
    assert!(context
        .activity_store
        .report_channel(&context.node, row.id, &progress(1, 1))
        .await
        .is_err());
    assert!(context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Media))
        .await
        .is_err());
    sqlx::query("UPDATE pixels.applications SET allow_observer=true WHERE id=$1")
        .bind(context.instance.application_id)
        .execute(&context.fixture.owner)
        .await
        .unwrap();
    let observer = context
        .session_store
        .open(
            ResourceCredential::User(&context.user),
            context.client,
            &OpenResourceSession {
                request_id: Uuid::new_v4(),
                target: context.session.target,
                access: SessionAccess::Observer,
            },
        )
        .await
        .unwrap();
    context.admit(&observer).await;
    let mut request = context.open(ChannelKind::File);
    request.session_id = observer.id;
    assert!(context
        .activity_store
        .open_channel(&context.node, &request)
        .await
        .is_err());
    request.kind = ChannelKind::Media;
    assert!(context
        .activity_store
        .open_channel(&context.node, &request)
        .await
        .is_ok());
    context.close().await;
}
#[tokio::test]
async fn rdp_channels_remain_one_frontend_and_never_fall_back_to_media_capture() {
    let context = Context::new(DeploymentTarget::Rdp, true).await;
    assert!(context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Media))
        .await
        .is_err());
    assert!(context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Audio))
        .await
        .is_err());
    assert!(context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::File))
        .await
        .is_err());
    context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Control))
        .await
        .unwrap();
    let rdp = context
        .activity_store
        .open_channel(&context.node, &context.open(ChannelKind::Rdp))
        .await
        .unwrap();
    let mut closed = progress(1, 100);
    closed.outcome = ChannelOutcome::Closed {
        reason: ChannelClose::PeerClosed,
    };
    context
        .activity_store
        .report_channel(&context.node, rdp.id, &closed)
        .await
        .unwrap();
    assert_eq!(context.visits().await[0].channel_count, 2);
    assert!(matches!(
        context
            .session_store
            .open(
                ResourceCredential::User(&context.user),
                context.client,
                &OpenResourceSession {
                    request_id: Uuid::new_v4(),
                    target: context.session.target,
                    access: SessionAccess::Controller
                }
            )
            .await,
        Err(StoreError::NoCapacity)
    ));
    context.close().await;
}
