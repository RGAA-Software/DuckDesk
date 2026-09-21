#!/usr/bin/env python3
"""Create strict release metadata from one independently verified Windows installer."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Callable
from urllib.parse import urlsplit

from verify_windows_installer_release import (
    VerifiedInstallerRelease,
    validate_release_directory,
    verify_file,
)


CHANNELS = {"stable", "preview"}
TARGET_COMPONENT = re.compile(r"^[a-z0-9][a-z0-9_-]{0,63}$")


def validate_base_url(value: str, field_name: str) -> str:
    if (
        not value
        or value.strip() != value
        or len(value) > 2048
        or any(character.isspace() or ord(character) < 32 for character in value)
    ):
        raise RuntimeError(f"{field_name} must be a canonical HTTPS base URL")
    parsed = urlsplit(value)
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or parsed.query
        or parsed.fragment
        or not parsed.path.endswith("/")
    ):
        raise RuntimeError(f"{field_name} must be an HTTPS URL without credentials, query, or fragment and must end with /")
    return value


def immutable_target_name(release: VerifiedInstallerRelease, channel: str) -> str:
    components = [
        "windows",
        release.product,
        release.distribution,
    ]
    if release.oem_id is not None:
        components.append(release.oem_id)
    components.extend([channel, "x86_64", str(release.product_version_code)])
    if any(TARGET_COMPONENT.fullmatch(component) is None for component in components):
        raise RuntimeError("installer identity cannot form a canonical immutable TUF target name")
    installer_path = Path(release.installer_path)
    installer_name = installer_path.name
    if not installer_path.is_absolute() or installer_path.parent != Path(release.directory):
        raise RuntimeError("verified installer path is invalid")
    if not installer_name or any(character.isspace() or ord(character) < 32 for character in installer_name):
        raise RuntimeError("verified installer file name is invalid")
    return "/".join([*components, installer_name])


def build_release_spec(
    release_directory: Path,
    approved_signer_sha256: str,
    metadata_base_url: str,
    targets_base_url: str,
    channel: str,
    signature_verifier: Callable[[Path, str], None] = verify_file,
) -> tuple[dict[str, object], VerifiedInstallerRelease]:
    if channel not in CHANNELS:
        raise RuntimeError(f"unsupported release channel: {channel}")
    verified_release = validate_release_directory(
        release_directory,
        signature_verifier=signature_verifier,
        expected_signer_sha256=approved_signer_sha256,
    )
    release_spec: dict[str, object] = {
        "target": {
            "product": verified_release.product,
            "distribution": verified_release.distribution,
            "release_namespace": verified_release.release_namespace,
            "oem_id": verified_release.oem_id,
            "channel": channel,
            "os": "windows",
            "architecture": "x86_64",
        },
        "build_number": verified_release.product_version_code,
        "version": verified_release.product_version,
        "metadata_base_url": validate_base_url(metadata_base_url, "metadata_base_url"),
        "targets_base_url": validate_base_url(targets_base_url, "targets_base_url"),
        "target_name": immutable_target_name(verified_release, channel),
        "sha256": verified_release.installer_sha256.lower(),
        "platform_signer_sha256": verified_release.signer_certificate_sha256.lower(),
        "size_bytes": Path(verified_release.installer_path).stat().st_size,
    }
    return release_spec, verified_release


def write_new_json(output_path: Path, document: dict[str, object]) -> None:
    resolved_output = output_path.resolve()
    if not output_path.is_absolute() or resolved_output.name != output_path.name:
        raise RuntimeError("release specification output must be a direct absolute file path")
    if not resolved_output.parent.is_dir():
        raise RuntimeError("release specification output parent does not exist")
    encoded = (json.dumps(document, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    descriptor = os.open(resolved_output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
    try:
        with os.fdopen(descriptor, "wb", closefd=True) as output_file:
            output_file.write(encoded)
            output_file.flush()
            os.fsync(output_file.fileno())
    except Exception:
        try:
            resolved_output.unlink()
        except FileNotFoundError:
            pass
        raise


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-directory", type=Path, required=True)
    parser.add_argument("--approved-signer-sha256", required=True)
    parser.add_argument("--metadata-base-url", required=True)
    parser.add_argument("--targets-base-url", required=True)
    parser.add_argument("--channel", choices=sorted(CHANNELS), default="stable")
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    release_spec, verified_release = build_release_spec(
        arguments.release_directory,
        arguments.approved_signer_sha256,
        arguments.metadata_base_url,
        arguments.targets_base_url,
        arguments.channel,
    )
    write_new_json(arguments.output, release_spec)
    print(
        "Prepared Windows update release: "
        f"{verified_release.product}/{verified_release.distribution} "
        f"build={verified_release.product_version_code} target={release_spec['target_name']}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1) from None
