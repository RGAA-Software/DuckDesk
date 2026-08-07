use crate::app_schedule::manager::{AppInstance, AppPlacement, Application};
use crate::gSpvrDatabase;
use futures_util::StreamExt;
use mongodb::bson::doc;

pub async fn upsert_application(app: &Application) -> Result<(), String> {
    let db = gSpvrDatabase.lock().await;
    let Some(coll) = db.c_app.as_ref() else {
        return Ok(()); // DB not ready; memory-only
    };
    let coll = coll.lock().await;
    coll.replace_one(doc! { "app_id": &app.app_id }, app.clone())
        .upsert(true)
        .await
        .map_err(|e| e.to_string())?;
    Ok(())
}

pub async fn upsert_placement(p: &AppPlacement) -> Result<(), String> {
    let db = gSpvrDatabase.lock().await;
    let Some(coll) = db.c_app_placement.as_ref() else {
        return Ok(());
    };
    let coll = coll.lock().await;
    coll.replace_one(doc! { "placement_id": &p.placement_id }, p.clone())
        .upsert(true)
        .await
        .map_err(|e| e.to_string())?;
    Ok(())
}

pub async fn upsert_instance(i: &AppInstance) -> Result<(), String> {
    let db = gSpvrDatabase.lock().await;
    let Some(coll) = db.c_app_instance.as_ref() else {
        return Ok(());
    };
    let coll = coll.lock().await;
    coll.replace_one(doc! { "instance_id": &i.instance_id }, i.clone())
        .upsert(true)
        .await
        .map_err(|e| e.to_string())?;
    Ok(())
}

pub async fn load_all() -> Result<(Vec<Application>, Vec<AppPlacement>, Vec<AppInstance>), String> {
    let db = gSpvrDatabase.lock().await;
    let (Some(c_app), Some(c_plc), Some(c_inst)) =
        (db.c_app.clone(), db.c_app_placement.clone(), db.c_app_instance.clone())
    else {
        return Ok((vec![], vec![], vec![]));
    };
    drop(db);

    let mut apps = Vec::new();
    {
        let coll = c_app.lock().await;
        let mut cursor = coll.find(doc! {}).await.map_err(|e| e.to_string())?;
        while let Some(item) = cursor.next().await {
            apps.push(item.map_err(|e| e.to_string())?);
        }
    }
    let mut placements = Vec::new();
    {
        let coll = c_plc.lock().await;
        let mut cursor = coll.find(doc! {}).await.map_err(|e| e.to_string())?;
        while let Some(item) = cursor.next().await {
            placements.push(item.map_err(|e| e.to_string())?);
        }
    }
    let mut instances = Vec::new();
    {
        let coll = c_inst.lock().await;
        let mut cursor = coll.find(doc! {}).await.map_err(|e| e.to_string())?;
        while let Some(item) = cursor.next().await {
            instances.push(item.map_err(|e| e.to_string())?);
        }
    }
    Ok((apps, placements, instances))
}
