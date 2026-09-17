fn main() {
    println!("cargo:rerun-if-changed=../migrations");
    println!("cargo:rerun-if-changed=queries");
    println!("cargo:rerun-if-changed=.sqlx");
    println!("cargo:rerun-if-env-changed=SQLX_OFFLINE");
    println!("cargo:rerun-if-env-changed=SQLX_OFFLINE_DIR");
}
