#!/usr/bin/env python3
"""Load and validate one immutable OEM release identity profile."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


OEM_PRODUCTS = ("cloud_node", "client", "remote")
RESERVED_OEM_IDS = {"pixels", "official", "customer", "oem"}
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ANDROID_APPLICATION_ID_PATTERN = re.compile(r"^[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*){2,}$")
WINDOWS_IDENTITY_PATTERN = re.compile(r"^[A-Za-z][A-Za-z0-9._-]{2,63}$")


@dataclass(frozen=True)
class OemWindowsProductIdentity:
    product_name: str
    install_directory_name: str
    uninstall_key: str
    installer_basename: str


@dataclass(frozen=True)
class OemReleaseProfile:
    source_path: Path
    oem_id: str
    release_namespace: str
    company_name: str
    application_name: str
    deployment_trust_store_sha256: str
    update_root_sha256: str
    windows_publisher_name: str
    windows_signer_certificate_sha256: str
    windows_icon_path: Path
    windows_products: dict[str, OemWindowsProductIdentity]
    android_application_id: str
    android_signer_certificate_sha256: str
    android_icon_foreground_path: Path
    android_icon_background_path: Path
    web_icon_path: Path
    profile_sha256: str
    document: dict[str, object]


def sha256_bytes(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def sha256_file(path: Path) -> str:
    try:
        return sha256_bytes(path.read_bytes())
    except OSError as error:
        raise RuntimeError(f"OEM release asset is unreadable: {path}") from error


def canonical_oem_id(value: object) -> str:
    if not isinstance(value, str):
        raise RuntimeError("OEM release profile oem_id must be a string")
    if (
        not 3 <= len(value) <= 32
        or value in RESERVED_OEM_IDS
        or value.startswith("-")
        or value.endswith("-")
        or "--" in value
        or any(
            not (character.isascii() and (character.islower() or character.isdecimal() or character == "-"))
            for character in value
        )
    ):
        raise RuntimeError("OEM release profile oem_id must be a canonical lowercase identifier")
    return value


def require_object(parent: dict[str, object], field_name: str, expected_fields: set[str]) -> dict[str, object]:
    field_value = parent.get(field_name)
    if not isinstance(field_value, dict) or set(field_value) != expected_fields:
        raise RuntimeError(f"OEM release profile {field_name} must contain exactly {sorted(expected_fields)}")
    return field_value


def require_text(parent: dict[str, object], field_name: str, minimum_length: int = 1, maximum_length: int = 128) -> str:
    field_value = parent.get(field_name)
    if (
        not isinstance(field_value, str)
        or field_value != field_value.strip()
        or not minimum_length <= len(field_value) <= maximum_length
        or any(ord(character) < 32 for character in field_value)
    ):
        raise RuntimeError(f"OEM release profile {field_name} is invalid")
    return field_value


def require_embeddable_text(parent: dict[str, object], field_name: str, minimum_length: int = 1, maximum_length: int = 128) -> str:
    field_value = require_text(parent, field_name, minimum_length, maximum_length)
    if any(character in {'"', "\\"} for character in field_value):
        raise RuntimeError(f"OEM release profile {field_name} cannot contain quote or backslash characters")
    return field_value


def require_sha256(parent: dict[str, object], field_name: str) -> str:
    field_value = parent.get(field_name)
    if not isinstance(field_value, str) or not SHA256_PATTERN.fullmatch(field_value) or field_value == "0" * 64:
        raise RuntimeError(f"OEM release profile {field_name} must be a nonzero lowercase SHA-256")
    return field_value


def validate_asset(profile_directory: Path, asset: dict[str, object], asset_role: str) -> Path:
    if set(asset) != {"path", "sha256"}:
        raise RuntimeError(f"OEM release profile {asset_role} asset must contain exactly path and sha256")
    relative_path = require_text(asset, "path", maximum_length=240)
    candidate_path = Path(relative_path)
    if candidate_path.is_absolute() or any(path_component == ".." for path_component in candidate_path.parts):
        raise RuntimeError(f"OEM release profile {asset_role} asset path must stay within the profile directory")
    resolved_profile_directory = profile_directory.resolve()
    resolved_asset_path = (resolved_profile_directory / candidate_path).resolve()
    try:
        resolved_asset_path.relative_to(resolved_profile_directory)
    except ValueError as error:
        raise RuntimeError(f"OEM release profile {asset_role} asset path escapes the profile directory") from error
    if not resolved_asset_path.is_file():
        raise RuntimeError(f"OEM release profile {asset_role} asset is missing: {resolved_asset_path}")
    expected_sha256 = require_sha256(asset, "sha256")
    if sha256_file(resolved_asset_path) != expected_sha256:
        raise RuntimeError(f"OEM release profile {asset_role} asset SHA-256 does not match")
    return resolved_asset_path


def validate_windows_products(windows: dict[str, object]) -> dict[str, OemWindowsProductIdentity]:
    products = require_object(windows, "products", set(OEM_PRODUCTS))
    uninstall_keys: set[str] = set()
    install_directories: set[str] = set()
    installer_basenames: set[str] = set()
    product_identities: dict[str, OemWindowsProductIdentity] = {}
    for product in OEM_PRODUCTS:
        product_identity = products.get(product)
        expected_fields = {"product_name", "install_directory_name", "uninstall_key", "installer_basename"}
        if not isinstance(product_identity, dict) or set(product_identity) != expected_fields:
            raise RuntimeError(f"OEM Windows {product} identity must contain exactly {sorted(expected_fields)}")
        product_name = require_embeddable_text(product_identity, "product_name")
        install_directory = require_text(product_identity, "install_directory_name", maximum_length=80)
        if any(character in '<>:"/\\|?*' for character in install_directory) or install_directory.endswith((".", " ")):
            raise RuntimeError(f"OEM Windows {product} install directory name is invalid")
        uninstall_key = require_text(product_identity, "uninstall_key", maximum_length=64)
        installer_basename = require_text(product_identity, "installer_basename", maximum_length=64)
        if not WINDOWS_IDENTITY_PATTERN.fullmatch(uninstall_key) or not WINDOWS_IDENTITY_PATTERN.fullmatch(installer_basename):
            raise RuntimeError(f"OEM Windows {product} installer identities must use safe ASCII identifiers")
        install_directories.add(install_directory.casefold())
        uninstall_keys.add(uninstall_key.casefold())
        installer_basenames.add(installer_basename.casefold())
        product_identities[product] = OemWindowsProductIdentity(
            product_name=product_name,
            install_directory_name=install_directory,
            uninstall_key=uninstall_key,
            installer_basename=installer_basename,
        )
    if len(install_directories) != len(OEM_PRODUCTS):
        raise RuntimeError("OEM Windows product install directories must be unique")
    if len(uninstall_keys) != len(OEM_PRODUCTS):
        raise RuntimeError("OEM Windows product uninstall keys must be unique")
    if len(installer_basenames) != len(OEM_PRODUCTS):
        raise RuntimeError("OEM Windows product installer basenames must be unique")
    return product_identities


def load_oem_release_profile(path: Path) -> OemReleaseProfile:
    resolved_path = path.resolve()
    try:
        profile_bytes = resolved_path.read_bytes()
        profile_document = json.loads(profile_bytes.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError("OEM release profile is unreadable or invalid UTF-8 JSON") from error
    expected_fields = {
        "schema_version",
        "oem_id",
        "release_namespace",
        "brand",
        "deployment",
        "update",
        "windows",
        "android",
        "web",
    }
    if not isinstance(profile_document, dict) or set(profile_document) != expected_fields:
        raise RuntimeError(f"OEM release profile must contain exactly {sorted(expected_fields)}")
    if profile_document.get("schema_version") != 1:
        raise RuntimeError("OEM release profile schema_version must be 1")

    oem_id = canonical_oem_id(profile_document.get("oem_id"))
    release_namespace = require_text(profile_document, "release_namespace", maximum_length=36)
    if release_namespace != f"oem.{oem_id}":
        raise RuntimeError("OEM release namespace must exactly match oem.<oem_id>")

    brand = require_object(profile_document, "brand", {"company_name", "application_name"})
    company_name = require_embeddable_text(brand, "company_name")
    application_name = require_embeddable_text(brand, "application_name")
    if company_name.casefold() == "pixels" or application_name.casefold() == "pixels":
        raise RuntimeError("OEM branding must not impersonate the Pixels product brand")

    deployment = require_object(profile_document, "deployment", {"trust_store_sha256"})
    deployment_trust_store_sha256 = require_sha256(deployment, "trust_store_sha256")
    update = require_object(profile_document, "update", {"root_sha256"})
    update_root_sha256 = require_sha256(update, "root_sha256")

    windows = require_object(
        profile_document,
        "windows",
        {"publisher_name", "signer_certificate_sha256", "icon", "products"},
    )
    windows_publisher_name = require_embeddable_text(windows, "publisher_name")
    windows_signer_certificate_sha256 = require_sha256(windows, "signer_certificate_sha256")
    windows_icon = windows.get("icon")
    if not isinstance(windows_icon, dict):
        raise RuntimeError("OEM Windows icon asset is invalid")
    windows_icon_path = validate_asset(resolved_path.parent, windows_icon, "Windows icon")
    if windows_icon_path.suffix.lower() != ".ico":
        raise RuntimeError("OEM Windows icon must be an .ico file")
    windows_products = validate_windows_products(windows)

    android = require_object(
        profile_document,
        "android",
        {"application_id", "signer_certificate_sha256", "icon_foreground", "icon_background"},
    )
    android_application_id = require_text(android, "application_id", maximum_length=150)
    if not ANDROID_APPLICATION_ID_PATTERN.fullmatch(android_application_id) or android_application_id.startswith("yun.pixels."):
        raise RuntimeError("OEM Android application_id must be an independent lowercase reverse-DNS identifier")
    android_signer_certificate_sha256 = require_sha256(android, "signer_certificate_sha256")
    android_asset_paths: dict[str, Path] = {}
    for asset_field in ("icon_foreground", "icon_background"):
        android_asset = android.get(asset_field)
        if not isinstance(android_asset, dict):
            raise RuntimeError(f"OEM Android {asset_field} asset is invalid")
        android_asset_path = validate_asset(resolved_path.parent, android_asset, f"Android {asset_field}")
        if android_asset_path.suffix.lower() != ".png":
            raise RuntimeError(f"OEM Android {asset_field} must be a .png file")
        android_asset_paths[asset_field] = android_asset_path

    web = require_object(profile_document, "web", {"application_name", "icon"})
    web_application_name = require_text(web, "application_name")
    if web_application_name != application_name:
        raise RuntimeError("OEM Web application name must match the shared brand application name")
    web_icon = web.get("icon")
    if not isinstance(web_icon, dict):
        raise RuntimeError("OEM Web icon asset is invalid")
    web_icon_path = validate_asset(resolved_path.parent, web_icon, "Web icon")
    if web_icon_path.suffix.lower() != ".png":
        raise RuntimeError("OEM Web icon must be a .png file")

    return OemReleaseProfile(
        source_path=resolved_path,
        oem_id=oem_id,
        release_namespace=release_namespace,
        company_name=company_name,
        application_name=application_name,
        deployment_trust_store_sha256=deployment_trust_store_sha256,
        update_root_sha256=update_root_sha256,
        windows_publisher_name=windows_publisher_name,
        windows_signer_certificate_sha256=windows_signer_certificate_sha256,
        windows_icon_path=windows_icon_path,
        windows_products=windows_products,
        android_application_id=android_application_id,
        android_signer_certificate_sha256=android_signer_certificate_sha256,
        android_icon_foreground_path=android_asset_paths["icon_foreground"],
        android_icon_background_path=android_asset_paths["icon_background"],
        web_icon_path=web_icon_path,
        profile_sha256=sha256_bytes(profile_bytes),
        document=profile_document,
    )


def cmake_bracket(value: str) -> str:
    if "]]" in value:
        raise RuntimeError("OEM release profile value cannot be represented in generated CMake")
    return f"[[{value}]]"


def emit_cmake(profile: OemReleaseProfile, product: str) -> str:
    if product not in OEM_PRODUCTS:
        raise RuntimeError(f"unsupported OEM Windows product: {product}")
    product_identity = profile.windows_products[product]
    variables = {
        "PX_OEM_ID": profile.oem_id,
        "PX_OEM_RELEASE_NAMESPACE": profile.release_namespace,
        "PX_OEM_COMPANY": profile.company_name,
        "PX_OEM_APPLICATION_NAME": profile.application_name,
        "PX_OEM_ICON": profile.windows_icon_path.as_posix(),
        "PX_OEM_BRAND_ICON": profile.web_icon_path.as_posix(),
        "PX_OEM_PROFILE_SHA256": profile.profile_sha256,
        "PX_OEM_PRODUCT_NAME": product_identity.product_name,
        "PX_OEM_STORAGE_DIRECTORY_NAME": product_identity.install_directory_name,
    }
    return "\n".join(f"set({variable_name} {cmake_bracket(variable_value)})" for variable_name, variable_value in variables.items()) + "\n"


def emit_android_json(profile: OemReleaseProfile) -> str:
    configuration = {
        "schema_version": 1,
        "oem_id": profile.oem_id,
        "release_namespace": profile.release_namespace,
        "company_name": profile.company_name,
        "application_name": profile.application_name,
        "application_id": profile.android_application_id,
        "signer_certificate_sha256": profile.android_signer_certificate_sha256,
        "deployment_trust_store_sha256": profile.deployment_trust_store_sha256,
        "update_root_sha256": profile.update_root_sha256,
        "icon_foreground_path": str(profile.android_icon_foreground_path),
        "icon_background_path": str(profile.android_icon_background_path),
        "profile_sha256": profile.profile_sha256,
    }
    return json.dumps(configuration, ensure_ascii=False, separators=(",", ":")) + "\n"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--product", choices=OEM_PRODUCTS)
    output_format = parser.add_mutually_exclusive_group(required=True)
    output_format.add_argument("--cmake", action="store_true")
    output_format.add_argument("--android-json", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    profile = load_oem_release_profile(arguments.profile)
    if arguments.cmake:
        if arguments.product is None:
            raise RuntimeError("--product is required with --cmake")
        sys.stdout.write(emit_cmake(profile, arguments.product))
    else:
        if arguments.product is not None:
            raise RuntimeError("--product is not accepted with --android-json")
        sys.stdout.write(emit_android_json(profile))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1) from None
