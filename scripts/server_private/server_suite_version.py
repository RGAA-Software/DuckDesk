#!/usr/bin/env python3
"""Reserve one independent private Server suite version for a formal build."""

from __future__ import annotations

import argparse
import json
import os
import re
import tempfile
from pathlib import Path


DEFAULT_VERSION_FILE = Path(__file__).with_suffix(".json")
VERSION_PATTERN = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")


def read_next_version(version_file: Path) -> str:
    version_state = json.loads(version_file.read_text(encoding="utf-8"))
    if not isinstance(version_state, dict) or set(version_state) != {"next_version"}:
        raise ValueError("Invalid Server suite version state")
    next_version = version_state["next_version"]
    if not isinstance(next_version, str) or not VERSION_PATTERN.fullmatch(next_version):
        raise ValueError("Invalid next Server suite version")
    return next_version


def reserve_version(version_file: Path, expected_version: str) -> str:
    current_version = read_next_version(version_file)
    if current_version != expected_version:
        raise ValueError("Server suite version changed after preflight")
    major_version, minor_version, patch_version = (int(part) for part in current_version.split("."))
    following_version = f"{major_version}.{minor_version}.{patch_version + 1}"
    temporary_name = ""
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=version_file.parent, prefix=".server-suite-version-", delete=False
        ) as temporary_file:
            temporary_name = temporary_file.name
            json.dump({"next_version": following_version}, temporary_file, indent=2)
            temporary_file.write("\n")
            temporary_file.flush()
            os.fsync(temporary_file.fileno())
        os.replace(temporary_name, version_file)
    finally:
        if temporary_name and os.path.exists(temporary_name):
            os.unlink(temporary_name)
    return current_version


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version-file", type=Path, default=DEFAULT_VERSION_FILE)
    parser.add_argument("--reserve", metavar="EXPECTED_VERSION")
    arguments = parser.parse_args()
    version = (
        reserve_version(arguments.version_file, arguments.reserve)
        if arguments.reserve is not None
        else read_next_version(arguments.version_file)
    )
    print(version)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
