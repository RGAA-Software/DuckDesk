#!/usr/bin/env python3
"""Reject retired ZLMediaKit and Coturn components inside Android archives."""

from __future__ import annotations

import argparse
import zipfile
from pathlib import Path, PurePosixPath


RETIRED_FILE_NAMES = {
    "coturn",
    "coturn.exe",
    "libmk_api.dll",
    "libmk_api.so",
    "mediaserver",
    "mediaserver.exe",
    "mk_api.dll",
    "mk_api.so",
    "px_media.exe",
    "px_turn.exe",
    "turnserver",
    "turnserver.conf",
    "turnserver.exe",
    "zlmediakit",
    "zlmediakit.exe",
}
RETIRED_DIRECTORY_NAMES = {"coturn", "zlmediakit"}


def retired_entries(entries: list[str]) -> list[str]:
    rejected: list[str] = []
    for entry in entries:
        path = PurePosixPath(entry.replace("\\", "/"))
        normalized_parts = {part.lower() for part in path.parts}
        if path.name.lower() in RETIRED_FILE_NAMES or normalized_parts & RETIRED_DIRECTORY_NAMES:
            rejected.append(entry)
    return sorted(set(rejected))


def audit_archive(path: Path) -> None:
    if not path.is_file() or path.suffix.lower() not in {".apk", ".aab"} or not zipfile.is_zipfile(path):
        raise RuntimeError(f"Android artifact is not a readable APK/AAB ZIP archive: {path}")
    with zipfile.ZipFile(path) as archive:
        rejected = retired_entries(archive.namelist())
    if rejected:
        raise RuntimeError(f"Android artifact contains retired ZLMediaKit/Coturn entries: {rejected}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archives", type=Path, nargs="+")
    arguments = parser.parse_args()
    for archive in arguments.archives:
        audit_archive(archive)
        print(f"Retired central media audit passed: {archive}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
