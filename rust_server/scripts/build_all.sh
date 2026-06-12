#!/usr/bin/env bash
set -e

# Build all programs in the rust_server workspace.
# Run this script from anywhere; it will switch to the workspace root.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

echo "Building rust_server workspace ..."
cargo build --workspace --release

echo ""
echo "Build succeeded. Binaries are in target/release/"
