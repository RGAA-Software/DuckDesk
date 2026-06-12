use std::sync::Arc;
use mongodb::{Client, Collection};
use mongodb::bson::doc;
use mongodb::options::ClientOptions;
use tokio::sync::Mutex;
use crate::update_info::UpdateInfo;

#[derive(Default)]
pub struct UpdateDatabase {
    pub client: Option<Arc<Mutex<Client>>>,
    pub c_update_info: Option<Arc<Mutex<Collection<UpdateInfo>>>>,
}

impl UpdateDatabase {
    pub fn new() -> Arc<Mutex<UpdateDatabase>> {
        Arc::new(Mutex::new(UpdateDatabase::default()))
    }

    pub async fn init(&mut self) -> bool {
        let url = "mongodb://localhost:27017/";
        let mut client_options = match ClientOptions::parse(url).await {
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
                let database = client.database("db_update_info");

                // consult
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
    
    pub async fn update_info(&mut self) -> Arc<Mutex<Collection<UpdateInfo>>> {
        self.c_update_info.clone().unwrap()
    }
}