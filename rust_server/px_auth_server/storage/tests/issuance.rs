use px_auth_store::{Activation, AuthError, IssueRequest, LicenseStore, LicenseTerms};
use px_license::{
    Distribution, Feature, LicenseSigner, LicenseVerifier, Mode, Product, VerifyContext,
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
    let f = Fixture::new("admin").await;
    let operator = f.store.operators();
    let salt = SaltString::encode_b64(&[37; 16]).unwrap();
    let hash = Argon2::default()
        .hash_password(b"synthetic password", &salt)
        .unwrap()
        .to_string();
    sqlx::query("UPDATE pixels.authors SET password_hash=$1 WHERE id=$2")
        .bind(&hash)
        .bind(f.author)
        .execute(&f.owner)
        .await
        .unwrap();
    let before = operator
        .credential(&f.author.to_string())
        .await
        .unwrap()
        .unwrap();
    assert_eq!(before.authorization_revision, 1);
    let token: [u8; 32] = Sha256::digest(Uuid::new_v4().as_bytes()).into();
    operator.issue_session(before.id, 1, &token).await.unwrap();
    assert_eq!(operator.authenticate(&token).await.unwrap().role, "admin");
    assert!(operator
        .set_password(
            &f.token,
            f.author,
            1,
            "$argon2id$v=19$m=19456,t=2,p=1$invalid"
        )
        .await
        .is_err());
    operator
        .set_password(&f.token, f.author, 1, &hash)
        .await
        .unwrap();
    assert!(operator.authenticate(&token).await.is_err());
    assert!(operator.authenticate(&f.token).await.is_err());
    let replacement: [u8; 32] = Sha256::digest(Uuid::new_v4().as_bytes()).into();
    assert!(operator
        .issue_session(before.id, 1, &replacement)
        .await
        .is_err());
    let next = operator
        .credential(&f.author.to_string())
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
    f.close().await;
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
    let f = Fixture::new("admin").await;
    let terms = f.terms().await;
    let request = Uuid::new_v4();
    let issued = f
        .store
        .issue(
            &f.token,
            request,
            IssueRequest::Create {
                terms: terms.clone(),
            },
        )
        .await
        .unwrap();
    let duplicate = f
        .store
        .issue(
            &f.token,
            request,
            IssueRequest::Create {
                terms: terms.clone(),
            },
        )
        .await
        .unwrap();
    assert_eq!(issued.wire, duplicate.wire);
    assert_eq!(issued.license_id, duplicate.license_id);
    let verifier = LicenseVerifier::new(signer().public_key().try_into().unwrap()).unwrap();
    let context = VerifyContext {
        deployment_id: terms.deployment_id,
        product: terms.product,
        distribution: terms.distribution,
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
        f.store.current(issued.license_id, 1).await.unwrap().wire,
        issued.wire
    );
    let count: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.license_issuances WHERE license_id=$1")
            .bind(issued.license_id)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(count, 1);
    let mut changed = terms;
    changed.max_devices += 1;
    assert!(matches!(
        f.store
            .issue(&f.token, request, IssueRequest::Create { terms: changed })
            .await,
        Err(AuthError::Conflict)
    ));
    f.close().await;
}

#[tokio::test]
async fn concurrent_same_request_has_one_signed_committed_result() {
    let f = Fixture::new("admin").await;
    let terms = f.terms().await;
    let request = Uuid::new_v4();
    let barrier = Arc::new(tokio::sync::Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let store = f.store.clone();
        let token = f.token;
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
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(count, 1);
    f.close().await;
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
    let f = Fixture::new("admin").await;
    let terms = f.terms().await;
    let original_request = Uuid::new_v4();
    let first = f
        .store
        .issue(
            &f.token,
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
        f.store
            .issue(
                &f.token,
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
    let (a, b) = tokio::join!(
        f.store.issue(&f.token, Uuid::new_v4(), request.clone()),
        f.store.issue(&f.token, Uuid::new_v4(), request)
    );
    assert_eq!(usize::from(a.is_ok()) + usize::from(b.is_ok()), 1);
    assert!(f.store.current(first.license_id, 1).await.is_err());
    assert!(f.store.current(first.license_id, 2).await.is_ok());
    assert_eq!(
        f.store.revoke(&f.token, first.license_id, 2).await.unwrap(),
        3
    );
    assert!(f.store.current(first.license_id, 2).await.is_err());
    assert!(f
        .store
        .issue(
            &f.token,
            original_request,
            IssueRequest::Create {
                terms: terms.clone()
            }
        )
        .await
        .is_err());
    assert!(f
        .store
        .issue(
            &f.token,
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
    .fetch_all(&f.owner)
    .await
    .unwrap();
    assert_eq!(revisions, vec![1, 2, 3]);
    f.close().await;
}

#[tokio::test]
async fn issuance_failure_rolls_back_license_request_and_audit() {
    let f = Fixture::new("admin").await;
    let terms = f.terms().await;
    let request = Uuid::new_v4();
    sqlx::query("REVOKE INSERT ON pixels.license_issuances FROM pixels_auth_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    let result = f
        .store
        .issue(
            &f.token,
            request,
            IssueRequest::Create {
                terms: terms.clone(),
            },
        )
        .await;
    sqlx::query("GRANT INSERT ON pixels.license_issuances TO pixels_auth_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(result.is_err());
    let licenses: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.licenses WHERE target_deployment=$1")
            .bind(terms.deployment_id)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    let requests: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.license_requests WHERE author_id=$1")
            .bind(f.author)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    let audits: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.license_audit WHERE author_id=$1")
            .bind(f.author)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!((licenses, requests, audits), (0, 0, 0));
    f.store
        .issue(&f.token, request, IssueRequest::Create { terms })
        .await
        .unwrap();
    f.close().await;
}

#[tokio::test]
async fn invalid_foreign_keys_limits_and_closed_database_never_succeed() {
    let f = Fixture::new("admin").await;
    let terms = f.terms().await;
    let mut invalid = terms.clone();
    invalid.customer_id = Uuid::new_v4();
    assert!(f
        .store
        .issue(
            &f.token,
            Uuid::new_v4(),
            IssueRequest::Create { terms: invalid }
        )
        .await
        .is_err());
    let mut invalid = terms.clone();
    invalid.max_sessions = 0;
    assert!(f
        .store
        .issue(
            &f.token,
            Uuid::new_v4(),
            IssueRequest::Create { terms: invalid }
        )
        .await
        .is_err());
    assert!(f
        .store
        .issue(
            &f.token,
            Uuid::nil(),
            IssueRequest::Create {
                terms: terms.clone()
            }
        )
        .await
        .is_err());
    let requests: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.license_requests WHERE author_id=$1")
            .bind(f.author)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(requests, 0);
    f.store.close().await;
    assert!(f
        .store
        .issue(&f.token, Uuid::new_v4(), IssueRequest::Create { terms })
        .await
        .is_err());
    f.close().await;
}
