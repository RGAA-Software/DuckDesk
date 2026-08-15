use std::fs;
use std::path::Path;

fn main() {
    // save folder with exe
    let src_path = "src/gr_auth_server_settings.toml";
    let out_dir = std::env::var("OUT_DIR").unwrap();
    let dest_path = Path::new(&out_dir)
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .join("gr_auth_server_settings.toml");

    if let Err(e) = fs::copy(src_path, dest_path) {
        eprintln!("copy settings failed: {}", e);
    }

    // root folder in RustRover IDE
    let src_path = "src/gr_auth_server_settings.toml";
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
        .join("gr_auth_server_settings.toml");

    if let Err(e) = fs::copy(src_path, dest_path) {
        eprintln!("copy settings failed: {}", e);
    }

    let dest_folder = Path::new(&out_dir)
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .parent()
        .unwrap();

    // remove web folder
    let web_folder = dest_folder.join("web_auth");
    let _ = builder::delete_dir_if_exists(web_folder.to_str().unwrap());

    // create it
    let _ = builder::create_dir_if_not_exists(web_folder.to_str().unwrap());

    //
    builder::copy_dir_all("assets", web_folder).unwrap();
}
