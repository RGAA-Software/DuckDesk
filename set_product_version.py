#!/usr/bin/env python3
"""Manage independent Pixels product release versions."""

from __future__ import annotations

import argparse
import contextlib
import json
import os
import re
import sys
import tempfile
from pathlib import Path
from typing import Iterator

ROOT = Path(__file__).resolve().parent
PRODUCT_DIR = ROOT / "packaging" / "products"
PRODUCTS = ("cloud_node", "client", "remote", "android")
CAPABILITIES = (
    "desktop_client",
    "desktop_host",
    "file_transfer",
    "rdp_client",
    "rdp_host",
    "cloud_app_catalog",
    "cloud_app_host",
    "game_hook",
    "webview_host",
    "browser_remote",
    "virtual_display",
    "joystick",
    "system_information",
)
SEMVER_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")
VERSION_LINE_RE = re.compile(r'(?m)^product_version\s*=\s*"[^"]*"\s*$')
VERSION_CODE_LINE_RE = re.compile(r"(?m)^product_version_code\s*=\s*\d+\s*$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Show or update one Pixels product version.")
    parser.add_argument("version", nargs="?", help="Explicit X.Y.Z version to store.")
    parser.add_argument("--product", choices=PRODUCTS, help="Product version line to operate on.")
    parser.add_argument("--bump", action="store_true", help="Increment this product once.")
    parser.add_argument("--show", action="store_true", help="Read the version without modifying it.")
    parser.add_argument("--json", action="store_true", help="Write the result as one JSON object.")
    parser.add_argument("--cmake", action="store_true", help="Write validated manifest values as CMake declarations.")
    parser.add_argument("--self-test", action="store_true", help="Run isolated built-in tests.")
    args = parser.parse_args()

    actions = int(bool(args.version)) + int(args.bump) + int(args.show)
    if args.self_test:
        if args.product or actions or args.json or args.cmake:
            parser.error("--self-test cannot be combined with product operations")
        return args
    if not args.product:
        parser.error("--product is required")
    if actions > 1:
        parser.error("choose exactly one of VERSION, --bump, or --show")
    if args.json and args.cmake:
        parser.error("--json and --cmake are mutually exclusive")
    if actions == 0:
        args.show = True
    return args


def parse_version(version: str) -> tuple[int, int, int]:
    match = SEMVER_RE.fullmatch(version.strip())
    if not match:
        raise ValueError(f"invalid version '{version}'; expected X.Y.Z")
    major, minor, patch = (int(value) for value in match.groups())
    if minor > 99 or patch > 99:
        raise ValueError("minor and patch components must be between 0 and 99")
    return major, minor, patch


def version_code(version: str) -> int:
    major, minor, patch = parse_version(version)
    code = major * 10000 + minor * 100 + patch
    if code <= 0 or code > 2_100_000_000:
        raise ValueError(f"version '{version}' is outside the supported Android versionCode range")
    return code


def bump_version(version: str) -> str:
    major, minor, patch = parse_version(version)
    patch += 1
    if patch == 100:
        patch = 0
        minor += 1
    if minor == 100:
        minor = 0
        major += 1
    bumped = f"{major}.{minor}.{patch}"
    version_code(bumped)
    return bumped


def manifest_path(product: str, product_dir: Path = PRODUCT_DIR) -> Path:
    return product_dir / f"{product}.toml"


def load_manifest(product: str, product_dir: Path = PRODUCT_DIR) -> dict[str, object]:
    path = manifest_path(product, product_dir)
    try:
        import tomllib

        with path.open("rb") as stream:
            manifest = tomllib.load(stream)
    except (OSError, ValueError) as exc:
        raise ValueError(f"cannot read product manifest {path}: {exc}") from exc

    if manifest.get("product") != product:
        raise ValueError(f"manifest {path} does not declare product = \"{product}\"")
    if manifest.get("company") != "Pixels":
        raise ValueError(f"manifest {path} must declare company = \"Pixels\"")
    current_version = manifest.get("product_version")
    current_code = manifest.get("product_version_code")
    if not isinstance(current_version, str) or not isinstance(current_code, int):
        raise ValueError(f"manifest {path} has invalid version fields")
    expected_code = version_code(current_version)
    if current_code != expected_code:
        raise ValueError(
            f"manifest {path} has version code {current_code}; expected {expected_code} for {current_version}"
        )
    capabilities = manifest.get("capabilities")
    if not isinstance(capabilities, list) or not all(isinstance(item, str) for item in capabilities):
        raise ValueError(f"manifest {path} has an invalid capabilities list")
    unknown_capabilities = sorted(set(capabilities) - set(CAPABILITIES))
    if unknown_capabilities:
        raise ValueError(f"manifest {path} has unknown capabilities: {', '.join(unknown_capabilities)}")
    if len(capabilities) != len(set(capabilities)):
        raise ValueError(f"manifest {path} contains duplicate capabilities")
    return manifest


def atomic_store_version(path: Path, new_version: str) -> None:
    raw = path.read_bytes()
    newline = b"\r\n" if b"\r\n" in raw else b"\n"
    text = raw.decode("utf-8")
    code = version_code(new_version)
    text, version_count = VERSION_LINE_RE.subn(f'product_version = "{new_version}"', text)
    text, code_count = VERSION_CODE_LINE_RE.subn(f"product_version_code = {code}", text)
    if version_count != 1 or code_count != 1:
        raise ValueError(f"manifest {path} must contain exactly one product version and version code")

    payload = text.replace("\r\n", "\n").replace("\n", newline.decode("ascii")).encode("utf-8")
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.stem}.", suffix=".tmp", dir=path.parent)
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, path)
    finally:
        temporary_path.unlink(missing_ok=True)


@contextlib.contextmanager
def version_lock(product_dir: Path = PRODUCT_DIR) -> Iterator[None]:
    lock_path = product_dir / ".version.lock"
    product_dir.mkdir(parents=True, exist_ok=True)
    with lock_path.open("a+b") as stream:
        stream.seek(0, os.SEEK_END)
        if stream.tell() == 0:
            stream.write(b"0")
            stream.flush()
        stream.seek(0)
        if os.name == "nt":
            import msvcrt

            msvcrt.locking(stream.fileno(), msvcrt.LK_LOCK, 1)
            try:
                yield
            finally:
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl

            fcntl.flock(stream.fileno(), fcntl.LOCK_EX)
            try:
                yield
            finally:
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


def result_for(product: str, manifest: dict[str, object]) -> dict[str, object]:
    result = {
        "product": product,
        "company": manifest["company"],
        "product_version": manifest["product_version"],
        "product_version_code": manifest["product_version_code"],
    }
    edition = manifest.get("edition")
    if edition is not None:
        result["edition"] = edition
    return result


def print_result(result: dict[str, object], as_json: bool) -> None:
    if as_json:
        print(json.dumps(result, ensure_ascii=False, separators=(",", ":")))
        return
    edition = f" [{result['edition']}]" if "edition" in result else ""
    print(f"{result['product']}{edition}: {result['product_version']} ({result['product_version_code']})")


def print_cmake(product: str, manifest: dict[str, object]) -> None:
    print(f'set(PX_PRODUCT_NAME "{product}")')
    print(f'set(PX_PRODUCT_COMPANY "{manifest["company"]}")')
    edition = manifest.get("edition")
    if edition is not None:
        print(f'set(PX_PRODUCT_EDITION "{edition}")')
    print(f'set(PX_PRODUCT_VERSION "{manifest["product_version"]}")')
    print(f'set(PX_PRODUCT_VERSION_CODE "{manifest["product_version_code"]}")')
    enabled = set(manifest["capabilities"])
    for capability in CAPABILITIES:
        value = "ON" if capability in enabled else "OFF"
        print(f"set(PX_CAPABILITY_{capability.upper()} {value})")


def run_self_test() -> None:
    assert bump_version("3.3.67") == "3.3.68"
    assert bump_version("3.3.99") == "3.4.0"
    assert bump_version("3.99.99") == "4.0.0"
    assert version_code("3.3.67") == 30367

    with tempfile.TemporaryDirectory() as directory:
        product_dir = Path(directory)
        for product, version in (("cloud_node", "3.3.67"), ("android", "1.0.0")):
            path = manifest_path(product, product_dir)
            edition = '\nedition = "CLOUD_NODE"' if product == "cloud_node" else ""
            path.write_text(
                f'product = "{product}"{edition}\nproduct_version = "{version}"\n'
                f'company = "Pixels"\nproduct_version_code = {version_code(version)}\ncapabilities = []\n',
                encoding="utf-8",
            )
        with version_lock(product_dir):
            cloud = load_manifest("cloud_node", product_dir)
            atomic_store_version(manifest_path("cloud_node", product_dir), bump_version(str(cloud["product_version"])))
        assert load_manifest("cloud_node", product_dir)["product_version"] == "3.3.68"
        assert load_manifest("android", product_dir)["product_version"] == "1.0.0"
    print("set_product_version self-test passed")


def main() -> int:
    args = parse_args()
    if args.self_test:
        run_self_test()
        return 0

    try:
        if args.show:
            manifest = load_manifest(args.product)
        else:
            with version_lock():
                manifest = load_manifest(args.product)
                requested_version = (
                    bump_version(str(manifest["product_version"])) if args.bump else args.version
                )
                atomic_store_version(manifest_path(args.product), requested_version)
                manifest = load_manifest(args.product)
        if args.cmake:
            print_cmake(args.product, manifest)
        else:
            print_result(result_for(args.product, manifest), args.json)
        return 0
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
