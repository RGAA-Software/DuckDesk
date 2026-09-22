#!/usr/bin/env python3
"""Require the current public Console deployment-identity wire generation."""

from __future__ import annotations

import argparse
import json
import ssl
import sys
import urllib.parse
import urllib.request
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def verify_current_console_identity(console_base: str, console_ca: Path | None = None) -> None:
    parsed_base = urllib.parse.urlsplit(console_base)
    if parsed_base.scheme != "https" or not parsed_base.hostname or parsed_base.username or parsed_base.password:
        raise RuntimeError("Console preflight requires an HTTPS origin without embedded credentials")
    identity_url = urllib.parse.urljoin(console_base.rstrip("/") + "/", ".well-known/pixels")
    request = urllib.request.Request(identity_url, headers={"Accept": "application/json"})
    if console_ca is not None and not console_ca.is_file():
        raise RuntimeError(f"Console CA certificate is missing: {console_ca}")
    tls_context = ssl.create_default_context(cafile=str(console_ca) if console_ca is not None else None)
    if hasattr(ssl, "VERIFY_X509_STRICT"):
        tls_context.verify_flags &= ~ssl.VERIFY_X509_STRICT
    try:
        with urllib.request.urlopen(request, timeout=10, context=tls_context) as response:
            response_bytes = response.read(64 * 1024 + 1)
    except OSError as error:
        raise RuntimeError("Authoritative Console deployment identity cannot be reached") from error
    if len(response_bytes) > 64 * 1024:
        raise RuntimeError("Authoritative Console deployment identity response is too large")
    try:
        identity = json.loads(response_bytes.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError("Authoritative Console deployment identity is not valid JSON") from error
    certificate_wire = identity.get("certificate_wire")
    descriptor_wire = identity.get("descriptor_wire")
    if not isinstance(certificate_wire, str) or not certificate_wire.startswith("PXDC2."):
        raise RuntimeError("Focused publish requires the current PXDC2 Console identity")
    if not isinstance(descriptor_wire, str) or not descriptor_wire.startswith("PXDD2."):
        raise RuntimeError("Focused publish requires the current PXDD2 Console descriptor")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console-base", required=True)
    parser.add_argument(
        "--console-ca",
        type=Path,
        default=ROOT / ".env" / "public_console_ca.pem",
        help="CA certificate used to authenticate the authoritative Console",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    verify_current_console_identity(arguments.console_base, arguments.console_ca)
    print(json.dumps({"ConsoleBase": arguments.console_base, "IdentityGeneration": "PXDC2/PXDD2"}, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"Console identity preflight failed: {error}", file=sys.stderr)
        raise SystemExit(1) from None
