use std::path::Path;

fn main() {
    let manifest_dir = Path::new(env!("CARGO_MANIFEST_DIR"));

    // 1. 优先使用环境变量 PROTOC（如果外部已经指定）
    let protoc_path = if let Ok(path) = std::env::var("PROTOC") {
        Path::new(&path).to_path_buf()
    } else {
        // 2. 从 vcpkg 获取 protoc
        let vcpkg_root =
            std::env::var("VCPKG_ROOT").unwrap_or_else(|_| "C:/source/vcpkg".to_string());
        let vcpkg_triplet = std::env::var("VCPKG_DEFAULT_TRIPLET")
            .unwrap_or_else(|_| "x64-windows-static-release".to_string());

        Path::new(&vcpkg_root)
            .join("installed")
            .join(&vcpkg_triplet)
            .join("tools/protobuf/protoc.exe")
    };

    if !protoc_path.exists() {
        panic!(
            "protoc not found: {:?}. Please install protobuf via vcpkg, e.g.:\n\
             vcpkg install protobuf:x64-windows",
            protoc_path
        );
    }

    unsafe {
        std::env::set_var("PROTOC", &protoc_path);
    }

    let proto_dir = manifest_dir.join("../../src/gr_deps/tc_server_protocol");

    tonic_prost_build::configure()
        .build_server(true)
        .out_dir("src/")
        .compile_protos(
            &[proto_dir.join("grpc_relay.proto").to_str().unwrap()],
            &[proto_dir.to_str().unwrap()],
        )
        .expect("Failed to compile grpc_relay.proto");

    tonic_prost_build::configure()
        .build_server(false)
        .out_dir("src/")
        .compile_protos(
            &[
                proto_dir.join("relay_message.proto").to_str().unwrap(),
                proto_dir.join("spvr_relay.proto").to_str().unwrap(),
                proto_dir.join("spvr_panel.proto").to_str().unwrap(),
                proto_dir.join("spvr_client.proto").to_str().unwrap(),
            ],
            &[proto_dir.to_str().unwrap()],
        )
        .expect("Failed to compile proto files");
}
