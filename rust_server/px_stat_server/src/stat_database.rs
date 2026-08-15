use crate::auth::auth_stat::StatAuth;
use crate::using::stat_open_up::StatOpenUp;
use mongodb::bson::doc;
use mongodb::options::ClientOptions;
use mongodb::{Client, Collection};
use std::sync::Arc;
use tokio::sync::Mutex;

#[derive(Default)]
pub struct StatDatabase {
    pub client: Option<Arc<Mutex<Client>>>,
    pub c_auth_stat: Option<Arc<Collection<StatAuth>>>,
    pub c_open_up: Option<Arc<Collection<StatOpenUp>>>,
}

impl StatDatabase {
    pub fn new() -> Arc<Mutex<StatDatabase>> {
        Arc::new(Mutex::new(StatDatabase::default()))
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
                let database = client.database("db_stat");

                // working auth stat
                let c_auth_stat: Collection<StatAuth> = database.collection("c_auth_stat");
                self.c_auth_stat = Some(Arc::new(c_auth_stat));

                // open up
                let c_open_up: Collection<StatOpenUp> = database.collection("c_open_up");
                self.c_open_up = Some(Arc::new(c_open_up));

                true
            }
            Err(e) => {
                tracing::error!("error connecting to MongoDB: {}", e);
                false
            }
        }
    }

    pub fn auth_stat(&mut self) -> Arc<Collection<StatAuth>> {
        self.c_auth_stat.clone().unwrap()
    }

    pub fn open_up(&self) -> Arc<Collection<StatOpenUp>> {
        self.c_open_up.clone().unwrap()
    }
}
