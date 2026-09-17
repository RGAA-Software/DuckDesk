#!/usr/bin/env bash
set -euo pipefail
[[ "${PIXELS_PG_ISOLATED_TEST:-}" == 1 ]] || { echo 'Isolated harness required' >&2; exit 1; }
repo="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
target="${XDG_CACHE_HOME:-${HOME}/.cache}/pixels-pg-cargo"
manifest="$repo/rust_server/Cargo.toml"
rustc --version
cargo --version
uname -sm
export SQLX_OFFLINE=true
cargo test --offline --locked --manifest-path "$manifest" -p px_release_catalog --lib --target-dir "$target"
cargo test --offline --locked --manifest-path "$manifest" -p px_private_files --features integration-probe --test cache_files --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_backup --all-targets --target-dir "$target"
cargo test --offline --locked --manifest-path "$manifest" -p px_backup --features pg-integration --test postgres --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_license --test contract --target-dir "$target"
cargo test --offline --locked --manifest-path "$manifest" -p px_pg --lib --target-dir "$target"
cargo test --offline --locked --manifest-path "$manifest" -p px_pg --features pg-integration --test postgres --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_pg --features pg-integration --test lease --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_pg --features pg-integration --test schema_gate --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --lib --target-dir "$target"
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test identity --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test accounts --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_runtime --lib --target-dir "$target"
cargo test --offline --locked --manifest-path "$manifest" -p px_node_protocol --lib --target-dir "$target"
cargo test --offline --locked --manifest-path "$manifest" -p px_console_runtime --features pg-integration --test identity_api --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_runtime --features pg-integration --test directory_api --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_runtime --features pg-integration --test node_control --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_runtime --features pg-integration --test process --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test control --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test devices --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test applications --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test guests --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test nodes --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test deployments --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test instances --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test commands --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test workspaces --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test database --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test sessions --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test transfers --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test recordings --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test preferences --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test cache --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test activity --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_console_store --features pg-integration --test updates --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_desk_server --features pg-integration --test postgres_api --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_auth_store --features pg-integration --test issuance --target-dir "$target" -- --test-threads=1
cargo test --offline --locked --manifest-path "$manifest" -p px_credentials --lib --target-dir "$target"
cargo test --offline --locked --manifest-path "$manifest" -p px_auth_server --features pg-integration --test postgres_api --target-dir "$target" -- --test-threads=1
cargo build --offline --locked --manifest-path "$manifest" -p px_pg --bin px_db --target-dir "$target"
for service in console auth desk; do
    key="PIXELS_TEST_${service^^}_RUNTIME_URL"
    export PIXELS_DATABASE_URL="${!key}"
    "$target/debug/px_db" check "$service"
done
sha256sum "$target/debug/px_db"
sha256sum "$target/debug/px_desk"
sha256sum "$target/debug/px_auth"
sha256sum "$target/debug/px_auth_admin"
sha256sum "$target/debug/px_cache_probe"
