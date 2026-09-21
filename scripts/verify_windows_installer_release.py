#!/usr/bin/env python3
"""Verify signed Pixels Windows installer releases and upgrade pairs."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Callable


REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from windows_release_signing import normalized_hex, verify_file  # noqa: E402


PRODUCT_BASENAMES = {
    "cloud_node": "PixelsCloudNode",
    "client": "PixelsClient",
    "remote": "PixelsRemote",
}
VALID_DISTRIBUTIONS = {"official", "customer"}
HEX_SHA256 = re.compile(r"^[0-9A-F]{64}$")
SEMANTIC_VERSION = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")


@dataclass(frozen=True)
class VerifiedInstallerRelease:
    directory: str
    installer_path: str
    installer_sha256: str
    product: str
    distribution: str
    product_version: str
    product_version_code: int
    signer_certificate_sha256: str
    payload_manifest_sha256: str
    payload_artifact_count: int
    git_revision: str


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)

    single_parser = subcommands.add_parser("single", help="Verify one installer release directory")
    single_parser.add_argument("--release-dir", type=Path, required=True)
    single_parser.add_argument("--output", type=Path)

    pair_parser = subcommands.add_parser("pair", help="Verify one same-channel upgrade pair")
    pair_parser.add_argument("--previous", type=Path, required=True)
    pair_parser.add_argument("--current", type=Path, required=True)
    pair_parser.add_argument("--previous-signer-sha256")
    pair_parser.add_argument("--current-signer-sha256")
    pair_parser.add_argument("--output", type=Path)

    installed_parser = subcommands.add_parser("installed", help="Verify one installed product against its release")
    installed_parser.add_argument("--release-dir", type=Path, required=True)
    installed_parser.add_argument("--install-dir", type=Path, required=True)
    installed_parser.add_argument("--output", type=Path)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for payload_chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(payload_chunk)
    return digest.hexdigest().upper()


def read_json_object(path: Path) -> dict[str, object]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read installer manifest {path}: {error}") from error
    if not isinstance(document, dict):
        raise RuntimeError(f"installer manifest must contain a JSON object: {path}")
    return document


def require_string(manifest: dict[str, object], field_name: str) -> str:
    field_value = manifest.get(field_name)
    if not isinstance(field_value, str) or not field_value:
        raise RuntimeError(f"installer manifest field must be a non-empty string: {field_name}")
    return field_value


def require_positive_integer(manifest: dict[str, object], field_name: str) -> int:
    field_value = manifest.get(field_name)
    if not isinstance(field_value, int) or isinstance(field_value, bool) or field_value <= 0:
        raise RuntimeError(f"installer manifest field must be a positive integer: {field_name}")
    return field_value


def parse_product_version(version: str) -> tuple[int, int, int]:
    version_match = SEMANTIC_VERSION.fullmatch(version)
    if version_match is None:
        raise RuntimeError(f"invalid three-component product version: {version}")
    version_components = tuple(int(version_match.group(component_index)) for component_index in range(1, 4))
    if version_components[1] > 99 or version_components[2] > 99:
        raise RuntimeError(f"product version minor and patch components must be at most 99: {version}")
    return version_components


def validate_sha256(value: str, field_name: str) -> str:
    normalized_value = normalized_hex(value)
    if HEX_SHA256.fullmatch(normalized_value) is None:
        raise RuntimeError(f"installer manifest field must contain a SHA-256 digest: {field_name}")
    return normalized_value


def validate_release_directory(
    release_directory: Path,
    signature_verifier: Callable[[Path, str], None] = verify_file,
) -> VerifiedInstallerRelease:
    resolved_directory = release_directory.resolve()
    if not resolved_directory.is_dir():
        raise RuntimeError(f"installer release directory does not exist: {resolved_directory}")
    manifest_path = resolved_directory / "installer-manifest.json"
    manifest = read_json_object(manifest_path)
    if manifest.get("schema_version") != 2:
        raise RuntimeError(f"unsupported installer manifest schema: {manifest.get('schema_version')}")

    product = require_string(manifest, "product")
    distribution = require_string(manifest, "distribution")
    company = require_string(manifest, "company")
    product_version = require_string(manifest, "product_version")
    product_version_code = require_positive_integer(manifest, "product_version_code")
    git_revision = require_string(manifest, "git_revision")
    signer_certificate_sha256 = validate_sha256(
        require_string(manifest, "signer_certificate_sha256"),
        "signer_certificate_sha256",
    )
    payload_manifest_sha256 = validate_sha256(
        require_string(manifest, "payload_manifest_sha256"),
        "payload_manifest_sha256",
    )
    payload_artifact_count = require_positive_integer(manifest, "payload_artifact_count")

    if product not in PRODUCT_BASENAMES:
        raise RuntimeError(f"unsupported Windows product in installer manifest: {product}")
    if distribution not in VALID_DISTRIBUTIONS:
        raise RuntimeError(f"unsupported distribution in installer manifest: {distribution}")
    if company != "Pixels":
        raise RuntimeError(f"installer company must be Pixels, got: {company}")

    version_components = parse_product_version(product_version)
    expected_version_code = version_components[0] * 10000 + version_components[1] * 100 + version_components[2]
    if product_version_code != expected_version_code:
        raise RuntimeError(
            f"installer version code mismatch: version={product_version}, "
            f"expected={expected_version_code}, actual={product_version_code}"
        )

    installer_entry = manifest.get("installer")
    if not isinstance(installer_entry, dict):
        raise RuntimeError("installer manifest field must be an object: installer")
    installer_name = require_string(installer_entry, "path")
    if Path(installer_name).name != installer_name:
        raise RuntimeError("installer manifest path must be a file name without directories")
    expected_installer_name = f"{PRODUCT_BASENAMES[product]}_{distribution}_{product_version}_Setup.exe"
    if installer_name != expected_installer_name:
        raise RuntimeError(
            f"installer file name mismatch: expected={expected_installer_name}, actual={installer_name}"
        )
    expected_installer_sha256 = validate_sha256(
        require_string(installer_entry, "sha256"),
        "installer.sha256",
    )
    installer_path = resolved_directory / installer_name
    if not installer_path.is_file():
        raise RuntimeError(f"installer file is missing: {installer_path}")
    actual_installer_sha256 = sha256(installer_path)
    if actual_installer_sha256 != expected_installer_sha256:
        raise RuntimeError(
            f"installer SHA-256 mismatch: expected={expected_installer_sha256}, actual={actual_installer_sha256}"
        )
    signature_verifier(installer_path, signer_certificate_sha256)

    return VerifiedInstallerRelease(
        directory=str(resolved_directory),
        installer_path=str(installer_path),
        installer_sha256=actual_installer_sha256,
        product=product,
        distribution=distribution,
        product_version=product_version,
        product_version_code=product_version_code,
        signer_certificate_sha256=signer_certificate_sha256,
        payload_manifest_sha256=payload_manifest_sha256,
        payload_artifact_count=payload_artifact_count,
        git_revision=git_revision,
    )


def validate_upgrade_pair(
    previous_directory: Path,
    current_directory: Path,
    signature_verifier: Callable[[Path, str], None] = verify_file,
    approved_signer_transition: tuple[str, str] | None = None,
) -> dict[str, object]:
    previous_release = validate_release_directory(previous_directory, signature_verifier)
    current_release = validate_release_directory(current_directory, signature_verifier)
    if previous_release.product != current_release.product:
        raise RuntimeError("upgrade pair products do not match")
    if previous_release.distribution != current_release.distribution:
        raise RuntimeError("upgrade pair distributions do not match")
    if approved_signer_transition is None:
        if previous_release.signer_certificate_sha256 != current_release.signer_certificate_sha256:
            raise RuntimeError("upgrade pair signer certificate pins do not match and no signer transition was approved")
    else:
        approved_previous_signer = validate_sha256(approved_signer_transition[0], "previous signer approval")
        approved_current_signer = validate_sha256(approved_signer_transition[1], "current signer approval")
        actual_signer_transition = (
            previous_release.signer_certificate_sha256,
            current_release.signer_certificate_sha256,
        )
        if actual_signer_transition != (approved_previous_signer, approved_current_signer):
            raise RuntimeError(
                "upgrade pair signer transition does not match the explicitly approved certificate pins"
            )
    if current_release.product_version_code <= previous_release.product_version_code:
        raise RuntimeError(
            "current installer version must be newer than the previous installer version: "
            f"previous={previous_release.product_version}, current={current_release.product_version}"
        )
    return {
        "schema_version": 1,
        "product": current_release.product,
        "distribution": current_release.distribution,
        "signer_transition": {
            "previous": previous_release.signer_certificate_sha256,
            "current": current_release.signer_certificate_sha256,
            "explicitly_approved": approved_signer_transition is not None,
        },
        "previous": asdict(previous_release),
        "current": asdict(current_release),
    }


def validate_installed_product(
    release_directory: Path,
    install_directory: Path,
    signature_verifier: Callable[[Path, str], None] = verify_file,
) -> dict[str, object]:
    verified_release = validate_release_directory(release_directory, signature_verifier)
    resolved_install_directory = install_directory.resolve()
    if not resolved_install_directory.is_dir():
        raise RuntimeError(f"installed product directory does not exist: {resolved_install_directory}")

    product_manifest_path = resolved_install_directory / "product-manifest.json"
    if not product_manifest_path.is_file():
        raise RuntimeError(f"installed product manifest is missing: {product_manifest_path}")
    actual_payload_manifest_sha256 = sha256(product_manifest_path)
    if actual_payload_manifest_sha256 != verified_release.payload_manifest_sha256:
        raise RuntimeError(
            "installed payload manifest SHA-256 mismatch: "
            f"expected={verified_release.payload_manifest_sha256}, actual={actual_payload_manifest_sha256}"
        )
    product_manifest = read_json_object(product_manifest_path)
    expected_identity = {
        "schema_version": 2,
        "product": verified_release.product,
        "distribution": verified_release.distribution,
        "company": "Pixels",
        "product_version": verified_release.product_version,
        "product_version_code": verified_release.product_version_code,
        "signer_certificate_sha256": verified_release.signer_certificate_sha256,
    }
    actual_identity = {field_name: product_manifest.get(field_name) for field_name in expected_identity}
    if actual_identity != expected_identity:
        raise RuntimeError(
            f"installed payload identity mismatch: expected={expected_identity}, actual={actual_identity}"
        )

    artifact_entries = product_manifest.get("artifacts")
    if not isinstance(artifact_entries, list) or not artifact_entries:
        raise RuntimeError("installed product manifest has no artifacts")
    artifact_hashes: dict[str, str] = {}
    for artifact_entry in artifact_entries:
        if not isinstance(artifact_entry, dict):
            raise RuntimeError("installed product manifest contains a non-object artifact")
        artifact_name = require_string(artifact_entry, "path")
        artifact_path = Path(artifact_name)
        if artifact_path.is_absolute() or ".." in artifact_path.parts or artifact_path.as_posix() != artifact_name:
            raise RuntimeError(f"installed product manifest contains an unsafe artifact path: {artifact_name}")
        if artifact_name in artifact_hashes:
            raise RuntimeError(f"installed product manifest contains a duplicate artifact path: {artifact_name}")
        artifact_hashes[artifact_name] = validate_sha256(
            require_string(artifact_entry, "sha256"),
            f"artifacts[{artifact_name}].sha256",
        )
    if len(artifact_hashes) != verified_release.payload_artifact_count:
        raise RuntimeError(
            "installed artifact count mismatch: "
            f"release={verified_release.payload_artifact_count}, manifest={len(artifact_hashes)}"
        )

    sums_document = read_json_object(resolved_install_directory / "sha256sums.json")
    if sums_document != artifact_hashes:
        raise RuntimeError("installed sha256sums.json does not match the product artifact inventory")
    try:
        licenses_document = json.loads((resolved_install_directory / "licenses.json").read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read installed licenses.json: {error}") from error
    expected_license_files = sorted(
        artifact_name
        for artifact_name in artifact_hashes
        if "license" in artifact_name.lower() or artifact_name.endswith("SOURCE.md")
    )
    if licenses_document != {"files": expected_license_files}:
        raise RuntimeError("installed licenses.json does not match the product license inventory")

    for artifact_name, expected_hash in artifact_hashes.items():
        artifact_path = resolved_install_directory / Path(artifact_name)
        if not artifact_path.is_file():
            raise RuntimeError(f"installed artifact is missing: {artifact_name}")
        actual_hash = sha256(artifact_path)
        if actual_hash != expected_hash:
            raise RuntimeError(
                f"installed artifact SHA-256 mismatch: path={artifact_name}, expected={expected_hash}, actual={actual_hash}"
            )

    expected_files = set(artifact_hashes) | {
        "product-manifest.json",
        "sha256sums.json",
        "licenses.json",
        "product-edition.txt",
        "Uninstall.exe",
    }
    actual_files = {
        installed_path.relative_to(resolved_install_directory).as_posix()
        for installed_path in resolved_install_directory.rglob("*")
        if installed_path.is_file()
    }
    if actual_files != expected_files:
        missing_files = sorted(expected_files - actual_files)
        extra_files = sorted(actual_files - expected_files)
        raise RuntimeError(f"installed product file set mismatch: missing={missing_files}, extra={extra_files}")

    edition_lines = (resolved_install_directory / "product-edition.txt").read_text(encoding="utf-8-sig").splitlines()
    expected_edition_lines = [verified_release.product, verified_release.product_version, "Pixels"]
    if edition_lines != expected_edition_lines:
        raise RuntimeError(
            f"installed product marker mismatch: expected={expected_edition_lines}, actual={edition_lines}"
        )

    owned_pe = product_manifest.get("owned_pe")
    if not isinstance(owned_pe, list) or not owned_pe or any(not isinstance(path, str) for path in owned_pe):
        raise RuntimeError("installed product manifest has an invalid owned_pe inventory")
    for owned_pe_name in owned_pe:
        if owned_pe_name not in artifact_hashes:
            raise RuntimeError(f"installed owned PE is absent from the artifact inventory: {owned_pe_name}")
        signature_verifier(resolved_install_directory / Path(owned_pe_name), verified_release.signer_certificate_sha256)
    signature_verifier(resolved_install_directory / "Uninstall.exe", verified_release.signer_certificate_sha256)

    return {
        "schema_version": 1,
        "product": verified_release.product,
        "distribution": verified_release.distribution,
        "product_version": verified_release.product_version,
        "install_directory": str(resolved_install_directory),
        "payload_manifest_sha256": actual_payload_manifest_sha256,
        "artifact_count": len(artifact_hashes),
        "owned_pe_signature_count": len(owned_pe),
        "uninstaller_signature_verified": True,
    }


def write_result(result: dict[str, object], output_path: Path | None) -> None:
    serialized_result = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if output_path is None:
        print(serialized_result, end="")
        return
    resolved_output = output_path.resolve()
    resolved_output.parent.mkdir(parents=True, exist_ok=True)
    resolved_output.write_text(serialized_result, encoding="utf-8")


def main() -> int:
    arguments = parse_arguments()
    if arguments.command == "single":
        verified_release = validate_release_directory(arguments.release_dir)
        write_result({"schema_version": 1, "release": asdict(verified_release)}, arguments.output)
        return 0
    if arguments.command == "installed":
        installed_result = validate_installed_product(arguments.release_dir, arguments.install_dir)
        write_result(installed_result, arguments.output)
        return 0
    signer_approval_values = (arguments.previous_signer_sha256, arguments.current_signer_sha256)
    if (signer_approval_values[0] is None) != (signer_approval_values[1] is None):
        raise RuntimeError("both signer transition approval pins must be provided together")
    approved_signer_transition = None
    if signer_approval_values[0] is not None and signer_approval_values[1] is not None:
        approved_signer_transition = (signer_approval_values[0], signer_approval_values[1])
    matrix = validate_upgrade_pair(
        arguments.previous,
        arguments.current,
        approved_signer_transition=approved_signer_transition,
    )
    write_result(matrix, arguments.output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1) from None
