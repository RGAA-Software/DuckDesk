use crate::consult::off_consult::OffConsult;
use crate::issue::off_issue::OffIssue;
use crate::version::off_version::OffVersion;
use mongodb::bson::doc;
use mongodb::options::ClientOptions;
use mongodb::{Client, Collection};
use std::sync::Arc;
use tokio::sync::Mutex;

#[derive(Default)]
pub struct OffDatabase {
    pub client: Option<Arc<Mutex<Client>>>,
    pub c_consult: Option<Arc<Mutex<Collection<OffConsult>>>>,
    pub c_issue: Option<Arc<Mutex<Collection<OffIssue>>>>,
    pub c_version: Option<Arc<Mutex<Collection<OffVersion>>>>,
}

impl OffDatabase {
    pub fn new() -> Arc<Mutex<OffDatabase>> {
        Arc::new(Mutex::new(OffDatabase::default()))
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
                let database = client.database("db_off_site");

                // consult
                let c_consult: Collection<OffConsult> = database.collection("c_consult");
                self.c_consult = Some(Arc::new(Mutex::new(c_consult)));

                // consult
                let c_issue: Collection<OffIssue> = database.collection("c_issue");
                self.c_issue = Some(Arc::new(Mutex::new(c_issue)));

                // version
                let c_version: Collection<OffVersion> = database.collection("c_version");
                self.c_version = Some(Arc::new(Mutex::new(c_version)));

                true
            }
            Err(e) => {
                tracing::error!("error connecting to MongoDB: {}", e);
                false
            }
        }
    }

    pub async fn issue(&mut self) -> Arc<Mutex<Collection<OffIssue>>> {
        self.c_issue.clone().unwrap()
    }

    pub async fn consult(&mut self) -> Arc<Mutex<Collection<OffConsult>>> {
        self.c_consult.clone().unwrap()
    }

    pub async fn version(&mut self) -> Arc<Mutex<Collection<OffVersion>>> {
        self.c_version.clone().unwrap()
    }
}
