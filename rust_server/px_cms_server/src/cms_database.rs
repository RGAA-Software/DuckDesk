use crate::app_schedule::manager::{AppInstance, AppNode, AppPlacement, Application};
use crate::connection_ticket::model::ConnectionTicket;
use crate::device::cms_device::CmsDevice;
use crate::event::cms_event::CmsEvent;
use crate::gCmsSettings;
use crate::identity::model::{GroupAppGrant, GroupDeviceGrant, UserGroup, UserGroupMember};
use crate::net_client::cms_client_conn::CmsClientConnVo;
use crate::record::cms_file_transfer::CmsFileTransfer;
use crate::record::cms_render_record::CmsRenderRecord;
use crate::record::cms_visit::CmsVisit;
use crate::stream::cms_stream::CmsStream;
use crate::update::update_info::UpdateInfo;
use crate::user::cms_user::CmsUser;
use crate::user::session::{CmsUserSession, GuestBlock};
use crate::user_device::cms_user_device::CmsUserDevice;
use mongodb::bson::doc;
use mongodb::options::{ClientOptions, IndexOptions};
use mongodb::{Client, Collection, IndexModel};
use std::sync::Arc;
use tokio::sync::Mutex;

#[derive(Default)]
pub struct CmsDatabase {
    pub client: Option<Arc<Mutex<Client>>>,
    // device
    pub c_device: Option<Arc<Mutex<Collection<CmsDevice>>>>,
    // event
    pub c_event: Option<Arc<Mutex<Collection<CmsEvent>>>>,
    // user
    pub c_user: Option<Arc<Mutex<Collection<CmsUser>>>>,
    pub c_user_session: Option<Arc<Mutex<Collection<CmsUserSession>>>>,
    pub c_guest_block: Option<Arc<Mutex<Collection<GuestBlock>>>>,
    pub c_user_group: Option<Arc<Mutex<Collection<UserGroup>>>>,
    pub c_user_group_member: Option<Arc<Mutex<Collection<UserGroupMember>>>>,
    pub c_group_device_grant: Option<Arc<Mutex<Collection<GroupDeviceGrant>>>>,
    pub c_group_app_grant: Option<Arc<Mutex<Collection<GroupAppGrant>>>>,
    pub c_connection_ticket: Option<Arc<Mutex<Collection<ConnectionTicket>>>>,
    // stream
    pub c_stream: Option<Arc<Mutex<Collection<CmsStream>>>>,
    // record: visit
    pub c_visit: Option<Arc<Mutex<Collection<CmsVisit>>>>,
    // record: file transfer
    pub c_file_transfer: Option<Arc<Mutex<Collection<CmsFileTransfer>>>>,
    // record: render records cache (design doc 6.3)
    pub c_records: Option<Arc<Mutex<Collection<CmsRenderRecord>>>>,
    // user device relationship
    pub c_user_device: Option<Arc<Mutex<Collection<CmsUserDevice>>>>,
    // cms conn; use adapter
    pub c_client_conn: Option<Arc<Mutex<Collection<CmsClientConnVo>>>>,
    // update
    pub c_update_info: Option<Arc<Mutex<Collection<UpdateInfo>>>>,
    // CMS app schedule
    pub c_app: Option<Arc<Mutex<Collection<Application>>>>,
    pub c_app_placement: Option<Arc<Mutex<Collection<AppPlacement>>>>,
    pub c_app_node: Option<Arc<Mutex<Collection<AppNode>>>>,
    pub c_app_instance: Option<Arc<Mutex<Collection<AppInstance>>>>,
}

impl CmsDatabase {
    pub fn new() -> Self {
        Self::default()
    }

    pub async fn init(&mut self) -> bool {
        let uri = gCmsSettings.lock().await.mongodb_url.clone();
        // The URI may contain credentials. Never write it to a log file.
        tracing::info!("connecting to configured MongoDB");

        let mut client_options = match ClientOptions::parse(&uri).await {
            Ok(opts) => opts,
            Err(_) => {
                tracing::error!("configured MongoDB URI is invalid (details redacted)");
                return false;
            }
        };
        client_options.connect_timeout = Some(std::time::Duration::from_secs(5));
        client_options.server_selection_timeout = Some(std::time::Duration::from_secs(5));

        let client = match Client::with_options(client_options) {
            Ok(c) => c,
            Err(_) => {
                tracing::error!("error creating MongoDB client (details redacted)");
                return false;
            }
        };

        // check database alive or not
        match client.database("admin").run_command(doc! {"ping": 1}).await {
            Ok(_) => {
                tracing::info!("connect to mongodb success!");
                let database = client.database("db_gr_cms_server");

                // device
                let c_device: Collection<CmsDevice> = database.collection("c_device");
                self.c_device = Some(Arc::new(Mutex::new(c_device)));

                // event
                let c_event: Collection<CmsEvent> = database.collection("c_event");
                for index in [
                    IndexModel::builder()
                        .keys(doc! { "event_type": 1, "timestamp": -1 })
                        .build(),
                    IndexModel::builder()
                        .keys(doc! {
                            "event_type": 1,
                            "device_id": 1,
                            "disk_path": 1,
                            "disk_usage": 1,
                            "timestamp": -1,
                        })
                        .build(),
                ] {
                    if let Err(e) = c_event.create_index(index).await {
                        tracing::error!("create c_event telemetry index failed: {}", e);
                        return false;
                    }
                }
                self.c_event = Some(Arc::new(Mutex::new(c_event)));

                // user
                let c_user: Collection<CmsUser> = database.collection("c_user");
                let user_uid_index = IndexModel::builder()
                    .keys(doc! { "uid": 1 })
                    .options(IndexOptions::builder().unique(true).build())
                    .build();
                if let Err(e) = c_user.create_index(user_uid_index).await {
                    tracing::error!("create c_user uid index failed: {}", e);
                    return false;
                }
                let user_name_index = IndexModel::builder()
                    .keys(doc! { "username_normalized": 1 })
                    .options(IndexOptions::builder().unique(true).build())
                    .build();
                if let Err(e) = c_user.create_index(user_name_index).await {
                    tracing::error!("create c_user normalized username index failed: {}", e);
                    return false;
                }
                if let Err(e) = c_user
                    .create_index(
                        IndexModel::builder()
                            .keys(doc! { "deleted": 1, "created_timestamp": -1 })
                            .build(),
                    )
                    .await
                {
                    tracing::error!("create c_user lifecycle index failed: {}", e);
                    return false;
                }
                self.c_user = Some(Arc::new(Mutex::new(c_user)));

                let c_user_session: Collection<CmsUserSession> =
                    database.collection("c_user_session");
                for index in [
                    IndexModel::builder()
                        .keys(doc! { "sid": 1 })
                        .options(IndexOptions::builder().unique(true).build())
                        .build(),
                    IndexModel::builder()
                        .keys(doc! { "token_hash": 1 })
                        .options(IndexOptions::builder().unique(true).build())
                        .build(),
                    IndexModel::builder()
                        .keys(doc! { "cleanup_at": 1 })
                        .options(
                            IndexOptions::builder()
                                .expire_after(std::time::Duration::from_secs(0))
                                .build(),
                        )
                        .build(),
                    IndexModel::builder()
                        .keys(doc! { "subject_type": 1, "subject_id": 1, "revoked_at": 1 })
                        .build(),
                ] {
                    if let Err(e) = c_user_session.create_index(index).await {
                        tracing::error!("create c_user_session index failed: {}", e);
                        return false;
                    }
                }
                self.c_user_session = Some(Arc::new(Mutex::new(c_user_session)));

                let c_guest_block: Collection<GuestBlock> = database.collection("c_guest_block");
                if let Err(e) = c_guest_block
                    .create_index(
                        IndexModel::builder()
                            .keys(doc! { "kind": 1, "value": 1 })
                            .options(IndexOptions::builder().unique(true).build())
                            .build(),
                    )
                    .await
                {
                    tracing::error!("create c_guest_block index failed: {}", e);
                    return false;
                }
                self.c_guest_block = Some(Arc::new(Mutex::new(c_guest_block)));

                let c_user_group: Collection<UserGroup> = database.collection("c_user_group");
                // Older test builds used unique(name_normalized, deleted),
                // which allowed only one deleted generation of a group name.
                // The active-name partial index is the intended soft-delete
                // model; dropping the obsolete index is safe and idempotent.
                let _ = c_user_group.drop_index("name_normalized_1_deleted_1").await;
                for index in [
                    IndexModel::builder()
                        .keys(doc! { "gid": 1 })
                        .options(IndexOptions::builder().unique(true).build())
                        .build(),
                    IndexModel::builder()
                        .keys(doc! { "name_normalized": 1 })
                        .options(
                            IndexOptions::builder()
                                .name("uniq_active_group_name".to_string())
                                .unique(true)
                                .partial_filter_expression(doc! { "deleted": false })
                                .build(),
                        )
                        .build(),
                ] {
                    if let Err(e) = c_user_group.create_index(index).await {
                        tracing::error!("create c_user_group index failed: {}", e);
                        return false;
                    }
                }
                self.c_user_group = Some(Arc::new(Mutex::new(c_user_group)));

                let c_user_group_member: Collection<UserGroupMember> =
                    database.collection("c_user_group_member");
                for index in [
                    IndexModel::builder()
                        .keys(doc! { "uid": 1, "gid": 1 })
                        .options(IndexOptions::builder().unique(true).build())
                        .build(),
                    IndexModel::builder()
                        .keys(doc! { "gid": 1, "uid": 1 })
                        .build(),
                ] {
                    if let Err(e) = c_user_group_member.create_index(index).await {
                        tracing::error!("create c_user_group_member index failed: {}", e);
                        return false;
                    }
                }
                self.c_user_group_member = Some(Arc::new(Mutex::new(c_user_group_member)));

                let c_group_device_grant: Collection<GroupDeviceGrant> =
                    database.collection("c_group_device_grant");
                for index in [
                    IndexModel::builder()
                        .keys(doc! { "gid": 1, "device_id": 1 })
                        .options(IndexOptions::builder().unique(true).build())
                        .build(),
                    IndexModel::builder()
                        .keys(doc! { "device_id": 1, "gid": 1 })
                        .build(),
                ] {
                    if let Err(e) = c_group_device_grant.create_index(index).await {
                        tracing::error!("create c_group_device_grant index failed: {}", e);
                        return false;
                    }
                }
                self.c_group_device_grant = Some(Arc::new(Mutex::new(c_group_device_grant)));

                let c_group_app_grant: Collection<GroupAppGrant> =
                    database.collection("c_group_app_grant");
                for index in [
                    IndexModel::builder()
                        .keys(doc! { "gid": 1, "app_id": 1 })
                        .options(IndexOptions::builder().unique(true).build())
                        .build(),
                    IndexModel::builder()
                        .keys(doc! { "app_id": 1, "gid": 1 })
                        .build(),
                ] {
                    if let Err(e) = c_group_app_grant.create_index(index).await {
                        tracing::error!("create c_group_app_grant index failed: {}", e);
                        return false;
                    }
                }
                self.c_group_app_grant = Some(Arc::new(Mutex::new(c_group_app_grant)));

                let c_connection_ticket: Collection<ConnectionTicket> =
                    database.collection("c_connection_ticket");
                for index in [
                    IndexModel::builder()
                        .keys(doc! { "ticket_hash": 1 })
                        .options(IndexOptions::builder().unique(true).build())
                        .build(),
                    IndexModel::builder()
                        .keys(doc! { "cleanup_at": 1 })
                        .options(
                            IndexOptions::builder()
                                .expire_after(std::time::Duration::from_secs(0))
                                .build(),
                        )
                        .build(),
                    IndexModel::builder()
                        .keys(doc! { "session_id": 1, "consumed_at": 1 })
                        .build(),
                ] {
                    if let Err(e) = c_connection_ticket.create_index(index).await {
                        tracing::error!("create c_connection_ticket index failed: {}", e);
                        return false;
                    }
                }
                self.c_connection_ticket = Some(Arc::new(Mutex::new(c_connection_ticket)));

                // stream
                let c_stream: Collection<CmsStream> = database.collection("c_stream");
                self.c_stream = Some(Arc::new(Mutex::new(c_stream)));

                // record: visit
                let c_visit: Collection<CmsVisit> = database.collection("c_visit");
                let visit_index = IndexModel::builder()
                    .keys(doc! { "conn_id": 1 })
                    .options(IndexOptions::builder().unique(true).build())
                    .build();
                if let Err(e) = c_visit.create_index(visit_index).await {
                    tracing::warn!("create visit conn_id index failed: {}", e);
                }
                self.c_visit = Some(Arc::new(Mutex::new(c_visit)));

                // record: file transfer
                let c_file_transfer: Collection<CmsFileTransfer> =
                    database.collection("c_file_transfer");
                let ft_index = IndexModel::builder()
                    .keys(doc! { "the_file_id": 1 })
                    .options(IndexOptions::builder().unique(true).build())
                    .build();
                if let Err(e) = c_file_transfer.create_index(ft_index).await {
                    tracing::warn!("create file_transfer the_file_id index failed: {}", e);
                }
                self.c_file_transfer = Some(Arc::new(Mutex::new(c_file_transfer)));

                // record: render records cache
                let c_records: Collection<CmsRenderRecord> = database.collection("c_records");
                let rec_index = IndexModel::builder()
                    .keys(doc! { "id": 1 })
                    .options(IndexOptions::builder().unique(true).build())
                    .build();
                if let Err(e) = c_records.create_index(rec_index).await {
                    tracing::warn!("create c_records id index failed: {}", e);
                }
                self.c_records = Some(Arc::new(Mutex::new(c_records)));

                let c_user_device: Collection<CmsUserDevice> = database.collection("c_user_device");
                for index in [
                    IndexModel::builder()
                        .keys(doc! { "uid": 1, "device_id": 1 })
                        .options(IndexOptions::builder().unique(true).build())
                        .build(),
                    IndexModel::builder()
                        .keys(doc! { "device_id": 1, "uid": 1 })
                        .build(),
                ] {
                    if let Err(e) = c_user_device.create_index(index).await {
                        tracing::error!("create c_user_device index failed: {}", e);
                        return false;
                    }
                }
                self.c_user_device = Some(Arc::new(Mutex::new(c_user_device)));

                let c_client_conn: Collection<CmsClientConnVo> =
                    database.collection("c_client_conn");
                self.c_client_conn = Some(Arc::new(Mutex::new(c_client_conn)));

                let c_update_info: Collection<UpdateInfo> = database.collection("c_update_info");
                self.c_update_info = Some(Arc::new(Mutex::new(c_update_info)));

                let c_app: Collection<Application> = database.collection("c_app");
                let app_index = IndexModel::builder()
                    .keys(doc! { "app_id": 1 })
                    .options(IndexOptions::builder().unique(true).build())
                    .build();
                if let Err(e) = c_app.create_index(app_index).await {
                    tracing::error!("create c_app app_id index failed: {}", e);
                    return false;
                }
                if let Err(e) = c_app
                    .create_index(
                        IndexModel::builder()
                            .keys(doc! { "access_mode": 1, "name": 1 })
                            .build(),
                    )
                    .await
                {
                    tracing::error!("create c_app access index failed: {}", e);
                    return false;
                }
                self.c_app = Some(Arc::new(Mutex::new(c_app)));

                let c_app_placement: Collection<AppPlacement> =
                    database.collection("c_app_placement");
                let plc_index = IndexModel::builder()
                    .keys(doc! { "placement_id": 1 })
                    .options(IndexOptions::builder().unique(true).build())
                    .build();
                if let Err(e) = c_app_placement.create_index(plc_index).await {
                    tracing::warn!("create c_app_placement index failed: {}", e);
                }
                self.c_app_placement = Some(Arc::new(Mutex::new(c_app_placement)));

                let c_app_node: Collection<AppNode> = database.collection("c_app_node");
                let node_index = IndexModel::builder()
                    .keys(doc! { "node_id": 1 })
                    .options(IndexOptions::builder().unique(true).build())
                    .build();
                if let Err(e) = c_app_node.create_index(node_index).await {
                    tracing::warn!("create c_app_node index failed: {}", e);
                }
                self.c_app_node = Some(Arc::new(Mutex::new(c_app_node)));

                let c_app_instance: Collection<AppInstance> = database.collection("c_app_instance");
                for index in [
                    IndexModel::builder()
                        .keys(doc! { "instance_id": 1 })
                        .options(IndexOptions::builder().unique(true).build())
                        .build(),
                    IndexModel::builder()
                        .keys(doc! { "owner_session_id": 1, "app_id": 1, "client_nonce": 1 })
                        .options(
                            IndexOptions::builder()
                                .unique(true)
                                .partial_filter_expression(doc! {
                                    "owner_type": { "$in": ["user", "guest"] },
                                    "state": { "$in": ["starting", "running", "stopping"] }
                                })
                                .build(),
                        )
                        .build(),
                    IndexModel::builder()
                        .keys(doc! { "owner_type": 1, "owner_id": 1, "state": 1 })
                        .build(),
                ] {
                    if let Err(e) = c_app_instance.create_index(index).await {
                        tracing::error!("create c_app_instance index failed: {}", e);
                        return false;
                    }
                }
                self.c_app_instance = Some(Arc::new(Mutex::new(c_app_instance)));

                true
            }
            Err(_) => {
                tracing::error!("error connecting to MongoDB (details redacted)");
                false
            }
        }
    }

    pub fn device(&self) -> Arc<Mutex<Collection<CmsDevice>>> {
        self.c_device.clone().unwrap()
    }

    pub fn event(&self) -> Arc<Mutex<Collection<CmsEvent>>> {
        self.c_event.clone().unwrap()
    }

    pub fn user(&self) -> Arc<Mutex<Collection<CmsUser>>> {
        self.c_user.clone().unwrap()
    }

    pub fn user_session(&self) -> Arc<Mutex<Collection<CmsUserSession>>> {
        self.c_user_session.clone().unwrap()
    }

    pub fn guest_block(&self) -> Arc<Mutex<Collection<GuestBlock>>> {
        self.c_guest_block.clone().unwrap()
    }

    pub fn user_group(&self) -> Arc<Mutex<Collection<UserGroup>>> {
        self.c_user_group.clone().unwrap()
    }

    pub fn user_group_member(&self) -> Arc<Mutex<Collection<UserGroupMember>>> {
        self.c_user_group_member.clone().unwrap()
    }

    pub fn group_device_grant(&self) -> Arc<Mutex<Collection<GroupDeviceGrant>>> {
        self.c_group_device_grant.clone().unwrap()
    }

    pub fn group_app_grant(&self) -> Arc<Mutex<Collection<GroupAppGrant>>> {
        self.c_group_app_grant.clone().unwrap()
    }

    pub fn connection_ticket(&self) -> Arc<Mutex<Collection<ConnectionTicket>>> {
        self.c_connection_ticket.clone().unwrap()
    }

    pub fn stream(&self) -> Arc<Mutex<Collection<CmsStream>>> {
        self.c_stream.clone().unwrap()
    }

    pub fn visit(&self) -> Arc<Mutex<Collection<CmsVisit>>> {
        self.c_visit.clone().unwrap()
    }

    pub fn file_transfer(&self) -> Arc<Mutex<Collection<CmsFileTransfer>>> {
        self.c_file_transfer.clone().unwrap()
    }

    pub fn records(&self) -> Arc<Mutex<Collection<CmsRenderRecord>>> {
        self.c_records.clone().unwrap()
    }

    pub fn user_device(&self) -> Arc<Mutex<Collection<CmsUserDevice>>> {
        self.c_user_device.clone().unwrap()
    }

    pub fn client_conn(&self) -> Arc<Mutex<Collection<CmsClientConnVo>>> {
        self.c_client_conn.clone().unwrap()
    }

    pub fn update_info(&self) -> Arc<Mutex<Collection<UpdateInfo>>> {
        self.c_update_info.clone().unwrap()
    }
}
