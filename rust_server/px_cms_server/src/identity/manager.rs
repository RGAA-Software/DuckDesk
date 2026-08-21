use crate::app_schedule::gAppScheduleManager;
use crate::cms_api_error::CmsApiError;
use crate::identity::model::{
    GroupAppGrant, GroupDeviceGrant, GroupRef, GroupView, UserGroup, UserGroupMember,
};
use crate::{gCmsDatabase, gDeviceManager, gUserManager};
use futures_util::StreamExt;
use mongodb::bson::doc;
use std::collections::BTreeSet;

fn validated_group_text(value: &str, min: usize, max: usize) -> Result<String, CmsApiError> {
    let value = value.trim().to_string();
    let count = value.chars().count();
    if count < min || count > max || value.chars().any(char::is_control) {
        return Err(CmsApiError::InvalidParams);
    }
    Ok(value)
}

pub struct IdentityManager;

impl IdentityManager {
    pub async fn create_group(name: String, remark: String) -> Result<GroupView, CmsApiError> {
        let name = validated_group_text(&name, 1, 64)?;
        let remark = if remark.trim().is_empty() {
            String::new()
        } else {
            validated_group_text(&remark, 1, 256)?
        };
        let now = px_base::get_current_timestamp();
        let group = UserGroup {
            gid: uuid::Uuid::new_v4().simple().to_string(),
            name_normalized: name.to_lowercase(),
            name,
            remark,
            deleted: false,
            created_at: now,
            updated_at: now,
            version: 1,
        };
        gCmsDatabase
            .lock()
            .await
            .user_group()
            .lock()
            .await
            .insert_one(group.clone())
            .await
            .map_err(|e| {
                tracing::warn!("create group failed: {}", e);
                CmsApiError::InvalidParams
            })?;
        Self::group_view(group).await
    }

    pub async fn get_group(gid: &str) -> Result<UserGroup, CmsApiError> {
        gCmsDatabase
            .lock()
            .await
            .user_group()
            .lock()
            .await
            .find_one(doc! { "gid": gid, "deleted": false })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?
            .ok_or(CmsApiError::GroupNotFound)
    }

    async fn group_view(group: UserGroup) -> Result<GroupView, CmsApiError> {
        let member_count = Self::active_group_member_ids(&group.gid).await?.len() as u64;
        let db = gCmsDatabase.lock().await;
        let device_count = db
            .group_device_grant()
            .lock()
            .await
            .count_documents(doc! { "gid": &group.gid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        let app_count = db
            .group_app_grant()
            .lock()
            .await
            .count_documents(doc! { "gid": &group.gid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        Ok(GroupView {
            gid: group.gid,
            name: group.name,
            remark: group.remark,
            member_count,
            device_count,
            app_count,
            created_at: group.created_at,
            updated_at: group.updated_at,
            version: group.version,
        })
    }

    pub async fn list_groups() -> Result<Vec<GroupView>, CmsApiError> {
        let mut cursor = gCmsDatabase
            .lock()
            .await
            .user_group()
            .lock()
            .await
            .find(doc! { "deleted": false })
            .sort(doc! { "name_normalized": 1 })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        let mut result = Vec::new();
        while let Some(group) = cursor.next().await {
            result.push(Self::group_view(group.map_err(|_| CmsApiError::DatabaseError)?).await?);
        }
        Ok(result)
    }

    pub async fn update_group(
        gid: &str,
        version: i64,
        name: Option<String>,
        remark: Option<String>,
    ) -> Result<GroupView, CmsApiError> {
        let mut set = doc! { "updated_at": px_base::get_current_timestamp() };
        if let Some(name) = name {
            let name = validated_group_text(&name, 1, 64)?;
            set.insert("name_normalized", name.to_lowercase());
            set.insert("name", name);
        }
        if let Some(remark) = remark {
            set.insert(
                "remark",
                if remark.trim().is_empty() {
                    String::new()
                } else {
                    validated_group_text(&remark, 1, 256)?
                },
            );
        }
        let result = gCmsDatabase
            .lock()
            .await
            .user_group()
            .lock()
            .await
            .update_one(
                doc! { "gid": gid, "version": version, "deleted": false },
                doc! { "$set": set, "$inc": { "version": 1_i64 } },
            )
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        if result.matched_count == 0 {
            return Err(CmsApiError::VersionConflict);
        }
        Self::group_view(Self::get_group(gid).await?).await
    }

    pub async fn delete_group(gid: &str, version: i64) -> Result<bool, CmsApiError> {
        let result = gCmsDatabase
            .lock()
            .await
            .user_group()
            .lock()
            .await
            .update_one(
                doc! { "gid": gid, "version": version, "deleted": false },
                doc! { "$set": { "deleted": true, "updated_at": px_base::get_current_timestamp() }, "$inc": { "version": 1_i64 } },
            )
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        if result.matched_count == 0 {
            return Err(CmsApiError::VersionConflict);
        }
        let db = gCmsDatabase.lock().await;
        db.user_group_member()
            .lock()
            .await
            .delete_many(doc! { "gid": gid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        db.group_device_grant()
            .lock()
            .await
            .delete_many(doc! { "gid": gid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        db.group_app_grant()
            .lock()
            .await
            .delete_many(doc! { "gid": gid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        Ok(true)
    }

    async fn bump_group_version(gid: &str, version: i64) -> Result<(), CmsApiError> {
        let result = gCmsDatabase
            .lock()
            .await
            .user_group()
            .lock()
            .await
            .update_one(
                doc! { "gid": gid, "version": version, "deleted": false },
                doc! { "$set": { "updated_at": px_base::get_current_timestamp() }, "$inc": { "version": 1_i64 } },
            )
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        if result.matched_count == 0 {
            return Err(CmsApiError::VersionConflict);
        }
        Ok(())
    }

    pub async fn replace_members(
        gid: &str,
        version: i64,
        user_ids: Vec<String>,
    ) -> Result<GroupView, CmsApiError> {
        let ids: BTreeSet<_> = user_ids.into_iter().filter(|id| !id.is_empty()).collect();
        for uid in &ids {
            let user = gUserManager.query_user_by_id(uid.clone()).await?;
            if user.deleted {
                return Err(CmsApiError::UserNotFound);
            }
        }
        Self::bump_group_version(gid, version).await?;
        let collection = gCmsDatabase.lock().await.user_group_member();
        collection
            .lock()
            .await
            .delete_many(doc! { "gid": gid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        if !ids.is_empty() {
            let now = px_base::get_current_timestamp();
            let rows = ids.into_iter().map(|uid| UserGroupMember {
                uid,
                gid: gid.to_string(),
                created_at: now,
            });
            collection
                .lock()
                .await
                .insert_many(rows)
                .await
                .map_err(|_| CmsApiError::DatabaseError)?;
        }
        Self::group_view(Self::get_group(gid).await?).await
    }

    pub async fn replace_devices(
        gid: &str,
        version: i64,
        device_ids: Vec<String>,
    ) -> Result<GroupView, CmsApiError> {
        let ids: BTreeSet<_> = device_ids.into_iter().filter(|id| !id.is_empty()).collect();
        for device_id in &ids {
            gDeviceManager.query_device_by_id(device_id.clone()).await?;
        }
        Self::bump_group_version(gid, version).await?;
        let collection = gCmsDatabase.lock().await.group_device_grant();
        collection
            .lock()
            .await
            .delete_many(doc! { "gid": gid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        if !ids.is_empty() {
            let now = px_base::get_current_timestamp();
            let rows = ids.into_iter().map(|device_id| GroupDeviceGrant {
                gid: gid.to_string(),
                device_id,
                created_at: now,
            });
            collection
                .lock()
                .await
                .insert_many(rows)
                .await
                .map_err(|_| CmsApiError::DatabaseError)?;
        }
        Self::group_view(Self::get_group(gid).await?).await
    }

    pub async fn replace_apps(
        gid: &str,
        version: i64,
        app_ids: Vec<String>,
    ) -> Result<GroupView, CmsApiError> {
        let ids: BTreeSet<_> = app_ids.into_iter().filter(|id| !id.is_empty()).collect();
        let known: BTreeSet<_> = gAppScheduleManager
            .list_applications()
            .await
            .into_iter()
            .map(|app| app.app_id)
            .collect();
        if ids.iter().any(|id| !known.contains(id)) {
            return Err(CmsApiError::InvalidParams);
        }
        Self::bump_group_version(gid, version).await?;
        let collection = gCmsDatabase.lock().await.group_app_grant();
        collection
            .lock()
            .await
            .delete_many(doc! { "gid": gid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        if !ids.is_empty() {
            let now = px_base::get_current_timestamp();
            let rows = ids.into_iter().map(|app_id| GroupAppGrant {
                gid: gid.to_string(),
                app_id,
                created_at: now,
            });
            collection
                .lock()
                .await
                .insert_many(rows)
                .await
                .map_err(|_| CmsApiError::DatabaseError)?;
        }
        Self::group_view(Self::get_group(gid).await?).await
    }

    pub async fn validate_group_ids(group_ids: &[String]) -> Result<(), CmsApiError> {
        let ids: BTreeSet<_> = group_ids.iter().filter(|id| !id.is_empty()).collect();
        for gid in ids {
            Self::get_group(gid).await?;
        }
        Ok(())
    }

    pub async fn app_group_ids(app_id: &str) -> Result<Vec<String>, CmsApiError> {
        let mut cursor = gCmsDatabase
            .lock()
            .await
            .group_app_grant()
            .lock()
            .await
            .find(doc! { "app_id": app_id })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        let mut ids = Vec::new();
        while let Some(row) = cursor.next().await {
            let row = row.map_err(|_| CmsApiError::DatabaseError)?;
            if Self::get_group(&row.gid).await.is_ok() {
                ids.push(row.gid);
            }
        }
        ids.sort();
        ids.dedup();
        Ok(ids)
    }

    /// Replaces the groups authorized for one application. This is the
    /// application-centric write path used by the scheduling page. Group
    /// versions are bumped so an already-open group editor cannot silently
    /// overwrite a concurrent authorization change.
    pub async fn replace_groups_for_app(
        app_id: &str,
        group_ids: Vec<String>,
    ) -> Result<Vec<String>, CmsApiError> {
        let known_app = gAppScheduleManager
            .list_applications()
            .await
            .into_iter()
            .any(|app| app.app_id == app_id);
        if !known_app {
            return Err(CmsApiError::ResourceNotFound);
        }
        Self::validate_group_ids(&group_ids).await?;
        let ids: BTreeSet<_> = group_ids.into_iter().filter(|id| !id.is_empty()).collect();
        let previous: BTreeSet<_> = Self::app_group_ids(app_id).await?.into_iter().collect();
        let collection = gCmsDatabase.lock().await.group_app_grant();
        collection
            .lock()
            .await
            .delete_many(doc! { "app_id": app_id })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        if !ids.is_empty() {
            let now = px_base::get_current_timestamp();
            let rows = ids.iter().map(|gid| GroupAppGrant {
                gid: gid.clone(),
                app_id: app_id.to_string(),
                created_at: now,
            });
            collection
                .lock()
                .await
                .insert_many(rows)
                .await
                .map_err(|_| CmsApiError::DatabaseError)?;
        }
        let affected: Vec<_> = previous.union(&ids).cloned().collect();
        if !affected.is_empty() {
            gCmsDatabase
                .lock()
                .await
                .user_group()
                .lock()
                .await
                .update_many(
                    doc! { "gid": { "$in": &affected }, "deleted": false },
                    doc! {
                        "$set": { "updated_at": px_base::get_current_timestamp() },
                        "$inc": { "version": 1_i64 }
                    },
                )
                .await
                .map_err(|_| CmsApiError::DatabaseError)?;
        }
        Ok(ids.into_iter().collect())
    }

    pub async fn groups_for_user(uid: &str) -> Result<Vec<GroupRef>, CmsApiError> {
        let mut memberships = gCmsDatabase
            .lock()
            .await
            .user_group_member()
            .lock()
            .await
            .find(doc! { "uid": uid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        let mut groups = Vec::new();
        while let Some(row) = memberships.next().await {
            let row = row.map_err(|_| CmsApiError::DatabaseError)?;
            if let Ok(group) = Self::get_group(&row.gid).await {
                groups.push(GroupRef {
                    gid: group.gid,
                    name: group.name,
                });
            }
        }
        groups.sort_by(|a, b| a.name.cmp(&b.name));
        Ok(groups)
    }

    pub async fn replace_groups_for_user(
        uid: &str,
        group_ids: Vec<String>,
    ) -> Result<Vec<GroupRef>, CmsApiError> {
        let user = gUserManager.query_user_by_id(uid.to_string()).await?;
        if user.deleted {
            return Err(CmsApiError::UserNotFound);
        }
        let ids: BTreeSet<_> = group_ids.into_iter().filter(|id| !id.is_empty()).collect();
        for gid in &ids {
            Self::get_group(gid).await?;
        }
        let collection = gCmsDatabase.lock().await.user_group_member();
        collection
            .lock()
            .await
            .delete_many(doc! { "uid": uid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        if !ids.is_empty() {
            let now = px_base::get_current_timestamp();
            collection
                .lock()
                .await
                .insert_many(ids.into_iter().map(|gid| UserGroupMember {
                    uid: uid.to_string(),
                    gid,
                    created_at: now,
                }))
                .await
                .map_err(|_| CmsApiError::DatabaseError)?;
        }
        Self::groups_for_user(uid).await
    }

    pub async fn group_member_ids(gid: &str) -> Result<Vec<String>, CmsApiError> {
        Self::get_group(gid).await?;
        Self::active_group_member_ids(gid).await
    }

    async fn active_group_member_ids(gid: &str) -> Result<Vec<String>, CmsApiError> {
        let mut cursor = gCmsDatabase
            .lock()
            .await
            .user_group_member()
            .lock()
            .await
            .find(doc! { "gid": gid })
            .sort(doc! { "uid": 1 })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        let mut relation_ids = BTreeSet::new();
        while let Some(row) = cursor.next().await {
            relation_ids.insert(row.map_err(|_| CmsApiError::DatabaseError)?.uid);
        }
        if relation_ids.is_empty() {
            return Ok(Vec::new());
        }
        let mut users = gCmsDatabase
            .lock()
            .await
            .user()
            .lock()
            .await
            .find(doc! { "uid": { "$in": relation_ids.into_iter().collect::<Vec<_>>() }, "deleted": false })
            .sort(doc! { "username_normalized": 1 })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        let mut ids = Vec::new();
        while let Some(user) = users.next().await {
            ids.push(user.map_err(|_| CmsApiError::DatabaseError)?.uid);
        }
        Ok(ids)
    }

    pub async fn remove_user_from_all_groups(uid: &str) -> Result<(), CmsApiError> {
        let collection = gCmsDatabase.lock().await.user_group_member();
        let mut cursor = collection
            .lock()
            .await
            .find(doc! { "uid": uid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        let mut affected = BTreeSet::new();
        while let Some(row) = cursor.next().await {
            affected.insert(row.map_err(|_| CmsApiError::DatabaseError)?.gid);
        }
        collection
            .lock()
            .await
            .delete_many(doc! { "uid": uid })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        if !affected.is_empty() {
            gCmsDatabase
                .lock()
                .await
                .user_group()
                .lock()
                .await
                .update_many(
                    doc! { "gid": { "$in": affected.into_iter().collect::<Vec<_>>() }, "deleted": false },
                    doc! {
                        "$set": { "updated_at": px_base::get_current_timestamp() },
                        "$inc": { "version": 1_i64 }
                    },
                )
                .await
                .map_err(|_| CmsApiError::DatabaseError)?;
        }
        Ok(())
    }

    pub async fn group_device_ids(gid: &str) -> Result<Vec<String>, CmsApiError> {
        Self::get_group(gid).await?;
        let mut cursor = gCmsDatabase
            .lock()
            .await
            .group_device_grant()
            .lock()
            .await
            .find(doc! { "gid": gid })
            .sort(doc! { "device_id": 1 })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        let mut ids = Vec::new();
        while let Some(row) = cursor.next().await {
            ids.push(row.map_err(|_| CmsApiError::DatabaseError)?.device_id);
        }
        Ok(ids)
    }

    pub async fn group_app_ids(gid: &str) -> Result<Vec<String>, CmsApiError> {
        Self::get_group(gid).await?;
        let mut cursor = gCmsDatabase
            .lock()
            .await
            .group_app_grant()
            .lock()
            .await
            .find(doc! { "gid": gid })
            .sort(doc! { "app_id": 1 })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        let mut ids = Vec::new();
        while let Some(row) = cursor.next().await {
            ids.push(row.map_err(|_| CmsApiError::DatabaseError)?.app_id);
        }
        Ok(ids)
    }

    pub async fn authorized_device_ids(uid: &str) -> Result<BTreeSet<String>, CmsApiError> {
        let groups = Self::groups_for_user(uid).await?;
        let gids: Vec<_> = groups.into_iter().map(|group| group.gid).collect();
        let mut ids = BTreeSet::new();
        if gids.is_empty() {
            return Ok(ids);
        }
        let mut cursor = gCmsDatabase
            .lock()
            .await
            .group_device_grant()
            .lock()
            .await
            .find(doc! { "gid": { "$in": gids } })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        while let Some(row) = cursor.next().await {
            ids.insert(row.map_err(|_| CmsApiError::DatabaseError)?.device_id);
        }
        Ok(ids)
    }

    pub async fn authorized_app_ids(uid: &str) -> Result<BTreeSet<String>, CmsApiError> {
        let gids: Vec<_> = Self::groups_for_user(uid)
            .await?
            .into_iter()
            .map(|group| group.gid)
            .collect();
        let mut ids = BTreeSet::new();
        if gids.is_empty() {
            return Ok(ids);
        }
        let mut cursor = gCmsDatabase
            .lock()
            .await
            .group_app_grant()
            .lock()
            .await
            .find(doc! { "gid": { "$in": gids } })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        while let Some(row) = cursor.next().await {
            ids.insert(row.map_err(|_| CmsApiError::DatabaseError)?.app_id);
        }
        Ok(ids)
    }
}

#[cfg(test)]
mod tests {
    use super::validated_group_text;

    #[test]
    fn group_text_is_trimmed_and_control_characters_are_rejected() {
        assert_eq!(
            validated_group_text("  Operators ", 1, 64).unwrap(),
            "Operators"
        );
        assert!(validated_group_text("bad\nname", 1, 64).is_err());
    }
}
