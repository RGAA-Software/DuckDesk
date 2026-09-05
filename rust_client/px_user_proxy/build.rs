fn main() {
    let manifest_dir =
        std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
    let proto_dir = manifest_dir.join("../../src/px_deps/px_message");
    let rp_proto = proto_dir.join("px_render_panel_message.proto");
    let px_proto = proto_dir.join("px_message.proto");
    println!("cargo:rerun-if-changed={}", rp_proto.display());
    println!("cargo:rerun-if-changed={}", px_proto.display());
    println!(
        "cargo:rerun-if-changed={}",
        proto_dir.join("px_file_transfer.proto").display()
    );

    let protoc = protoc_bin_vendored::protoc_bin_path().expect("failed to fetch vendored protoc");
    unsafe {
        std::env::set_var("PROTOC", protoc);
    }

    prost_build::Config::new()
        .compile_protos(
            &[
                rp_proto.to_string_lossy().into_owned(),
                px_proto.to_string_lossy().into_owned(),
            ],
            &[proto_dir.to_string_lossy().into_owned()],
        )
        .expect("failed to compile user-proxy protos");
}
