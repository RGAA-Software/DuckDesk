#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo 'usage: build_single_server_linux_image.sh <verified-candidate-dir> <versioned-image-tag> <new-image-archive.tar>' >&2
    exit 2
fi

source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
candidate_directory="$(realpath "$1")"
image_tag="$2"
archive_path="$3"

if [[ ! "$image_tag" =~ ^pixels-server:[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo 'Image tag must be pixels-server:MAJOR.MINOR.PATCH' >&2
    exit 2
fi
if [[ -e "$archive_path" ]]; then
    echo "Image archive already exists: $archive_path" >&2
    exit 2
fi
python3 "$source_root/scripts/verify_private_server_candidate.py" "$candidate_directory"
python3 "$source_root/scripts/server_private/verify_pg_toolchain.py" "$candidate_directory/postgresql/18"
docker build --pull=false --tag "$image_tag" --file "$source_root/deploy/single_server/linux/Dockerfile" "$candidate_directory"
docker save --output "$archive_path" "$image_tag"
sha256sum "$archive_path" > "$archive_path.sha256"
