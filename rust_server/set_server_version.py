#!/usr/bin/env python3
"""Manage versions of the rust_server services independently.

Each service (gr_auth_server / gr_cms_server / gr_desk_server / gr_stat_server /
gr_updater / builder) owns the `version` field in its own Cargo.toml and bumps
it separately — the same logic as the GammaRay-side set_app_version.py:
patch += 1 on every bump; when patch would reach 100, minor += 1 and patch
resets to 0 (e.g. 1.0.99 -> 1.1.0).

Usage:
    python set_server_version.py                     # show all service versions
    python set_server_version.py gr_cms_server --bump
    python set_server_version.py gr_cms_server 1.2.3 # set explicitly
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent  # rust_server/

SERVICES = [
    "gr_auth_server",
    "gr_cms_server",
    "gr_desk_server",
    "gr_stat_server",
    "gr_updater",
    "builder",
]

SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Manage rust_server service versions.")
    parser.add_argument("service", nargs="?", choices=SERVICES, help="Service name.")
    parser.add_argument("version", nargs="?", help="New version in X.Y.Z form.")
    parser.add_argument(
        "--bump",
        action="store_true",
        help="Auto-increment: patch += 1; patch reaching 100 bumps minor and resets to 0.",
    )
    return parser.parse_args()


def bump_version(version: str) -> str:
    """patch += 1; patch 达到 100 时进位 minor 并归零。"""
    major, minor, patch = (int(part) for part in version.split("."))
    patch += 1
    if patch >= 100:
        minor += 1
        patch = 0
    return f"{major}.{minor}.{patch}"


def manifest_for(service: str) -> Path:
    path = ROOT / service / "Cargo.toml"
    if not path.is_file():
        raise SystemExit(f"Manifest not found: {path}")
    return path


def read_version(service: str) -> str:
    text = manifest_for(service).read_text(encoding="utf-8")
    match = re.search(r'^version\s*=\s*"([^"]+)"', text, flags=re.MULTILINE)
    if not match:
        raise SystemExit(f"Cannot read version from {service}/Cargo.toml")
    return match.group(1)


def write_version(service: str, version: str) -> None:
    path = manifest_for(service)
    text = path.read_text(encoding="utf-8")
    new_text, count = re.subn(
        r'^(version\s*=\s*")[^"]+(")',
        rf"\g<1>{version}\g<2>",
        text,
        count=1,
        flags=re.MULTILINE,
    )
    if count != 1:
        raise SystemExit(f"Failed to update version in {path}")
    path.write_text(new_text, encoding="utf-8", newline="\n")


def sync_lockfile() -> None:
    # 保守同步 Cargo.lock：保留现有依赖锁定，仅写入本地包版本号变更。
    # 不要用 cargo generate-lockfile（会把依赖升到 latest compatible）。
    subprocess.run(
        [
            "cargo",
            "metadata",
            "--format-version",
            "1",
            "--manifest-path",
            str(ROOT / "Cargo.toml"),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )


def show_versions() -> None:
    print("rust_server service versions:")
    for service in SERVICES:
        try:
            version = read_version(service)
        except SystemExit:
            version = "(missing)"
        print(f"  {version:<10} {service}")


def main() -> None:
    args = parse_args()

    if args.service is None:
        show_versions()
        print()
        print("Usage: python set_server_version.py <service> --bump | <service> X.Y.Z")
        return

    if args.version is not None and args.bump:
        raise SystemExit("Pass either an explicit version or --bump, not both.")

    current = read_version(args.service)
    if args.bump:
        new_version = bump_version(current)
    elif args.version is not None:
        new_version = args.version.strip()
        if not SEMVER_RE.fullmatch(new_version):
            raise SystemExit(f"Invalid version '{new_version}'. Expected X.Y.Z.")
    else:
        print(f"{args.service}: {current}")
        return

    write_version(args.service, new_version)
    sync_lockfile()
    print(f"{args.service}: {current} -> {new_version}")


if __name__ == "__main__":
    main()
