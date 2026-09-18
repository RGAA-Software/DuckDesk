use std::fs;
use std::path::Path;

fn main() {
    println!("cargo:rerun-if-changed=src/px_console.toml");
    println!("cargo:rerun-if-changed=../../web/px_console/dist");
    #[cfg(windows)]
    {
        let mut res = winres::WindowsResource::new();
        res.set_icon("assets/logo.ico");
        res.compile().unwrap();
    }

    // save folder with exe
    let src_path = "src/px_console.toml";
    let out_dir = std::env::var("OUT_DIR").unwrap();
    let dest_folder = Path::new(&out_dir)
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .parent()
        .unwrap();

    let dest_path = dest_folder.join("px_console.toml");
    if let Err(copy_error) = fs::copy(src_path, dest_path) {
        eprintln!("copy settings failed: {}", copy_error);
    }

    // make certs if needed
    let certs_folder = dest_folder.join("certs");
    builder::create_dir_if_not_exists(certs_folder.to_str().unwrap()).unwrap();

    // cert.pem
    let cert_path = certs_folder.clone().join("cert.pem");
    if let Err(copy_error) = fs::copy("../../certs/cert.pem", cert_path) {
        eprintln!("copy settings failed: {}", copy_error);
    }

    // key.pem
    let key_path = certs_folder.clone().join("key.pem");
    if let Err(copy_error) = fs::copy("../../certs/key.pem", key_path) {
        eprintln!("copy settings failed: {}", copy_error);
    }

    // root folder in RustRover IDE
    let src_path = "src/px_console.toml";
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
        .join("px_console.toml");

    if let Err(copy_error) = fs::copy(src_path, dest_path) {
        eprintln!("copy settings failed: {}", copy_error);
    }

    //remove web folder
    let web_folder = dest_folder.join("web");
    let _ = builder::delete_dir_if_exists(web_folder.to_str().unwrap());

    // make the folder
    let _ = builder::create_dir_if_not_exists(web_folder.to_str().unwrap());

    // Copy the Console Web production bundle. `rust_server/web` is a legacy
    // CoDesk bundle and does not contain the current Vue Console.
    builder::copy_dir_all("../../web/px_console/dist", web_folder).unwrap();
}
