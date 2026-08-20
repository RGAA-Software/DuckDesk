use futures_util::TryStreamExt;
use mongodb::{bson::doc, Client};

const EXPECTED_DATABASE: &str = "db_gr_cms_server";
const IDENTITY_COLLECTIONS: &[&str] = &[
    "c_user",
    "c_user_device",
    "c_user_group",
    "c_user_group_member",
    "c_group_device_grant",
    "c_group_app_grant",
    "c_user_session",
    "c_guest_block",
    "c_user_invite",
    "c_connection_ticket",
    "c_app_instance",
];

pub async fn reset(expected_database: &str) -> Result<(), String> {
    let settings = crate::gCmsSettings.lock().await.clone();
    if settings.environment != "test" {
        return Err("refusing reset: configuration environment must equal 'test'".to_string());
    }
    if expected_database != EXPECTED_DATABASE {
        return Err(format!(
            "refusing reset: database must exactly equal {EXPECTED_DATABASE}"
        ));
    }
    let client = Client::with_uri_str(&settings.mongodb_url)
        .await
        .map_err(|error| error.to_string())?;
    let database = client.database(EXPECTED_DATABASE);

    println!("Reset target: {EXPECTED_DATABASE}");
    for name in IDENTITY_COLLECTIONS {
        let count = database
            .collection::<mongodb::bson::Document>(name)
            .count_documents(doc! {})
            .await
            .map_err(|error| error.to_string())?;
        println!("  {name}: {count} documents");
    }
    let legacy_apps = database
        .collection::<mongodb::bson::Document>("c_app")
        .count_documents(doc! { "access_mode": { "$exists": false } })
        .await
        .map_err(|error| error.to_string())?;
    println!("  c_app legacy rows without access_mode: {legacy_apps}");

    for name in IDENTITY_COLLECTIONS {
        database
            .collection::<mongodb::bson::Document>(name)
            .delete_many(doc! {})
            .await
            .map_err(|error| error.to_string())?;
    }
    database
        .collection::<mongodb::bson::Document>("c_app")
        .delete_many(doc! { "access_mode": { "$exists": false } })
        .await
        .map_err(|error| error.to_string())?;

    // App nodes/placements are operational children of c_app.  The previous
    // test model allowed them to outlive deleted application rows, so remove
    // every orphan explicitly without touching valid applications.
    let app_rows = database
        .collection::<mongodb::bson::Document>("c_app")
        .find(doc! {})
        .projection(doc! { "app_id": 1, "_id": 0 })
        .await
        .map_err(|error| error.to_string())?
        .try_collect::<Vec<_>>()
        .await
        .map_err(|error| error.to_string())?;
    let valid_app_ids: Vec<String> = app_rows
        .into_iter()
        .filter_map(|row| row.get_str("app_id").ok().map(str::to_string))
        .collect();
    for name in ["c_app_node", "c_app_placement"] {
        let filter = if valid_app_ids.is_empty() {
            doc! {}
        } else {
            doc! { "app_id": { "$nin": &valid_app_ids } }
        };
        let deleted = database
            .collection::<mongodb::bson::Document>(name)
            .delete_many(filter)
            .await
            .map_err(|error| error.to_string())?
            .deleted_count;
        println!("  {name} orphan rows removed: {deleted}");
    }
    println!("Identity/ACL test data reset completed; device, license and configuration collections were preserved.");
    Ok(())
}
