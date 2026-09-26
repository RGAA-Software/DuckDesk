#!/usr/bin/env python3
"""Assemble a strict Customer Single Server Windows candidate from explicit inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BINARIES = ("px_console.exe", "px_console_admin.exe", "px_db.exe", "px_relay.exe", "px_backup.exe")
PG_MANIFEST = ROOT / "deploy/production/windows-backup/postgresql-client-18.6.json"


def digest(path: Path) -> str:
    checksum = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest().lower()


def copy_file(source: Path, destination: Path) -> None:
    if source.is_symlink() or not source.is_file():
        raise ValueError(f"Required regular file is unavailable: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    if digest(source) != digest(destination):
        raise RuntimeError(f"Copied file hash differs: {source.name}")


def verify_postgresql_client(client_directory: Path) -> list[str]:
    manifest = json.loads(PG_MANIFEST.read_text(encoding="utf-8"))
    if (manifest.get("schema_version") != 1 or manifest.get("product") != "postgresql-client-windows-x64"
            or manifest.get("postgresql_version") != "18.6"):
        raise ValueError("Pinned PostgreSQL client identity is invalid")
    expected_files = {entry["path"]: entry for entry in manifest["files"]}
    if len(expected_files) != len(manifest["files"]) or len(expected_files) != 14:
        raise ValueError("Pinned PostgreSQL client manifest has duplicate or missing files")
    actual_files = set()
    for source in client_directory.rglob("*"):
        if source.is_symlink():
            raise ValueError(f"PostgreSQL client contains a link: {source}")
        if source.is_file():
            actual_files.add(source.relative_to(client_directory).as_posix())
    if actual_files != set(expected_files):
        raise ValueError("PostgreSQL client file list differs from pinned 18.6 manifest")
    for relative_path, entry in expected_files.items():
        source = client_directory / relative_path
        if source.stat().st_size != entry["size"] or digest(source) != entry["sha256"]:
            raise ValueError(f"PostgreSQL client hash differs: {relative_path}")
    return sorted(expected_files)


def assemble(binary_directory: Path, static_directory: Path, postgresql_directory: Path,
             output_directory: Path, suite_version: str, build_profile: str = "fast-release") -> None:
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", suite_version):
        raise ValueError("Server suite version must be MAJOR.MINOR.PATCH")
    if build_profile not in {"fast-release", "optimized-release"}:
        raise ValueError("Server build profile is invalid")
    if output_directory.exists():
        raise ValueError(f"Output already exists: {output_directory}")
    destination = output_directory.resolve()
    for source_directory in (binary_directory, static_directory, postgresql_directory):
        resolved_source = source_directory.resolve()
        if destination == resolved_source or destination in resolved_source.parents or resolved_source in destination.parents:
            raise ValueError("Output and input directories must be separate")
    if not (static_directory / "index.html").is_file():
        raise ValueError("Console Web assets are unavailable")
    for binary_name in BINARIES:
        source = binary_directory / binary_name
        if not source.is_file():
            raise ValueError(f"Expected Windows executable: {source}")
        with source.open("rb") as executable:
            if executable.read(2) != b"MZ":
                raise ValueError(f"Expected Windows executable: {source}")
    postgresql_files = verify_postgresql_client(postgresql_directory)
    output_directory.mkdir(parents=True)
    try:
        for binary_name in BINARIES:
            copy_file(binary_directory / binary_name, output_directory / "bin" / binary_name)
        for source in sorted(static_directory.rglob("*")):
            if source.is_symlink():
                raise ValueError(f"Console Web contains a symlink: {source}")
            if source.is_file():
                copy_file(source, output_directory / "static" / "console" / source.relative_to(static_directory))
        for relative_path in postgresql_files:
            copy_file(postgresql_directory / relative_path, output_directory / "postgresql" / relative_path)
        copy_file(ROOT / "deploy/single_server/assets/license-trust.json",
                  output_directory / "assets/license-trust.json")
        for script_name in ("install.ps1", "uninstall.ps1", "stage_setup.ps1"):
            copy_file(ROOT / "deploy" / "single_server" / "windows" / script_name,
                      output_directory / script_name)
        files = {
            source.relative_to(output_directory).as_posix(): digest(source)
            for source in sorted(output_directory.rglob("*")) if source.is_file()
        }
        if any("desk" in name.lower() or "auth" in name.lower() for name in files):
            raise ValueError("Customer Server cannot contain Desk or Auth")
        manifest = {
            "schema_version": 1,
            "product": "pixels-single-server",
            "distribution": "customer",
            "platform": "windows-x86_64",
            "suite_version": suite_version,
            "build_profile": build_profile,
            "files": files,
        }
        (output_directory / "sha256.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    except BaseException:
        shutil.rmtree(output_directory)
        raise


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--static", type=Path, required=True)
    parser.add_argument("--postgresql-client", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--suite-version", required=True)
    parser.add_argument("--build-profile", choices=("fast-release", "optimized-release"), default="fast-release")
    arguments = parser.parse_args()
    assemble(arguments.bin, arguments.static, arguments.postgresql_client,
             arguments.output, arguments.suite_version, arguments.build_profile)


if __name__ == "__main__":
    main()
