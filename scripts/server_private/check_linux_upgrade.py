#!/usr/bin/env python3
"""Reject formal private Server downgrades and same-version byte replacement."""

from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from verify_candidate import verify


def release_version(version_text: str) -> tuple[int, int, int]:
    major_version, minor_version, patch_version = (int(component) for component in version_text.split("."))
    return major_version, minor_version, patch_version


def check_upgrade(installed_directory: Path, incoming_directory: Path) -> None:
    installed_manifest = verify(installed_directory)
    incoming_manifest = verify(incoming_directory)
    if installed_manifest["product"] != "pixels-private-server":
        return
    if incoming_manifest["product"] != "pixels-private-server":
        raise ValueError("Formal Server release cannot be replaced by a development candidate")
    installed_version = release_version(installed_manifest["suite_version"])
    incoming_version = release_version(incoming_manifest["suite_version"])
    if incoming_version < installed_version:
        raise ValueError("Formal Server release downgrade is forbidden")
    if incoming_version == installed_version:
        installed_digest = hashlib.sha256((installed_directory / "sha256.json").read_bytes()).digest()
        incoming_digest = hashlib.sha256((incoming_directory / "sha256.json").read_bytes()).digest()
        if installed_digest != incoming_digest:
            raise ValueError("Formal Server release version cannot change its artifacts")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("installed_directory", type=Path)
    parser.add_argument("incoming_directory", type=Path)
    arguments = parser.parse_args()
    check_upgrade(arguments.installed_directory, arguments.incoming_directory)
    print("Private Server component upgrade identity accepted")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
