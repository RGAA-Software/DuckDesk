#!/usr/bin/env python3
"""Verify every file in a Linux Customer server candidate before installation."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath


COMMON_IDENTITY = {
    "platform": "linux-x86_64",
    "distribution": "customer",
    "contains_auth_signer": False,
}
PACKAGE_IDENTITIES = {
    "pixels-private-server-candidate": {"schema_version": 1},
    "pixels-private-server": {"schema_version": 2, "build_profile": "optimized-release"},
}
REQUIRED_ARTIFACTS = {
    "bin/px_console",
    "bin/px_console_admin",
    "bin/px_db",
    "bin/px_relay",
    "static/console/index.html",
    "tools/verify_candidate.py",
    "tools/preflight_linux_host.sh",
    "tools/install_linux_component.sh",
    "tools/uninstall_linux_component.sh",
    "tools/install_pg_toolchain.sh",
    "tools/verify_pg_toolchain.py",
    "systemd/pixels-private-console@.service",
    "systemd/pixels-private-relay@.service",
}
HASH_FORMAT = re.compile(r"[0-9A-F]{64}")
VERSION_FORMAT = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
FORBIDDEN_NAMES = {"px_auth", "px_auth_admin", "px_desk", "auth.env", "signing.key", "private.key", "license.pxlic2"}


def file_hash(artifact_path: Path) -> str:
    digest = hashlib.sha256()
    with artifact_path.open("rb") as artifact_file:
        for artifact_chunk in iter(lambda: artifact_file.read(1024 * 1024), b""):
            digest.update(artifact_chunk)
    return digest.hexdigest().upper()


def verify(candidate_directory: Path) -> dict[str, object]:
    if candidate_directory.is_symlink() or not candidate_directory.is_dir():
        raise ValueError("Candidate must be a regular directory")
    manifest_path = candidate_directory / "sha256.json"
    if manifest_path.is_symlink() or not manifest_path.is_file() or manifest_path.stat().st_size > 1024 * 1024:
        raise ValueError("Candidate manifest is missing or invalid")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    product_name = manifest.get("product") if isinstance(manifest, dict) else None
    package_identity = PACKAGE_IDENTITIES.get(product_name) if isinstance(product_name, str) else None
    if (not isinstance(manifest, dict) or package_identity is None
            or type(manifest.get("schema_version")) is not int
            or manifest.get("contains_auth_signer") is not False
            or any(manifest.get(key) != value for key, value in COMMON_IDENTITY.items())
            or any(manifest.get(key) != value for key, value in package_identity.items())):
        raise ValueError("Unexpected private server candidate identity")
    if product_name == "pixels-private-server-candidate" and ("suite_version" in manifest or "build_profile" in manifest):
        raise ValueError("Development candidate must not claim a formal release")
    expected_manifest_keys = set(COMMON_IDENTITY) | {"schema_version", "product", "component_versions", "artifacts"}
    if product_name == "pixels-private-server":
        expected_manifest_keys.update({"suite_version", "build_profile"})
    if set(manifest) != expected_manifest_keys:
        raise ValueError("Unexpected private server manifest fields")
    suite_version = manifest.get("suite_version")
    if manifest["product"] == "pixels-private-server":
        if not isinstance(suite_version, str) or not VERSION_FORMAT.fullmatch(suite_version):
            raise ValueError("Formal server suite version is invalid")
    artifact_hashes = manifest.get("artifacts")
    versions = manifest.get("component_versions")
    if not isinstance(artifact_hashes, dict) or not artifact_hashes or not isinstance(versions, dict):
        raise ValueError("Candidate manifest is incomplete")
    actual_artifacts = set()
    for artifact_path in candidate_directory.rglob("*"):
        if artifact_path.is_symlink():
            raise ValueError(f"Candidate contains a symlink: {artifact_path}")
        if artifact_path.is_file() and artifact_path != manifest_path:
            actual_artifacts.add(artifact_path.relative_to(candidate_directory).as_posix())
    if set(artifact_hashes) != actual_artifacts or not REQUIRED_ARTIFACTS.issubset(actual_artifacts):
        raise ValueError("Candidate file list does not match its manifest")
    if any(artifact_name == "bin/px_desk" or artifact_name.startswith(("static/desk/", "systemd/pixels-private-desk", "examples/desk."))
           for artifact_name in actual_artifacts):
        raise ValueError("Desk belongs to the official website, not the Customer Server")
    if manifest["product"] == "pixels-private-server" and "bin/px_backup" not in actual_artifacts:
        raise ValueError("Formal Server release requires its backup executor")
    pg_toolchain_present = "postgresql/18/sha256.json" in actual_artifacts
    if manifest["product"] == "pixels-private-server" and not pg_toolchain_present:
        raise ValueError("Formal Server release requires its PostgreSQL client toolchain")
    if pg_toolchain_present and "bin/px_backup" not in actual_artifacts:
        raise ValueError("PostgreSQL client toolchain requires the backup executor")
    if ("bin/px_backup" in actual_artifacts) != ("systemd/pixels-private-backup@.service" in actual_artifacts):
        raise ValueError("Backup executable and systemd unit must be paired")
    if manifest["product"] == "pixels-private-server" and "tools/check_upgrade.py" not in actual_artifacts:
        raise ValueError("Formal Server release requires its upgrade verifier")
    expected_versions = {"console", "database_tool", "relay"}
    if "bin/px_backup" in actual_artifacts:
        expected_versions.add("backup")
    if set(versions) != expected_versions or any(
        not isinstance(version, str) or not VERSION_FORMAT.fullmatch(version) for version in versions.values()
    ):
        raise ValueError("Candidate component versions are invalid")
    for artifact_name, expected_hash in artifact_hashes.items():
        relative_path = PurePosixPath(artifact_name)
        if (relative_path.is_absolute() or not relative_path.parts or ".." in relative_path.parts
                or "\\" in artifact_name or relative_path.name.lower() in FORBIDDEN_NAMES
                or not isinstance(expected_hash, str) or not HASH_FORMAT.fullmatch(expected_hash)):
            raise ValueError(f"Invalid candidate artifact entry: {artifact_name}")
        if file_hash(candidate_directory / artifact_name) != expected_hash:
            raise ValueError(f"Candidate artifact hash mismatch: {artifact_name}")
    for executable_name in ("px_console", "px_console_admin", "px_db", "px_relay", "px_backup"):
        executable_path = candidate_directory / "bin" / executable_name
        if not executable_path.is_file():
            continue
        with executable_path.open("rb") as executable_file:
            header = executable_file.read(20)
        if (len(header) != 20 or header[:4] != b"\x7fELF" or header[4:6] != b"\x02\x01"
                or header[18:20] != b"\x3e\x00"):
            raise ValueError(f"Candidate executable is not Linux x86-64: {executable_name}")
    if pg_toolchain_present:
        subprocess.run(
            [sys.executable, str(candidate_directory / "tools/verify_pg_toolchain.py"), str(candidate_directory / "postgresql/18")],
            check=True, capture_output=True, text=True,
        )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    arguments = parser.parse_args()
    manifest = verify(arguments.candidate)
    print(f"Verified private server candidate: {len(manifest['artifacts'])} artifacts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
