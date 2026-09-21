#!/usr/bin/env python3
"""Sign and independently verify Pixels Windows release artifacts."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import urlparse


CODE_SIGNING_EKU = "1.3.6.1.5.5.7.3.3"
HEX_SHA1 = re.compile(r"^[0-9A-F]{40}$")
HEX_SHA256 = re.compile(r"^[0-9A-F]{64}$")


@dataclass(frozen=True)
class SigningConfiguration:
    certificate_sha1: str
    certificate_sha256: str
    certificate_store: str
    timestamp_url: str
    signtool: Path

    @property
    def machine_store(self) -> bool:
        return self.certificate_store == "local_machine"


def normalized_hex(value: str) -> str:
    return re.sub(r"[:\s]", "", value).upper()


def required_environment(name: str) -> str:
    value = os.environ.get(name, "").strip()
    if not value:
        raise RuntimeError(f"required Windows release signing input is missing: {name}")
    return value


def find_signtool() -> Path:
    configured = os.environ.get("PIXELS_WINDOWS_SIGNTOOL", "").strip()
    if configured:
        configured_path = Path(configured).expanduser().resolve()
        if not configured_path.is_file():
            raise RuntimeError(f"configured signtool.exe is missing: {configured_path}")
        return configured_path

    executable = shutil.which("signtool.exe") or shutil.which("signtool")
    if executable:
        return Path(executable).resolve()

    program_files_x86 = os.environ.get("ProgramFiles(x86)", "").strip()
    if program_files_x86:
        windows_kit_bin = Path(program_files_x86) / "Windows Kits" / "10" / "bin"
        candidates = sorted(windows_kit_bin.glob("*/x64/signtool.exe"), reverse=True)
        if candidates:
            return candidates[0].resolve()
    raise RuntimeError("signtool.exe is required for a Windows release build")


def load_configuration() -> SigningConfiguration:
    certificate_sha1 = normalized_hex(required_environment("PIXELS_WINDOWS_SIGNING_CERT_SHA1"))
    certificate_sha256 = normalized_hex(required_environment("PIXELS_WINDOWS_SIGNING_CERT_SHA256"))
    certificate_store = required_environment("PIXELS_WINDOWS_SIGNING_STORE").lower()
    timestamp_url = required_environment("PIXELS_WINDOWS_TIMESTAMP_URL")
    if not HEX_SHA1.fullmatch(certificate_sha1):
        raise RuntimeError("PIXELS_WINDOWS_SIGNING_CERT_SHA1 must contain exactly 40 hexadecimal characters")
    if not HEX_SHA256.fullmatch(certificate_sha256):
        raise RuntimeError("PIXELS_WINDOWS_SIGNING_CERT_SHA256 must contain exactly 64 hexadecimal characters")
    if certificate_store not in {"current_user", "local_machine"}:
        raise RuntimeError("PIXELS_WINDOWS_SIGNING_STORE must be current_user or local_machine")
    parsed_timestamp = urlparse(timestamp_url)
    if parsed_timestamp.scheme.lower() != "https" or not parsed_timestamp.netloc or parsed_timestamp.username or parsed_timestamp.password:
        raise RuntimeError("PIXELS_WINDOWS_TIMESTAMP_URL must be an HTTPS origin without credentials")
    return SigningConfiguration(
        certificate_sha1=certificate_sha1,
        certificate_sha256=certificate_sha256,
        certificate_store=certificate_store,
        timestamp_url=timestamp_url,
        signtool=find_signtool(),
    )


def powershell_certificate_details(configuration: SigningConfiguration) -> dict[str, object]:
    powershell = shutil.which("powershell.exe") or shutil.which("powershell")
    if not powershell:
        raise RuntimeError("Windows PowerShell is required for release certificate validation")
    environment = os.environ.copy()
    environment["PIXELS_CERTIFICATE_STORE_SCOPE"] = (
        "LocalMachine" if configuration.machine_store else "CurrentUser"
    )
    environment["PIXELS_CERTIFICATE_SHA1"] = configuration.certificate_sha1
    probe = r"""
$ErrorActionPreference = 'Stop'
$certificatePath = "Cert:\$env:PIXELS_CERTIFICATE_STORE_SCOPE\My\$env:PIXELS_CERTIFICATE_SHA1"
$certificate = Get-Item -LiteralPath $certificatePath -ErrorAction Stop
$sha256 = [BitConverter]::ToString(
    [Security.Cryptography.SHA256]::Create().ComputeHash($certificate.RawData)
).Replace('-', '')
$hasCodeSigningEku = @($certificate.EnhancedKeyUsageList | Where-Object {
    $_.ObjectId.Value -eq '1.3.6.1.5.5.7.3.3'
}).Count -gt 0
[ordered]@{
    sha1 = $certificate.Thumbprint
    sha256 = $sha256
    has_private_key = $certificate.HasPrivateKey
    has_code_signing_eku = $hasCodeSigningEku
    not_before = $certificate.NotBefore.ToUniversalTime().ToString('O')
    not_after = $certificate.NotAfter.ToUniversalTime().ToString('O')
    currently_valid = $certificate.NotBefore.ToUniversalTime() -le [DateTime]::UtcNow -and
        $certificate.NotAfter.ToUniversalTime() -gt [DateTime]::UtcNow
} | ConvertTo-Json -Compress
"""
    result = subprocess.run(
        [powershell, "-NoProfile", "-NonInteractive", "-Command", probe],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=environment,
    )
    details = json.loads(result.stdout.strip())
    if not isinstance(details, dict):
        raise RuntimeError("Windows signing certificate probe returned an invalid result")
    return details


def preflight(configuration: SigningConfiguration) -> None:
    details = powershell_certificate_details(configuration)
    if normalized_hex(str(details.get("sha1", ""))) != configuration.certificate_sha1:
        raise RuntimeError("selected Windows signing certificate SHA-1 does not match the approved selector")
    if normalized_hex(str(details.get("sha256", ""))) != configuration.certificate_sha256:
        raise RuntimeError("selected Windows signing certificate does not match the approved SHA-256 pin")
    if details.get("has_private_key") is not True:
        raise RuntimeError("selected Windows signing certificate has no accessible private key")
    if details.get("has_code_signing_eku") is not True:
        raise RuntimeError("selected Windows signing certificate does not permit code signing")
    if details.get("currently_valid") is not True:
        raise RuntimeError("selected Windows signing certificate is not currently valid")


def signing_arguments(configuration: SigningConfiguration, artifact: str | Path) -> list[str]:
    arguments = [
        str(configuration.signtool),
        "sign",
        "/fd",
        "SHA256",
        "/sha1",
        configuration.certificate_sha1,
        "/s",
        "My",
    ]
    if configuration.machine_store:
        arguments.append("/sm")
    arguments.extend(
        [
            "/tr",
            configuration.timestamp_url,
            "/td",
            "SHA256",
            str(artifact),
        ]
    )
    return arguments


def verify_file(artifact: Path, expected_certificate_sha256: str, signtool: Path | None = None) -> None:
    artifact = artifact.resolve()
    if not artifact.is_file():
        raise RuntimeError(f"Windows release artifact is missing: {artifact}")
    verification_tool = signtool or find_signtool()
    subprocess.run(
        [str(verification_tool), "verify", "/pa", "/all", str(artifact)],
        check=True,
    )
    powershell = shutil.which("powershell.exe") or shutil.which("powershell")
    if not powershell:
        raise RuntimeError("Windows PowerShell is required for Authenticode verification")
    environment = os.environ.copy()
    environment["PIXELS_SIGNED_ARTIFACT"] = str(artifact)
    verification = r"""
$ErrorActionPreference = 'Stop'
$signature = Get-AuthenticodeSignature -LiteralPath $env:PIXELS_SIGNED_ARTIFACT
$signerSha256 = if ($null -eq $signature.SignerCertificate) { '' } else {
    [BitConverter]::ToString(
        [Security.Cryptography.SHA256]::Create().ComputeHash($signature.SignerCertificate.RawData)
    ).Replace('-', '')
}
[ordered]@{
    status = [string]$signature.Status
    signer_sha256 = $signerSha256
    timestamped = $null -ne $signature.TimeStamperCertificate
} | ConvertTo-Json -Compress
"""
    result = subprocess.run(
        [powershell, "-NoProfile", "-NonInteractive", "-Command", verification],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=environment,
    )
    details = json.loads(result.stdout.strip())
    if details.get("status") != "Valid":
        raise RuntimeError(f"Authenticode validation failed for {artifact}: {details.get('status')}")
    if normalized_hex(str(details.get("signer_sha256", ""))) != normalized_hex(expected_certificate_sha256):
        raise RuntimeError(f"Authenticode signer pin mismatch: {artifact}")
    if details.get("timestamped") is not True:
        raise RuntimeError(f"Authenticode timestamp is missing: {artifact}")


def sign_file(artifact: Path, configuration: SigningConfiguration) -> None:
    artifact = artifact.resolve()
    if not artifact.is_file():
        raise RuntimeError(f"Windows release artifact is missing: {artifact}")
    subprocess.run(signing_arguments(configuration, artifact), check=True)
    verify_file(artifact, configuration.certificate_sha256, configuration.signtool)


def nsis_finalize_command(configuration: SigningConfiguration) -> str:
    return subprocess.list2cmdline(signing_arguments(configuration, "%1"))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    subcommands.add_parser("preflight")
    sign_parser = subcommands.add_parser("sign")
    sign_parser.add_argument("artifacts", type=Path, nargs="+")
    verify_parser = subcommands.add_parser("verify")
    verify_parser.add_argument("--certificate-sha256", required=True)
    verify_parser.add_argument("artifacts", type=Path, nargs="+")
    return parser.parse_args()


def main() -> int:
    arguments = parse_args()
    if arguments.command == "verify":
        expected_pin = normalized_hex(arguments.certificate_sha256)
        if not HEX_SHA256.fullmatch(expected_pin):
            raise RuntimeError("--certificate-sha256 must contain exactly 64 hexadecimal characters")
        for artifact in arguments.artifacts:
            verify_file(artifact, expected_pin)
        return 0

    configuration = load_configuration()
    preflight(configuration)
    if arguments.command == "sign":
        for artifact in arguments.artifacts:
            sign_file(artifact, configuration)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1) from None
