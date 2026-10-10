#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo 'usage: package_px_relay_linux.sh <fresh-relay-binary> <new-output-directory> <image-tag>' >&2
    exit 2
fi
repository_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
relay_binary="$(realpath "$1")"
output_directory="$2"
image_tag="$3"
[[ ! -e "${output_directory}" ]] || { echo 'Package output must be new' >&2; exit 2; }
[[ "${image_tag}" =~ ^pixels-relay:[a-zA-Z0-9][a-zA-Z0-9_.-]*$ ]] || { echo 'Invalid Relay image tag' >&2; exit 2; }
mkdir -p -- "${output_directory}/build-context"
output_directory="$(realpath "${output_directory}")"
install -m 755 -- "${relay_binary}" "${output_directory}/build-context/px_relay"
runtime_hash="$(sha256sum "${relay_binary}" | cut -d ' ' -f 1)"
docker build --pull=false --build-arg "RELAY_VERSION=${image_tag#pixels-relay:}" \
    --build-arg "RELAY_SHA256=${runtime_hash}" --tag "${image_tag}" \
    --file "${repository_root}/deploy/relay/Dockerfile" "${output_directory}/build-context"
docker save --output "${output_directory}/relay-image.tar" "${image_tag}"
cp -- "${repository_root}/deploy/relay/compose.yaml" "${output_directory}/compose.yaml"
cp -- "${repository_root}/deploy/relay/iroh-relay.example.json" "${output_directory}/relay.example.json"
cp -- "${repository_root}/deploy/relay/deploy.py" "${output_directory}/deploy.py"
printf 'PIXELS_RELAY_IMAGE=%s\n' "${image_tag}" > "${output_directory}/.env"
image_id="$(docker image inspect --format '{{.Id}}' "${image_tag}")"
python3 - "${output_directory}" "${image_tag}" "${image_id}" "${runtime_hash}" <<'PY'
import hashlib, json, pathlib, sys
directory = pathlib.Path(sys.argv[1])
def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()
manifest = {
    'product': 'pixels-relay', 'platform': 'linux-x86_64', 'image_tag': sys.argv[2],
    'image_id': sys.argv[3], 'runtime_sha256': sys.argv[4],
    'files': {name: sha256(directory / name) for name in ['relay-image.tar', 'compose.yaml', 'relay.example.json', '.env', 'deploy.py']},
}
(directory / 'manifest.json').write_text(json.dumps(manifest, indent=2))
print(json.dumps(manifest, indent=2))
PY
