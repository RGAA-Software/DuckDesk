#![cfg_attr(not(test), windows_subsystem = "windows")]

use px_user_proxy::app::UserProxyApp;

#[tokio::main]
async fn main() {
    match UserProxyApp::bootstrap() {
        Ok(app) => {
            if let Err(err) = app.run().await {
                eprintln!("px_function failed: {err:#}");
                std::process::exit(1);
            }
        }
        Err(err) => {
            eprintln!("px_function bootstrap failed: {err:#}");
            std::process::exit(1);
        }
    }
}
