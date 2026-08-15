fn main() {
    let proto_file = "../../../src/px_deps/px_message_new/tc_service_message.proto";
    println!("cargo:rerun-if-changed={proto_file}");

    let protoc = protoc_bin_vendored::protoc_bin_path().expect("failed to fetch vendored protoc");
    unsafe {
        std::env::set_var("PROTOC", protoc);
    }

    prost_build::Config::new()
        .compile_protos(&[proto_file], &["../../../src/px_deps/px_message_new"])
        .expect("failed to compile tc_service_message.proto");
}
