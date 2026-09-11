"""Create one reusable self-signed TLS identity for a private debug environment.

Requires cryptography. Existing identities are reused unless --renew is explicitly requested.
Only the public certificate is distributed to clients; keep key.pem private.
"""
import argparse
import datetime
import ipaddress
import shutil
import uuid
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


def general_name(host):
    try:
        return x509.IPAddress(ipaddress.ip_address(host))
    except ValueError:
        return x509.DNSName(host)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('.env/shared_debug_tls'))
    parser.add_argument('--host', action='append', default=[])
    parser.add_argument('--renew', action='store_true', help='Renew the certificate for 100 years, retaining its key and backing up the old certificate')
    args = parser.parse_args()
    hosts = sorted(set(['localhost', '127.0.0.1', '::1', *args.host]))
    cert_path, key_path = args.output / 'cert.pem', args.output / 'key.pem'
    existing = cert_path.exists() or key_path.exists()
    names = [general_name(host) for host in hosts]
    if existing:
        cert = x509.load_pem_x509_certificate(cert_path.read_bytes())
        key = serialization.load_pem_private_key(key_path.read_bytes(), password=None)
        assert cert.public_key().public_numbers() == key.public_key().public_numbers(), 'Certificate/key mismatch'
        names = cert.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
        assert all(general_name(host) in names for host in hosts), 'Existing identity does not cover requested hosts; use a new output directory'
        if not args.renew:
            assert cert.not_valid_after_utc > datetime.datetime.now(datetime.timezone.utc), 'Existing certificate expired'
            print('Reused existing shared debug certificate:', cert_path)
            return
        names = list(names)
        shutil.copy2(cert_path, cert_path.with_name('cert.before-renew-' + uuid.uuid4().hex + '.pem'))

    if not existing:
        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Pixels Debug')])
    now = datetime.datetime.now(datetime.timezone.utc)
    try:
        expires = now.replace(year=now.year + 100)
    except ValueError:
        expires = now.replace(year=now.year + 100, day=28)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(minutes=5))
            .not_valid_after(expires)
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .add_extension(x509.SubjectAlternativeName(names), critical=False)
            .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
            .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(key.public_key()), critical=False)
            .add_extension(x509.KeyUsage(True, False, True, False, False, False, False, None, None), critical=True)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
            .sign(key, hashes.SHA256()))
    args.output.mkdir(parents=True, exist_ok=True)
    # Exclusive creation prevents accidental replacement of an installation identity.
    if not existing:
        with key_path.open('xb') as output:
            output.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    with cert_path.open('wb' if existing else 'xb') as output:
        output.write(cert.public_bytes(serialization.Encoding.PEM))
    print('Shared self-signed debug certificate:', cert_path, 'expires:', expires.isoformat())


if __name__ == '__main__':
    main()
