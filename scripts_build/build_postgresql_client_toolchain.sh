#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || "$1" != /* || -e "$1" ]]; then
    echo "usage: build_postgresql_client_toolchain.sh <new-absolute-output-directory>" >&2
    exit 2
fi

readonly postgresql_version=18.6
readonly source_sha256=555610c24d53e4316da5b7d3fc25c279d96856d5e0e23ee308c328c5fa881d9f
readonly source_url="https://ftp.postgresql.org/pub/source/v$postgresql_version/postgresql-$postgresql_version.tar.bz2"
readonly download_url="https://mirrors.aliyun.com/postgresql/source/v$postgresql_version/postgresql-$postgresql_version.tar.bz2"
readonly output_directory=$1
build_directory=$(mktemp -d -t pixels-postgresql-client-XXXXXXXX)
cleanup() {
    cleanup_status=$?
    if [[ -d "$build_directory" && ! -L "$build_directory" ]]; then
        resolved_build_directory=$(realpath -e -- "$build_directory")
        case "$resolved_build_directory" in /tmp/pixels-postgresql-client-*) rm -r -- "$resolved_build_directory" ;; esac
    fi
    trap - EXIT
    exit "$cleanup_status"
}
trap cleanup EXIT

for required_command in curl sha256sum tar gcc make bison flex python3; do
    command -v "$required_command" >/dev/null || { echo "missing build prerequisite: $required_command" >&2; exit 2; }
done
[[ -f /usr/include/openssl/ssl.h && -f /usr/include/zlib.h ]] || {
    echo "OpenSSL and zlib development headers are required" >&2
    exit 2
}
source_archive="$build_directory/postgresql-$postgresql_version.tar.bz2"
curl --fail --location --silent --show-error --max-time 180 "$download_url" --output "$source_archive"
printf '%s  %s\n' "$source_sha256" "$source_archive" | sha256sum --check --status || {
    echo "PostgreSQL source archive digest mismatch" >&2
    exit 2
}
tar -xjf "$source_archive" -C "$build_directory"
source_directory="$build_directory/postgresql-$postgresql_version"
cd "$source_directory"
./configure --prefix=/opt/pixels/postgresql/18 --with-openssl --without-readline --without-icu --without-zstd --without-lz4 \
    >"$build_directory/configure.log"
make -j"$(nproc)" >"$build_directory/build.log"
make install DESTDIR="$build_directory/staged" >"$build_directory/install.log"

staged_directory="$build_directory/staged/opt/pixels/postgresql/18"
[[ -x "$staged_directory/bin/pg_dump" && -x "$staged_directory/bin/pg_restore" ]] || {
    echo "PostgreSQL client tools were not built" >&2
    exit 2
}
mkdir -p "$output_directory/bin" "$output_directory/lib" "$output_directory/licenses"
install -m 0755 "$staged_directory/bin/pg_dump" "$output_directory/bin/pg_dump"
install -m 0755 "$staged_directory/bin/pg_restore" "$output_directory/bin/pg_restore"
install -m 0644 -T "$(realpath -e "$staged_directory/lib/libpq.so.5")" "$output_directory/lib/libpq.so.5"
for shared_library in libssl.so.1.1 libcrypto.so.1.1; do
    install -m 0644 "/usr/lib/x86_64-linux-gnu/$shared_library" "$output_directory/lib/$shared_library"
done
install -m 0644 "$source_directory/COPYRIGHT" "$output_directory/licenses/PostgreSQL-COPYRIGHT"
install -m 0644 /usr/share/doc/libssl1.1/copyright "$output_directory/licenses/OpenSSL-Ubuntu-copyright"

LD_LIBRARY_PATH="$output_directory/lib" "$output_directory/bin/pg_dump" --version | grep -Fx "pg_dump (PostgreSQL) $postgresql_version"
LD_LIBRARY_PATH="$output_directory/lib" "$output_directory/bin/pg_restore" --version | grep -Fx "pg_restore (PostgreSQL) $postgresql_version"
python3 - "$output_directory" "$source_url" "$download_url" "$source_sha256" "$postgresql_version" <<'PY'
import hashlib
import json
import sys
from pathlib import Path

output_directory = Path(sys.argv[1])
source_url, download_url, source_sha256, postgresql_version = sys.argv[2:]
artifacts = {}
for artifact_path in sorted(output_directory.rglob("*")):
    if artifact_path.is_file():
        artifacts[artifact_path.relative_to(output_directory).as_posix()] = hashlib.sha256(artifact_path.read_bytes()).hexdigest()
manifest = {
    "schema_version": 1,
    "product": "pixels-postgresql-client-toolchain",
    "postgresql_version": postgresql_version,
    "platform": "linux-x86_64-glibc-2.31",
    "source_url": source_url,
    "download_url": download_url,
    "source_sha256": source_sha256,
    "artifacts": artifacts,
}
with (output_directory / "sha256.json").open("w", encoding="utf-8") as manifest_file:
    json.dump(manifest, manifest_file, indent=2)
    manifest_file.write("\n")
PY
echo "PostgreSQL client toolchain: $output_directory"
