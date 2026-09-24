#!/bin/sh
set -eu

if [ "$#" -ne 0 ]; then
    echo "usage: preflight_linux_host.sh" >&2
    exit 2
fi

for required_command in uname python3 systemctl ps realpath sha256sum runuser install; do
    if ! command -v "$required_command" >/dev/null 2>&1; then
        echo "private Server host is missing required command: $required_command" >&2
        exit 2
    fi
done
if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
    echo "private Server host requires Linux x86-64" >&2
    exit 2
fi
if ! python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)'; then
    echo "private Server host requires Python 3.8 or newer" >&2
    exit 2
fi
if [ "$(ps -p 1 -o comm= | tr -d ' ')" != systemd ]; then
    echo "private Server host requires systemd as PID 1" >&2
    exit 2
fi

echo "PASS PRIVATE SERVER HOST PREFLIGHT: Linux x86-64, Python 3.8+, systemd PID 1 and installer commands"
