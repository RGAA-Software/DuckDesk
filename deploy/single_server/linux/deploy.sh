#!/usr/bin/env bash
set -euo pipefail

deployment_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$deployment_directory"

if [[ $# -eq 1 && "$1" == uninstall ]]; then
    docker compose down --remove-orphans
    echo 'Pixels containers removed; configuration, data, and external PostgreSQL were retained.'
    exit 0
fi
if [[ $# -ne 0 ]]; then
    echo 'usage: ./deploy.sh [uninstall]' >&2
    exit 2
fi

archive_name="$(find . -maxdepth 1 -type f -name 'pixels-server-*.tar' -printf '%f\n')"
if [[ -z "$archive_name" || "$archive_name" == *$'\n'* ]]; then
    echo 'The package must contain exactly one versioned pixels-server image archive.' >&2
    exit 2
fi
python3 - "$archive_name" <<'PY'
import hashlib
import json
import pathlib
import sys

archive = pathlib.Path(sys.argv[1])
manifest = json.loads(pathlib.Path('sha256.json').read_text(encoding='utf-8'))
if manifest.get('product') != 'pixels-single-server' or manifest.get('platform') != 'linux-x86_64-compose':
    raise SystemExit('Package identity differs.')
files = manifest['files']
for name, expected in files.items():
    source = pathlib.Path(name)
    if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
        raise SystemExit(f'Package file differs: {name}')
if archive.name not in files:
    raise SystemExit('Image archive is not in the package manifest.')
PY
docker load --input "$archive_name"
docker image inspect "$(python3 -c 'import json; print(json.load(open("sha256.json"))["image"])')" >/dev/null
docker compose config --quiet
docker compose up -d
echo 'On first install, open http://127.0.0.1:4700/ on this host to finish setup.'
