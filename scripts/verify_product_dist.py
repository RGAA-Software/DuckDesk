#!/usr/bin/env python3
"""Verify a product dist directory against its generated SHA-256 manifests."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
from pathlib import Path


WINDOWS_PRODUCTS = {"cloud_node", "client", "remote"}
HOST_PRODUCTS = {"cloud_node", "remote"}
RETIRED_RDP_FILES = {
    "freerdp-proxy.exe",
    "freerdp-server-proxy3.dll",
    "freerdp-server3.dll",
    "freerdp-client3.dll",
    "freerdp3.dll",
    "winpr3.dll",
    "proxy-pixels-policy-plugin.dll",
    "pixels-rdp-sdk.json",
}
REQUIRED_RDP_CLIENT_FILES = {
    "px_rdp_client.dll",
    "px_rdp_core.dll",
    "px_rdp_winpr.dll",
}
REQUIRED_RDP_HOST_FILES = {
    "rdp/px_rdp_proxy.exe",
    "rdp/px_rdp_server_proxy.dll",
    "rdp/px_rdp_server.dll",
    "rdp/px_rdp_client.dll",
    "rdp/px_rdp_core.dll",
    "rdp/px_rdp_winpr.dll",
    "rdp/proxy/px_rdp_policy.dll",
}
REMOTE_FORBIDDEN_DEPENDENCIES = {
    "libcef.dll",
    "easyhook.dll",
    "easyhook32.dll",
    "easyhook64.dll",
    "easyload32.dll",
    "easyload64.dll",
    "px_gh.dll",
}
DEPENDENCY_LINE = re.compile(r"^\s*([^\s]+\.(?:dll|exe))\s*$", re.IGNORECASE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dist_dir", type=Path)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def find_dumpbin() -> Path:
    from_path = shutil.which("dumpbin")
    if from_path:
        return Path(from_path).resolve()

    program_files_x86 = os.environ.get("ProgramFiles(x86)")
    if not program_files_x86:
        raise RuntimeError("ProgramFiles(x86) is unavailable; cannot locate dumpbin.exe")
    vswhere = Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        raise RuntimeError(f"cannot locate Visual Studio discovery tool: {vswhere}")
    result = subprocess.run(
        [
            str(vswhere),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    installation = Path(result.stdout.strip())
    candidates = sorted(
        (installation / "VC" / "Tools" / "MSVC").glob("*/bin/Hostx64/x64/dumpbin.exe"),
        reverse=True,
    )
    if not candidates:
        candidates = sorted(
            (installation / "VC" / "Tools" / "MSVC").glob("*/bin/Hostx86/x86/dumpbin.exe"),
            reverse=True,
        )
    if not candidates:
        raise RuntimeError(f"cannot locate dumpbin.exe below Visual Studio installation: {installation}")
    return candidates[0].resolve()


def pe_dependencies(dumpbin: Path, path: Path) -> set[str]:
    result = subprocess.run(
        [str(dumpbin), "/nologo", "/dependents", str(path)],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return {
        match.group(1).lower()
        for line in result.stdout.splitlines()
        if (match := DEPENDENCY_LINE.match(line))
    }


def verify_windows_product_boundary(dist_dir: Path, product: str, actual_files: set[str]) -> None:
    lower_files = {relative.lower() for relative in actual_files}
    retired = {Path(relative).name.lower() for relative in actual_files} & RETIRED_RDP_FILES
    if retired:
        raise RuntimeError(f"distribution contains retired RDP artifact names: {sorted(retired)}")

    missing_client_rdp = REQUIRED_RDP_CLIENT_FILES - lower_files
    if missing_client_rdp:
        raise RuntimeError(f"distribution is missing RDP client artifacts: {sorted(missing_client_rdp)}")
    if product in HOST_PRODUCTS:
        missing_host_rdp = REQUIRED_RDP_HOST_FILES - lower_files
        if missing_host_rdp:
            raise RuntimeError(f"host distribution is missing RDP host artifacts: {sorted(missing_host_rdp)}")

    dumpbin = find_dumpbin()
    pe_files = sorted(
        relative for relative in actual_files if Path(relative).suffix.lower() in {".exe", ".dll"}
    )
    dependencies_by_file = {
        relative: pe_dependencies(dumpbin, dist_dir / relative)
        for relative in pe_files
    }
    retired_dependencies: dict[str, list[str]] = {}
    for relative, dependencies in dependencies_by_file.items():
        matches = dependencies & RETIRED_RDP_FILES
        if matches:
            retired_dependencies[relative] = sorted(matches)
    if retired_dependencies:
        raise RuntimeError(f"PE files import retired RDP names: {retired_dependencies}")

    client_dependencies = dependencies_by_file.get("px_client.exe", set())
    missing_client_imports = REQUIRED_RDP_CLIENT_FILES - client_dependencies
    if missing_client_imports:
        raise RuntimeError(f"px_client.exe is missing RDP imports: {sorted(missing_client_imports)}")

    if product == "remote":
        forbidden_dependencies: dict[str, list[str]] = {}
        for relative, dependencies in dependencies_by_file.items():
            matches = dependencies & REMOTE_FORBIDDEN_DEPENDENCIES
            if matches:
                forbidden_dependencies[relative] = sorted(matches)
        if forbidden_dependencies:
            raise RuntimeError(f"Remote PE dependency boundary violation: {forbidden_dependencies}")


def main() -> int:
    args = parse_args()
    dist_dir = args.dist_dir.resolve()
    product_manifest_path = dist_dir / "product-manifest.json"
    sums_path = dist_dir / "sha256sums.json"
    product_manifest = json.loads(product_manifest_path.read_text(encoding="utf-8"))
    sums = json.loads(sums_path.read_text(encoding="utf-8"))
    manifest_sums = {item["path"]: item["sha256"] for item in product_manifest["artifacts"]}
    if sums != manifest_sums:
        raise RuntimeError("product-manifest.json and sha256sums.json disagree")

    generated_manifests = {"product-manifest.json", "sha256sums.json", "licenses.json"}
    actual_files = {
        path.relative_to(dist_dir).as_posix()
        for path in dist_dir.rglob("*")
        if path.is_file() and path.name not in generated_manifests
    }
    expected_files = set(sums)
    if actual_files != expected_files:
        missing = sorted(expected_files - actual_files)
        extra = sorted(actual_files - expected_files)
        raise RuntimeError(f"dist file set mismatch; missing={missing}, extra={extra}")
    product = product_manifest.get("product")
    if product in WINDOWS_PRODUCTS:
        verify_windows_product_boundary(dist_dir, str(product), actual_files)
    for relative, expected_hash in sums.items():
        actual_hash = sha256(dist_dir / relative)
        if actual_hash != expected_hash:
            raise RuntimeError(f"SHA-256 mismatch: {relative}")
    licenses = json.loads((dist_dir / "licenses.json").read_text(encoding="utf-8"))
    expected_licenses = sorted(path for path in sums if "license" in path.lower() or path.endswith("SOURCE.md"))
    if licenses != {"files": expected_licenses}:
        raise RuntimeError("licenses.json does not match the packaged license files")
    print(
        f"Verified {product_manifest['product']} {product_manifest['product_version']}: "
        f"{len(sums)} artifacts with matching SHA-256 hashes and product dependency boundaries."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
