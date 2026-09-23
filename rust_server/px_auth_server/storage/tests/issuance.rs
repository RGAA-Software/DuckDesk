use px_auth_store::{AuthError, IssueRequest, LicenseStore, LicenseTerms};
use px_license::{LicenseSigner, LicenseVerifierSet, LicensedService, VerifyContext};
use px_pg::{DatabaseConfig, Transport};
use sha2::{Digest, Sha256};
use std::{env, sync::Arc};
use uuid::Uuid;

fn signer() -> Arc<LicenseSigner> {
    Arc::new(LicenseSigner::from_pkcs8(&hex::decode(
        "3053020101300506032b6570042204209d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60a123032100d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
    ).unwrap()).unwrap())
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
        let runtime_configuration = DatabaseConfig::parse(
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
            &runtime_configuration,
            env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
            signer(),
        )
        .await
        .unwrap();
        let author = Uuid::new_v4();
        let token: [u8; 32] = Sha256::digest(Uuid::new_v4().as_bytes()).into();
        let password = px_credentials::hash("synthetic fixture password").unwrap();
        sqlx::query("INSERT INTO pixels.authors(id,username_normalized,password_hash,role) VALUES($1,$2,$3,$4)")
            .bind(author)
            .bind(author.to_string())
            .bind(password.as_str())
            .bind(role)
            .execute(&owner)
            .await
            .unwrap();
        sqlx::query("INSERT INTO pixels.author_sessions(id,author_id,token_hash,authorization_revision,expires_at) VALUES($1,$2,$3,1,clock_timestamp()+interval '1 hour')")
            .bind(Uuid::new_v4())
            .bind(author)
            .bind(token.as_slice())
            .execute(&owner)
            .await
            .unwrap();
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
            expires_at: chrono::Utc::now().timestamp() + 86400,
            max_streams: 8,
            services: vec![
                LicensedService::CloudApplications,
                LicensedService::Desktop,
                LicensedService::Rdp,
            ],
        }
    }

    async fn close(self) {
        self.store.close().await;
        self.owner.close().await;
    }
}

#[tokio::test]
async fn committed_issuance_is_signed_and_identical_retry_returns_exact_wire() {
    let fixture = Fixture::new("admin").await;
    let terms = fixture.terms().await;
    let request_id = Uuid::new_v4();
    let request = IssueRequest::Create {
        terms: terms.clone(),
    };
    let issued = fixture
        .store
        .issue(&fixture.token, request_id, request.clone())
        .await
        .unwrap();
    let duplicate = fixture
        .store
        .issue(&fixture.token, request_id, request)
        .await
        .unwrap();
    assert_eq!(issued.wire, duplicate.wire);
    assert_eq!(issued.license_id, duplicate.license_id);
    let verifier = LicenseVerifierSet::new([signer().public_key().try_into().unwrap()]).unwrap();
    let payload = verifier
        .verify(
            &issued.wire,
            &VerifyContext::new(terms.deployment_id, chrono::Utc::now().timestamp()),
        )
        .unwrap();
    assert_eq!(payload.max_streams, 8);
    assert_eq!(payload.services, terms.services);
    fixture.close().await;
}

#[tokio::test]
async fn concurrent_same_request_has_one_committed_result() {
    let fixture = Fixture::new("admin").await;
    let request_id = Uuid::new_v4();
    let request = IssueRequest::Create {
        terms: fixture.terms().await,
    };
    let (first, second) = tokio::join!(
        fixture
            .store
            .issue(&fixture.token, request_id, request.clone()),
        fixture.store.issue(&fixture.token, request_id, request),
    );
    assert_eq!(first.unwrap().wire, second.unwrap().wire);
    let request_count: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels.license_requests WHERE author_id=$1 AND request_id=$2",
    )
    .bind(fixture.author)
    .bind(request_id)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(request_count, 1);
    fixture.close().await;
}

#[tokio::test]
async fn renewal_preserves_customer_and_deployment_and_revocation_allows_replacement() {
    let fixture = Fixture::new("admin").await;
    let terms = fixture.terms().await;
    let issued = fixture
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
    let mut changed_identity = terms.clone();
    changed_identity.deployment_id = Uuid::new_v4();
    assert!(matches!(
        fixture
            .store
            .issue(
                &fixture.token,
                Uuid::new_v4(),
                IssueRequest::Renew {
                    license_id: issued.license_id,
                    expected_revision: 1,
                    terms: changed_identity,
                },
            )
            .await,
        Err(AuthError::Conflict)
    ));
    let mut renewed_terms = terms.clone();
    renewed_terms.max_streams = 16;
    let renewed = fixture
        .store
        .issue(
            &fixture.token,
            Uuid::new_v4(),
            IssueRequest::Renew {
                license_id: issued.license_id,
                expected_revision: 1,
                terms: renewed_terms,
            },
        )
        .await
        .unwrap();
    assert_eq!(renewed.revision, 2);
    assert_eq!(
        fixture
            .store
            .revoke(&fixture.token, issued.license_id, 2)
            .await
            .unwrap(),
        3
    );
    let replacement = fixture
        .store
        .issue(
            &fixture.token,
            Uuid::new_v4(),
            IssueRequest::Create { terms },
        )
        .await
        .unwrap();
    assert_ne!(replacement.license_id, issued.license_id);
    fixture.close().await;
}

#[tokio::test]
async fn visitor_invalid_limits_and_failed_issuance_never_write() {
    let administrator = Fixture::new("admin").await;
    let visitor = Fixture::new("visitor").await;
    let terms = administrator.terms().await;
    assert!(matches!(
        visitor
            .store
            .issue(
                &visitor.token,
                Uuid::new_v4(),
                IssueRequest::Create {
                    terms: terms.clone()
                },
            )
            .await,
        Err(AuthError::Rejected)
    ));
    for invalid_terms in [
        LicenseTerms {
            max_streams: 0,
            ..terms.clone()
        },
        LicenseTerms {
            services: vec![],
            ..terms.clone()
        },
        LicenseTerms {
            expires_at: 0,
            ..terms.clone()
        },
    ] {
        assert!(administrator
            .store
            .issue(
                &administrator.token,
                Uuid::new_v4(),
                IssueRequest::Create {
                    terms: invalid_terms
                },
            )
            .await
            .is_err());
    }
    let written: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.licenses WHERE target_deployment=$1")
            .bind(terms.deployment_id)
            .fetch_one(&administrator.owner)
            .await
            .unwrap();
    assert_eq!(written, 0);
    visitor.close().await;
    administrator.close().await;
}

#[tokio::test]
async fn operator_password_change_invalidates_existing_sessions() {
    let fixture = Fixture::new("admin").await;
    let operators = fixture.store.operators();
    let replacement_hash = px_credentials::hash("replacement synthetic password").unwrap();
    operators
        .set_password(&fixture.token, fixture.author, 1, replacement_hash.as_str())
        .await
        .unwrap();
    assert!(operators.authenticate(&fixture.token).await.is_err());
    fixture.close().await;
}
