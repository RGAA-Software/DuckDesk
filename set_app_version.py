#!/usr/bin/env python3
"""Set a unified product version for all app executables and installers."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VERSION_CMAKE = ROOT / "src" / "gr_base" / "version.cmake"

SEMVER_RE = re.compile(r"^\d+\.\d+\.\d+$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Show or update the unified GammaRay product version.",
    )
    parser.add_argument(
        "version",
        nargs="?",
        help="New version in X.Y.Z form (for example 3.1.0).",
    )
    parser.add_argument(
        "--show",
        action="store_true",
        help="Only print current versions; do not modify files.",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run built-in replacement tests and exit.",
    )
    return parser.parse_args()


def validate_version(version: str) -> str:
    version = version.strip()
    if not SEMVER_RE.fullmatch(version):
        raise SystemExit(f"Invalid version '{version}'. Expected X.Y.Z (for example 3.1.0).")
    return version


def read_tc_app_version() -> str:
    text = VERSION_CMAKE.read_text(encoding="utf-8")
    match = re.search(r"set\(TC_APP_VERSION\s+([\d.]+)\)", text)
    if not match:
        raise SystemExit(f"Cannot read TC_APP_VERSION from {VERSION_CMAKE}")
    return match.group(1)


def replace_once(path: Path, pattern: str, replacement: str, label: str) -> bool:
    text = path.read_text(encoding="utf-8")
    new_text, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE)
    if count != 1:
        raise SystemExit(f"Failed to update {label} in {path}")
    if new_text != text:
        path.write_text(new_text, encoding="utf-8", newline="\n")
    return True


def update_tc_app_version(path: Path, version: str) -> None:
    replace_once(
        path,
        r"set\(TC_APP_VERSION\s+[\d.]+\)",
        f"set(TC_APP_VERSION {version})",
        "TC_APP_VERSION",
    )


def update_nsis_product_version(path: Path, version: str) -> None:
    replace_once(
        path,
        r'!define PRODUCT_VERSION\s+"[\d.]+"',
        f'!define PRODUCT_VERSION "{version}"',
        "PRODUCT_VERSION",
    )


def update_cargo_workspace_version(path: Path, version: str) -> None:
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r'(\[workspace\.package\][\s\S]*?^version\s*=\s*")[^"]+(")',
        re.MULTILINE,
    )

    def repl(match: re.Match[str]) -> str:
        return f"{match.group(1)}{version}{match.group(2)}"

    new_text, count = pattern.subn(repl, text, count=1)
    if count != 1:
        raise SystemExit(f"Failed to update [workspace.package] version in {path}")
    path.write_text(new_text, encoding="utf-8", newline="\n")


def update_cargo_package_version(path: Path, version: str) -> None:
    text = path.read_text(encoding="utf-8")
    if "version.workspace = true" in text:
        return
    pattern = re.compile(r'^(version\s*=\s*")[^"]+(")', re.MULTILINE)

    def repl(match: re.Match[str]) -> str:
        return f"{match.group(1)}{version}{match.group(2)}"

    new_text, count = pattern.subn(repl, text, count=1)
    if count != 1:
        raise SystemExit(f"Failed to update package version in {path}")
    path.write_text(new_text, encoding="utf-8", newline="\n")


def extract_nsis_version(path: Path) -> str | None:
    match = re.search(r'!define PRODUCT_VERSION\s+"([\d.]+)"', path.read_text(encoding="utf-8"))
    return match.group(1) if match else None


def extract_cargo_workspace_version(path: Path) -> str | None:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        r"(?ms)^\[workspace\.package\].*?^version\s*=\s*\"([^\"]+)\"",
        text,
    )
    return match.group(1) if match else None


def extract_cargo_package_version(path: Path) -> str | None:
    text = path.read_text(encoding="utf-8")
    if "version.workspace = true" in text:
        return None
    match = re.search(r'^version\s*=\s*"([^"]+)"', text, flags=re.MULTILINE)
    return match.group(1) if match else None


def version_targets() -> list[tuple[str, Path, str]]:
    return [
        ("C++ (TC_APP_VERSION)", VERSION_CMAKE, "cmake"),
        ("NSIS main installer", ROOT / "setup" / "proj_version.nsh", "nsis"),
        ("NSIS panel package", ROOT / "src" / "gr_panel" / "package" / "proj_version.nsh", "nsis"),
        ("Rust client workspace", ROOT / "rust_client" / "Cargo.toml", "cargo_workspace"),
        ("Rust base workspace", ROOT / "rust_base" / "Cargo.toml", "cargo_workspace"),
        ("Rust base protocol", ROOT / "rust_base" / "protocol" / "Cargo.toml", "cargo_package"),
        ("Rust server workspace", ROOT / "rust_server" / "Cargo.toml", "cargo_workspace"),
        ("Rust gr_cms_server", ROOT / "rust_server" / "gr_cms_server" / "Cargo.toml", "cargo_package"),
        ("Rust gr_auth_server", ROOT / "rust_server" / "gr_auth_server" / "Cargo.toml", "cargo_package"),
        ("Rust gr_desk_server", ROOT / "rust_server" / "gr_desk_server" / "Cargo.toml", "cargo_package"),
        ("Rust gr_updater", ROOT / "rust_server" / "gr_updater" / "Cargo.toml", "cargo_package"),
        ("Rust gr_stat_server", ROOT / "rust_server" / "gr_stat_server" / "Cargo.toml", "cargo_package"),
        ("Rust builder", ROOT / "rust_server" / "builder" / "Cargo.toml", "cargo_package"),
    ]


def read_version_for_target(kind: str, path: Path) -> str | None:
    if kind == "cmake":
        return read_tc_app_version() if path == VERSION_CMAKE else None
    if kind == "nsis":
        return extract_nsis_version(path)
    if kind == "cargo_workspace":
        return extract_cargo_workspace_version(path)
    if kind == "cargo_package":
        return extract_cargo_package_version(path)
    return None


def show_versions() -> None:
    print("Current product versions:")
    seen: dict[str, list[str]] = {}
    for label, path, kind in version_targets():
        if not path.is_file():
            print(f"  [missing] {label}: {path}")
            continue
        version = read_version_for_target(kind, path)
        if version is None:
            print(f"  [n/a]     {label}: {path}")
            continue
        print(f"  {version:<8}  {label}")
        seen.setdefault(version, []).append(label)

    print()
    if len(seen) == 1:
        only = next(iter(seen))
        print(f"All tracked locations use {only}.")
    elif seen:
        print("Version mismatch detected:")
        for version, labels in sorted(seen.items()):
            print(f"  {version}:")
            for label in labels:
                print(f"    - {label}")


def sync_cargo_locks() -> None:
    for workspace in (
        ROOT / "rust_client",
        ROOT / "rust_base",
        ROOT / "rust_server",
    ):
        manifest = workspace / "Cargo.toml"
        if not manifest.is_file():
            continue
        print(f"Updating {workspace.name}/Cargo.lock ...")
        subprocess.run(
            ["cargo", "generate-lockfile", "--manifest-path", str(manifest)],
            check=True,
        )


def apply_version(version: str) -> None:
    validate_version(version)

    update_tc_app_version(VERSION_CMAKE, version)
    update_nsis_product_version(ROOT / "setup" / "proj_version.nsh", version)
    update_nsis_product_version(ROOT / "src" / "gr_panel" / "package" / "proj_version.nsh", version)

    update_cargo_workspace_version(ROOT / "rust_client" / "Cargo.toml", version)
    update_cargo_workspace_version(ROOT / "rust_base" / "Cargo.toml", version)
    update_cargo_workspace_version(ROOT / "rust_server" / "Cargo.toml", version)

    rust_packages = [
        ROOT / "rust_base" / "protocol" / "Cargo.toml",
        ROOT / "rust_server" / "gr_cms_server" / "Cargo.toml",
        ROOT / "rust_server" / "gr_auth_server" / "Cargo.toml",
        ROOT / "rust_server" / "gr_desk_server" / "Cargo.toml",
        ROOT / "rust_server" / "gr_updater" / "Cargo.toml",
        ROOT / "rust_server" / "gr_stat_server" / "Cargo.toml",
        ROOT / "rust_server" / "builder" / "Cargo.toml",
    ]
    for path in rust_packages:
        update_cargo_package_version(path, version)

    sync_cargo_locks()

    print(f"Updated unified product version to {version}.")
    print()
    print("Next steps:")
    print("  1. Rebuild: .\\build_official.bat")
    print("  2. Rebuild servers if needed: .\\build_servers.bat")
    print("  3. Repackage installers under setup/ and src/gr_panel/package/")


def self_test() -> None:
    import tempfile

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)

        cmake = root / "version.cmake"
        cmake.write_text("set(TC_APP_VERSION 1.0.0)\n", encoding="utf-8")
        update_tc_app_version(cmake, "3.2.0")
        assert cmake.read_text(encoding="utf-8") == "set(TC_APP_VERSION 3.2.0)\n"

        nsis = root / "proj_version.nsh"
        nsis.write_text('!define PRODUCT_VERSION "1.0.0"\n', encoding="utf-8")
        update_nsis_product_version(nsis, "3.2.0")
        assert nsis.read_text(encoding="utf-8") == '!define PRODUCT_VERSION "3.2.0"\n'

        workspace = root / "Cargo.toml"
        workspace.write_text(
            '[workspace.package]\nversion = "2.0.1"\nedition = "2021"\n',
            encoding="utf-8",
        )
        update_cargo_workspace_version(workspace, "3.2.0")
        assert 'version = "3.2.0"' in workspace.read_text(encoding="utf-8")

        package = root / "pkg.toml"
        package.write_text(
            '[package]\nname = "demo"\nversion = "0.1.0"\nedition = "2021"\n',
            encoding="utf-8",
        )
        update_cargo_package_version(package, "3.2.0")
        assert package.read_text(encoding="utf-8").splitlines()[2] == 'version = "3.2.0"'

    print("set_app_version self-test passed.")


def main() -> None:
    args = parse_args()

    if args.self_test:
        self_test()
        return

    if args.show or args.version is None:
        show_versions()
        if args.version is None and not args.show:
            print()
            print("Usage: python set_app_version.py 3.2.0")
        return

    apply_version(validate_version(args.version))


if __name__ == "__main__":
    main()
