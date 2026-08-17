use crate::app_schedule::manager::{AppInstance, AppNode, AppPlacement, Application};
use crate::device::cms_device::CmsDevice;
use crate::event::cms_event::CmsEvent;
use crate::gCmsSettings;
use crate::net_client::cms_client_conn::CmsClientConnVo;
use crate::record::cms_file_transfer::CmsFileTransfer;
use crate::record::cms_render_record::CmsRenderRecord;
use crate::record::cms_visit::CmsVisit;
use crate::stream::cms_stream::CmsStream;
use crate::update::update_info::UpdateInfo;
use crate::user::cms_user::CmsUser;
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
        tracing::info!("mongo uri: {}, will connect it!", uri);

        let mut client_options = match ClientOptions::parse(&uri).await {
            Ok(opts) => opts,
            Err(e) => {
                tracing::error!("error parsing MongoDB URI: {}", e);
                return false;
            }
        };
        client_options.connect_timeout = Some(std::time::Duration::from_secs(5));
        client_options.server_selection_timeout = Some(std::time::Duration::from_secs(5));

        let client = match Client::with_options(client_options) {
            Ok(c) => c,
            Err(e) => {
                tracing::error!("error creating MongoDB client: {}", e);
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
                self.c_event = Some(Arc::new(Mutex::new(c_event)));

                // user
                let c_user: Collection<CmsUser> = database.collection("c_user");
                self.c_user = Some(Arc::new(Mutex::new(c_user)));

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

                let c_user_device: Collection<CmsUserDevice> =
                    database.collection("c_user_device");
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
                    tracing::warn!("create c_app app_id index failed: {}", e);
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
                let inst_index = IndexModel::builder()
                    .keys(doc! { "instance_id": 1 })
                    .options(IndexOptions::builder().unique(true).build())
                    .build();
                if let Err(e) = c_app_instance.create_index(inst_index).await {
                    tracing::warn!("create c_app_instance index failed: {}", e);
                }
                self.c_app_instance = Some(Arc::new(Mutex::new(c_app_instance)));

                true
            }
            Err(e) => {
                tracing::error!("error connecting to MongoDB: {}", e);
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
