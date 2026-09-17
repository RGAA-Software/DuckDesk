fn main() {
    for path in [
        "../px_console_server/migrations",
        "../px_auth_server/migrations",
        "../px_desk_server/migrations",
    ] {
        println!("cargo:rerun-if-changed={path}");
    }
}
