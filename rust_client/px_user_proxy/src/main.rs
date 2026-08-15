#![cfg_attr(not(test), windows_subsystem = "windows")]

use px_user_proxy::app::UserProxyApp;

#[tokio::main]
async fn main() {
    match UserProxyApp::bootstrap() {
        Ok(app) => {
            if let Err(err) = app.run().await {
                eprintln!("GammaRayUserProxy failed: {err:#}");
                std::process::exit(1);
            }
        }
        Err(err) => {
            eprintln!("GammaRayUserProxy bootstrap failed: {err:#}");
            std::process::exit(1);
        }
    }
}
