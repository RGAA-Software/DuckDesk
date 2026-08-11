use crate::app_schedule::manager::{AppInstance, AppNode, AppPlacement, Application};
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

pub async fn upsert_node(n: &AppNode) -> Result<(), String> {
    let db = gSpvrDatabase.lock().await;
    let Some(coll) = db.c_app_node.as_ref() else {
        return Ok(());
    };
    let coll = coll.lock().await;
    coll.replace_one(doc! { "node_id": &n.node_id }, n.clone())
        .upsert(true)
        .await
        .map_err(|e| e.to_string())?;
    Ok(())
}

pub async fn delete_node(node_id: &str) -> Result<(), String> {
    let db = gSpvrDatabase.lock().await;
    let Some(coll) = db.c_app_node.as_ref() else {
        return Ok(());
    };
    let coll = coll.lock().await;
    coll.delete_one(doc! { "node_id": node_id })
        .await
        .map_err(|e| e.to_string())?;
    Ok(())
}

pub async fn delete_nodes_by_app(app_id: &str) -> Result<(), String> {
    let db = gSpvrDatabase.lock().await;
    let Some(coll) = db.c_app_node.as_ref() else {
        return Ok(());
    };
    let coll = coll.lock().await;
    coll.delete_many(doc! { "app_id": app_id })
        .await
        .map_err(|e| e.to_string())?;
    Ok(())
}

pub async fn delete_instances_by_node(node_id: &str) -> Result<(), String> {
    let db = gSpvrDatabase.lock().await;
    let Some(coll) = db.c_app_instance.as_ref() else {
        return Ok(());
    };
    let coll = coll.lock().await;
    coll.delete_many(doc! { "node_id": node_id })
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

pub async fn delete_application(app_id: &str) -> Result<(), String> {
    let db = gSpvrDatabase.lock().await;
    let Some(coll) = db.c_app.as_ref() else {
        return Ok(());
    };
    let coll = coll.lock().await;
    coll.delete_one(doc! { "app_id": app_id })
        .await
        .map_err(|e| e.to_string())?;
    Ok(())
}

#[allow(dead_code)] // 遗留 placement 清理工具,节点结构后暂无调用方
pub async fn delete_placement(placement_id: &str) -> Result<(), String> {
    let db = gSpvrDatabase.lock().await;
    let Some(coll) = db.c_app_placement.as_ref() else {
        return Ok(());
    };
    let coll = coll.lock().await;
    coll.delete_one(doc! { "placement_id": placement_id })
        .await
        .map_err(|e| e.to_string())?;
    Ok(())
}

pub async fn delete_instances_by_app(app_id: &str) -> Result<(), String> {
    let db = gSpvrDatabase.lock().await;
    let Some(coll) = db.c_app_instance.as_ref() else {
        return Ok(());
    };
    let coll = coll.lock().await;
    coll.delete_many(doc! { "app_id": app_id })
        .await
        .map_err(|e| e.to_string())?;
    Ok(())
}

pub async fn load_all() -> Result<(Vec<Application>, Vec<AppPlacement>, Vec<AppNode>, Vec<AppInstance>), String> {
    let db = gSpvrDatabase.lock().await;
    let (Some(c_app), Some(c_plc), Some(c_node), Some(c_inst)) = (
        db.c_app.clone(),
        db.c_app_placement.clone(),
        db.c_app_node.clone(),
        db.c_app_instance.clone(),
    )
    else {
        return Ok((vec![], vec![], vec![], vec![]));
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
    let mut nodes = Vec::new();
    {
        let coll = c_node.lock().await;
        let mut cursor = coll.find(doc! {}).await.map_err(|e| e.to_string())?;
        while let Some(item) = cursor.next().await {
            nodes.push(item.map_err(|e| e.to_string())?);
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
    Ok((apps, placements, nodes, instances))
}
