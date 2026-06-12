use std::sync::Arc;
use mongodb::{Client, Collection};
use mongodb::bson::doc;
use mongodb::options::ClientOptions;
use tokio::sync::Mutex;
use crate::gSpvrSettings;
use crate::device::spvr_device::SpvrDevice;
use crate::record::spvr_file_transfer::SpvrFileTransfer;
use crate::record::spvr_visit::SpvrVisit;
use crate::stream::spvr_stream::SpvrStream;
use crate::user::spvr_user::SpvrUser;
use crate::event::spvr_event::SpvrEvent;
use crate::net_client::spvr_client_conn::SpvrClientConnVo;
use crate::update::update_info::UpdateInfo;
use crate::user_device::spvr_user_device::SpvrUserDevice;

#[derive(Default)]
pub struct SpvrDatabase {
    pub client: Option<Arc<Mutex<Client>>>,
    // device
    pub c_device: Option<Arc<Mutex<Collection<SpvrDevice>>>>,
    // event
    pub c_event: Option<Arc<Mutex<Collection<SpvrEvent>>>>,
    // user
    pub c_user: Option<Arc<Mutex<Collection<SpvrUser>>>>,
    // stream
    pub c_stream: Option<Arc<Mutex<Collection<SpvrStream>>>>,
    // record: visit
    pub c_visit: Option<Arc<Mutex<Collection<SpvrVisit>>>>,
    // record: file transfer
    pub c_file_transfer: Option<Arc<Mutex<Collection<SpvrFileTransfer>>>>,
    // user device relationship
    pub c_user_device: Option<Arc<Mutex<Collection<SpvrUserDevice>>>>,
    // spvr conn; use adapter
    pub c_client_conn: Option<Arc<Mutex<Collection<SpvrClientConnVo>>>>,
    // update
    pub c_update_info: Option<Arc<Mutex<Collection<UpdateInfo>>>>,
}

impl SpvrDatabase {
    pub fn new() -> Self {
        Self::default()
    }

    pub async fn init(&mut self) -> bool {
        let uri = gSpvrSettings.lock().await.mongodb_url.clone();
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
                let database = client.database("db_gr_spvr");

                // device
                let c_device: Collection<SpvrDevice> = database.collection("c_device");
                self.c_device = Some(Arc::new(Mutex::new(c_device)));

                // event
                let c_event: Collection<SpvrEvent> = database.collection("c_event");
                self.c_event = Some(Arc::new(Mutex::new(c_event)));

                // user
                let c_user: Collection<SpvrUser> = database.collection("c_user");
                self.c_user = Some(Arc::new(Mutex::new(c_user)));

                // stream
                let c_stream: Collection<SpvrStream> = database.collection("c_stream");
                self.c_stream = Some(Arc::new(Mutex::new(c_stream)));

                // record: visit
                let c_visit: Collection<SpvrVisit> = database.collection("c_visit");
                self.c_visit = Some(Arc::new(Mutex::new(c_visit)));

                // record: file transfer
                let c_file_transfer: Collection<SpvrFileTransfer> = database.collection("c_file_transfer");
                self.c_file_transfer = Some(Arc::new(Mutex::new(c_file_transfer)));

                let c_user_device: Collection<SpvrUserDevice> = database.collection("c_user_device");
                self.c_user_device = Some(Arc::new(Mutex::new(c_user_device)));

                let c_client_conn: Collection<SpvrClientConnVo> = database.collection("c_client_conn");
                self.c_client_conn = Some(Arc::new(Mutex::new(c_client_conn)));

                let c_update_info: Collection<UpdateInfo> = database.collection("c_update_info");
                self.c_update_info = Some(Arc::new(Mutex::new(c_update_info)));

                true
            }
            Err(e) => {
                tracing::error!("error connecting to MongoDB: {}", e);
                false
            }
        }
    }

    pub fn device(&self) -> Arc<Mutex<Collection<SpvrDevice>>> {
        self.c_device.clone().unwrap()
    }

    pub fn event(&self) -> Arc<Mutex<Collection<SpvrEvent>>> {
        self.c_event.clone().unwrap()
    }

    pub fn user(&self) -> Arc<Mutex<Collection<SpvrUser>>> {
        self.c_user.clone().unwrap()
    }

    pub fn stream(&self) -> Arc<Mutex<Collection<SpvrStream>>> {
        self.c_stream.clone().unwrap()
    }

    pub fn visit(&self) -> Arc<Mutex<Collection<SpvrVisit>>> {
        self.c_visit.clone().unwrap()
    }

    pub fn file_transfer(&self) -> Arc<Mutex<Collection<SpvrFileTransfer>>> {
        self.c_file_transfer.clone().unwrap()
    }

    pub fn user_device(&self) -> Arc<Mutex<Collection<SpvrUserDevice>>> {
        self.c_user_device.clone().unwrap()
    }

    pub fn client_conn(&self) -> Arc<Mutex<Collection<SpvrClientConnVo>>> {
        self.c_client_conn.clone().unwrap()
    }

    pub fn update_info(&self) -> Arc<Mutex<Collection<UpdateInfo>>> {
        self.c_update_info.clone().unwrap()
    }

}