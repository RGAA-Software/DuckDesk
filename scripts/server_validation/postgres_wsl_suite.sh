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
export PROTOC=/usr/bin/protoc

if [[ -n "${PIXELS_TEST_SERVER_CANDIDATE:-}" ]]; then
    python3 "$PIXELS_TEST_SERVER_CANDIDATE/tools/verify_candidate.py" "$PIXELS_TEST_SERVER_CANDIDATE"
    export PIXELS_TEST_CONSOLE_BINARY="$PIXELS_TEST_SERVER_CANDIDATE/bin/px_console"
    export PIXELS_TEST_CONSOLE_STATIC_DIRECTORY="$PIXELS_TEST_SERVER_CANDIDATE/static/console"
    export PIXELS_TEST_DESK_BINARY="$PIXELS_TEST_SERVER_CANDIDATE/bin/px_desk"
    export PIXELS_TEST_DESK_STATIC_DIRECTORY="$PIXELS_TEST_SERVER_CANDIDATE/static/desk"
    export PIXELS_TEST_RELAY_BINARY="$PIXELS_TEST_SERVER_CANDIDATE/bin/px_relay"
    PIXELS_DATABASE_URL="$PIXELS_TEST_CONSOLE_RUNTIME_URL" PIXELS_PG_LOCAL_DEVELOPMENT=1 \
        "$PIXELS_TEST_SERVER_CANDIDATE/bin/px_db" check console
    PIXELS_DATABASE_URL="$PIXELS_TEST_DESK_RUNTIME_URL" PIXELS_PG_LOCAL_DEVELOPMENT=1 \
        "$PIXELS_TEST_SERVER_CANDIDATE/bin/px_db" check desk
fi

case "$focused_suite" in
    console-process)
        cargo test --offline --locked --manifest-path "$manifest_path" \
            -p px_console_runtime --features pg-integration --test process \
            --target-dir "$target_directory" -- --test-threads=1
        if [[ -n "${PIXELS_TEST_SERVER_CANDIDATE:-}" ]]; then
            cargo build --offline --locked --manifest-path "$manifest_path" \
                -p px_console_runtime --example private_console_fixture --target-dir "$target_directory"
            test -x "$target_directory/release/examples/private_console_fixture"
            echo "FIXTURE_BINARY=$target_directory/release/examples/private_console_fixture"
        fi
        test -x "$target_directory/release/px_console"
        sha256sum "$target_directory/release/px_console"
        if [[ -n "${PIXELS_TEST_SERVER_CANDIDATE:-}" ]]; then
            sha256sum "$PIXELS_TEST_CONSOLE_BINARY"
        fi
        ;;
    desk)
        cargo test --offline --locked --manifest-path "$manifest_path" \
            -p px_desk_server --features pg-integration --test postgres_api \
            --target-dir "$target_directory" -- --test-threads=1
        test -x "$target_directory/release/px_desk"
        sha256sum "$target_directory/release/px_desk"
        if [[ -n "${PIXELS_TEST_SERVER_CANDIDATE:-}" ]]; then
            sha256sum "$PIXELS_TEST_DESK_BINARY"
        fi
        ;;
    relay-control)
        cargo test --offline --locked --manifest-path "$manifest_path" \
            -p px_console_runtime --features pg-integration --test relay_control \
            --target-dir "$target_directory" -- --test-threads=1
        cargo build --offline --locked --manifest-path "$manifest_path" \
            -p px_relay_server --bin px_relay --target-dir "$target_directory"
        test -x "$target_directory/release/px_relay"
        sha256sum "$target_directory/release/px_relay"
        if [[ -n "${PIXELS_TEST_SERVER_CANDIDATE:-}" ]]; then
            sha256sum "$PIXELS_TEST_RELAY_BINARY"
        fi
        ;;
    *)
        echo "unsupported focused Linux suite: $focused_suite" >&2
        exit 2
        ;;
esac
