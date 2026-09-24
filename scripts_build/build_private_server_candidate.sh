#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "usage: build_private_server_candidate.sh <new-output-directory> [verified-pg-toolchain-directory]" >&2
    exit 2
fi

source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
candidate_output="$1"
pg_toolchain_arguments=()
if [[ $# -eq 2 ]]; then
    pg_toolchain_arguments=(--pg-toolchain "$2")
fi
if [[ -f "$HOME/.cargo/env" ]]; then
    source "$HOME/.cargo/env"
fi
if [[ -e "$candidate_output" ]]; then
    echo "Candidate output already exists: $candidate_output" >&2
    exit 2
fi

export SQLX_OFFLINE=true
export PROTOC=/usr/bin/protoc
export CARGO_TARGET_DIR=/tmp/pixels-private-server-target
export CARGO_PROFILE_RELEASE_OPT_LEVEL=1
export CARGO_PROFILE_RELEASE_INCREMENTAL=true
export CARGO_PROFILE_RELEASE_CODEGEN_UNITS=256

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
    --desk "$CARGO_TARGET_DIR/release/px_desk" \
    --console-static "$source_root/web/px_console/dist" \
    --desk-static "$source_root/web/px_pixels/dist" \
    "${pg_toolchain_arguments[@]}" \
    --output "$candidate_output"
