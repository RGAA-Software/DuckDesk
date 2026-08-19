use std::fs;
use std::path::{Path, PathBuf};

fn copy_required(source: &str, destination: &Path) {
    if !Path::new(source).is_file() {
        panic!("required CMS media artifact is missing: {source}");
    }
    fs::copy(source, destination).unwrap_or_else(|error| {
        panic!(
            "failed to copy {source} to {}: {error}",
            destination.display()
        )
    });
}

fn copy_media_runtime(source: &Path, destination: &Path) {
    for entry in fs::read_dir(source)
        .unwrap_or_else(|error| panic!("failed to list {}: {error}", source.display()))
    {
        let entry =
            entry.unwrap_or_else(|error| panic!("failed to read media runtime entry: {error}"));
        let source_path = entry.path();
        let destination_path: PathBuf = destination.join(entry.file_name());

        if source_path.is_dir() {
            fs::create_dir_all(&destination_path).unwrap_or_else(|error| {
                panic!("failed to create {}: {error}", destination_path.display())
            });
            copy_media_runtime(&source_path, &destination_path);
        } else if source_path.is_file() {
            fs::copy(&source_path, &destination_path).unwrap_or_else(|error| {
                panic!(
                    "failed to copy {} to {}: {error}",
                    source_path.display(),
                    destination_path.display()
                )
            });
        }
    }
}

fn main() {
    println!("cargo:rerun-if-changed=src/px_cms.toml");
    println!("cargo:rerun-if-changed=media/px_media.exe");
    println!("cargo:rerun-if-changed=media/config.ini");
    println!("cargo:rerun-if-changed=media");
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

    // ZLMediaKit is a fixed, prebuilt CMS sidecar. Keep the complete runtime
    // beside px_cms.exe: FFmpeg/SRT/WebRTC/OpenSSL DLLs are dynamically loaded
    // by px_media.exe, so shipping only the executable is not sufficient.
    copy_required("media/px_media.exe", &dest_folder.join("px_media.exe"));
    copy_required("media/config.ini", &dest_folder.join("config.ini"));
    copy_media_runtime(Path::new("media"), dest_folder);

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
