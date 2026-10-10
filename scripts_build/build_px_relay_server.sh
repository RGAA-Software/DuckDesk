#!/usr/bin/env bash
set -euo pipefail

# Focused development build; no version bump or installable product package.
repository_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
target_root="${PIXELS_RELAY_TARGET_DIR:-${repository_root}/.cache/relay-dev-linux}"
output_root="${PIXELS_RELAY_OUTPUT_DIR:-${repository_root}/output/px_relay/dev-linux}"

command -v cargo >/dev/null
command -v sha256sum >/dev/null
export SQLX_OFFLINE=true
export PROTOC="${PROTOC:-$(command -v protoc)}"
export CARGO_PROFILE_RELEASE_OPT_LEVEL="${CARGO_PROFILE_RELEASE_OPT_LEVEL:-1}"
export CARGO_PROFILE_RELEASE_INCREMENTAL=true
cargo build --locked --release --manifest-path "${repository_root}/rust_server/Cargo.toml" \
    -p px_relay_server --bin px_relay --target-dir "${target_root}"
mkdir -p -- "${output_root}"
install -m 755 -- "${target_root}/release/px_relay" "${output_root}/px_relay"
source_hash="$(sha256sum "${target_root}/release/px_relay" | cut -d ' ' -f 1)"
output_hash="$(sha256sum "${output_root}/px_relay" | cut -d ' ' -f 1)"
if [[ "${source_hash}" != "${output_hash}" ]]; then
    echo 'ERROR: px_relay hash mismatch' >&2
    exit 1
fi
printf 'HASH OK px_relay %s\nRelay development build: %s\n' "${output_hash}" "${output_root}"
