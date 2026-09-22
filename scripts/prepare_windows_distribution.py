#!/usr/bin/env python3
"""Validate Windows release inputs and stage the signed TUF update root."""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path
from urllib.parse import urlsplit

from oem_release_profile import load_oem_release_profile, sha256_bytes


MAXIMUM_UPDATE_ROOT_BYTES = 1024 * 1024


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", required=True, choices=("cloud_node", "client", "remote"))
    parser.add_argument("--distribution", required=True, choices=("official", "customer", "oem"))
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    return parser.parse_args()


def load_tuf_update_root(path: Path) -> bytes:
    try:
        root_bytes = path.read_bytes()
        root_metadata = json.loads(root_bytes.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError("the approved TUF update root is unreadable or invalid") from error
    if not 0 < len(root_bytes) <= MAXIMUM_UPDATE_ROOT_BYTES or not isinstance(root_metadata, dict):
        raise RuntimeError("the approved TUF update root has an invalid size or document shape")
    signed = root_metadata.get("signed")
    signatures = root_metadata.get("signatures")
    required_roles = {"root", "snapshot", "targets", "timestamp"}
    if (
        not isinstance(signed, dict)
        or signed.get("_type") != "root"
        or signed.get("spec_version") != "1.0.0"
        or not isinstance(signed.get("version"), int)
        or isinstance(signed.get("version"), bool)
        or signed["version"] <= 0
        or not isinstance(signed.get("expires"), str)
        or not isinstance(signed.get("keys"), dict)
        or not signed["keys"]
        or not isinstance(signed.get("roles"), dict)
        or not required_roles.issubset(signed["roles"])
        or not isinstance(signatures, list)
        or not signatures
    ):
        raise RuntimeError("the approved TUF update root is not a complete signed TUF 1.0 root")
    return root_bytes


def canonical_https_origin(value: str) -> str:
    parsed = urlsplit(value)
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or parsed.path
        or parsed.query
        or parsed.fragment
    ):
        raise RuntimeError("PIXELS_OFFICIAL_CONSOLE_URL must be a canonical HTTPS origin without credentials, path, query, or fragment")
    try:
        port = parsed.port
    except ValueError as error:
        raise RuntimeError("PIXELS_OFFICIAL_CONSOLE_URL has an invalid port") from error
    host = parsed.hostname
    try:
        ip_address = ipaddress.ip_address(host)
        normalized_host = f"[{ip_address.compressed}]" if ip_address.version == 6 else ip_address.compressed
    except ValueError:
        normalized_host = host.lower()
        labels = normalized_host.split(".")
        if any(not label or len(label) > 63 or label.startswith("-") or label.endswith("-") for label in labels):
            raise RuntimeError("PIXELS_OFFICIAL_CONSOLE_URL has an invalid DNS host")
        if any(not all(character.isascii() and (character.isalnum() or character == "-") for character in label) for label in labels):
            raise RuntimeError("PIXELS_OFFICIAL_CONSOLE_URL has an invalid DNS host")
    normalized = f"https://{normalized_host}"
    if port not in (None, 443):
        normalized += f":{port}"
    if normalized != value:
        raise RuntimeError("PIXELS_OFFICIAL_CONSOLE_URL must already be in canonical form")
    return value


def validate_distribution_inputs(distribution: str, update_root_bytes: bytes) -> None:
    official_origin = os.environ.get("PIXELS_OFFICIAL_CONSOLE_URL", "").strip()
    oem_profile_value = os.environ.get("PIXELS_OEM_RELEASE_PROFILE", "").strip()
    if distribution in {"official", "customer"}:
        if not official_origin:
            raise RuntimeError("Official and Customer Windows builds require PIXELS_OFFICIAL_CONSOLE_URL")
        canonical_https_origin(official_origin)
        if oem_profile_value:
            raise RuntimeError("Official and Customer Windows builds must not configure an OEM release profile")
        return
    if official_origin:
        raise RuntimeError("OEM Windows builds must not configure the Pixels Official Console origin")
    if not oem_profile_value:
        raise RuntimeError("OEM Windows builds require PIXELS_OEM_RELEASE_PROFILE")
    oem_profile = load_oem_release_profile(Path(oem_profile_value))
    if sha256_bytes(update_root_bytes) != oem_profile.update_root_sha256:
        raise RuntimeError("OEM release profile TUF root SHA-256 does not match the approved input")


def publish_update_root(output_dir: Path, update_root_bytes: bytes) -> None:
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    staging_dir = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.staging-", dir=output_dir.parent))
    try:
        (staging_dir / "update-root.json").write_bytes(update_root_bytes)
        if output_dir.exists():
            shutil.rmtree(output_dir)
        os.replace(staging_dir, output_dir)
    finally:
        shutil.rmtree(staging_dir, ignore_errors=True)


def main() -> int:
    arguments = parse_args()
    update_root_value = os.environ.get("PIXELS_UPDATE_ROOT_FILE", "").strip()
    if not update_root_value:
        raise RuntimeError("PIXELS_UPDATE_ROOT_FILE must identify the approved TUF update root")
    update_root_path = Path(update_root_value).resolve()
    if not update_root_path.is_file():
        raise RuntimeError("PIXELS_UPDATE_ROOT_FILE does not identify a regular file")
    update_root_bytes = load_tuf_update_root(update_root_path)
    validate_distribution_inputs(arguments.distribution, update_root_bytes)
    if arguments.validate_only:
        if arguments.output_dir is not None:
            raise RuntimeError("--output-dir cannot be combined with --validate-only")
        print(f"Validated Windows {arguments.product}/{arguments.distribution} release inputs.")
        return 0
    if arguments.output_dir is None:
        raise RuntimeError("--output-dir is required unless --validate-only is used")
    publish_update_root(arguments.output_dir.resolve(), update_root_bytes)
    print(f"Prepared Windows {arguments.product}/{arguments.distribution} update trust: {arguments.output_dir.resolve()}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1) from None
