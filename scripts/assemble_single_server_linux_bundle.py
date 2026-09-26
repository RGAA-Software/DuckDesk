#!/usr/bin/env python3
"""Create an offline Linux Compose bundle from a verified versioned image archive."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import tarfile
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEPLOYMENT_ROOT = ROOT / "deploy/single_server/linux"
DEPLOYMENT_FILES = ("compose.yaml", "deploy.sh", "README.md")


def sha256(path: Path) -> str:
    checksum = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def assemble(image_archive: Path, expected_hash: str, version: str, output: Path,
             build_profile: str = "fast-release") -> None:
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("Server suite version must be MAJOR.MINOR.PATCH")
    if build_profile not in {"fast-release", "optimized-release"}:
        raise ValueError("Server build profile is invalid")
    if not re.fullmatch(r"[0-9a-f]{64}", expected_hash) or sha256(image_archive) != expected_hash:
        raise ValueError("Image archive SHA-256 differs from the reviewed value")
    if output.exists() or output.name != f"PixelsServer_{version}_Linux.tar.gz":
        raise ValueError("Output must be a new versioned Linux bundle")
    for filename in DEPLOYMENT_FILES:
        if not (DEPLOYMENT_ROOT / filename).is_file():
            raise ValueError(f"Linux deployment file is unavailable: {filename}")
    with tempfile.TemporaryDirectory() as staging_directory:
        staging = Path(staging_directory)
        package_name = f"PixelsServer_{version}_Linux"
        package = staging / package_name
        package.mkdir()
        archive_name = f"pixels-server-{version}.tar"
        image_target = package / archive_name
        with image_archive.open("rb") as source, image_target.open("wb") as destination:
            while chunk := source.read(1024 * 1024):
                destination.write(chunk)
        files = {archive_name: expected_hash}
        for filename in DEPLOYMENT_FILES:
            source = DEPLOYMENT_ROOT / filename
            target = package / filename
            source_bytes = source.read_bytes()
            if filename == "compose.yaml":
                source_bytes, replacement_count = re.subn(
                    rb"pixels-server:[0-9]+\.[0-9]+\.[0-9]+",
                    f"pixels-server:{version}".encode("ascii"),
                    source_bytes,
                )
                if replacement_count != 1:
                    raise ValueError("Compose must contain exactly one versioned Server image")
            target.write_bytes(source_bytes)
            if filename == "deploy.sh":
                target.chmod(0o755)
            files[filename] = sha256(target)
        manifest = {
            "schema_version": 1,
            "product": "pixels-single-server",
            "distribution": "customer",
            "platform": "linux-x86_64-compose",
            "suite_version": version,
            "build_profile": build_profile,
            "image": f"pixels-server:{version}",
            "files": files,
        }
        (package / "sha256.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        with tarfile.open(output, "w:gz") as bundle:
            bundle.add(package, arcname=package_name, recursive=True)
    output.with_suffix(output.suffix + ".sha256").write_bytes(f"{sha256(output)}  {output.name}\n".encode("ascii"))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image-archive", type=Path, required=True)
    parser.add_argument("--image-sha256", required=True)
    parser.add_argument("--suite-version", required=True)
    parser.add_argument("--build-profile", choices=("fast-release", "optimized-release"), default="fast-release")
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    assemble(arguments.image_archive, arguments.image_sha256, arguments.suite_version,
             arguments.output, arguments.build_profile)


if __name__ == "__main__":
    main()
