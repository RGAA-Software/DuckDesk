use mongodb::bson::doc;
use mongodb::options::ClientOptions;
use mongodb::Client;

pub async fn check_mongodb_available(uri: String) -> bool {
    let mut client_options = match ClientOptions::parse(&uri).await {
        Ok(opts) => opts,
        Err(parse_error) => {
            tracing::error!("error parsing MongoDB URI: {}", parse_error);
            return false;
        }
    };
    client_options.connect_timeout = Some(std::time::Duration::from_secs(5));
    client_options.server_selection_timeout = Some(std::time::Duration::from_secs(5));

    let client = match Client::with_options(client_options) {
        Ok(client) => client,
        Err(client_error) => {
            tracing::error!("error creating MongoDB client: {}", client_error);
            return false;
        }
    };

    // check database alive or not
    match client.database("admin").run_command(doc! {"ping": 1}).await {
        Ok(_) => true,
        Err(_) => false,
    }
}
