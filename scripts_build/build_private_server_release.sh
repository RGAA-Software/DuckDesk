#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 || "$1" != /* || ! "$2" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    echo "usage: build_private_server_release.sh <new-absolute-output-directory> <suite-version>" >&2
    exit 2
fi

source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
release_output=$1
suite_version=$2
if [[ -f "$HOME/.cargo/env" ]]; then
    source "$HOME/.cargo/env"
fi
if [[ -e "$release_output" || -e "$release_output.tar.gz" ]]; then
    echo "Formal Server release output already exists" >&2
    exit 2
fi
toolchain_workspace=$(mktemp -d -t pixels-private-release-toolchain-XXXXXXXX)
cleanup() {
    cleanup_status=$?
    if [[ -d "$toolchain_workspace" && ! -L "$toolchain_workspace" ]]; then
        resolved_workspace=$(realpath -e -- "$toolchain_workspace")
        case "$resolved_workspace" in /tmp/pixels-private-release-toolchain-*) rm -r -- "$resolved_workspace" ;; esac
    fi
    trap - EXIT
    exit "$cleanup_status"
}
trap cleanup EXIT
bash "$source_root/scripts_build/build_postgresql_client_toolchain.sh" "$toolchain_workspace/postgresql-18"

export SQLX_OFFLINE=true
export PROTOC=/usr/bin/protoc
export CARGO_TARGET_DIR=/tmp/pixels-private-server-release-target
export CARGO_PROFILE_RELEASE_OPT_LEVEL=3
export CARGO_PROFILE_RELEASE_LTO=fat
export CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1
export CARGO_PROFILE_RELEASE_INCREMENTAL=false

cd "$source_root/rust_server"
cargo build --locked --release \
    -p px_console_runtime --bin px_console --bin px_console_admin \
    -p px_pg --bin px_db \
    -p px_relay_server --bin px_relay \
    -p px_desk_server --bin px_desk \
    -p px_backup --bin px_backup

python3 "$source_root/scripts/assemble_private_server_candidate.py" \
    --console "$CARGO_TARGET_DIR/release/px_console" \
    --console-admin "$CARGO_TARGET_DIR/release/px_console_admin" \
    --database-admin "$CARGO_TARGET_DIR/release/px_db" \
    --relay "$CARGO_TARGET_DIR/release/px_relay" \
    --backup "$CARGO_TARGET_DIR/release/px_backup" \
    --pg-toolchain "$toolchain_workspace/postgresql-18" \
    --desk "$CARGO_TARGET_DIR/release/px_desk" \
    --console-static "$source_root/web/px_console/dist" \
    --desk-static "$source_root/web/px_pixels/dist" \
    --suite-version "$suite_version" \
    --output "$release_output"

python3 "$release_output/tools/verify_candidate.py" "$release_output"
tar --sort=name --mtime='@0' --owner=0 --group=0 --numeric-owner \
    -czf "$release_output.tar.gz" -C "$(dirname "$release_output")" "$(basename "$release_output")"
(cd "$(dirname "$release_output")" && sha256sum "$(basename "$release_output").tar.gz" >"$(basename "$release_output").tar.gz.sha256")
echo "Private Server Customer release: $release_output.tar.gz"
