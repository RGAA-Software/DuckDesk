#[path = "support/runtime_fixture.rs"]
mod fixture;

use axum::http::StatusCode;
use fixture::{call, login, register, resource_call, PASSWORD};
use px_console_runtime::{ConsoleRuntime, LicenseLaunchConfig, ReleaseIdentity, RuntimeResources};
use px_console_store::{initialize_administrator, PasswordDigest, Username};
use serde_json::{json, Value};
use std::{env, path::PathBuf};
use uuid::Uuid;

const FIXTURE_KIND: &str = "cn_license";

#[tokio::test]
async fn cn_signed_customer_license_controls_live_console_api() {
    let deployment_id = fixture::deployment();
    let license_trust_path = PathBuf::from(env::var("PIXELS_TEST_CN_LICENSE_TRUST_STORE").unwrap());
    let license_path = PathBuf::from(env::var("PIXELS_TEST_CN_LICENSE_FILE").unwrap());
    let license = LicenseLaunchConfig::new(license_trust_path, license_path)
        .unwrap()
        .admit(deployment_id)
        .await
        .unwrap();
    let license_id = license.payload.license_id;
    assert_eq!(license.payload.max_streams, 1);
    assert_eq!(
        license.payload.services,
        vec![px_license::LicensedService::CloudApplications]
    );

    let password_digest = px_credentials::hash(PASSWORD).unwrap();
    initialize_administrator(
        &fixture::config("OWNER"),
        deployment_id,
        &Username::parse("initial-admin").unwrap(),
        &PasswordDigest::parse(password_digest.to_string()).unwrap(),
    )
    .await
    .unwrap();
    let runtime = ConsoleRuntime::activate_product_with_cache(
        &fixture::config("RUNTIME"),
        deployment_id,
        fixture::vault(),
        fixture::policy(),
        fixture::guests(),
        RuntimeResources {
            recording_cache: None,
            relay_admission: None,
            release: ReleaseIdentity::integration(),
        },
        license,
    )
    .await
    .unwrap();
    let router = runtime.router();
    let administrator_token = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let (license_status_code, license_status) = call(
        &router,
        "GET",
        "/api/console/managed/license",
        "admin_web",
        Some(&administrator_token),
        Value::Null,
    )
    .await;
    assert_eq!(license_status_code, StatusCode::OK, "{license_status}");
    assert_eq!(license_status["license_id"], license_id.to_string());
    assert_eq!(license_status["max_streams"], 1);
    assert_eq!(license_status["services"], json!(["cloud_applications"]));

    let username = register(&router).await;
    let user_token = login(&router, &username, PASSWORD, "android").await;
    for (application_kind, expected_status) in [
        ("rdp", StatusCode::FORBIDDEN),
        ("webview", StatusCode::SERVICE_UNAVAILABLE),
    ] {
        let application_specification = match application_kind {
            "rdp" => {
                json!({"name":Uuid::new_v4().to_string(),"launch":{"kind":"rdp"},"access":"public","allow_observer":false,"allow_takeover":false,"disabled":false})
            }
            _ => {
                json!({"name":Uuid::new_v4().to_string(),"launch":{"kind":"webview","entry_url":"https://example.test/app","video":{"codec":"h264","bitrate_kbps":8000}},"access":"public","allow_observer":false,"allow_takeover":false,"disabled":false})
            }
        };
        let (create_status, application) = call(
            &router,
            "POST",
            "/api/console/managed/applications",
            "admin_web",
            Some(&administrator_token),
            application_specification,
        )
        .await;
        assert_eq!(create_status, StatusCode::CREATED, "{application}");
        let (start_status, start_response) = resource_call(
            &router,
            "POST",
            "/api/console/instances",
            "android",
            Some(&user_token),
            Some("user"),
            json!({"request_id":Uuid::new_v4(),"application_id":application["id"],"deployment_id":null}),
        )
        .await;
        assert_eq!(
            start_status, expected_status,
            "{application_kind}: {start_response}"
        );
    }
    let (instances_status, instances) = resource_call(
        &router,
        "GET",
        "/api/console/instances?limit=100",
        "android",
        Some(&user_token),
        Some("user"),
        Value::Null,
    )
    .await;
    assert_eq!(instances_status, StatusCode::OK, "{instances}");
    assert_eq!(instances, json!([]));
    runtime.shutdown().await;
}
