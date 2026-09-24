#!/usr/bin/env python3
"""Assemble a Linux customer server candidate from explicitly built artifacts.

This does not initialize PostgreSQL or install services. It never includes Auth.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
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
SOURCE_ROOT = Path(__file__).resolve().parents[1]
PACKAGE_VERSION = re.compile(r'^version\s*=\s*"([0-9]+\.[0-9]+\.[0-9]+)"\s*$')
SUITE_VERSION = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")


def package_version(manifest_path: Path) -> str:
    in_package = False
    for source_line in manifest_path.read_text(encoding="utf-8").splitlines():
        line = source_line.strip()
        if line.startswith("["):
            in_package = line == "[package]"
        elif in_package:
            version_match = PACKAGE_VERSION.fullmatch(line)
            if version_match:
                return version_match.group(1)
    raise ValueError(f"Missing package version: {manifest_path}")


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
        elf_header = executable.read(20)
        if (len(elf_header) != 20 or elf_header[:4] != b"\x7fELF"
                or elf_header[4:6] != b"\x02\x01" or elf_header[18:20] != b"\x3e\x00"):
            raise ValueError(f"Expected a Linux x86-64 executable: {source}")
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


def copy_pg_toolchain(source: Path, destination: Path) -> None:
    verifier_source = SOURCE_ROOT / "scripts/server_private/verify_pg_toolchain.py"
    subprocess.run([sys.executable, str(verifier_source), str(source)], check=True, capture_output=True, text=True)
    for source_path in sorted(source.rglob("*")):
        if source_path.is_dir():
            continue
        require_regular_file(source_path)
        destination_path = destination / source_path.relative_to(source)
        destination_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_path, destination_path)
        if sha256(source_path) != sha256(destination_path):
            raise RuntimeError(f"Copied PostgreSQL toolchain hash mismatch: {source_path.name}")


def assemble(arguments: argparse.Namespace) -> dict[str, object]:
    suite_version = getattr(arguments, "suite_version", None)
    if suite_version is not None and (not isinstance(suite_version, str) or not SUITE_VERSION.fullmatch(suite_version)):
        raise ValueError("Formal server suite version must be MAJOR.MINOR.PATCH")
    executable_sources = {
        "px_console": arguments.console,
        "px_console_admin": arguments.console_admin,
        "px_db": arguments.database_admin,
        "px_relay": arguments.relay,
    }
    backup_executable = getattr(arguments, "backup", None)
    pg_toolchain = getattr(arguments, "pg_toolchain", None)
    if suite_version is not None and backup_executable is None:
        raise ValueError("Formal Server release requires the PostgreSQL backup executor")
    if suite_version is not None and pg_toolchain is None:
        raise ValueError("Formal Server release requires the PostgreSQL client toolchain")
    if pg_toolchain is not None and backup_executable is None:
        raise ValueError("PostgreSQL client toolchain requires the backup executor")
    if backup_executable is not None:
        executable_sources["px_backup"] = backup_executable
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
    if pg_toolchain is not None:
        resolved_pg_toolchain = pg_toolchain.resolve()
        if destination == resolved_pg_toolchain or destination in resolved_pg_toolchain.parents or resolved_pg_toolchain in destination.parents:
            raise ValueError("Candidate output and PostgreSQL toolchain must be separate")

    destination.mkdir(parents=True)
    try:
        binary_directory = destination / "bin"
        binary_directory.mkdir()
        for executable_name, source in executable_sources.items():
            copy_executable(source, binary_directory / executable_name)
        copy_static_tree(arguments.console_static, destination / "static" / "console")
        if arguments.desk_static is not None:
            copy_static_tree(arguments.desk_static, destination / "static" / "desk")
        if pg_toolchain is not None:
            copy_pg_toolchain(pg_toolchain, destination / "postgresql" / "18")
        verifier_directory = destination / "tools"
        verifier_directory.mkdir()
        verifier_source = SOURCE_ROOT / "scripts/verify_private_server_candidate.py"
        require_regular_file(verifier_source)
        shutil.copy2(verifier_source, verifier_directory / "verify_candidate.py")
        upgrade_check_source = SOURCE_ROOT / "scripts/server_private/check_linux_upgrade.py"
        require_regular_file(upgrade_check_source)
        shutil.copy2(upgrade_check_source, verifier_directory / "check_upgrade.py")
        for tool_name in (
            "preflight_linux_host.sh", "install_linux_component.sh", "uninstall_linux_component.sh",
            "install_pg_toolchain.sh", "verify_pg_toolchain.py",
        ):
            tool_source = SOURCE_ROOT / "scripts/server_private" / tool_name
            require_regular_file(tool_source)
            shutil.copy2(tool_source, verifier_directory / tool_name)
        systemd_directory = destination / "systemd"
        systemd_directory.mkdir()
        component_names = ["console", "relay", "desk"]
        if backup_executable is not None:
            component_names.append("backup")
        for component_name in component_names:
            unit_name = f"pixels-private-{component_name}@.service"
            unit_source = SOURCE_ROOT / "deploy/systemd" / unit_name
            require_regular_file(unit_source)
            shutil.copy2(unit_source, systemd_directory / unit_name)

        artifact_hashes = {
            path.relative_to(destination).as_posix(): sha256(path)
            for path in sorted(destination.rglob("*"))
            if path.is_file()
        }
        manifest: dict[str, object] = {
            "schema_version": 2 if suite_version is not None else 1,
            "product": "pixels-private-server" if suite_version is not None else "pixels-private-server-candidate",
            "platform": "linux-x86_64",
            "distribution": "customer",
            "contains_auth_signer": False,
            "component_versions": {
                "console": package_version(SOURCE_ROOT / "rust_server/px_console_server/runtime/Cargo.toml"),
                "database_tool": package_version(SOURCE_ROOT / "rust_server/px_pg/Cargo.toml"),
                "relay": package_version(SOURCE_ROOT / "rust_server/px_relay_server/Cargo.toml"),
                **({"backup": package_version(SOURCE_ROOT / "rust_server/px_backup/Cargo.toml")}
                   if backup_executable is not None else {}),
                **({"desk": package_version(SOURCE_ROOT / "rust_server/px_desk_server/Cargo.toml")}
                   if arguments.desk is not None else {}),
            },
            "artifacts": artifact_hashes,
        }
        if suite_version is not None:
            manifest["suite_version"] = suite_version
            manifest["build_profile"] = "optimized-release"
        (destination / "sha256.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
        subprocess.run([sys.executable, str(verifier_source), str(destination)], check=True, capture_output=True, text=True)
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
    parser.add_argument("--backup", type=Path, help="PostgreSQL backup and restore executor; mandatory for formal releases")
    parser.add_argument("--pg-toolchain", type=Path, help="Pinned PostgreSQL 18.6 client toolchain; mandatory for formal releases")
    parser.add_argument("--console-static", required=True, type=Path)
    parser.add_argument("--desk", type=Path)
    parser.add_argument("--desk-static", type=Path)
    parser.add_argument("--suite-version", help="Formal Customer release version; omit for development candidate")
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    manifest = assemble(arguments)
    print(f"Private server candidate: {arguments.output} ({len(manifest['artifacts'])} artifacts)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
