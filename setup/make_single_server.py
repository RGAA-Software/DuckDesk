#!/usr/bin/env python3
"""Build the unsigned Customer Single Server Setup from an exact Windows package."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

if __package__:
    from .make_setup import find_nsis, validate_pinned_nsis
else:
    from make_setup import find_nsis, validate_pinned_nsis


ROOT = Path(__file__).resolve().parents[1]
SETUP = Path(__file__).resolve().parent
REQUIRED_FILES = {
    "bin/px_console.exe", "bin/px_console_admin.exe", "bin/px_db.exe",
    "bin/px_relay.exe", "bin/px_backup.exe", "static/console/index.html",
    "postgresql/bin/pg_dump.exe", "postgresql/bin/pg_restore.exe", "install.ps1", "uninstall.ps1",
    "stage_setup.ps1", "assets/license-trust.json",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_package(package_root: Path) -> tuple[str, str]:
    if package_root.is_symlink() or not package_root.is_dir():
        raise ValueError("Windows package directory is unavailable")
    manifest_path = package_root / "sha256.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (manifest.get("schema_version") != 1 or manifest.get("product") != "pixels-single-server"
            or manifest.get("distribution") != "customer" or manifest.get("platform") != "windows-x86_64"):
        raise ValueError("Windows package identity is invalid")
    if manifest.get("build_profile") not in {"fast-release", "optimized-release"}:
        raise ValueError("Windows package build profile is invalid")
    version = manifest.get("suite_version")
    if not isinstance(version, str) or not version.replace(".", "").isdigit() or version.count(".") != 2:
        raise ValueError("Windows Server version is invalid")
    files = manifest.get("files")
    if not isinstance(files, dict) or not REQUIRED_FILES.issubset(files):
        raise ValueError("Windows package is missing a required component")
    actual_files = set()
    for path in package_root.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"Package contains a link: {path}")
        if path.is_file() and path != manifest_path:
            actual_files.add(path.relative_to(package_root).as_posix())
    if actual_files != set(files):
        raise ValueError("Windows package file list differs from manifest")
    for relative_path, expected_hash in files.items():
        if ("desk" in relative_path.lower() or "auth" in relative_path.lower()
                or not isinstance(expected_hash, str) or len(expected_hash) != 64
                or sha256(package_root / relative_path) != expected_hash):
            raise ValueError(f"Windows package file is not verified: {relative_path}")
    return version, sha256(manifest_path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    package_root = arguments.package.resolve()
    version, manifest_hash = validate_package(package_root)
    output = arguments.output.resolve()
    if output.exists() or output.name != f"PixelsServer_{version}_Setup.exe":
        raise ValueError("Output must be a new PixelsServer_<version>_Setup.exe")
    output.parent.mkdir(parents=True, exist_ok=True)
    makensis = find_nsis(None, ROOT)
    validate_pinned_nsis(ROOT, makensis)
    subprocess.run([
        str(makensis), "/INPUTCHARSET", "UTF8", f"/DPAYLOAD_DIR={package_root}", f"/DSUITE_VERSION={version}",
        f"/DMANIFEST_SHA256={manifest_hash}", f"/DOUTPUT_FILE={output}",
        str(SETUP / "single_server.nsi"),
    ], cwd=SETUP, check=True)
    output.with_suffix(output.suffix + ".sha256").write_bytes(f"{sha256(output)}  {output.name}\n".encode("ascii"))


if __name__ == "__main__":
    main()
