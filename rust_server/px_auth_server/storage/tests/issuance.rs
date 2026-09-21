use px_auth_store::{
    Activation, AuthError, IssueRequest, LicenseStore, LicenseTerms, NotificationFailure,
};
use px_license::{
    Distribution, Feature, LicenseSigner, LicenseVerifierSet, Mode, Product, VerifyContext,
};
use px_pg::{DatabaseConfig, Transport};
use sha2::{Digest, Sha256};
use std::{env, sync::Arc};
use uuid::Uuid;

fn signer() -> Arc<LicenseSigner> {
    // RFC 8032 public test key, PKCS#8 v2.
    Arc::new(LicenseSigner::from_pkcs8(&hex::decode(
        "3053020101300506032b6570042204209d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60a123032100d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
    ).unwrap()).unwrap())
}

#[tokio::test]
async fn operator_password_change_invalidates_sessions_and_inflight_verification() {
    use argon2::{
        password_hash::{PasswordHasher, SaltString},
        Argon2,
    };
    let fixture = Fixture::new("admin").await;
    let operator = fixture.store.operators();
    let salt = SaltString::encode_b64(&[37; 16]).unwrap();
    let hash = Argon2::default()
        .hash_password(b"synthetic password", &salt)
        .unwrap()
        .to_string();
    sqlx::query("UPDATE pixels.authors SET password_hash=$1 WHERE id=$2")
        .bind(&hash)
        .bind(fixture.author)
        .execute(&fixture.owner)
        .await
        .unwrap();
    let before = operator
        .credential(&fixture.author.to_string())
        .await
        .unwrap()
        .unwrap();
    assert_eq!(before.authorization_revision, 1);
    let token: [u8; 32] = Sha256::digest(Uuid::new_v4().as_bytes()).into();
    operator.issue_session(before.id, 1, &token).await.unwrap();
    assert_eq!(operator.authenticate(&token).await.unwrap().role, "admin");
    assert!(operator
        .set_password(
            &fixture.token,
            fixture.author,
            1,
            "$argon2id$v=19$m=19456,t=2,p=1$invalid"
        )
        .await
        .is_err());
    operator
        .set_password(&fixture.token, fixture.author, 1, &hash)
        .await
        .unwrap();
    assert!(operator.authenticate(&token).await.is_err());
    assert!(operator.authenticate(&fixture.token).await.is_err());
    let replacement: [u8; 32] = Sha256::digest(Uuid::new_v4().as_bytes()).into();
    assert!(operator
        .issue_session(before.id, 1, &replacement)
        .await
        .is_err());
    let next = operator
        .credential(&fixture.author.to_string())
        .await
        .unwrap()
        .unwrap();
    operator
        .issue_session(next.id, next.authorization_revision, &replacement)
        .await
        .unwrap();
    operator.revoke_session(&replacement).await.unwrap();
    operator.revoke_session(&replacement).await.unwrap();
    assert!(operator.authenticate(&replacement).await.is_err());
    fixture.close().await;
}
struct Fixture {
    store: LicenseStore,
    owner: sqlx::PgPool,
    token: [u8; 32],
    author: Uuid,
}
impl Fixture {
    async fn new(role: &str) -> Self {
        assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
        let config = DatabaseConfig::parse(
            &env::var("PIXELS_TEST_AUTH_RUNTIME_URL").unwrap(),
            Transport::LocalDevelopment,
        )
        .unwrap();
        let owner = DatabaseConfig::parse(
            &env::var("PIXELS_TEST_AUTH_OWNER_URL").unwrap(),
            Transport::LocalDevelopment,
        )
        .unwrap()
        .connect()
        .await
        .unwrap();
        let store = LicenseStore::connect(
            &config,
            env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
            signer(),
        )
        .await
        .unwrap();
        let author = Uuid::new_v4();
        let token: [u8; 32] = Sha256::digest(Uuid::new_v4().as_bytes()).into();
        let password = px_credentials::hash("synthetic fixture password").unwrap();
        sqlx::query("INSERT INTO pixels.authors(id,username_normalized,password_hash,role) VALUES($1,$2,$3,$4)")
            .bind(author).bind(author.to_string()).bind(password.as_str()).bind(role).execute(&owner).await.unwrap();
        sqlx::query("INSERT INTO pixels.author_sessions(id,author_id,token_hash,authorization_revision,expires_at) VALUES($1,$2,$3,1,clock_timestamp()+interval '1 hour')")
            .bind(Uuid::new_v4()).bind(author).bind(token.as_slice()).execute(&owner).await.unwrap();
        Self {
            store,
            owner,
            token,
            author,
        }
    }
    async fn terms(&self) -> LicenseTerms {
        let customer = self
            .store
            .create_customer(&self.token, &Uuid::new_v4().to_string(), "synthetic")
            .await
            .unwrap();
        LicenseTerms {
            customer_id: customer.id,
            deployment_id: Uuid::new_v4(),
            product: Product::PixelsConsole,
            distribution: Distribution::Customer,
            release_namespace: "pixels.customer".into(),
            oem_id: None,
            machine_sha256: "a".repeat(64),
            mode: Mode::Licensed,
            activation: Activation::Immediately,
            expires_at: chrono::Utc::now().timestamp() + 86400,
            max_devices: 4,
            max_sessions: 8,
            features: vec![Feature::CloudApplications, Feature::Desktop, Feature::Rdp],
        }
    }
    async fn close(self) {
        self.store.close().await;
        self.owner.close().await;
    }
}

#[tokio::test]
async fn committed_issuance_verifies_and_identical_retry_returns_exact_wire() {
    let fixture = Fixture::new("admin").await;
    let terms = fixture.terms().await;
    let request = Uuid::new_v4();
    let issued = fixture
        .store
        .issue(
            &fixture.token,
            request,
            IssueRequest::Create {
                terms: terms.clone(),
            },
        )
        .await
        .unwrap();
    let duplicate = fixture
        .store
        .issue(
            &fixture.token,
            request,
            IssueRequest::Create {
                terms: terms.clone(),
            },
        )
        .await
        .unwrap();
    assert_eq!(issued.wire, duplicate.wire);
    assert_eq!(issued.license_id, duplicate.license_id);
    let verifier = LicenseVerifierSet::new([signer().public_key().try_into().unwrap()]).unwrap();
    let context = VerifyContext {
        deployment_id: terms.deployment_id,
        product: terms.product,
        distribution: terms.distribution,
        release_namespace: &terms.release_namespace,
        oem_id: terms.oem_id.as_deref(),
        machine_sha256: &terms.machine_sha256,
        now: chrono::Utc::now().timestamp(),
        minimum_revision: 1,
        last_trusted_time: 0,
    };
    assert_eq!(
        verifier.verify(&issued.wire, &context).unwrap().license_id,
        issued.license_id
    );
    assert_eq!(
        fixture
            .store
            .current(issued.license_id, 1)
            .await
            .unwrap()
            .wire,
        issued.wire
    );
    let count: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.license_issuances WHERE license_id=$1")
            .bind(issued.license_id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(count, 1);
    let mut changed = terms;
    changed.max_devices += 1;
    assert!(matches!(
        fixture
            .store
            .issue(
                &fixture.token,
                request,
                IssueRequest::Create { terms: changed }
            )
            .await,
        Err(AuthError::Conflict)
    ));
    fixture.close().await;
}

#[tokio::test]
async fn concurrent_same_request_has_one_signed_committed_result() {
    let fixture = Fixture::new("admin").await;
    let terms = fixture.terms().await;
    let request = Uuid::new_v4();
    let barrier = Arc::new(tokio::sync::Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let store = fixture.store.clone();
        let token = fixture.token;
        let terms = terms.clone();
        let barrier = barrier.clone();
        tasks.push(tokio::spawn(async move {
            barrier.wait().await;
            store
                .issue(&token, request, IssueRequest::Create { terms })
                .await
                .unwrap()
        }));
    }
    let mut wires = std::collections::BTreeSet::new();
    for task in tasks {
        wires.insert(task.await.unwrap().wire);
    }
    assert_eq!(wires.len(), 1);
    let count: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.licenses WHERE target_deployment=$1")
            .bind(terms.deployment_id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(count, 1);
    fixture.close().await;
}

#[tokio::test]
async fn visitor_revoked_expired_and_stale_author_sessions_cannot_write() {
    let admin = Fixture::new("admin").await;
    let terms = admin.terms().await;
    let visitor = Fixture::new("visitor").await;
    assert!(visitor
        .store
        .list_customers(&visitor.token, None, 100)
        .await
        .is_ok());
    assert!(matches!(
        visitor
            .store
            .create_customer(&visitor.token, "forbidden", "")
            .await,
        Err(AuthError::Rejected)
    ));
    assert!(matches!(
        visitor
            .store
            .issue(
                &visitor.token,
                Uuid::new_v4(),
                IssueRequest::Create {
                    terms: terms.clone()
                }
            )
            .await,
        Err(AuthError::Rejected)
    ));
    for sql in [
        "UPDATE pixels.author_sessions SET revoked_at=clock_timestamp() WHERE author_id=$1",
        "UPDATE pixels.author_sessions SET revoked_at=NULL,created_at=clock_timestamp()-interval '2 hours',expires_at=clock_timestamp()-interval '1 hour' WHERE author_id=$1",
        "UPDATE pixels.author_sessions SET created_at=clock_timestamp(),expires_at=clock_timestamp()+interval '1 hour',authorization_revision=99 WHERE author_id=$1",
    ] {
        sqlx::query(sql).bind(admin.author).execute(&admin.owner).await.unwrap();
        assert!(matches!(admin.store.issue(&admin.token,Uuid::new_v4(),IssueRequest::Create {terms:terms.clone()}).await,Err(AuthError::Rejected)));
    }
    let count: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.license_requests WHERE author_id=ANY($1)")
            .bind(vec![admin.author, visitor.author])
            .fetch_one(&admin.owner)
            .await
            .unwrap();
    assert_eq!(count, 0);
    visitor.close().await;
    admin.close().await;
}

#[tokio::test]
async fn renewal_cas_preserves_identity_and_revocation_never_reissues() {
    let fixture = Fixture::new("admin").await;
    let terms = fixture.terms().await;
    let original_request = Uuid::new_v4();
    let first = fixture
        .store
        .issue(
            &fixture.token,
            original_request,
            IssueRequest::Create {
                terms: terms.clone(),
            },
        )
        .await
        .unwrap();
    let mut changed = terms.clone();
    changed.deployment_id = Uuid::new_v4();
    assert!(matches!(
        fixture
            .store
            .issue(
                &fixture.token,
                Uuid::new_v4(),
                IssueRequest::Renew {
                    license_id: first.license_id,
                    expected_revision: 1,
                    terms: changed
                }
            )
            .await,
        Err(AuthError::Conflict)
    ));
    let request = IssueRequest::Renew {
        license_id: first.license_id,
        expected_revision: 1,
        terms: terms.clone(),
    };
    let (first_renewal, second_renewal) = tokio::join!(
        fixture
            .store
            .issue(&fixture.token, Uuid::new_v4(), request.clone()),
        fixture.store.issue(&fixture.token, Uuid::new_v4(), request)
    );
    assert_eq!(
        usize::from(first_renewal.is_ok()) + usize::from(second_renewal.is_ok()),
        1
    );
    assert!(fixture.store.current(first.license_id, 1).await.is_err());
    assert!(fixture.store.current(first.license_id, 2).await.is_ok());
    assert_eq!(
        fixture
            .store
            .revoke(&fixture.token, first.license_id, 2)
            .await
            .unwrap(),
        3
    );
    assert!(fixture.store.current(first.license_id, 2).await.is_err());
    assert!(fixture
        .store
        .issue(
            &fixture.token,
            original_request,
            IssueRequest::Create {
                terms: terms.clone()
            }
        )
        .await
        .is_err());
    assert!(fixture
        .store
        .issue(
            &fixture.token,
            Uuid::new_v4(),
            IssueRequest::Renew {
                license_id: first.license_id,
                expected_revision: 3,
                terms
            }
        )
        .await
        .is_err());
    let revisions: Vec<i64> = sqlx::query_scalar(
        "SELECT revision FROM pixels.license_audit WHERE license_id=$1 ORDER BY revision",
    )
    .bind(first.license_id)
    .fetch_all(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(revisions, vec![1, 2, 3]);
    fixture.close().await;
}

#[tokio::test]
async fn notification_outbox_orders_revisions_and_rejects_stale_delivery_acks() {
    let fixture = Fixture::new("admin").await;
    sqlx::query(
        "UPDATE pixels.license_notification_outbox SET delivered_at=clock_timestamp(),lease_id=NULL,lease_until=NULL WHERE delivered_at IS NULL",
    )
    .execute(&fixture.owner)
    .await
    .unwrap();
    let terms = fixture.terms().await;
    let created = fixture
        .store
        .issue(
            &fixture.token,
            Uuid::new_v4(),
            IssueRequest::Create {
                terms: terms.clone(),
            },
        )
        .await
        .unwrap();
    let renewed = fixture
        .store
        .issue(
            &fixture.token,
            Uuid::new_v4(),
            IssueRequest::Renew {
                license_id: created.license_id,
                expected_revision: 1,
                terms: terms.clone(),
            },
        )
        .await
        .unwrap();
    fixture
        .store
        .revoke(&fixture.token, created.license_id, 2)
        .await
        .unwrap();

    assert!(matches!(
        fixture.store.claim_notifications(0).await,
        Err(AuthError::Invalid)
    ));
    let (first_claim, competing_claim) = tokio::join!(
        fixture.store.claim_notifications(1),
        fixture.store.claim_notifications(1)
    );
    let mut claimed = first_claim.unwrap();
    let mut competing = competing_claim.unwrap();
    claimed.append(&mut competing);
    assert_eq!(claimed.len(), 1);
    let original = claimed.remove(0);
    assert_eq!(original.license_id, created.license_id);
    assert_eq!(original.revision, 1);
    assert_eq!(original.action, "issued");
    assert_eq!(original.deployment_id, terms.deployment_id);
    assert_eq!(original.product, "pixels_console");
    assert_eq!(original.distribution, "customer");
    assert_eq!(original.machine_sha256, terms.machine_sha256);
    assert_eq!(original.wire.as_deref(), Some(created.wire.as_str()));
    assert_eq!(original.attempts, 1);
    assert!(fixture
        .store
        .claim_notifications(100)
        .await
        .unwrap()
        .is_empty());

    sqlx::query(
        "UPDATE pixels.license_notification_outbox SET lease_until=clock_timestamp()-interval '1 second' WHERE id=$1",
    )
    .bind(original.id)
    .execute(&fixture.owner)
    .await
    .unwrap();
    let reclaimed = fixture
        .store
        .claim_notifications(1)
        .await
        .unwrap()
        .remove(0);
    assert_eq!(reclaimed.id, original.id);
    assert_ne!(reclaimed.lease_id, original.lease_id);
    assert_eq!(reclaimed.attempts, 2);
    assert!(fixture
        .store
        .complete_notification(original.id, original.lease_id)
        .await
        .is_err());
    fixture
        .store
        .complete_notification(reclaimed.id, reclaimed.lease_id)
        .await
        .unwrap();

    let renewal = fixture
        .store
        .claim_notifications(1)
        .await
        .unwrap()
        .remove(0);
    assert_eq!(renewal.revision, 2);
    assert_eq!(renewal.action, "renewed");
    assert_eq!(renewal.wire.as_deref(), Some(renewed.wire.as_str()));
    assert!(fixture
        .store
        .retry_notification(
            renewal.id,
            renewal.lease_id,
            0,
            NotificationFailure::Unavailable
        )
        .await
        .is_err());
    fixture
        .store
        .retry_notification(
            renewal.id,
            renewal.lease_id,
            1,
            NotificationFailure::Rejected,
        )
        .await
        .unwrap();
    assert!(fixture
        .store
        .claim_notifications(1)
        .await
        .unwrap()
        .is_empty());
    sqlx::query(
        "UPDATE pixels.license_notification_outbox SET available_at=clock_timestamp()-interval '1 second' WHERE id=$1",
    )
    .bind(renewal.id)
    .execute(&fixture.owner)
    .await
    .unwrap();
    let renewal_retry = fixture
        .store
        .claim_notifications(1)
        .await
        .unwrap()
        .remove(0);
    assert_eq!(renewal_retry.attempts, 2);
    fixture
        .store
        .complete_notification(renewal_retry.id, renewal_retry.lease_id)
        .await
        .unwrap();

    let revocation = fixture
        .store
        .claim_notifications(1)
        .await
        .unwrap()
        .remove(0);
    assert_eq!(revocation.revision, 3);
    assert_eq!(revocation.action, "revoked");
    assert!(revocation.wire.is_none());
    fixture
        .store
        .complete_notification(revocation.id, revocation.lease_id)
        .await
        .unwrap();
    assert!(fixture
        .store
        .claim_notifications(1)
        .await
        .unwrap()
        .is_empty());
    fixture.close().await;
}

#[tokio::test]
async fn notification_failure_rolls_back_license_request_and_audit() {
    let fixture = Fixture::new("admin").await;
    let terms = fixture.terms().await;
    let request = Uuid::new_v4();
    let notifications_before: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.license_notification_outbox")
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    sqlx::query("REVOKE INSERT ON pixels.license_notification_outbox FROM pixels_auth_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let result = fixture
        .store
        .issue(
            &fixture.token,
            request,
            IssueRequest::Create {
                terms: terms.clone(),
            },
        )
        .await;
    sqlx::query("GRANT INSERT ON pixels.license_notification_outbox TO pixels_auth_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(result.is_err());
    let state: (i64, i64, i64, i64) = sqlx::query_as(
        "SELECT (SELECT count(*) FROM pixels.licenses WHERE target_deployment=$1), \
                (SELECT count(*) FROM pixels.license_requests WHERE author_id=$2), \
                (SELECT count(*) FROM pixels.license_audit WHERE author_id=$2), \
                (SELECT count(*) FROM pixels.license_notification_outbox)",
    )
    .bind(terms.deployment_id)
    .bind(fixture.author)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(state, (0, 0, 0, notifications_before));
    fixture
        .store
        .issue(&fixture.token, request, IssueRequest::Create { terms })
        .await
        .unwrap();
    fixture.close().await;
}

#[tokio::test]
async fn issuance_failure_rolls_back_license_request_and_audit() {
    let fixture = Fixture::new("admin").await;
    let terms = fixture.terms().await;
    let request = Uuid::new_v4();
    sqlx::query("REVOKE INSERT ON pixels.license_issuances FROM pixels_auth_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let result = fixture
        .store
        .issue(
            &fixture.token,
            request,
            IssueRequest::Create {
                terms: terms.clone(),
            },
        )
        .await;
    sqlx::query("GRANT INSERT ON pixels.license_issuances TO pixels_auth_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(result.is_err());
    let licenses: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.licenses WHERE target_deployment=$1")
            .bind(terms.deployment_id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    let requests: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.license_requests WHERE author_id=$1")
            .bind(fixture.author)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    let audits: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.license_audit WHERE author_id=$1")
            .bind(fixture.author)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!((licenses, requests, audits), (0, 0, 0));
    fixture
        .store
        .issue(&fixture.token, request, IssueRequest::Create { terms })
        .await
        .unwrap();
    fixture.close().await;
}

#[tokio::test]
async fn invalid_foreign_keys_limits_and_closed_database_never_succeed() {
    let fixture = Fixture::new("admin").await;
    let terms = fixture.terms().await;
    let mut invalid = terms.clone();
    invalid.customer_id = Uuid::new_v4();
    assert!(fixture
        .store
        .issue(
            &fixture.token,
            Uuid::new_v4(),
            IssueRequest::Create { terms: invalid }
        )
        .await
        .is_err());
    let mut invalid = terms.clone();
    invalid.max_sessions = 0;
    assert!(fixture
        .store
        .issue(
            &fixture.token,
            Uuid::new_v4(),
            IssueRequest::Create { terms: invalid }
        )
        .await
        .is_err());
    assert!(fixture
        .store
        .issue(
            &fixture.token,
            Uuid::nil(),
            IssueRequest::Create {
                terms: terms.clone()
            }
        )
        .await
        .is_err());
    let requests: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.license_requests WHERE author_id=$1")
            .bind(fixture.author)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(requests, 0);
    fixture.store.close().await;
    assert!(fixture
        .store
        .issue(
            &fixture.token,
            Uuid::new_v4(),
            IssueRequest::Create { terms }
        )
        .await
        .is_err());
    fixture.close().await;
}
