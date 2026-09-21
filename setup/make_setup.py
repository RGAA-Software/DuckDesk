#!/usr/bin/env python3
"""Create one strict Pixels Windows product installer from its verified dist."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from windows_release_signing import (  # noqa: E402
    load_configuration,
    nsis_finalize_command,
    preflight,
    sign_file,
)
from oem_release_profile import OemReleaseProfile, OemWindowsProductIdentity, load_oem_release_profile  # noqa: E402


PRODUCTS = ("cloud_node", "client", "remote")
HOST_PRODUCTS = {"cloud_node", "remote"}
MINIMUM_NSIS_VERSION = (3, 11)
FORBIDDEN_FILES = {
    "client": {
        "px_render.exe",
        "px_service.exe",
        "px_service_manager.exe",
        "px_function.exe",
        "px_display.exe",
        "px_joystick.exe",
        "libcef.dll",
        "px_gh.dll",
        "px_gh_injector.exe",
        "px_gh_address.exe",
    },
    "remote": {
        "libcef.dll",
        "chrome_elf.dll",
        "px_gh.dll",
        "px_gh_injector.exe",
        "px_gh_address.exe",
    },
}
REQUIRED_HOST_FILES = {
    "px_render.exe",
    "px_service.exe",
    "px_service_manager.exe",
    "px_function.exe",
    "px_display.exe",
    "px_joystick.exe",
    "rdp/px_rdp_proxy.exe",
    "rdp/px_rdp_server_proxy.dll",
    "rdp/px_rdp_server.dll",
    "rdp/proxy/px_rdp_policy.dll",
}
RETIRED_RDP_NAMES = {
    "freerdp-proxy.exe",
    "freerdp-server-proxy3.dll",
    "freerdp-server3.dll",
    "freerdp-client3.dll",
    "freerdp3.dll",
    "winpr3.dll",
    "proxy-pixels-policy-plugin.dll",
    "pixels-rdp-sdk.json",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", required=True, choices=PRODUCTS)
    parser.add_argument("--distribution", required=True, choices=("official", "customer", "oem"))
    parser.add_argument("--oem-profile", type=Path, help="Immutable OEM release profile; required only for OEM builds")
    parser.add_argument("--dist-dir", type=Path, help="Verified product dist directory")
    parser.add_argument("--output-root", type=Path, help="Installer output root")
    parser.add_argument("--preflight-only", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    return parser.parse_args()


def load_json(path: Path) -> dict[str, object]:
    with path.open("r", encoding="utf-8") as source:
        value = json.load(source)
    if not isinstance(value, dict):
        raise RuntimeError(f"expected a JSON object: {path}")
    return value


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def load_product_config(repo_root: Path, product: str) -> dict[str, object]:
    path = repo_root / "packaging" / "products" / f"{product}.toml"
    with path.open("rb") as source:
        config = tomllib.load(source)
    if config.get("product") != product or config.get("company") != "Pixels":
        raise RuntimeError(f"invalid Pixels product manifest: {path}")
    return config


def validate_dist(
    repo_root: Path,
    product: str,
    distribution: str,
    release_namespace: str,
    oem_id: str | None,
    company: str,
    oem_profile_sha256: str | None,
    dist_dir: Path,
    config: dict[str, object],
) -> dict[str, object]:
    if not dist_dir.is_dir():
        raise RuntimeError(f"product dist folder not found: {dist_dir}")
    subprocess.run(
        ["python", str(repo_root / "scripts" / "verify_product_dist.py"), str(dist_dir)],
        check=True,
    )
    manifest = load_json(dist_dir / "product-manifest.json")
    expected_identity = {
        "product": product,
        "distribution": distribution,
        "release_namespace": release_namespace,
        "oem_id": oem_id,
        "company": company,
        "oem_profile_sha256": oem_profile_sha256,
        "product_version": config["product_version"],
        "product_version_code": config["product_version_code"],
    }
    actual_identity = {key: manifest.get(key) for key in expected_identity}
    if actual_identity != expected_identity:
        raise RuntimeError(f"dist identity mismatch: expected={expected_identity}, actual={actual_identity}")

    present = {path.relative_to(dist_dir).as_posix() for path in dist_dir.rglob("*") if path.is_file()}
    retired_rdp = {Path(path).name for path in present} & RETIRED_RDP_NAMES
    if retired_rdp:
        raise RuntimeError(f"installer payload contains retired RDP artifact names: {sorted(retired_rdp)}")
    missing_base = {"px_panel.exe", "px_client.exe", "px_osinfo.exe"} - present
    if missing_base:
        raise RuntimeError(f"installer payload is missing base files: {sorted(missing_base)}")
    if product in HOST_PRODUCTS:
        missing_host = REQUIRED_HOST_FILES - present
        if missing_host:
            raise RuntimeError(f"host installer payload is missing files: {sorted(missing_host)}")
    forbidden = FORBIDDEN_FILES.get(product, set()) & present
    if forbidden:
        raise RuntimeError(f"{product} installer payload contains forbidden files: {sorted(forbidden)}")
    return manifest


def load_tool_config(setup_dir: Path) -> dict[str, str]:
    path = setup_dir / "make_setup_config.json"
    return load_json(path) if path.is_file() else {}


def nsis_version(makensis: Path) -> tuple[int, ...]:
    result = subprocess.run(
        [str(makensis), "/VERSION"],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    version_text = result.stdout.strip().removeprefix("v")
    if not version_text or any(not component.isdigit() for component in version_text.split(".")):
        raise RuntimeError(f"cannot parse NSIS version from {makensis}: {result.stdout!r}")
    return tuple(int(component) for component in version_text.split("."))


def require_supported_nsis(makensis: Path) -> Path:
    version = nsis_version(makensis)
    if version < MINIMUM_NSIS_VERSION:
        required = ".".join(str(component) for component in MINIMUM_NSIS_VERSION)
        actual = ".".join(str(component) for component in version)
        raise RuntimeError(
            f"NSIS {required} or newer is required for signed uninstallers and the SYSTEM security fix; "
            f"got {actual}: {makensis}"
        )
    return makensis.resolve()


def find_nsis(configured_dir: str | None, repo_root: Path) -> Path:
    if configured_dir:
        configured_path = Path(configured_dir) / "makensis.exe"
        if not configured_path.is_file():
            raise RuntimeError(f"configured makensis.exe is missing: {configured_path}")
        return require_supported_nsis(configured_path)
    candidates = [
        repo_root / "tools" / "nsis" / "makensis.exe",
        Path(r"C:\Program Files (x86)\NSIS\makensis.exe"),
        Path(r"C:\Program Files\NSIS\makensis.exe"),
    ]
    for path in candidates:
        if path and path.is_file():
            return require_supported_nsis(path)
    raise RuntimeError("Cannot find makensis.exe; configure setup/make_setup_config.json")


def validate_pinned_nsis(repo_root: Path, makensis: Path) -> None:
    expected_root = (repo_root / "tools" / "nsis").resolve()
    if makensis.resolve() != expected_root / "makensis.exe":
        raise RuntimeError(f"release builds must use the pinned repository NSIS toolchain: {expected_root}")
    toolchain_manifest = load_json(repo_root / "packaging" / "windows_toolchain.json")
    nsis_manifest = toolchain_manifest.get("nsis")
    if toolchain_manifest.get("schema_version") != 1 or not isinstance(nsis_manifest, dict):
        raise RuntimeError("Windows toolchain manifest is invalid")
    expected_version = nsis_manifest.get("version")
    actual_version = ".".join(str(component) for component in nsis_version(makensis))
    if expected_version != actual_version:
        raise RuntimeError(f"pinned NSIS version mismatch: expected={expected_version}, actual={actual_version}")
    expected_files = nsis_manifest.get("files")
    if not isinstance(expected_files, dict) or not expected_files:
        raise RuntimeError("Windows toolchain manifest has no NSIS file inventory")
    for relative_path, expected_sha256 in expected_files.items():
        if not isinstance(relative_path, str) or not isinstance(expected_sha256, str):
            raise RuntimeError("Windows toolchain manifest has an invalid NSIS file entry")
        tool_path = expected_root / relative_path
        if not tool_path.is_file() or sha256(tool_path) != expected_sha256:
            raise RuntimeError(f"pinned NSIS toolchain hash mismatch: {relative_path}")


def stage_payload(dist_dir: Path, payload_directory: Path) -> None:
    shutil.copytree(dist_dir, payload_directory)
    source_files = {
        path.relative_to(dist_dir).as_posix(): sha256(path)
        for path in dist_dir.rglob("*")
        if path.is_file()
    }
    staged_files = {
        path.relative_to(payload_directory).as_posix(): sha256(path)
        for path in payload_directory.rglob("*")
        if path.is_file()
    }
    if staged_files != source_files:
        raise RuntimeError("staged installer payload does not exactly match the verified product dist")


def create_installer(
    makensis: Path,
    setup_dir: Path,
    staging_dir: Path,
    product: str,
    distribution: str,
    version: str,
    version_code: int,
    company: str,
    publisher_name: str,
    release_namespace: str,
    oem_id: str | None,
    product_identity: OemWindowsProductIdentity | None,
    icon_path: Path | None,
    uninstaller_sign_command: str,
) -> Path:
    definition_values = {
        "OUTPUT_DIR": str(staging_dir),
        "PRODUCT_ID": product,
        "DISTRIBUTION": distribution,
        "RELEASE_NAMESPACE": release_namespace,
        "OEM_ID": oem_id or "",
        "PRODUCT_VERSION": version,
        "PRODUCT_VERSION_CODE": str(version_code),
        "COMPANY": company,
        "PUBLISHER_NAME": publisher_name,
        "UNINSTALL_SIGN_COMMAND": uninstaller_sign_command,
    }
    if product_identity is not None:
        definition_values.update(
            {
                "OEM_PRODUCT_NAME": product_identity.product_name,
                "OEM_INSTALL_DIRECTORY_NAME": product_identity.install_directory_name,
                "OEM_UNINSTALL_KEY": product_identity.uninstall_key,
                "OEM_INSTALLER_BASENAME": product_identity.installer_basename,
                "OEM_ICON": str(icon_path),
            }
        )
    profile_definition_names = {
        "COMPANY",
        "PUBLISHER_NAME",
        "OEM_PRODUCT_NAME",
        "OEM_INSTALL_DIRECTORY_NAME",
        "OEM_UNINSTALL_KEY",
        "OEM_INSTALLER_BASENAME",
        "OEM_ICON",
    }
    for definition_name, definition_value in definition_values.items():
        empty_oem_id = definition_name == "OEM_ID" and not definition_value
        has_line_break = any(character in definition_value for character in ("\r", "\n"))
        has_unsafe_profile_character = definition_name in profile_definition_names and any(
            character in definition_value for character in ('"', "$")
        )
        if (not definition_value and not empty_oem_id) or has_line_break or has_unsafe_profile_character:
            raise RuntimeError(f"installer definition cannot be represented safely: {definition_name}")
    makensis_arguments = [
        str(makensis),
        *(f"/D{definition_name}={definition_value}" for definition_name, definition_value in definition_values.items()),
        str(setup_dir / "make_setup.nsi"),
    ]
    subprocess.run(
        makensis_arguments,
        cwd=setup_dir,
        check=True,
    )
    basename = (
        product_identity.installer_basename
        if product_identity is not None
        else {"cloud_node": "PixelsCloudNode", "client": "PixelsClient", "remote": "PixelsRemote"}[product]
    )
    installer = staging_dir / f"{basename}_{distribution}_{version}_Setup.exe"
    if not installer.is_file():
        raise RuntimeError(f"NSIS did not create expected installer: {installer}")
    return installer


def main() -> int:
    args = parse_args()
    setup_dir = Path(__file__).resolve().parent
    repo_root = setup_dir.parent
    config = load_product_config(repo_root, args.product)
    if args.distribution == "oem":
        if args.oem_profile is None:
            raise RuntimeError("OEM installers require --oem-profile")
        oem_profile: OemReleaseProfile | None = load_oem_release_profile(args.oem_profile)
        release_namespace = oem_profile.release_namespace
        oem_id: str | None = oem_profile.oem_id
        company = oem_profile.company_name
        publisher_name = oem_profile.windows_publisher_name
        product_identity: OemWindowsProductIdentity | None = oem_profile.windows_products[args.product]
        icon_path: Path | None = oem_profile.windows_icon_path
    else:
        if args.oem_profile is not None:
            raise RuntimeError("Official and Customer installers must not receive --oem-profile")
        oem_profile = None
        release_namespace = f"pixels.{args.distribution}"
        oem_id = None
        company = str(config["company"])
        publisher_name = company
        product_identity = None
        icon_path = None
    signing_configuration = load_configuration()
    preflight(signing_configuration)
    if (
        oem_profile is not None
        and signing_configuration.certificate_sha256.lower() != oem_profile.windows_signer_certificate_sha256
    ):
        raise RuntimeError("Windows signing certificate does not match the OEM release profile")
    tool_config = load_tool_config(setup_dir)
    makensis = find_nsis(tool_config.get("nsis_dir_path"), repo_root)
    validate_pinned_nsis(repo_root, makensis)
    if args.preflight_only:
        print("Windows installer signing and pinned toolchain preflight passed.")
        return 0
    distribution_directory = Path("oem") / oem_id if oem_id is not None else Path(args.distribution)
    expected_dist_dir = (repo_root / "build_official" / args.product / distribution_directory / "dist").resolve()
    dist_dir = (args.dist_dir or expected_dist_dir).resolve()
    if dist_dir != expected_dist_dir:
        raise RuntimeError(f"installer input must be the isolated product dist {expected_dist_dir}; got {dist_dir}")
    manifest = validate_dist(
        repo_root,
        args.product,
        args.distribution,
        release_namespace,
        oem_id,
        company,
        oem_profile.profile_sha256.upper() if oem_profile is not None else None,
        dist_dir,
        config,
    )
    if args.validate_only:
        print(f"Validated installer input: {args.product}/{args.distribution} {config['product_version']} ({dist_dir})")
        return 0

    expected_output_root = (repo_root / "build_official" / args.product / distribution_directory / "installer").resolve()
    output_root = (args.output_root or expected_output_root).resolve()
    if output_root != expected_output_root:
        raise RuntimeError(f"installer output must be the isolated product directory {expected_output_root}; got {output_root}")
    final_dir = output_root / str(config["product_version"])
    if final_dir.exists():
        raise RuntimeError(f"installer version output already exists and will not be overwritten: {final_dir}")
    output_root.mkdir(parents=True, exist_ok=True)

    staging_dir = Path(tempfile.mkdtemp(prefix=f".{args.product}-installer-", dir=output_root))
    try:
        stage_payload(dist_dir, staging_dir / "app")
        installer = create_installer(
            makensis,
            setup_dir,
            staging_dir,
            args.product,
            args.distribution,
            str(config["product_version"]),
            int(config["product_version_code"]),
            company,
            publisher_name,
            release_namespace,
            oem_id,
            product_identity,
            icon_path,
            nsis_finalize_command(signing_configuration),
        )
        sign_file(installer, signing_configuration)
        release_manifest = {
            "schema_version": 3,
            "product": args.product,
            "distribution": args.distribution,
            "release_namespace": release_namespace,
            "oem_id": oem_id,
            "company": company,
            "publisher_name": publisher_name,
            "installer_basename": (
                product_identity.installer_basename
                if product_identity is not None
                else {"cloud_node": "PixelsCloudNode", "client": "PixelsClient", "remote": "PixelsRemote"}[args.product]
            ),
            "oem_profile_sha256": oem_profile.profile_sha256.upper() if oem_profile is not None else None,
            "product_version": config["product_version"],
            "product_version_code": config["product_version_code"],
            "git_revision": manifest["git_revision"],
            "signer_certificate_sha256": signing_configuration.certificate_sha256,
            "payload_manifest_sha256": sha256(dist_dir / "product-manifest.json"),
            "payload_artifact_count": len(manifest["artifacts"]),
            "installer": {"path": installer.name, "sha256": sha256(installer)},
        }
        (staging_dir / "installer-manifest.json").write_text(
            json.dumps(release_manifest, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        os.replace(staging_dir, final_dir)
    except Exception:
        shutil.rmtree(staging_dir, ignore_errors=True)
        raise

    print(f"Published installer atomically: {final_dir}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1) from None
