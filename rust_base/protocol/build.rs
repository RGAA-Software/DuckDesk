use std::path::Path;

fn main() {
    let manifest_dir = Path::new(env!("CARGO_MANIFEST_DIR"));
    let protoc_path = manifest_dir.join("../../tools/protoc.exe");
    unsafe {
        std::env::set_var("PROTOC", &protoc_path);
    }

    let proto_dir = manifest_dir.join("../../src/gr_deps/tc_server_protocol");

    tonic_prost_build::configure()
        .build_server(true)
        .out_dir("src/")
        .compile_protos(&[
            proto_dir.join("grpc_relay.proto").to_str().unwrap(),
        ], &[proto_dir.to_str().unwrap()])
        .expect("Failed to compile grpc_relay.proto");

    tonic_prost_build::configure()
        .build_server(false)
        .out_dir("src/")
        .compile_protos(&[
            proto_dir.join("relay_message.proto").to_str().unwrap(),
            proto_dir.join("spvr_relay.proto").to_str().unwrap(),
            proto_dir.join("spvr_panel.proto").to_str().unwrap(),
            proto_dir.join("spvr_client.proto").to_str().unwrap(),
        ], &[proto_dir.to_str().unwrap()])
        .expect("Failed to compile proto files");
}
