use std::fs;
use std::path::Path;

fn copy_required(source: &str, destination: &Path) {
    if !Path::new(source).is_file() {
        panic!("required CMS media artifact is missing: {source}");
    }
    fs::copy(source, destination).unwrap_or_else(|error| {
        panic!("failed to copy {source} to {}: {error}", destination.display())
    });
}

fn main() {
    println!("cargo:rerun-if-changed=src/px_cms.toml");
    println!("cargo:rerun-if-changed=media/px_media.exe");
    println!("cargo:rerun-if-changed=media/config.ini");
    #[cfg(windows)]
    {
        let mut res = winres::WindowsResource::new();
        res.set_icon("assets/logo.ico");
        res.compile().unwrap();
    }

    // save folder with exe
    let src_path = "src/px_cms.toml";
    let out_dir = std::env::var("OUT_DIR").unwrap();
    let dest_folder = Path::new(&out_dir)
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .parent()
        .unwrap();

    let dest_path = dest_folder.join("px_cms.toml");
    if let Err(e) = fs::copy(src_path, dest_path) {
        eprintln!("copy settings failed: {}", e);
    }

    // ZLMediaKit is a fixed, prebuilt CMS sidecar.  Keep its executable and
    // default configuration beside px_cms.exe so Cargo builds are directly
    // runnable and the package script can deploy the exact same layout.
    copy_required("media/px_media.exe", &dest_folder.join("px_media.exe"));
    copy_required("media/config.ini", &dest_folder.join("config.ini"));

    // make certs if needed
    let certs_folder = dest_folder.join("certs");
    builder::create_dir_if_not_exists(certs_folder.to_str().unwrap()).unwrap();

    // cert.pem
    let cert_path = certs_folder.clone().join("cert.pem");
    if let Err(e) = fs::copy("../../certs/cert.pem", cert_path) {
        eprintln!("copy settings failed: {}", e);
    }

    // key.pem
    let key_path = certs_folder.clone().join("key.pem");
    if let Err(e) = fs::copy("../../certs/key.pem", key_path) {
        eprintln!("copy settings failed: {}", e);
    }

    // root folder in RustRover IDE
    let src_path = "src/px_cms.toml";
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
        .join("px_cms.toml");

    if let Err(e) = fs::copy(src_path, dest_path) {
        eprintln!("copy settings failed: {}", e);
    }

    //remove web folder
    let web_folder = dest_folder.join("web");
    let _ = builder::delete_dir_if_exists(web_folder.to_str().unwrap());

    // make the folder
    let _ = builder::create_dir_if_not_exists(web_folder.to_str().unwrap());

    // copy it
    builder::copy_dir_all("../web", web_folder).unwrap();
}
