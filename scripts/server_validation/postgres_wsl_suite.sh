#!/usr/bin/env bash
set -euo pipefail

cargo() {
    local subcommand="$1"
    shift
    command cargo "$subcommand" --release "$@"
}

[[ "${PIXELS_PG_ISOLATED_TEST:-}" == 1 ]] || { echo 'Isolated harness required' >&2; exit 1; }
[[ "$#" == 1 ]] || { echo 'usage: postgres_wsl_suite.sh <suite>' >&2; exit 2; }

repository_root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
target_directory="${XDG_CACHE_HOME:-${HOME}/.cache}/pixels-pg-cargo"
manifest_path="$repository_root/rust_server/Cargo.toml"
focused_suite=$1
export SQLX_OFFLINE=true

case "$focused_suite" in
    console-process)
        cargo test --offline --locked --manifest-path "$manifest_path" \
            -p px_console_runtime --features pg-integration --test process \
            --target-dir "$target_directory" -- --test-threads=1
        test -x "$target_directory/release/px_console"
        sha256sum "$target_directory/release/px_console"
        ;;
    *)
        echo "unsupported focused Linux suite: $focused_suite" >&2
        exit 2
        ;;
esac
