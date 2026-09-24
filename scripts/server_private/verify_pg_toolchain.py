#!/usr/bin/env python3
"""Verify the pinned Linux PostgreSQL client toolchain before use or packaging."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path


POSTGRESQL_VERSION = "18.6"
SOURCE_SHA256 = "555610c24d53e4316da5b7d3fc25c279d96856d5e0e23ee308c328c5fa881d9f"
REQUIRED_ARTIFACTS = {
    "bin/pg_dump",
    "bin/pg_restore",
    "lib/libpq.so.5",
    "lib/libssl.so.1.1",
    "lib/libcrypto.so.1.1",
    "licenses/PostgreSQL-COPYRIGHT",
    "licenses/OpenSSL-Ubuntu-copyright",
}


def verify(toolchain_directory: Path) -> dict[str, object]:
    if toolchain_directory.is_symlink() or not toolchain_directory.is_dir():
        raise ValueError("PostgreSQL toolchain directory is unavailable")
    manifest_path = toolchain_directory / "sha256.json"
    if manifest_path.is_symlink() or not manifest_path.is_file() or manifest_path.stat().st_size > 64 * 1024:
        raise ValueError("PostgreSQL toolchain manifest is unavailable")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (not isinstance(manifest, dict) or set(manifest) != {
            "schema_version", "product", "postgresql_version", "platform", "source_url", "download_url",
            "source_sha256", "artifacts",
    } or manifest["schema_version"] != 1 or manifest["product"] != "pixels-postgresql-client-toolchain"
            or manifest["postgresql_version"] != POSTGRESQL_VERSION
            or manifest["platform"] != "linux-x86_64-glibc-2.31"
            or manifest["source_url"] != f"https://ftp.postgresql.org/pub/source/v{POSTGRESQL_VERSION}/postgresql-{POSTGRESQL_VERSION}.tar.bz2"
            or manifest["source_sha256"] != SOURCE_SHA256):
        raise ValueError("PostgreSQL toolchain identity is invalid")
    artifact_hashes = manifest["artifacts"]
    if not isinstance(artifact_hashes, dict) or set(artifact_hashes) != REQUIRED_ARTIFACTS:
        raise ValueError("PostgreSQL toolchain artifact list is invalid")
    actual_artifacts = set()
    for artifact_path in toolchain_directory.rglob("*"):
        if artifact_path.is_symlink():
            raise ValueError("PostgreSQL toolchain contains a symbolic link")
        if artifact_path.is_file() and artifact_path != manifest_path:
            actual_artifacts.add(artifact_path.relative_to(toolchain_directory).as_posix())
    if actual_artifacts != REQUIRED_ARTIFACTS:
        raise ValueError("PostgreSQL toolchain contains an unexpected artifact")
    for artifact_name, expected_hash in artifact_hashes.items():
        if (not isinstance(expected_hash, str) or len(expected_hash) != 64
                or any(character not in "0123456789abcdef" for character in expected_hash)):
            raise ValueError("PostgreSQL toolchain hash is invalid")
        actual_hash = hashlib.sha256((toolchain_directory / artifact_name).read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            raise ValueError(f"PostgreSQL toolchain artifact hash mismatch: {artifact_name}")
    for executable_name in ("pg_dump", "pg_restore"):
        executable_path = toolchain_directory / "bin" / executable_name
        if not os.access(executable_path, os.X_OK):
            raise ValueError(f"PostgreSQL toolchain executable lacks execute permission: {executable_name}")
        with executable_path.open("rb") as executable_file:
            elf_header = executable_file.read(20)
        if (len(elf_header) != 20 or elf_header[:4] != b"\x7fELF" or elf_header[4:6] != b"\x02\x01"
                or elf_header[18:20] != b"\x3e\x00"):
            raise ValueError(f"PostgreSQL toolchain executable is not Linux x86-64: {executable_name}")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("toolchain_directory", type=Path)
    arguments = parser.parse_args()
    manifest = verify(arguments.toolchain_directory)
    print(f"Verified PostgreSQL {manifest['postgresql_version']} client toolchain: {len(manifest['artifacts'])} artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
