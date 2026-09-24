#!/bin/sh
set -eu

if [ "$#" -ne 1 ] || [ "$(id -u)" -ne 0 ]; then
    echo "usage (as root): install_pg_toolchain.sh <absolute-private-server-package-directory>" >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "PostgreSQL client installation requires Python 3.8 or newer" >&2
    exit 2
fi
package_directory=$1
case "$package_directory" in /*) ;; *) echo "private server package path must be absolute" >&2; exit 2 ;; esac
if [ -L "$package_directory" ] || [ ! -d "$package_directory" ]; then
    echo "private server package directory is unavailable" >&2
    exit 2
fi
package_directory=$(realpath -e -- "$package_directory")
python3 "$package_directory/tools/verify_candidate.py" "$package_directory"
/bin/sh "$package_directory/tools/preflight_linux_host.sh"
toolchain_source="$package_directory/postgresql/18"
python3 "$package_directory/tools/verify_pg_toolchain.py" "$toolchain_source"

toolchain_parent=/opt/pixels/postgresql
toolchain_destination="$toolchain_parent/18"
for protected_path in /opt/pixels "$toolchain_parent" "$toolchain_destination"; do
    if [ -L "$protected_path" ]; then
        echo "PostgreSQL toolchain installation path must not be a symbolic link" >&2
        exit 4
    fi
done
if [ -e "$toolchain_destination" ]; then
    python3 "$package_directory/tools/verify_pg_toolchain.py" "$toolchain_destination"
    if ! cmp -s "$toolchain_source/sha256.json" "$toolchain_destination/sha256.json"; then
        echo "a different PostgreSQL client toolchain is already installed" >&2
        exit 4
    fi
    if ! env LD_LIBRARY_PATH="$toolchain_destination/lib" "$toolchain_destination/bin/pg_dump" --version | grep -Fxq 'pg_dump (PostgreSQL) 18.6' ||
       ! env LD_LIBRARY_PATH="$toolchain_destination/lib" "$toolchain_destination/bin/pg_restore" --version | grep -Fxq 'pg_restore (PostgreSQL) 18.6'; then
        echo "installed PostgreSQL client cannot run on this host" >&2
        exit 4
    fi
    echo "PostgreSQL 18.6 client toolchain already installed"
    exit 0
fi

install -d -o root -g root -m 0755 /opt/pixels "$toolchain_parent"
staged_directory=$(mktemp -d "$toolchain_parent/.stage-18-XXXXXXXX")
cleanup() {
    cleanup_status=$?
    if [ -d "$staged_directory" ] && [ ! -L "$staged_directory" ]; then
        resolved_stage=$(realpath -e -- "$staged_directory")
        case "$resolved_stage" in /opt/pixels/postgresql/.stage-18-*) rm -r -- "$resolved_stage" ;; esac
    fi
    trap - EXIT
    exit "$cleanup_status"
}
trap cleanup EXIT
cp -a "$toolchain_source/." "$staged_directory/"
python3 "$package_directory/tools/verify_pg_toolchain.py" "$staged_directory"
chown -R root:root "$staged_directory"
chmod -R go-w "$staged_directory"
if ! env LD_LIBRARY_PATH="$staged_directory/lib" "$staged_directory/bin/pg_dump" --version | grep -Fxq 'pg_dump (PostgreSQL) 18.6' ||
   ! env LD_LIBRARY_PATH="$staged_directory/lib" "$staged_directory/bin/pg_restore" --version | grep -Fxq 'pg_restore (PostgreSQL) 18.6'; then
    echo "packaged PostgreSQL client cannot run on this host" >&2
    exit 4
fi
mv -- "$staged_directory" "$toolchain_destination"
trap - EXIT
echo "installed PostgreSQL 18.6 client toolchain at $toolchain_destination"
