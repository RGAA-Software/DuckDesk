#!/usr/bin/env python3
"""Opt-in short business smoke for the formal Windows Single Server package.

Run elevated with PIXELS_SINGLE_SERVER_WINDOWS_BUSINESS_TEST=1. The test owns one
temporary ProgramData directory, three Pixels SCM services and one PostgreSQL
container. It refuses to run when any Pixels service already exists.
"""

from __future__ import annotations

import hashlib
import http.client
import json
import os
import re
import secrets
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path
from urllib.parse import quote

from setup.make_single_server import validate_package


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
PACKAGE_ROOT = Path(os.getenv(
    "PIXELS_SINGLE_SERVER_PACKAGE",
    REPOSITORY_ROOT / "build_official/private_server/customer/1.0.3/windows/package",
)).resolve()
LICENSE_FIXTURE = "/tmp/pixels-private-server-target/release/examples/private_console_fixture"
POSTGRES_IMAGE = "postgres:18.6"


def checked(arguments: list[str], *, environment: dict[str, str] | None = None,
            input_text: str | None = None) -> str:
    result = subprocess.run(arguments, env=environment, input=input_text, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{arguments[0]} failed ({result.returncode}): {result.stderr[-1200:]}")
    return result.stdout.strip()


def postgres_sql(container_name: str, database_name: str, statements: str) -> None:
    checked(["docker", "exec", "-i", "-u", "postgres", container_name,
             "psql", "-X", "-v", "ON_ERROR_STOP=1", "-U", "postgres", "-d", database_name],
            input_text=statements)


def reserve_port() -> int:
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def service_state(service_name: str) -> str:
    result = subprocess.run(["sc.exe", "query", service_name], capture_output=True, text=True)
    if result.returncode:
        return "missing"
    state_match = re.search(r"STATE\s+:\s+\d+\s+(\w+)", result.stdout)
    return state_match.group(1) if state_match else "unknown"


def console_request(port: int, certificate: Path, method: str, route: str,
                    payload: dict[str, object] | None = None,
                    token: str | None = None) -> tuple[int, object]:
    context = ssl.create_default_context(cafile=str(certificate))
    connection = http.client.HTTPSConnection("localhost", port, context=context, timeout=5)
    headers = {"Origin": f"https://localhost:{port}", "x-pixels-client-type": "admin_web"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    body = None
    if payload is not None:
        headers["Content-Type"] = "application/json"
        body = json.dumps(payload).encode("utf-8")
    connection.request(method, route, body=body, headers=headers)
    response = connection.getresponse()
    response_body = response.read()
    connection.close()
    return response.status, json.loads(response_body) if response_body else None


def write_private(path: Path, content: str) -> None:
    path.write_text(content, encoding="utf-8")


def configure_private_directory(path: Path) -> None:
    # The product's private-file reader rejects inherited Users/Authenticated Users grants.
    checked(["icacls.exe", str(path), "/inheritance:r"])
    checked(["icacls.exe", str(path), "/grant:r", "*S-1-5-32-544:(OI)(CI)(F)",
             "*S-1-5-18:(OI)(CI)(F)"])


def wait_for_postgres(container_name: str) -> None:
    for _attempt in range(80):
        result = subprocess.run(["docker", "exec", container_name, "pg_isready", "-h", "127.0.0.1",
                                 "-U", "postgres"], capture_output=True, text=True)
        if result.returncode == 0:
            return
        time.sleep(0.5)
    raise RuntimeError("Isolated PostgreSQL did not become ready")


def main() -> None:
    if os.name != "nt" or os.getenv("PIXELS_SINGLE_SERVER_WINDOWS_BUSINESS_TEST") != "1":
        raise SystemExit("Run elevated on Windows with PIXELS_SINGLE_SERVER_WINDOWS_BUSINESS_TEST=1")
    existing_services = checked(["powershell.exe", "-NoProfile", "-Command",
                                 "Get-Service -Name 'Pixels.Setup','Pixels.Console','Pixels.Relay','Pixels.Backup.*' "
                                 "-ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name; exit 0"])
    if existing_services:
        raise SystemExit("Existing Pixels services are present; refusing to alter them")
    _, manifest_hash = validate_package(PACKAGE_ROOT)
    deployment_id = str(uuid.uuid4())
    backup_service = "Pixels.Backup." + deployment_id.replace("-", "")[:12]
    postgres_container = "pixels-win-single-pg-" + deployment_id[:8]
    postgres_password = secrets.token_hex(24)
    owner_password = secrets.token_hex(24)
    runtime_password = secrets.token_hex(24)
    administrator_password = secrets.token_hex(16)
    relay_app_key = secrets.token_hex(24)
    relay_control_key = secrets.token_hex(24)
    workspace_key_id = str(uuid.uuid4())
    console_port = reserve_port()
    relay_port = reserve_port()
    postgres_started = False
    program_data = Path(os.environ["ProgramData"])
    with tempfile.TemporaryDirectory(prefix="PixelsSingleBusiness-", dir=program_data) as temporary_name:
        test_root = Path(temporary_name)
        config_root = test_root / "config"
        license_directory = config_root / "license"
        data_root = test_root / "data"
        install_root = test_root / "installed"
        for directory in (config_root, license_directory, data_root, data_root / "recording", data_root / "repository",
                          data_root / "scheduler", data_root / "status"):
            directory.mkdir()
        configure_private_directory(config_root)
        configure_private_directory(license_directory)
        for directory in (data_root, data_root / "recording", data_root / "repository",
                          data_root / "scheduler", data_root / "status"):
            configure_private_directory(directory)
        try:
            checked(["docker", "run", "--rm", "--detach", "--name", postgres_container,
                     "--label", "pixels.validation=single-server-windows", "-p", "127.0.0.1::5432",
                     "-e", f"POSTGRES_PASSWORD={postgres_password}", POSTGRES_IMAGE])
            postgres_started = True
            wait_for_postgres(postgres_container)
            checked(["docker", "exec", "-u", "postgres", postgres_container, "openssl", "req",
                     "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-sha256",
                     "-subj", "/CN=pixels-windows-smoke-ca",
                     "-addext", "basicConstraints=critical,CA:TRUE",
                     "-addext", "keyUsage=critical,keyCertSign,cRLSign",
                     "-keyout", "/var/lib/postgresql/ca.key", "-out", "/var/lib/postgresql/ca.crt"])
            checked(["docker", "exec", "-u", "postgres", postgres_container, "openssl", "req",
                     "-newkey", "rsa:2048", "-nodes", "-sha256", "-subj", "/CN=localhost",
                     "-addext", "subjectAltName=DNS:localhost", "-addext", "extendedKeyUsage=serverAuth",
                     "-keyout", "/var/lib/postgresql/server.key",
                     "-out", "/var/lib/postgresql/server.csr"])
            checked(["docker", "exec", "-u", "postgres", postgres_container, "openssl", "x509",
                     "-req", "-in", "/var/lib/postgresql/server.csr", "-CA", "/var/lib/postgresql/ca.crt",
                     "-CAkey", "/var/lib/postgresql/ca.key", "-CAcreateserial", "-copy_extensions", "copy",
                     "-days", "1", "-sha256", "-out", "/var/lib/postgresql/server.crt"])
            postgres_sql(postgres_container, "postgres", """
ALTER SYSTEM SET ssl = 'on';
ALTER SYSTEM SET ssl_cert_file = '/var/lib/postgresql/server.crt';
ALTER SYSTEM SET ssl_key_file = '/var/lib/postgresql/server.key';
""")
            checked(["docker", "restart", postgres_container])
            wait_for_postgres(postgres_container)
            published_port = checked(["docker", "port", postgres_container, "5432/tcp"]).splitlines()[0]
            postgres_port = int(re.search(r":([0-9]+)$", published_port).group(1))
            pg_ca = config_root / "postgres-ca.crt"
            console_certificate = config_root / "console-tls.crt"
            console_key = config_root / "console-tls.key"
            for source, destination in (("ca.crt", pg_ca), ("server.crt", console_certificate),
                                        ("server.key", console_key)):
                checked(["docker", "cp", f"{postgres_container}:/var/lib/postgresql/{source}",
                         str(destination)])
            postgres_sql(postgres_container, "postgres", f"""
CREATE ROLE pixels_console_owner LOGIN PASSWORD '{owner_password}';
CREATE ROLE pixels_console_runtime LOGIN PASSWORD '{runtime_password}';
CREATE DATABASE pixels_console OWNER pixels_console_owner;
REVOKE ALL ON DATABASE pixels_console FROM PUBLIC;
GRANT CONNECT ON DATABASE pixels_console TO pixels_console_runtime;
""")
            postgres_sql(postgres_container, "pixels_console", f"""
REVOKE ALL ON SCHEMA public FROM PUBLIC;
SET ROLE pixels_console_owner;
CREATE SCHEMA pixels;
CREATE TABLE pixels.deployment_identity (
    singleton boolean PRIMARY KEY DEFAULT true CHECK(singleton),
    deployment_id uuid NOT NULL, service text NOT NULL CHECK(service IN ('console','auth','desk'))
);
INSERT INTO pixels.deployment_identity(deployment_id, service) VALUES ('{deployment_id}', 'console');
GRANT USAGE ON SCHEMA pixels TO pixels_console_runtime;
GRANT SELECT ON pixels.deployment_identity TO pixels_console_runtime;
""")
            print("PASS separate PostgreSQL 18 with verified TLS", flush=True)

            encoded_ca_path = quote(pg_ca.as_posix(), safe="/:\\")
            owner_url = (f"postgresql://pixels_console_owner:{owner_password}@localhost:{postgres_port}/"
                         f"pixels_console?sslrootcert={encoded_ca_path}")
            runtime_url = (f"postgresql://pixels_console_runtime:{runtime_password}@localhost:{postgres_port}/"
                           f"pixels_console?sslrootcert={encoded_ca_path}")
            package_bin = PACKAGE_ROOT / "bin"
            admin_environment = os.environ.copy()
            admin_environment.update({"PIXELS_CONSOLE_GUEST_SOURCE_KEY": str(config_root / "guest-source.key"),
                                      "PIXELS_CONSOLE_WORKSPACE_KEY": str(config_root / "workspace.key"),
                                      "PIXELS_CONSOLE_WORKSPACE_KEY_ID": workspace_key_id})
            checked([str(package_bin / "px_console_admin.exe"), "generate-secrets"], environment=admin_environment)
            wsl_config_root = checked(["wsl.exe", "-d", "Ubuntu-20.04", "--exec", "wslpath", "-a",
                                       str(config_root)])
            wsl_fixture_root = f"/tmp/pixels-win-single-{deployment_id}"
            checked(["wsl.exe", "-d", "Ubuntu-20.04", "-u", "root", "--exec", "mkdir", "-m", "700",
                     wsl_fixture_root])
            try:
                checked(["wsl.exe", "-d", "Ubuntu-20.04", "-u", "root", "--exec", "env",
                         "PIXELS_PG_ISOLATED_TEST=1", LICENSE_FIXTURE, deployment_id, wsl_fixture_root])
                for license_name in ("license-trust.json", "console.license"):
                    checked(["wsl.exe", "-d", "Ubuntu-20.04", "-u", "root", "--exec", "cp",
                             f"{wsl_fixture_root}/{license_name}",
                             f"{wsl_config_root}/license/{license_name}" if license_name == "console.license"
                             else f"{wsl_config_root}/{license_name}"])
            finally:
                if wsl_fixture_root == f"/tmp/pixels-win-single-{deployment_id}":
                    subprocess.run(["wsl.exe", "-d", "Ubuntu-20.04", "-u", "root", "--exec", "rm",
                                    "-r", "--", wsl_fixture_root], capture_output=True, text=True)
            write_private(config_root / "admin-password", administrator_password)
            write_private(config_root / "console.pgpass",
                          f"localhost:{postgres_port}:*:pixels_console_owner:{owner_password}\n")
            cache_environment = os.environ.copy()
            cache_environment.update({"PIXELS_DEPLOYMENT_ID": deployment_id,
                                      "PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY": str(data_root / "recording")})
            checked([str(package_bin / "px_console_admin.exe"), "initialize-recording-cache"],
                    environment=cache_environment)

            console_environment = {
                "PIXELS_DEPLOYMENT_ID": deployment_id,
                "PIXELS_CONSOLE_DISTRIBUTION": "customer",
                "PIXELS_CONSOLE_RELEASE_NAMESPACE": "pixels.customer",
                "PIXELS_CONSOLE_LOCAL_DEVELOPMENT": "0",
                "PIXELS_CONSOLE_DATABASE_URL": runtime_url,
                "PIXELS_CONSOLE_LISTEN": f"127.0.0.1:{console_port}",
                "PIXELS_CONSOLE_STATIC_DIRECTORY": str(PACKAGE_ROOT / "static/console"),
                "PIXELS_CONSOLE_TLS_CERT": str(console_certificate),
                "PIXELS_CONSOLE_TLS_KEY": str(console_key),
                "PIXELS_CONSOLE_PUBLIC_ORIGIN": f"https://localhost:{console_port}",
                "PIXELS_CONSOLE_REGISTRATION": "0",
                "PIXELS_CONSOLE_GUESTS": "0",
                "PIXELS_CONSOLE_SESSION_LIFETIME_SECONDS": "3600",
                "PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS": "3600",
                "PIXELS_CONSOLE_GUEST_SOURCE_KEY": str(config_root / "guest-source.key"),
                "PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY": workspace_key_id,
                "PIXELS_CONSOLE_WORKSPACE_KEYS": "'" + json.dumps([{
                    "id": workspace_key_id, "path": str(config_root / "workspace.key"),
                }]) + "'",
                "PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY": str(data_root / "recording"),
                "PIXELS_CONSOLE_RECORDING_CACHE_BYTES": "1073741824",
                "PIXELS_CONSOLE_RECORDING_CACHE_DOWNLOADS": "4",
                "PIXELS_CONSOLE_RECORDING_CACHE_TTL_SECONDS": "86400",
                "PIXELS_CONSOLE_LICENSE_TRUST_STORE": str(config_root / "license-trust.json"),
                "PIXELS_CONSOLE_LICENSE_FILE": str(license_directory / "console.license"),
                "PIXELS_RELAY_APP_KEY": relay_app_key,
            }
            write_private(config_root / "console.env", "".join(
                f"{name}={value}\n" for name, value in console_environment.items()))
            console_check_environment = os.environ.copy()
            console_check_environment.update({name: value.strip("'")
                                              for name, value in console_environment.items()})
            checked([str(package_bin / "px_console_admin.exe"), "validate-environment"],
                    environment=console_check_environment)
            relay_environment = {
                "PIXELS_DEPLOYMENT_ID": deployment_id,
                "PIXELS_RELAY_LISTEN": f"127.0.0.1:{relay_port}",
                "PIXELS_RELAY_APP_KEY": relay_app_key,
                "PIXELS_RELAY_CONTROL_KEY": relay_control_key,
                "PIXELS_RELAY_CONSOLE_CONTROL_URL": f"wss://localhost:{console_port}/api/console/relay-control",
                "PIXELS_RELAY_CONSOLE_CA_FILE": str(pg_ca),
                "PIXELS_RELAY_NODE_TOKEN": "0" * 64,
            }
            write_private(config_root / "relay.env", "".join(
                f"{name}={value}\n" for name, value in relay_environment.items()))

            tool_root = PACKAGE_ROOT / "postgresql/bin"
            backup_configuration = {
                "schema_version": 2, "deployment_id": deployment_id,
                "repository_root": str(data_root / "repository"),
                "offsite_repository_root": None,
                "scheduler_root": str(data_root / "scheduler"),
                "status_root": str(data_root / "status"),
                "pg_dump_path": str(install_root / "current/postgresql/bin/pg_dump.exe"),
                "pg_dump_sha256": hashlib.sha256((tool_root / "pg_dump.exe").read_bytes()).hexdigest(),
                "pg_restore_path": str(install_root / "current/postgresql/bin/pg_restore.exe"),
                "pg_restore_sha256": hashlib.sha256((tool_root / "pg_restore.exe").read_bytes()).hexdigest(),
                "command_timeout_seconds": 60, "poll_interval_seconds": 1,
                "schedule": {"deployment_id": deployment_id, "anchor_unix": int(time.time()),
                             "period_seconds": 3600},
                "retention": {"hourly": 24, "daily": 7, "weekly": 4, "monthly": 6,
                              "pre_upgrade": 5, "manual_days": 30},
                "offsite_retention": None,
                "plan": {"deployment_id": deployment_id, "kind": "independent",
                         "write_barrier_proof_file": None, "retention": ["hourly"],
                         "previous_recovery_set_id": None,
                         "targets": [
                             {"state": "required", "database": {
                                 "service": "console", "host": "localhost", "port": postgres_port,
                                 "database": "pixels_console", "username": "pixels_console_owner",
                                 "password_file": str(config_root / "console.pgpass"), "schema_version": 1}},
                             {"state": "not_applicable", "service": "auth", "reason": "not installed"},
                             {"state": "not_applicable", "service": "desk", "reason": "not installed"},
                         ]},
            }
            write_private(config_root / "backup.json", json.dumps(backup_configuration))
            database_environment = os.environ.copy()
            database_environment.update({"PIXELS_DEPLOYMENT_ID": deployment_id,
                                         "PIXELS_DATABASE_URL": owner_url,
                                         "PIXELS_PG_LOCAL_DEVELOPMENT": "0"})
            checked([str(package_bin / "px_db.exe"), "migrate", "console"], environment=database_environment)
            bootstrap_environment = database_environment | {
                "PIXELS_CONSOLE_INITIAL_USERNAME": "smoke-admin",
                "PIXELS_CONSOLE_INITIAL_PASSWORD_FILE": str(config_root / "admin-password"),
                "PIXELS_CONSOLE_LOCAL_DEVELOPMENT": "0",
            }
            checked([str(package_bin / "px_console_admin.exe"), "bootstrap"],
                    environment=bootstrap_environment)
            license_environment = os.environ.copy()
            license_environment.update({"PIXELS_DEPLOYMENT_ID": deployment_id,
                                        "PIXELS_CONSOLE_LICENSE_TRUST_STORE": str(config_root / "license-trust.json"),
                                        "PIXELS_CONSOLE_LICENSE_FILE": str(license_directory / "console.license")})
            checked([str(package_bin / "px_console_admin.exe"), "validate-license"],
                    environment=license_environment)

            # Start Console first so Relay can be registered before its first connection attempt.
            checked(["sc.exe", "create", "Pixels.Console", "binPath=", "\"" + str(package_bin / "px_console.exe")
                     + "\" --service \"" + str(config_root / "console.env") + "\"", "start=", "demand",
                     "obj=", "NT SERVICE\\Pixels.Console"])
            checked(["sc.exe", "sidtype", "Pixels.Console", "unrestricted"])
            checked(["icacls.exe", str(config_root), "/grant", "NT SERVICE\\Pixels.Console:(RX)"])
            for secret_path in (config_root / "console.env", config_root / "console-tls.crt",
                                config_root / "console-tls.key",
                                config_root / "guest-source.key", config_root / "workspace.key",
                                 config_root / "license-trust.json", license_directory / "console.license",
                                pg_ca):
                checked(["icacls.exe", str(secret_path), "/grant", "NT SERVICE\\Pixels.Console:(R)"])
            checked(["icacls.exe", str(data_root / "recording"), "/grant",
                     "NT SERVICE\\Pixels.Console:(OI)(CI)(M)"])
            checked(["icacls.exe", str(data_root), "/grant", "NT SERVICE\\Pixels.Console:(RX)"])
            checked(["icacls.exe", str(test_root), "/grant", "NT SERVICE\\Pixels.Console:(RX)"])
            checked(["sc.exe", "start", "Pixels.Console"])
            for _attempt in range(50):
                if service_state("Pixels.Console") == "STOPPED":
                    raise RuntimeError("Temporary real Console service exited before readiness")
                try:
                    ready_status, _ready_body = console_request(console_port, pg_ca, "GET", "/health/ready")
                    if ready_status == 204:
                        break
                except (OSError, ssl.SSLError):
                    pass
                time.sleep(0.2)
            else:
                raise RuntimeError(f"Real Windows Console did not become ready: {service_state('Pixels.Console')}")
            login_status, login_body = console_request(console_port, pg_ca, "POST",
                                                       "/api/console/sessions", {
                                                           "username": "smoke-admin",
                                                           "password": administrator_password,
                                                       })
            if login_status != 200 or not isinstance(login_body, dict):
                raise RuntimeError(f"Real Windows Console login failed: HTTP {login_status}")
            administrator_token = login_body["token"]
            print("PASS real Windows Console ready and administrator login", flush=True)

            relay_status, relay_body = console_request(console_port, pg_ca, "POST",
                                                       "/api/console/managed/relays", {
                                                           "name": "isolated-windows-relay", "public_host": "localhost",
                                                           "public_port": relay_port,
                                                       }, administrator_token)
            if relay_status != 201 or not isinstance(relay_body, dict):
                raise RuntimeError(f"Relay registration failed: HTTP {relay_status}")
            relay_environment["PIXELS_RELAY_NODE_TOKEN"] = relay_body["relay_token"]
            write_private(config_root / "relay.env", "".join(
                f"{name}={value}\n" for name, value in relay_environment.items()))
            configure_status, _configured_relay = console_request(
                console_port, pg_ca, "PATCH", f"/api/console/managed/relays/{relay_body['relay']['id']}",
                {"revision": relay_body["relay"]["revision"],
                 "configuration": {"draining": False, "disabled": False}}, administrator_token)
            if configure_status != 200:
                raise RuntimeError(f"Relay activation failed: HTTP {configure_status}")
            checked(["sc.exe", "stop", "Pixels.Console"])
            for _attempt in range(100):
                if service_state("Pixels.Console") == "STOPPED":
                    break
                time.sleep(0.2)
            else:
                raise RuntimeError("Temporary Console service did not stop")
            checked(["sc.exe", "delete", "Pixels.Console"])
            for _attempt in range(100):
                if service_state("Pixels.Console") == "missing":
                    break
                time.sleep(0.2)
            else:
                raise RuntimeError("Temporary Console service did not release its name")

            console_environment["PIXELS_CONSOLE_STATIC_DIRECTORY"] = str(
                install_root / "current/static/console")
            write_private(config_root / "console.env", "".join(
                f"{name}={value}\n" for name, value in console_environment.items()))

            installation = checked(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                                    "-File", str(PACKAGE_ROOT / "install.ps1"), "-PackageRoot",
                                    str(PACKAGE_ROOT), "-ExpectedManifestSha256", manifest_hash,
                                    "-ConfigRoot", str(config_root), "-DataRoot", str(data_root),
                                    "-InstallRoot", str(install_root)])
            if "RUNNING customer-server" not in installation:
                raise RuntimeError("Installer did not report three running services")
            if any(service_state(service_name) != "RUNNING" for service_name in
                   ("Pixels.Console", "Pixels.Relay", backup_service)):
                raise RuntimeError("A real Windows service did not remain running")
            for _attempt in range(50):
                if service_state("Pixels.Console") != "RUNNING":
                    raise RuntimeError("Installed Console exited before readiness")
                try:
                    ready_status, _ready_body = console_request(console_port, pg_ca, "GET", "/health/ready")
                    if ready_status == 204:
                        break
                except (OSError, ssl.SSLError):
                    pass
                time.sleep(0.2)
            else:
                raise RuntimeError("Installed Console did not become ready")
            print("PASS formal Windows package installed three real SCM services", flush=True)

            for _attempt in range(50):
                relay_list_status, relay_list_body = console_request(
                    console_port, pg_ca, "GET", "/api/console/managed/relays?limit=100",
                    token=administrator_token)
                relay_entries = relay_list_body if isinstance(relay_list_body, list) else []
                if relay_list_status == 200 and any(
                    entry.get("id") == relay_body["relay"]["id"] and entry.get("state") == "ready"
                    and entry.get("fresh") is True for entry in relay_entries
                ):
                    break
                time.sleep(0.2)
            else:
                raise RuntimeError("Real Windows Relay did not become ready/fresh")
            print("PASS real Windows Relay registered and ready/fresh", flush=True)

            recovery_set_id = None
            for _attempt in range(100):
                status_file = data_root / "status/status.json"
                if status_file.is_file():
                    backup_status = json.loads(status_file.read_text(encoding="utf-8"))
                    recovery_set_id = backup_status.get("last_recovery_set_id")
                    if recovery_set_id and (data_root / "repository" / recovery_set_id /
                                            "manifest.json").is_file():
                        break
                time.sleep(0.2)
            else:
                raise RuntimeError("Real Windows Backup did not publish a verified recovery point")
            recovery_root = data_root / "repository" / recovery_set_id
            recovery_record = json.loads((recovery_root / "manifest.json").read_text(encoding="utf-8"))
            archive_file = recovery_root / "console.dump"
            console_member = next(member["member"] for member in recovery_record["members"]
                                  if member["service"] == "console")
            if (recovery_record["status"] != "verified" or
                console_member["archive_sha256"] != hashlib.sha256(archive_file.read_bytes()).hexdigest()):
                raise RuntimeError("Real Windows Backup manifest or archive hash invalid")
            postgres_sql(postgres_container, "postgres",
                         "CREATE DATABASE pixels_console_restore OWNER pixels_console_owner;")
            restore_environment = os.environ.copy()
            restore_environment.update({"PGSSLMODE": "verify-full", "PGSSLROOTCERT": str(pg_ca),
                                        "PGPASSFILE": str(config_root / "console.pgpass")})
            checked([str(install_root / "current/postgresql/bin/pg_restore.exe"), "--exit-on-error",
                     "--no-owner", "--no-password", "--host", "localhost", "--port", str(postgres_port),
                     "--username", "pixels_console_owner", "--dbname", "pixels_console_restore",
                     str(archive_file)], environment=restore_environment)
            restored_identity = checked(["docker", "exec", "-u", "postgres", postgres_container, "psql",
                                         "-X", "-At", "-U", "postgres", "-d", "pixels_console_restore",
                                         "-c", "SELECT deployment_id FROM pixels.deployment_identity"])
            if restored_identity != deployment_id:
                raise RuntimeError("Restored Windows backup has the wrong deployment identity")
            print("PASS real Windows Backup verified and restored into isolated PostgreSQL database", flush=True)

            retained_marker = data_root / "retained.txt"
            write_private(retained_marker, "retained")
            covered_installation = checked(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                                            "-File", str(PACKAGE_ROOT / "install.ps1"), "-PackageRoot",
                                            str(PACKAGE_ROOT), "-ExpectedManifestSha256", manifest_hash,
                                            "-ConfigRoot", str(config_root), "-DataRoot", str(data_root),
                                            "-InstallRoot", str(install_root)])
            if ("RUNNING customer-server" not in covered_installation or
                retained_marker.read_text(encoding="utf-8") != "retained" or
                any(service_state(service_name) != "RUNNING" for service_name in
                    ("Pixels.Console", "Pixels.Relay", backup_service))):
                raise RuntimeError("Direct overwrite lost service state or persistent data")
            print("PASS direct overwrite retained data and three real SCM services", flush=True)
        finally:
            if (install_root / "current/uninstall.ps1").is_file():
                subprocess.run(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                                str(install_root / "current/uninstall.ps1"), "-InstallRoot", str(install_root)],
                               capture_output=True, text=True)
            for service_name in ("Pixels.Console", "Pixels.Relay", backup_service):
                if service_state(service_name) != "missing":
                    subprocess.run(["sc.exe", "stop", service_name], capture_output=True, text=True)
                    subprocess.run(["sc.exe", "delete", service_name], capture_output=True, text=True)
            if postgres_started:
                subprocess.run(["docker", "stop", postgres_container], capture_output=True, text=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as smoke_error:
        print(f"FAIL Windows Single Server smoke: {smoke_error}", file=sys.stderr)
        raise
