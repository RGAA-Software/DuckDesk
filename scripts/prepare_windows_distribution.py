#!/usr/bin/env python3
"""Validate approved deployment identity inputs and emit Windows package policy."""

from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import os
import shutil
import sys
import tempfile
import uuid
from pathlib import Path
from urllib.parse import urlsplit


ENVIRONMENT_FIELDS = {
    "trust_store": "PIXELS_DEPLOYMENT_TRUST_STORE_FILE",
    "certificate_version": "PIXELS_DEPLOYMENT_CERTIFICATE_VERSION",
    "descriptor_revision": "PIXELS_DESCRIPTOR_REVISION",
    "trust_epoch": "PIXELS_DEPLOYMENT_TRUST_EPOCH",
    "deployment_id": "PIXELS_EXPECTED_DEPLOYMENT_ID",
    "official_origin": "PIXELS_OFFICIAL_CONSOLE_URL",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", required=True, choices=("cloud_node", "client", "remote"))
    parser.add_argument("--distribution", required=True, choices=("official", "customer"))
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument(
        "--matrix-customer",
        action="store_true",
        help="Build the customer half of one official+customer transaction while ignoring official-only environment inputs.",
    )
    return parser.parse_args()


def required_positive_integer(name: str) -> int:
    value = os.environ.get(name, "")
    if not value.isascii() or not value.isdecimal() or value.startswith("0"):
        raise RuntimeError(f"{name} must be an explicit positive integer")
    number = int(value)
    if number <= 0 or number > (2**63 - 1):
        raise RuntimeError(f"{name} is outside the supported positive integer range")
    return number


def load_canonical_trust_store(path: Path) -> tuple[bytes, int]:
    try:
        trust_bytes = path.read_bytes()
        trust_store = json.loads(trust_bytes.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError("the approved deployment trust store is unreadable or invalid") from error
    if not isinstance(trust_store, dict) or list(trust_store) != ["schema_version", "trust_epoch", "trusted_keys"]:
        raise RuntimeError("the deployment trust store must use the exact schema and canonical field order")
    trust_epoch = trust_store.get("trust_epoch")
    trusted_keys = trust_store.get("trusted_keys")
    if trust_store.get("schema_version") != 1 or not isinstance(trust_epoch, int) or isinstance(trust_epoch, bool) or trust_epoch <= 0:
        raise RuntimeError("the deployment trust store has an invalid schema version or trust epoch")
    if not isinstance(trusted_keys, list) or not 1 <= len(trusted_keys) <= 16:
        raise RuntimeError("the deployment trust store must contain between 1 and 16 trusted keys")
    previous_key_id = ""
    for trusted_key in trusted_keys:
        if not isinstance(trusted_key, dict) or list(trusted_key) != ["key_id", "public_key_hex"]:
            raise RuntimeError("each trusted key must use the exact canonical schema")
        key_id = trusted_key.get("key_id")
        public_key_hex = trusted_key.get("public_key_hex")
        if not isinstance(key_id, str) or not isinstance(public_key_hex, str):
            raise RuntimeError("trusted key values must be strings")
        try:
            public_key = bytes.fromhex(public_key_hex)
        except ValueError as error:
            raise RuntimeError("a trusted deployment public key is not lowercase hexadecimal") from error
        if (
            len(key_id) != 64
            or key_id.lower() != key_id
            or len(public_key_hex) != 64
            or public_key_hex.lower() != public_key_hex
            or len(public_key) != 32
            or not any(public_key)
            or hashlib.sha256(public_key).hexdigest() != key_id
            or (previous_key_id and previous_key_id >= key_id)
        ):
            raise RuntimeError("the deployment trust store contains a malformed, mismatched, or unsorted key")
        previous_key_id = key_id
    canonical_bytes = json.dumps(trust_store, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if trust_bytes != canonical_bytes:
        raise RuntimeError("the deployment trust store must be canonical UTF-8 JSON without a trailing newline")
    return trust_bytes, trust_epoch


def canonical_deployment_id(value: str) -> str:
    try:
        parsed = uuid.UUID(value)
    except ValueError as error:
        raise RuntimeError("PIXELS_EXPECTED_DEPLOYMENT_ID must be a canonical non-nil UUID") from error
    if parsed.int == 0 or str(parsed) != value:
        raise RuntimeError("PIXELS_EXPECTED_DEPLOYMENT_ID must be a canonical non-nil UUID")
    return value


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


def build_policy(distribution: str, trust_epoch: int, matrix_customer: bool) -> dict[str, object]:
    certificate_version = required_positive_integer(ENVIRONMENT_FIELDS["certificate_version"])
    descriptor_revision = required_positive_integer(ENVIRONMENT_FIELDS["descriptor_revision"])
    configured_epoch = required_positive_integer(ENVIRONMENT_FIELDS["trust_epoch"])
    if configured_epoch != trust_epoch:
        raise RuntimeError("PIXELS_DEPLOYMENT_TRUST_EPOCH must exactly match the approved trust store")
    deployment_id = os.environ.get(ENVIRONMENT_FIELDS["deployment_id"], "").strip()
    official_origin = os.environ.get(ENVIRONMENT_FIELDS["official_origin"], "").strip()
    if distribution == "official":
        if not deployment_id or not official_origin:
            raise RuntimeError("Official Windows builds require the expected deployment ID and Console origin")
        expected_deployment_id: str | None = canonical_deployment_id(deployment_id)
        expected_origin: str | None = canonical_https_origin(official_origin)
    else:
        if not matrix_customer and (deployment_id or official_origin):
            raise RuntimeError("Customer Windows builds must not configure Official deployment identity inputs")
        expected_deployment_id = None
        expected_origin = None
    return {
        "schema_version": 1,
        "distribution": distribution,
        "expected_deployment_id": expected_deployment_id,
        "official_console_origin": expected_origin,
        "minimum_certificate_version": certificate_version,
        "minimum_descriptor_revision": descriptor_revision,
        "minimum_trust_epoch": configured_epoch,
        "protocol_version": 2,
    }


def publish_policy(output_dir: Path, policy: dict[str, object], trust_bytes: bytes) -> None:
    output_dir.parent.mkdir(parents=True, exist_ok=True)
    staging_dir = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.staging-", dir=output_dir.parent))
    try:
        (staging_dir / "deployment-policy.json").write_text(
            json.dumps(policy, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
        (staging_dir / "deployment-trust.json").write_bytes(trust_bytes)
        if output_dir.exists():
            shutil.rmtree(output_dir)
        os.replace(staging_dir, output_dir)
    finally:
        shutil.rmtree(staging_dir, ignore_errors=True)


def main() -> int:
    arguments = parse_args()
    trust_store_value = os.environ.get(ENVIRONMENT_FIELDS["trust_store"], "").strip()
    if not trust_store_value:
        raise RuntimeError("PIXELS_DEPLOYMENT_TRUST_STORE_FILE must identify the approved canonical public trust store")
    trust_store_path = Path(trust_store_value).resolve()
    if not trust_store_path.is_file():
        raise RuntimeError("PIXELS_DEPLOYMENT_TRUST_STORE_FILE does not identify a regular file")
    trust_bytes, trust_epoch = load_canonical_trust_store(trust_store_path)
    policy = build_policy(arguments.distribution, trust_epoch, arguments.matrix_customer)
    if arguments.validate_only:
        if arguments.output_dir is not None:
            raise RuntimeError("--output-dir cannot be combined with --validate-only")
        print(f"Validated Windows {arguments.product}/{arguments.distribution} deployment identity inputs.")
        return 0
    if arguments.output_dir is None:
        raise RuntimeError("--output-dir is required unless --validate-only is used")
    publish_policy(arguments.output_dir.resolve(), policy, trust_bytes)
    print(f"Prepared Windows {arguments.product}/{arguments.distribution} deployment policy: {arguments.output_dir.resolve()}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1) from None
