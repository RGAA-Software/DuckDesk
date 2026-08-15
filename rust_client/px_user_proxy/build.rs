fn main() {
    let manifest_dir = std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
    let proto_dir = manifest_dir.join("../../src/px_deps/px_message_new");
    let rp_proto = proto_dir.join("tc_render_panel_message.proto");
    let tc_proto = proto_dir.join("tc_message.proto");
    println!("cargo:rerun-if-changed={}", rp_proto.display());
    println!("cargo:rerun-if-changed={}", tc_proto.display());

    let protoc = protoc_bin_vendored::protoc_bin_path().expect("failed to fetch vendored protoc");
    unsafe {
        std::env::set_var("PROTOC", protoc);
    }

    prost_build::Config::new()
        .compile_protos(
            &[rp_proto.to_string_lossy().into_owned(), tc_proto.to_string_lossy().into_owned()],
            &[proto_dir.to_string_lossy().into_owned()],
        )
        .expect("failed to compile user-proxy protos");
}
