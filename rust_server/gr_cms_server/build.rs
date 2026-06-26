use std::path::Path;
use std::{fs, io};

fn main() {
    #[cfg(windows)]
    {
        let mut res = winres::WindowsResource::new();
        res.set_icon("assets/logo.ico");
        res.compile().unwrap();
    }

    // save folder with exe
    let src_path = "src/gr_cms_server_settings.toml";
    let out_dir = std::env::var("OUT_DIR").unwrap();
    let dest_folder = Path::new(&out_dir)
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .parent()
        .unwrap();

    let dest_path = dest_folder.join("gr_cms_server_settings.toml");
    if let Err(e) = fs::copy(src_path, dest_path) {
        eprintln!("copy settings failed: {}", e);
    }

    // make certs if needed
    let certs_folder = dest_folder.join("certs");
    builder::create_dir_if_not_exists(certs_folder.to_str().unwrap()).unwrap();

    // cert.pem
    let cert_path = certs_folder.clone().join("cert.pem");
    if let Err(e) = fs::copy("certs/cert.pem", cert_path) {
        eprintln!("copy settings failed: {}", e);
    }

    // key.pem
    let key_path = certs_folder.clone().join("key.pem");
    if let Err(e) = fs::copy("certs/key.pem", key_path) {
        eprintln!("copy settings failed: {}", e);
    }

    // root folder in RustRover IDE
    let src_path = "src/gr_cms_server_settings.toml";
    let out_dir = std::env::var("OUT_DIR").unwrap();
    let dest_path = Path::new(&out_dir)
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .join("gr_cms_server_settings.toml");

    if let Err(e) = fs::copy(src_path, dest_path) {
        eprintln!("copy settings failed: {}", e);
    }

    // auth folder
    let auth_folder = dest_folder.join("auth");
    builder::create_dir_if_not_exists(auth_folder.to_str().unwrap()).unwrap();

    // auth.info
    let auth_path = auth_folder.clone().join("auth.info");
    if let Err(e) = fs::copy("auth/auth.info", auth_path) {
        eprintln!("** copy auth.info failed: {}", e);
    }

    //remove web folder
    let web_folder = dest_folder.join("web");
    let _ = builder::delete_dir_if_exists(web_folder.to_str().unwrap());

    // make the folder
    let _ = builder::create_dir_if_not_exists(web_folder.to_str().unwrap());

    // copy it
    builder::copy_dir_all("../web", web_folder).unwrap();
}
