#!/usr/bin/env python3
"""Set a unified product version for all app executables and installers."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VERSION_CMAKE = ROOT / "src" / "px_base" / "version.cmake"

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
        "--code",
        type=int,
        help="Explicit version code override (default: major*10000 + minor*100 + patch).",
    )
    parser.add_argument(
        "--show",
        action="store_true",
        help="Only print current versions; do not modify files.",
    )
    parser.add_argument(
        "--bump",
        action="store_true",
        help="Auto-increment the current version: patch += 1; when patch would reach "
        "100, bump minor and reset patch to 0 (e.g. 3.2.99 -> 3.3.0).",
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="Run built-in replacement tests and exit.",
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


def semver_to_version_code(version: str) -> int:
    major, minor, patch = (int(part) for part in version.split("."))
    return major * 10000 + minor * 100 + patch


def read_tc_app_version_code() -> int:
    text = VERSION_CMAKE.read_text(encoding="utf-8")
    match = re.search(r"set\(TC_APP_VERSION_CODE\s+(\d+)\)", text)
    if not match:
        raise SystemExit(f"Cannot read TC_APP_VERSION_CODE from {VERSION_CMAKE}")
    return int(match.group(1))


def update_tc_app_version_code(path: Path, version_code: int) -> None:
    replace_once(
        path,
        r"set\(TC_APP_VERSION_CODE\s+\d+\)",
        f"set(TC_APP_VERSION_CODE {version_code})",
        "TC_APP_VERSION_CODE",
    )


def update_nsis_product_version_code(path: Path, version_code: int) -> None:
    replace_once(
        path,
        r"!define PRODUCT_VERSION_CODE\s+\d+",
        f"!define PRODUCT_VERSION_CODE {version_code}",
        "PRODUCT_VERSION_CODE",
    )


def extract_nsis_version_code(path: Path) -> int | None:
    match = re.search(
        r"!define PRODUCT_VERSION_CODE\s+(\d+)",
        path.read_text(encoding="utf-8"),
    )
    return int(match.group(1)) if match else None


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
    # 版本管理边界：GammaRay 相关的 exe（C++ 客户端、rust_client、rust_base）
    # 统一由本脚本随 build_official 递增；rust_server 下的每个服务独立管理
    # 自己的版本（见 rust_server/set_server_version.py）。
    return [
        ("C++ (TC_APP_VERSION)", VERSION_CMAKE, "cmake"),
        ("NSIS main installer", ROOT / "setup" / "proj_version.nsh", "nsis"),
        ("NSIS panel package", ROOT / "src" / "px_panel" / "package" / "proj_version.nsh", "nsis"),
        ("Rust client workspace", ROOT / "rust_client" / "Cargo.toml", "cargo_workspace"),
        ("Rust base workspace", ROOT / "rust_base" / "Cargo.toml", "cargo_workspace"),
        ("Rust base protocol", ROOT / "rust_base" / "protocol" / "Cargo.toml", "cargo_package"),
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
    version_name = read_tc_app_version()
    version_code = read_tc_app_version_code()
    print(f"Current product version name: {version_name}")
    print(f"Current product version code: {version_code}")
    print()
    print("Tracked version name locations:")
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
    print("Tracked version code locations:")
    code_seen: dict[int, list[str]] = {}
    for label, path in (
        ("C++ (TC_APP_VERSION_CODE)", VERSION_CMAKE),
        ("NSIS main installer", ROOT / "setup" / "proj_version.nsh"),
        ("NSIS panel package", ROOT / "src" / "px_panel" / "package" / "proj_version.nsh"),
    ):
        if not path.is_file():
            print(f"  [missing] {label}: {path}")
            continue
        if path == VERSION_CMAKE:
            code = read_tc_app_version_code()
        else:
            code = extract_nsis_version_code(path)
        if code is None:
            print(f"  [n/a]     {label}: {path}")
            continue
        print(f"  {code:<8}  {label}")
        code_seen.setdefault(code, []).append(label)

    print()
    if len(seen) == 1:
        only = next(iter(seen))
        print(f"All tracked version names use {only}.")
    elif seen:
        print("Version name mismatch detected:")
        for version, labels in sorted(seen.items()):
            print(f"  {version}:")
            for label in labels:
                print(f"    - {label}")

    if len(code_seen) == 1:
        only_code = next(iter(code_seen))
        print(f"All tracked version codes use {only_code}.")
    elif code_seen:
        print("Version code mismatch detected:")
        for code, labels in sorted(code_seen.items()):
            print(f"  {code}:")
            for label in labels:
                print(f"    - {label}")


def sync_cargo_locks() -> None:
    # 注意：不能用 `cargo generate-lockfile`——它会把所有依赖升到
    # "latest compatible"（包括 git 依赖拉新 revision），曾导致 gpui 出现两个
    # 不兼容 revision 使 px_sysinfo 编译失败。`cargo metadata` 只做保守解析：
    # 保留现有锁定版本，仅同步本地 workspace 包的版本号变更。
    # rust_server 的 lock 由各服务自己的版本脚本同步（set_server_version.py）。
    for workspace in (
        ROOT / "rust_client",
        ROOT / "rust_base",
    ):
        manifest = workspace / "Cargo.toml"
        if not manifest.is_file():
            continue
        print(f"Syncing {workspace.name}/Cargo.lock ...")
        subprocess.run(
            [
                "cargo",
                "metadata",
                "--format-version",
                "1",
                "--manifest-path",
                str(manifest),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )


def apply_version(version: str, version_code: int | None = None) -> None:
    validate_version(version)
    code = version_code if version_code is not None else semver_to_version_code(version)

    update_tc_app_version(VERSION_CMAKE, version)
    update_tc_app_version_code(VERSION_CMAKE, code)
    update_nsis_product_version(ROOT / "setup" / "proj_version.nsh", version)
    update_nsis_product_version_code(ROOT / "setup" / "proj_version.nsh", code)
    update_nsis_product_version(ROOT / "src" / "px_panel" / "package" / "proj_version.nsh", version)
    update_nsis_product_version_code(
        ROOT / "src" / "px_panel" / "package" / "proj_version.nsh",
        code,
    )

    update_cargo_workspace_version(ROOT / "rust_client" / "Cargo.toml", version)
    update_cargo_workspace_version(ROOT / "rust_base" / "Cargo.toml", version)

    rust_packages = [
        ROOT / "rust_base" / "protocol" / "Cargo.toml",
    ]
    for path in rust_packages:
        update_cargo_package_version(path, version)

    sync_cargo_locks()

    print(f"Updated unified product version to {version} (code {code}).")
    print()
    print("Next steps:")
    print("  1. Rebuild: .\\build_official.bat")
    print("  2. Rebuild servers if needed: .\\build_gr_cms_server.bat / build_gr_auth_server.bat / build_gr_desk_server.bat")
    print("  3. Repackage installers under setup/ and src/px_panel/package/")


def self_test() -> None:
    import tempfile

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)

        cmake = root / "version.cmake"
        cmake.write_text(
            "set(TC_APP_VERSION 1.0.0)\nset(TC_APP_VERSION_CODE 10000)\n",
            encoding="utf-8",
        )
        update_tc_app_version(cmake, "3.2.0")
        update_tc_app_version_code(cmake, 30200)
        assert cmake.read_text(encoding="utf-8") == (
            "set(TC_APP_VERSION 3.2.0)\nset(TC_APP_VERSION_CODE 30200)\n"
        )

        nsis = root / "proj_version.nsh"
        nsis.write_text(
            '!define PRODUCT_VERSION "1.0.0"\n!define PRODUCT_VERSION_CODE 10000\n',
            encoding="utf-8",
        )
        update_nsis_product_version(nsis, "3.2.0")
        update_nsis_product_version_code(nsis, 30200)
        assert nsis.read_text(encoding="utf-8") == (
            '!define PRODUCT_VERSION "3.2.0"\n!define PRODUCT_VERSION_CODE 30200\n'
        )

        assert semver_to_version_code("3.2.0") == 30200
        assert semver_to_version_code("10.20.30") == 102030

        assert bump_version("3.2.0") == "3.2.1"
        assert bump_version("3.2.98") == "3.2.99"
        assert bump_version("3.2.99") == "3.3.0"
        assert bump_version("3.9.99") == "3.10.0"

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

    if args.bump:
        current = read_tc_app_version()
        new_version = bump_version(current)
        print(f"Bumping product version: {current} -> {new_version}")
        apply_version(new_version)
        return

    if args.show or args.version is None:
        show_versions()
        if args.version is None and not args.show:
            print()
            print("Usage: python set_app_version.py 3.2.0 [--code 30200]")
            print("       python set_app_version.py --bump")
        return

    apply_version(validate_version(args.version), args.code)


if __name__ == "__main__":
    main()
