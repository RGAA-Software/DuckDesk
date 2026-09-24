#!/usr/bin/env python3
"""Assemble a Linux customer server candidate from explicitly built artifacts.

This does not initialize PostgreSQL or install services. It never includes Auth.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path


EXECUTABLES = ("px_console", "px_console_admin", "px_db", "px_relay")
FORBIDDEN_NAMES = {
    "px_auth",
    "px_auth_admin",
    "auth.env",
    "signing.key",
    "private.key",
    "license.pxlic2",
}


def sha256(artifact_path: Path) -> str:
    digest = hashlib.sha256()
    with artifact_path.open("rb") as artifact_file:
        for artifact_chunk in iter(lambda: artifact_file.read(1024 * 1024), b""):
            digest.update(artifact_chunk)
    return digest.hexdigest().upper()


def require_regular_file(artifact_path: Path) -> None:
    if artifact_path.is_symlink() or not artifact_path.is_file():
        raise ValueError(f"Expected a regular file: {artifact_path}")
    if artifact_path.name.lower() in FORBIDDEN_NAMES:
        raise ValueError(f"Private customer bundle forbids: {artifact_path.name}")


def copy_executable(source: Path, destination: Path) -> None:
    require_regular_file(source)
    with source.open("rb") as executable:
        if executable.read(4) != b"\x7fELF":
            raise ValueError(f"Expected a Linux executable: {source}")
    shutil.copy2(source, destination)
    destination.chmod(0o755)
    if sha256(source) != sha256(destination):
        raise RuntimeError(f"Copied executable hash mismatch: {source.name}")


def copy_static_tree(source: Path, destination: Path) -> None:
    if source.is_symlink() or not source.is_dir():
        raise ValueError(f"Expected a static asset directory: {source}")
    asset_paths = sorted(source.rglob("*"))
    if not (source / "index.html").is_file() or not asset_paths:
        raise ValueError(f"Static assets are missing index.html: {source}")
    for source_path in asset_paths:
        if source_path.is_symlink():
            raise ValueError(f"Static assets contain a symlink: {source_path}")
        if source_path.is_dir():
            continue
        require_regular_file(source_path)
        relative_path = source_path.relative_to(source)
        destination_path = destination / relative_path
        destination_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_path, destination_path)
        if sha256(source_path) != sha256(destination_path):
            raise RuntimeError(f"Copied static asset hash mismatch: {relative_path}")


def assemble(arguments: argparse.Namespace) -> dict[str, object]:
    executable_sources = {
        "px_console": arguments.console,
        "px_console_admin": arguments.console_admin,
        "px_db": arguments.database_admin,
        "px_relay": arguments.relay,
    }
    if arguments.desk is not None:
        if arguments.desk_static is None:
            raise ValueError("Desk executable requires Desk static assets")
        executable_sources["px_desk"] = arguments.desk
    elif arguments.desk_static is not None:
        raise ValueError("Desk static assets require a Desk executable")

    destination = arguments.output.resolve()
    if destination.exists():
        raise ValueError(f"Candidate output already exists: {destination}")
    for source in (*executable_sources.values(), arguments.console_static):
        resolved_source = source.resolve()
        if destination == resolved_source or destination in resolved_source.parents or resolved_source in destination.parents:
            raise ValueError("Candidate output and input sources must be separate")
    if arguments.desk_static is not None:
        resolved_desk_static = arguments.desk_static.resolve()
        if destination == resolved_desk_static or destination in resolved_desk_static.parents or resolved_desk_static in destination.parents:
            raise ValueError("Candidate output and Desk static assets must be separate")

    destination.mkdir(parents=True)
    try:
        binary_directory = destination / "bin"
        binary_directory.mkdir()
        for executable_name, source in executable_sources.items():
            copy_executable(source, binary_directory / executable_name)
        copy_static_tree(arguments.console_static, destination / "static" / "console")
        if arguments.desk_static is not None:
            copy_static_tree(arguments.desk_static, destination / "static" / "desk")

        artifact_hashes = {
            path.relative_to(destination).as_posix(): sha256(path)
            for path in sorted(destination.rglob("*"))
            if path.is_file()
        }
        manifest: dict[str, object] = {
            "schema_version": 1,
            "product": "pixels-private-server-candidate",
            "platform": "linux",
            "distribution": "customer",
            "contains_auth_signer": False,
            "artifacts": artifact_hashes,
        }
        (destination / "sha256.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
        return manifest
    except Exception:
        shutil.rmtree(destination)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console", required=True, type=Path)
    parser.add_argument("--console-admin", required=True, type=Path)
    parser.add_argument("--database-admin", required=True, type=Path)
    parser.add_argument("--relay", required=True, type=Path)
    parser.add_argument("--console-static", required=True, type=Path)
    parser.add_argument("--desk", type=Path)
    parser.add_argument("--desk-static", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    manifest = assemble(arguments)
    print(f"Private server candidate: {arguments.output} ({len(manifest['artifacts'])} artifacts)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
