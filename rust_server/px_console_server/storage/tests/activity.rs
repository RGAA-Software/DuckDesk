#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, node_report, request, token, Fixture};
use px_console_store::*;
use std::{env, sync::Arc, time::Duration};
use tokio::sync::Barrier;
use uuid::Uuid;
struct Context {
    f: Fixture,
    a: ActivityStore,
    s: ResourceSessionStore,
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
        let f = Fixture::new().await;
        let a = ActivityStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let s = ResourceSessionStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let (node, app, _) = f.prepared(target, 1).await;
        let user = f.session("user", client).await;
        let instance = f
            .instances
            .reserve(
                ResourceCredential::User(&user),
                client,
                node.epoch(),
                &request(app.id),
            )
            .await
            .unwrap();
        let cmd = f.instances.next_command(&node).await.unwrap().unwrap();
        let port = match cmd.action {
            NodeCommandAction::Start { port, .. } => port,
            _ => panic!("start required"),
        };
        f.instances
            .acknowledge_command(
                &node,
                &CommandReceipt {
                    command_id: cmd.id,
                    lease_id: cmd.lease_id,
                    instance_id: instance.id,
                    launch_id: cmd.launch_id,
                    instance_revision: cmd.instance_revision,
                    outcome: CommandOutcome::Running { port },
                },
            )
            .await
            .unwrap();
        let session = s
            .open(
                ResourceCredential::User(&user),
                client,
                &OpenResourceSession {
                    request_id: Uuid::new_v4(),
                    target: SessionTarget::CloudApplication {
                        application_id: app.id,
                        instance_id: instance.id,
                    },
                    access: SessionAccess::Controller,
                },
            )
            .await
            .unwrap();
        let c = Self {
            f,
            a,
            s,
            node,
            user,
            client,
            instance,
            session,
        };
        if admitted {
            c.admit(&c.session).await;
        }
        c
    }
    async fn admit(&self, session: &ResourceSession) {
        let secret = token();
        let d = self
            .s
            .descriptor(
                ResourceCredential::User(&self.user),
                self.client,
                session.id,
                session.revision,
                &secret,
            )
            .await
            .unwrap();
        self.s
            .admit_frontend(&self.node, session.id, d.session.revision, &secret)
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
        self.a
            .channels_managed(&self.f.admin, Some(self.session.id), None, 100)
            .await
            .unwrap()
    }
    async fn visits(&self) -> Vec<VisitRecord> {
        self.a
            .visits_owned(ResourceCredential::User(&self.user), self.client, None, 100)
            .await
            .unwrap()
    }
    async fn close(self) {
        self.a.close().await;
        self.s.close().await;
        self.f.close().await;
    }
}
fn progress(sequence: u64, bytes: u64) -> ChannelProgress {
    ChannelProgress {
        sequence,
        sent_bytes: bytes,
        received_bytes: bytes,
        elapsed_ms: bytes,
        outcome: ChannelOutcome::Progress,
    }
}
async fn permission(c: &Context, allow: bool) {
    sqlx::query(if allow {
        "GRANT INSERT ON pixels.connection_observation_events TO pixels_console_runtime"
    } else {
        "REVOKE INSERT ON pixels.connection_observation_events FROM pixels_console_runtime"
    })
    .execute(&c.f.owner)
    .await
    .unwrap();
}
#[tokio::test]
async fn visit_confirmation_and_multiple_channels_never_create_more_occupants_or_close_the_app() {
    let c = Context::new(DeploymentTarget::Webview, false).await;
    assert!(c.visits().await[0].first_connected_at.is_none());
    assert!(c
        .a
        .open_channel(&c.node, &c.open(ChannelKind::Media))
        .await
        .is_err());
    c.admit(&c.session).await;
    let initial = c.visits().await.remove(0);
    assert!(initial.first_connected_at.is_some());
    assert_eq!(initial.channel_count, 0);
    let first =
        c.a.open_channel(&c.node, &c.open(ChannelKind::Control))
            .await
            .unwrap();
    c.a.open_channel(&c.node, &c.open(ChannelKind::Audio))
        .await
        .unwrap();
    let mut closed = progress(1, 10);
    closed.outcome = ChannelOutcome::Closed {
        reason: ChannelClose::PeerClosed,
    };
    let done =
        c.a.report_channel(&c.node, first.id, &closed)
            .await
            .unwrap();
    assert!(done.ended_at.is_some());
    assert_eq!(
        c.a.report_channel(&c.node, first.id, &closed)
            .await
            .unwrap(),
        done
    );
    let visit = c.visits().await.remove(0);
    assert_eq!(visit.channel_count, 2);
    assert_eq!(visit.first_connected_at, initial.first_connected_at);
    assert_eq!(visit.session.state, "connected");
    assert!(visit.session.closed_at.is_none());
    let count: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.resource_sessions WHERE instance_id=$1")
            .bind(c.instance.id)
            .fetch_one(&c.f.owner)
            .await
            .unwrap();
    assert_eq!(count, 1);
    let state: String = sqlx::query_scalar("SELECT state FROM pixels.instances WHERE id=$1")
        .bind(c.instance.id)
        .fetch_one(&c.f.owner)
        .await
        .unwrap();
    assert_eq!(state, "running");
    c.close().await;
}
#[tokio::test]
async fn concurrent_duplicate_channels_and_ordered_reports_have_one_identity_and_exact_body_retry()
{
    let c = Context::new(DeploymentTarget::Webview, true).await;
    let req = c.open(ChannelKind::Media);
    let b = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (a, node, r, b) = (c.a.clone(), c.node.clone(), req.clone(), b.clone());
        tasks.push(tokio::spawn(async move {
            b.wait().await;
            a.open_channel(&node, &r).await.unwrap()
        }));
    }
    let mut ids = Vec::new();
    for t in tasks {
        ids.push(t.await.unwrap().id);
    }
    assert!(ids.iter().all(|id| *id == ids[0]));
    let id = ids[0];
    let mut changed = req.clone();
    changed.kind = ChannelKind::Control;
    assert!(c.a.open_channel(&c.node, &changed).await.is_err());
    let b = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for i in 0..20 {
        let (a, node, b) = (c.a.clone(), c.node.clone(), b.clone());
        tasks.push(tokio::spawn(async move {
            b.wait().await;
            a.report_channel(&node, id, &progress(1, 10 + i % 2)).await
        }));
    }
    let mut accepted = Vec::new();
    let mut denied = 0;
    for t in tasks {
        match t.await.unwrap() {
            Ok(v) => accepted.push(v),
            Err(_) => denied += 1,
        }
    }
    assert_eq!(accepted.len(), 10);
    assert_eq!(denied, 10);
    assert!(accepted.iter().all(|r| *r == accepted[0]));
    assert!(c
        .a
        .report_channel(&c.node, id, &progress(2, 1))
        .await
        .is_err());
    let mut fail = progress(2, 20);
    fail.outcome = ChannelOutcome::Failed {
        reason: ChannelFailure::TransportLost,
    };
    let failed = c.a.report_channel(&c.node, id, &fail).await.unwrap();
    assert_eq!(failed.state, "failed");
    assert!(c
        .a
        .report_channel(&c.node, id, &progress(3, 30))
        .await
        .is_err());
    c.close().await;
}
#[tokio::test]
async fn channel_limit_is_atomic_under_twenty_contenders_and_pages_do_not_leak_producer_secrets() {
    let c = Context::new(DeploymentTarget::Webview, true).await;
    for _ in 0..15 {
        c.a.open_channel(&c.node, &c.open(ChannelKind::Media))
            .await
            .unwrap();
    }
    let b = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (a, node, r, b) = (
            c.a.clone(),
            c.node.clone(),
            c.open(ChannelKind::Media),
            b.clone(),
        );
        tasks.push(tokio::spawn(async move {
            b.wait().await;
            a.open_channel(&node, &r).await
        }));
    }
    let mut won = 0;
    for t in tasks {
        if t.await.unwrap().is_ok() {
            won += 1;
        }
    }
    assert_eq!(won, 1);
    let mut after = None;
    let mut ids = Vec::new();
    loop {
        let rows =
            c.a.channels_managed(&c.f.admin, Some(c.session.id), after, 3)
                .await
                .unwrap();
        if rows.is_empty() {
            break;
        }
        after = Some(rows.last().unwrap().id);
        ids.extend(rows.into_iter().map(|r| r.id));
    }
    assert_eq!(ids.len(), 16);
    let mut unique = ids.clone();
    unique.sort();
    unique.dedup();
    assert_eq!(unique.len(), 16);
    let body = serde_json::to_string(&c.channels().await).unwrap();
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
    assert!(c
        .a
        .channels_managed(&c.f.admin, None, None, 101)
        .await
        .is_err());
    assert!(c.a.visits_managed(&c.f.admin, None, 0).await.is_err());
    c.close().await;
}
#[tokio::test]
async fn node_rotation_preserves_unknown_history_and_never_adopts_old_source_identity() {
    let c = Context::new(DeploymentTarget::Webview, true).await;
    let req = c.open(ChannelKind::Media);
    let row = c.a.open_channel(&c.node, &req).await.unwrap();
    c.a.report_channel(&c.node, row.id, &progress(1, 20))
        .await
        .unwrap();
    let key = token();
    c.f.nodes
        .rotate_key(&c.f.admin, c.node.id(), 2, &key)
        .await
        .unwrap();
    let current =
        c.f.nodes
            .open_connection(c.node.epoch(), &key, &token())
            .await
            .unwrap();
    c.f.nodes.report(&current, &node_report(1)).await.unwrap();
    let unknown = c.channels().await.remove(0);
    assert_eq!(unknown.state, "unknown");
    assert!(unknown.ended_at.is_none());
    assert_eq!(unknown.sent_bytes, 20);
    assert!(c.a.open_channel(&current, &req).await.is_err());
    assert!(c
        .a
        .report_channel(&current, row.id, &progress(2, 30))
        .await
        .is_err());
    assert!(c
        .a
        .report_channel(&c.node, row.id, &progress(2, 30))
        .await
        .is_err());
    assert!(c.visits().await[0].session.closed_at.is_none());
    c.close().await;
}
#[tokio::test]
async fn audit_failures_roll_back_channel_mutations_node_invalidation_and_frontend_retirement() {
    let c = Context::new(DeploymentTarget::Webview, true).await;
    permission(&c, false).await;
    let failed = c.a.open_channel(&c.node, &c.open(ChannelKind::Media)).await;
    permission(&c, true).await;
    assert!(failed.is_err());
    assert!(c.channels().await.is_empty());
    let row =
        c.a.open_channel(&c.node, &c.open(ChannelKind::Media))
            .await
            .unwrap();
    permission(&c, false).await;
    let failed = c.a.report_channel(&c.node, row.id, &progress(1, 1)).await;
    permission(&c, true).await;
    assert!(failed.is_err());
    assert_eq!(c.channels().await[0].revision, 1);
    permission(&c, false).await;
    let failed = c.f.nodes.close_connection(&c.node).await;
    permission(&c, true).await;
    assert!(failed.is_err());
    assert_eq!(c.channels().await[0].state, "active");
    let challenge = c.s.begin_retirement(&c.node, c.session.id).await.unwrap();
    permission(&c, false).await;
    let failed =
        c.s.finish_retirement(&c.node, c.session.id, challenge.challenge_id)
            .await;
    permission(&c, true).await;
    assert!(failed.is_err());
    assert_eq!(c.channels().await[0].state, "active");
    assert_eq!(c.visits().await[0].session.state, "closing");
    c.s.finish_retirement(&c.node, c.session.id, challenge.challenge_id)
        .await
        .unwrap();
    c.s.finish_retirement(&c.node, c.session.id, challenge.challenge_id)
        .await
        .unwrap();
    let result = c.channels().await.remove(0);
    assert_eq!(result.state, "unknown");
    assert_eq!(result.revision, 2);
    assert!(result.ended_at.is_none());
    assert!(c.visits().await[0].session.closed_at.is_some());
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
    c.close().await;
}
#[tokio::test]
async fn revoked_origin_denies_progress_but_same_producer_can_close_and_fresh_login_can_read_history(
) {
    let c = Context::new(DeploymentTarget::Webview, true).await;
    let row =
        c.a.open_channel(&c.node, &c.open(ChannelKind::Media))
            .await
            .unwrap();
    let identity = c.f.identity.authenticate(&c.user, c.client).await.unwrap();
    c.f.identity
        .revoke_session(identity.user_id, identity.session_id)
        .await
        .unwrap();
    assert!(c
        .a
        .report_channel(&c.node, row.id, &progress(1, 1))
        .await
        .is_err());
    assert!(c
        .a
        .open_channel(&c.node, &c.open(ChannelKind::Audio))
        .await
        .is_err());
    let mut closed = progress(1, 1);
    closed.outcome = ChannelOutcome::Closed {
        reason: ChannelClose::UserStopped,
    };
    c.a.report_channel(&c.node, row.id, &closed).await.unwrap();
    let login = token();
    c.f.identity
        .issue_session(
            identity.user_id,
            1,
            &login,
            c.client,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    let history =
        c.a.visits_owned(ResourceCredential::User(&login), c.client, None, 100)
            .await
            .unwrap();
    assert_eq!(history.len(), 1);
    assert!(history[0].first_connected_at.is_some());
    let other = c.f.session("user", c.client).await;
    let (guest, _) = c.f.guest().await;
    assert!(c
        .a
        .channels_owned(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            Some(c.session.id),
            None,
            100
        )
        .await
        .unwrap()
        .is_empty());
    assert!(c
        .a
        .visits_owned(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            None,
            100
        )
        .await
        .unwrap()
        .is_empty());
    assert!(c
        .a
        .visits_owned(ResourceCredential::User(&other), c.client, None, 100)
        .await
        .unwrap()
        .is_empty());
    assert!(c
        .a
        .channels_owned(
            ResourceCredential::User(&other),
            c.client,
            Some(c.session.id),
            None,
            100
        )
        .await
        .unwrap()
        .is_empty());
    assert!(c
        .a
        .channels_owned(
            ResourceCredential::User(&login),
            ClientType::Panel,
            None,
            None,
            100
        )
        .await
        .is_err());
    let viewer = c.f.session("viewer", ClientType::AdminWeb).await;
    assert!(c
        .a
        .channels_managed(&viewer, Some(c.session.id), None, 100)
        .await
        .is_ok());
    assert!(c.a.visits_managed(&login, None, 100).await.is_err());
    c.close().await;
}
#[tokio::test]
async fn wrong_node_expired_lease_and_observer_file_channel_are_denied() {
    let c = Context::new(DeploymentTarget::Webview, true).await;
    let (_, key) = c.f.node().await;
    let other =
        c.f.nodes
            .open_connection(c.node.epoch(), &key, &token())
            .await
            .unwrap();
    c.f.nodes.report(&other, &node_report(1)).await.unwrap();
    assert!(c
        .a
        .open_channel(&other, &c.open(ChannelKind::Media))
        .await
        .is_err());
    assert!(c
        .a
        .open_channel(&c.node, &c.open(ChannelKind::Rdp))
        .await
        .is_err());
    let row =
        c.a.open_channel(&c.node, &c.open(ChannelKind::Media))
            .await
            .unwrap();
    sqlx::query("UPDATE pixels.resource_sessions SET descriptor_expires_at=clock_timestamp()-interval '1 second' WHERE id=$1").bind(c.session.id).execute(&c.f.owner).await.unwrap();
    assert!(c
        .a
        .report_channel(&c.node, row.id, &progress(1, 1))
        .await
        .is_err());
    assert!(c
        .a
        .open_channel(&c.node, &c.open(ChannelKind::Media))
        .await
        .is_err());
    sqlx::query("UPDATE pixels.applications SET allow_observer=true WHERE id=$1")
        .bind(c.instance.application_id)
        .execute(&c.f.owner)
        .await
        .unwrap();
    let observer =
        c.s.open(
            ResourceCredential::User(&c.user),
            c.client,
            &OpenResourceSession {
                request_id: Uuid::new_v4(),
                target: c.session.target,
                access: SessionAccess::Observer,
            },
        )
        .await
        .unwrap();
    c.admit(&observer).await;
    let mut request = c.open(ChannelKind::File);
    request.session_id = observer.id;
    assert!(c.a.open_channel(&c.node, &request).await.is_err());
    request.kind = ChannelKind::Media;
    assert!(c.a.open_channel(&c.node, &request).await.is_ok());
    c.close().await;
}
#[tokio::test]
async fn rdp_channels_remain_one_frontend_and_never_fall_back_to_media_capture() {
    let c = Context::new(DeploymentTarget::Rdp, true).await;
    assert!(c
        .a
        .open_channel(&c.node, &c.open(ChannelKind::Media))
        .await
        .is_err());
    assert!(c
        .a
        .open_channel(&c.node, &c.open(ChannelKind::Audio))
        .await
        .is_err());
    assert!(c
        .a
        .open_channel(&c.node, &c.open(ChannelKind::File))
        .await
        .is_err());
    c.a.open_channel(&c.node, &c.open(ChannelKind::Control))
        .await
        .unwrap();
    let rdp =
        c.a.open_channel(&c.node, &c.open(ChannelKind::Rdp))
            .await
            .unwrap();
    let mut closed = progress(1, 100);
    closed.outcome = ChannelOutcome::Closed {
        reason: ChannelClose::PeerClosed,
    };
    c.a.report_channel(&c.node, rdp.id, &closed).await.unwrap();
    assert_eq!(c.visits().await[0].channel_count, 2);
    assert!(matches!(
        c.s.open(
            ResourceCredential::User(&c.user),
            c.client,
            &OpenResourceSession {
                request_id: Uuid::new_v4(),
                target: c.session.target,
                access: SessionAccess::Controller
            }
        )
        .await,
        Err(StoreError::NoCapacity)
    ));
    c.close().await;
}
