#!/usr/bin/env python3
"""Generate an isolated replacement CA and Console certificate for the 90 test deployment."""

from __future__ import annotations

import argparse
import datetime as dt
import ipaddress
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


def write_new(destination: Path, contents: bytes) -> None:
    with destination.open("xb") as output:
        output.write(contents)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    output_directory = arguments.output.resolve(strict=True)
    if not output_directory.is_dir() or any(output_directory.iterdir()):
        raise ValueError("TLS output must be an existing empty private directory")
    current_time = dt.datetime.now(dt.timezone.utc)
    valid_from = current_time - dt.timedelta(minutes=5)
    valid_until = current_time + dt.timedelta(days=365)
    authority_key = ec.generate_private_key(ec.SECP256R1())
    authority_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Pixels Single Server CA")])
    authority_certificate = (
        x509.CertificateBuilder()
        .subject_name(authority_name)
        .issuer_name(authority_name)
        .public_key(authority_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(valid_from)
        .not_valid_after(valid_until)
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(authority_key.public_key()), critical=False)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(authority_key.public_key()), critical=False)
        .add_extension(x509.KeyUsage(digital_signature=False, content_commitment=False,
                                     key_encipherment=False, data_encipherment=False,
                                     key_agreement=False, key_cert_sign=True, crl_sign=True,
                                     encipher_only=False, decipher_only=False), critical=True)
        .sign(authority_key, hashes.SHA256())
    )
    console_key = ec.generate_private_key(ec.SECP256R1())
    console_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Pixels Console")])
    console_certificate = (
        x509.CertificateBuilder()
        .subject_name(console_name)
        .issuer_name(authority_name)
        .public_key(console_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(valid_from)
        .not_valid_after(valid_until)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(console_key.public_key()), critical=False)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(authority_key.public_key()), critical=False)
        .add_extension(x509.SubjectAlternativeName([
            x509.IPAddress(ipaddress.ip_address("39.71.45.66")),
            x509.IPAddress(ipaddress.ip_address("127.0.0.1")),
            x509.DNSName("localhost"),
            x509.DNSName("console"),
        ]), critical=False)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False,
                                     key_encipherment=False, data_encipherment=False,
                                     key_agreement=False, key_cert_sign=False, crl_sign=False,
                                     encipher_only=False, decipher_only=False), critical=True)
        .sign(authority_key, hashes.SHA256())
    )
    write_new(output_directory / "ca.pem", authority_certificate.public_bytes(serialization.Encoding.PEM))
    write_new(output_directory / "server.crt", console_certificate.public_bytes(serialization.Encoding.PEM))
    write_new(output_directory / "server.key", console_key.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    print("Generated a distinct-subject test CA and Console certificate")


if __name__ == "__main__":
    main()
