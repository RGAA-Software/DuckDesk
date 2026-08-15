#!/usr/bin/env bash
# Cross-build px_auth_server for Linux (x86_64-unknown-linux-musl, static)
# using zig + cargo-zigbuild. Requires Strawberry perl's pkg-config (shim in .tooling/bin).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"          # rust_server
PROJ_ROOT="$(cd "$WS_ROOT/.." && pwd)"          # GammaRayPremium
TOOLING="$PROJ_ROOT/.tooling"
ZIG="$TOOLING/zig/zig.exe"
CZB="$TOOLING/cargo-zigbuild/cargo-zigbuild.exe"
TARGET=x86_64-unknown-linux-musl
STUB_DIR="$WS_ROOT/target/cross-stubs"

export PATH="$TOOLING/zig:$TOOLING/bin:$PATH"

# --- libudev stub (px_base -> disk-serial-number -> udev -> libudev-sys) ---
# The auth server never uses disk info; a stub libudev satisfies the linker.
if [ ! -f "$STUB_DIR/libudev.a" ]; then
  echo ">> building libudev stub"
  mkdir -p "$STUB_DIR"
  LIBUDEV_SYS=$(ls -d "$HOME"/.cargo/registry/src/*/libudev-sys-0.1.4 2>/dev/null | head -1)
  [ -n "$LIBUDEV_SYS" ] || { echo "libudev-sys source not found; run 'cargo fetch' first"; exit 1; }
  grep -o 'pub fn udev_[a-z_0-9]*' "$LIBUDEV_SYS/src/lib.rs" | sed 's/pub fn //' | sort -u | \
    awk '{print "long " $1 "(void){return 0;}"}' > "$STUB_DIR/stubs.c"
  "$ZIG" cc -target x86_64-linux-musl -O2 -c "$STUB_DIR/stubs.c" -o "$STUB_DIR/stubs.o"
  "$ZIG" ar rcs "$STUB_DIR/libudev.a" "$STUB_DIR/stubs.o"
fi
cat > "$STUB_DIR/libudev.pc" <<EOF
prefix=$(cygpath -m "$STUB_DIR")
libdir=\${prefix}
includedir=\${prefix}

Name: libudev
Description: stub for cross build
Version: 999
Libs: -L\${libdir} -ludev
Cflags: -I\${includedir}
EOF

export PKG_CONFIG_PATH="$(cygpath -m "$STUB_DIR")"
export PKG_CONFIG_ALLOW_CROSS=1

cd "$WS_ROOT"
echo ">> cargo zigbuild --release --target $TARGET -p px_auth_server"
"$CZB" zigbuild --release --target $TARGET -p px_auth_server "$@"
echo ""
echo "OK: $WS_ROOT/target/$TARGET/release/px_auth_server"
