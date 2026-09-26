#!/usr/bin/env python3
"""Short, isolated Linux Compose business smoke test; requires root in WSL and Docker."""

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


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
IMAGE_ARCHIVE = Path(sys.argv[1]).resolve() if len(sys.argv) == 2 else None
IMAGE_TAG = "pixels-server:1.0.3"
LICENSE_FIXTURE = Path("/tmp/pixels-private-server-target/release/examples/private_console_fixture")
POSTGRES_IMAGE = "postgres:18.6"


def checked(arguments: list[str], *, input_text: str | None = None, capture: bool = True) -> str:
    result = subprocess.run(arguments, input=input_text, text=True, capture_output=capture, check=False)
    if result.returncode:
        raise RuntimeError(f"Command failed: {arguments[0]} {arguments[1]}: {result.stderr[-1600:]}")
    return result.stdout.strip() if capture else ""


def private_text(path: Path, contents: str, owner: int, mode: int = 0o400) -> None:
    path.write_text(contents, encoding="utf-8")
    os.chown(path, owner, owner)
    path.chmod(mode)


def reserve_port() -> int:
    with socket.socket() as listener:
        listener.bind(("", 0))
        return listener.getsockname()[1]


def wait_for_postgres(container_name: str) -> None:
    for _attempt in range(90):
        readiness = subprocess.run([
            "docker", "exec", container_name, "pg_isready", "-h", "127.0.0.1", "-U", "postgres",
        ], capture_output=True, text=True)
        if readiness.returncode == 0:
            return
        time.sleep(0.5)
    raise RuntimeError("Isolated PostgreSQL did not become ready")


def postgres_sql(container_name: str, database_name: str, statements: str) -> None:
    checked(["docker", "exec", "-i", "-u", "postgres", container_name,
             "psql", "-X", "-v", "ON_ERROR_STOP=1", "-U", "postgres", "-d", database_name],
            input_text=statements)


def console_request(port: int, certificate: Path, method: str, path: str,
                    payload: dict[str, object] | None = None, token: str | None = None) -> tuple[int, object]:
    tls_context = ssl.create_default_context(cafile=str(certificate))
    connection = http.client.HTTPSConnection("host.docker.internal", port, context=tls_context, timeout=5)
    headers = {"Origin": f"https://host.docker.internal:{port}", "x-pixels-client-type": "admin_web"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    encoded_payload = None
    if payload is not None:
        headers["Content-Type"] = "application/json"
        encoded_payload = json.dumps(payload).encode("utf-8")
    connection.request(method, path, body=encoded_payload, headers=headers)
    response = connection.getresponse()
    response_bytes = response.read()
    connection.close()
    response_value: object = json.loads(response_bytes) if response_bytes else None
    return response.status, response_value


def main() -> None:
    if os.geteuid() != 0 or IMAGE_ARCHIVE is None or not IMAGE_ARCHIVE.is_file() or not LICENSE_FIXTURE.is_file():
        raise SystemExit("usage (root WSL, built isolated fixture): single_server_compose_smoke.py <image-tar>")
    if checked(["docker", "ps", "--filter", "label=com.docker.compose.project=pixels-server", "-q"]):
        raise SystemExit("The pixels-server Compose project is already in use")

    deployment_id = str(uuid.uuid4())
    postgres_container = "pixels-single-pg-" + deployment_id[:8]
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

    with tempfile.TemporaryDirectory(prefix="pixels-single-compose-") as temporary_name:
        test_root = Path(temporary_name)
        test_root.chmod(0o755)
        config_root = test_root / "config"
        data_root = test_root / "data"
        console_secrets = config_root / "console"
        license_directory = console_secrets / "license"
        backup_secrets = config_root / "backup"
        recording_cache = data_root / "recording-cache"
        repository = data_root / "repository"
        scheduler = data_root / "scheduler"
        status = data_root / "status"
        for directory, owner, mode in (
            (config_root, 0, 0o755), (data_root, 0, 0o755),
            (console_secrets, 10001, 0o700), (license_directory, 10001, 0o700),
            (backup_secrets, 10003, 0o700),
            (recording_cache, 10001, 0o700), (repository, 10003, 0o700),
            (scheduler, 10003, 0o700), (status, 10003, 0o700),
        ):
            directory.mkdir()
            os.chown(directory, owner, owner)
            directory.chmod(mode)
        try:
            checked(["docker", "run", "--rm", "--detach", "--name", postgres_container,
                     "--label", "pixels.validation=single-server-compose", "-p", "0:5432",
                     "-e", f"POSTGRES_PASSWORD={postgres_password}", POSTGRES_IMAGE])
            postgres_started = True
            wait_for_postgres(postgres_container)
            checked(["docker", "exec", "-u", "postgres", postgres_container, "openssl", "req",
                     "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-sha256",
                     "-subj", "/CN=pixels-isolated-postgres-ca",
                     "-addext", "basicConstraints=critical,CA:TRUE",
                     "-addext", "keyUsage=critical,keyCertSign,cRLSign",
                     "-keyout", "/var/lib/postgresql/ca.key",
                     "-out", "/var/lib/postgresql/ca.crt"])
            checked(["docker", "exec", "-u", "postgres", postgres_container, "openssl", "req",
                     "-newkey", "rsa:2048", "-nodes", "-sha256",
                     "-subj", "/CN=host.docker.internal",
                     "-addext", "subjectAltName=DNS:host.docker.internal",
                     "-addext", "extendedKeyUsage=serverAuth",
                     "-keyout", "/var/lib/postgresql/server.key",
                     "-out", "/var/lib/postgresql/server.csr"])
            checked(["docker", "exec", "-u", "postgres", postgres_container, "openssl", "x509",
                     "-req", "-in", "/var/lib/postgresql/server.csr",
                     "-CA", "/var/lib/postgresql/ca.crt", "-CAkey", "/var/lib/postgresql/ca.key",
                     "-CAcreateserial", "-copy_extensions", "copy", "-days", "1", "-sha256",
                     "-out", "/var/lib/postgresql/server.crt"])
            postgres_sql(postgres_container, "postgres", """
ALTER SYSTEM SET ssl = 'on';
ALTER SYSTEM SET ssl_cert_file = '/var/lib/postgresql/server.crt';
ALTER SYSTEM SET ssl_key_file = '/var/lib/postgresql/server.key';
""")
            checked(["docker", "restart", postgres_container])
            wait_for_postgres(postgres_container)
            published_port = checked(["docker", "port", postgres_container, "5432/tcp"]).splitlines()[0]
            postgres_port = int(re.search(r":([0-9]+)$", published_port).group(1))
            pg_ca = config_root / "pg-ca.crt"
            checked(["docker", "cp", f"{postgres_container}:/var/lib/postgresql/ca.crt", str(pg_ca)])
            pg_ca.chmod(0o444)
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
            print("PASS isolated PostgreSQL 18 is separately running with verify-full TLS", flush=True)

            ssl_parameters = "sslrootcert=/etc/pixels/pg-ca.crt"
            owner_url = (f"postgresql://pixels_console_owner:{owner_password}@host.docker.internal:"
                         f"{postgres_port}/pixels_console?{ssl_parameters}")
            runtime_url = (f"postgresql://pixels_console_runtime:{runtime_password}@host.docker.internal:"
                           f"{postgres_port}/pixels_console?{ssl_parameters}")
            checked(["docker", "run", "--rm", "--user", "10001:10001",
                     "--volume", f"{config_root}:/etc/pixels",
                     "--env", "PIXELS_CONSOLE_GUEST_SOURCE_KEY=/etc/pixels/console/guest-source.key",
                     "--env", "PIXELS_CONSOLE_WORKSPACE_KEY=/etc/pixels/console/workspace.key",
                     "--env", f"PIXELS_CONSOLE_WORKSPACE_KEY_ID={workspace_key_id}",
                     IMAGE_TAG, "/opt/pixels/bin/px_console_admin", "generate-secrets"])
            fixture_environment = os.environ.copy()
            fixture_environment["PIXELS_PG_ISOLATED_TEST"] = "1"
            subprocess.run([str(LICENSE_FIXTURE), deployment_id, str(console_secrets)],
                           env=fixture_environment, check=True, capture_output=True, text=True)
            signed_license = test_root / "signed-license.pxlic"
            (console_secrets / "console.license").replace(signed_license)
            for license_file in (console_secrets / "license-trust.json",):
                os.chown(license_file, 10001, 10001)
            console_certificate = console_secrets / "console-tls.crt"
            console_key = console_secrets / "console-tls.key"
            console_ca = config_root / "console-ca.crt"
            checked(["docker", "cp", f"{postgres_container}:/var/lib/postgresql/ca.crt", str(console_ca)])
            checked(["docker", "cp", f"{postgres_container}:/var/lib/postgresql/server.crt",
                     str(console_certificate)])
            checked(["docker", "cp", f"{postgres_container}:/var/lib/postgresql/server.key",
                     str(console_key)])
            checked(["openssl", "verify", "-CAfile", str(console_ca), str(console_certificate)])
            console_ca.chmod(0o444)
            for tls_file in (console_certificate, console_key):
                os.chown(tls_file, 10001, 10001)
                tls_file.chmod(0o400)
            checked(["docker", "run", "--rm", "--user", "10001:10001",
                     "--volume", f"{data_root}:/var/lib/pixels",
                     "--env", f"PIXELS_DEPLOYMENT_ID={deployment_id}",
                     "--env", "PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY=/var/lib/pixels/recording-cache",
                     IMAGE_TAG, "/opt/pixels/bin/px_console_admin", "initialize-recording-cache"])
            private_text(console_secrets / "admin-password", administrator_password, 10001)
            private_text(backup_secrets / "console.pgpass",
                         f"host.docker.internal:{postgres_port}:*:pixels_console_owner:{owner_password}\n",
                         10003)

            console_environment = {
                "PIXELS_DEPLOYMENT_ID": deployment_id,
                "PIXELS_CONSOLE_DISTRIBUTION": "customer",
                "PIXELS_CONSOLE_RELEASE_NAMESPACE": "pixels.customer",
                "PIXELS_CONSOLE_LOCAL_DEVELOPMENT": "0",
                "PIXELS_CONSOLE_DATABASE_URL": runtime_url,
                "PIXELS_CONSOLE_LISTEN": "0.0.0.0:4600",
                "PIXELS_CONSOLE_STATIC_DIRECTORY": "/opt/pixels/static/console",
                "PIXELS_CONSOLE_TLS_CERT": "/etc/pixels/console/console-tls.crt",
                "PIXELS_CONSOLE_TLS_KEY": "/etc/pixels/console/console-tls.key",
                "PIXELS_CONSOLE_PUBLIC_ORIGIN": f"https://host.docker.internal:{console_port}",
                "PIXELS_CONSOLE_REGISTRATION": "0", "PIXELS_CONSOLE_GUESTS": "0",
                "PIXELS_CONSOLE_SESSION_LIFETIME_SECONDS": "3600",
                "PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS": "3600",
                "PIXELS_CONSOLE_GUEST_SOURCE_KEY": "/etc/pixels/console/guest-source.key",
                "PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY": workspace_key_id,
                "PIXELS_CONSOLE_WORKSPACE_KEYS": "'" + json.dumps([{
                    "id": workspace_key_id, "path": "/etc/pixels/console/workspace.key",
                }]) + "'",
                "PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY": "/var/lib/pixels/recording-cache",
                "PIXELS_CONSOLE_RECORDING_CACHE_BYTES": "1073741824",
                "PIXELS_CONSOLE_RECORDING_CACHE_DOWNLOADS": "4",
                "PIXELS_CONSOLE_RECORDING_CACHE_TTL_SECONDS": "86400",
                "PIXELS_CONSOLE_LICENSE_TRUST_STORE": "/etc/pixels/console/license-trust.json",
                "PIXELS_CONSOLE_LICENSE_FILE": "/etc/pixels/console/license/console.license",
                "PIXELS_RELAY_APP_KEY": relay_app_key,
            }
            private_text(config_root / "console.env",
                         "".join(f"{name}={value}\n" for name, value in console_environment.items()), 10001)
            private_text(config_root / "database.env",
                         f"PIXELS_DEPLOYMENT_ID={deployment_id}\nPIXELS_DATABASE_URL={owner_url}\n"
                         "PIXELS_PG_LOCAL_DEVELOPMENT=0\n", 10001)
            relay_environment = {
                "PIXELS_DEPLOYMENT_ID": deployment_id,
                "PIXELS_RELAY_LISTEN": "0.0.0.0:4605",
                "PIXELS_RELAY_APP_KEY": relay_app_key,
                "PIXELS_RELAY_CONTROL_KEY": relay_control_key,
                "PIXELS_RELAY_CONSOLE_CONTROL_URL": (
                    f"wss://host.docker.internal:{console_port}/api/console/relay-control"
                ),
                "PIXELS_RELAY_CONSOLE_CA_FILE": "/etc/pixels/console-ca.crt",
                "PIXELS_RELAY_NODE_TOKEN": "0" * 64,
            }
            private_text(config_root / "relay.env",
                         "".join(f"{name}={value}\n" for name, value in relay_environment.items()), 10002)

            pg_toolchain_manifest = json.loads(Path(
                REPOSITORY_ROOT / "build_official/private_server/candidates/linux-single-dev-20260925/"
                "postgresql/18/sha256.json").read_text(encoding="utf-8"))
            anchor_unix = int(time.time())
            backup_configuration = {
                "schema_version": 2, "deployment_id": deployment_id,
                "repository_root": "/var/lib/pixels/repository",
                "offsite_repository_root": None,
                "scheduler_root": "/var/lib/pixels/scheduler", "status_root": "/var/lib/pixels/status",
                "pg_dump_path": "/opt/pixels/postgresql/18/bin/pg_dump",
                "pg_dump_sha256": pg_toolchain_manifest["artifacts"]["bin/pg_dump"],
                "pg_restore_path": "/opt/pixels/postgresql/18/bin/pg_restore",
                "pg_restore_sha256": pg_toolchain_manifest["artifacts"]["bin/pg_restore"],
                "command_timeout_seconds": 60, "poll_interval_seconds": 1,
                "schedule": {"deployment_id": deployment_id, "anchor_unix": anchor_unix,
                             "period_seconds": 3600},
                "retention": {"hourly": 24, "daily": 7, "weekly": 4, "monthly": 6,
                              "pre_upgrade": 5, "manual_days": 30},
                "offsite_retention": None,
                "plan": {"deployment_id": deployment_id, "kind": "independent",
                         "write_barrier_proof_file": None, "retention": ["hourly"],
                         "previous_recovery_set_id": None,
                         "targets": [
                             {"state": "required", "database": {
                                 "service": "console", "host": "host.docker.internal", "port": postgres_port,
                                 "database": "pixels_console", "username": "pixels_console_owner",
                                 "password_file": "/etc/pixels/backup/console.pgpass", "schema_version": 1}},
                             {"state": "not_applicable", "service": "auth", "reason": "not installed"},
                             {"state": "not_applicable", "service": "desk", "reason": "not installed"},
                         ]},
            }
            private_text(config_root / "backup.json", json.dumps(backup_configuration), 10003)
            settings = test_root / "settings.env"
            private_text(settings, f"PIXELS_SERVER_IMAGE={IMAGE_TAG}\nPIXELS_CONFIG_DIR={config_root}\n"
                         f"PIXELS_DATA_DIR={data_root}\nPIXELS_CONSOLE_PORT={console_port}\n"
                         f"PIXELS_RELAY_PORT={relay_port}\n", 0, 0o600)
            compose_file = REPOSITORY_ROOT / "deploy/single_server/linux/compose.yaml"
            compose = ["docker", "compose", "--env-file", str(settings), "-f", str(compose_file)]
            checked(compose + ["run", "--rm", "--no-deps", "--entrypoint", "/bin/bash", "tools",
                             "-c", f"echo >/dev/tcp/host.docker.internal/{postgres_port}"])
            checked(["docker", "run", "--rm", "--network", "pixels-server_default",
                     "--volume", f"{config_root}:/etc/pixels:ro",
                     "--env", f"PGPASSWORD={owner_password}", "--env", "PGSSLMODE=verify-full",
                     "--env", "PGSSLROOTCERT=/etc/pixels/pg-ca.crt", POSTGRES_IMAGE, "psql",
                     "-h", "host.docker.internal", "-p", str(postgres_port),
                     "-U", "pixels_console_owner", "-d", "pixels_console",
                     "-X", "-At", "-c", "SELECT 1"])
            print("PASS Compose network reached isolated database with TLS", flush=True)
            checked(compose + ["run", "--rm", "--no-deps", "tools", "migrate", "console"])
            checked(compose + ["run", "--rm", "--no-deps", "-e", f"PIXELS_DATABASE_URL={owner_url}",
                             "-e", "PIXELS_CONSOLE_INITIAL_USERNAME=smoke-admin",
                             "-e", "PIXELS_CONSOLE_INITIAL_PASSWORD_FILE=/etc/pixels/console/admin-password",
                             "--entrypoint", "/opt/pixels/bin/px_console_admin", "console", "bootstrap"])
            checked(compose + ["up", "-d", "console"])

            last_readiness_error = "no response"
            for _attempt in range(40):
                try:
                    ready_status, _ready_body = console_request(console_port, console_ca,
                                                                 "GET", "/health/ready")
                    if ready_status == 503:
                        break
                    last_readiness_error = f"HTTP {ready_status}"
                except (OSError, ssl.SSLError) as readiness_error:
                    last_readiness_error = str(readiness_error)
                time.sleep(0.25)
            else:
                service_state = checked(compose + ["ps", "--all"])
                console_logs = checked(compose + ["logs", "--no-color", "--tail", "30", "console"])
                observed_fingerprint = "unavailable"
                try:
                    with socket.create_connection(("host.docker.internal", console_port), timeout=3) as raw_socket:
                        with ssl._create_unverified_context().wrap_socket(
                            raw_socket, server_hostname="host.docker.internal"
                        ) as tls_socket:
                            observed_fingerprint = hashlib.sha256(tls_socket.getpeercert(binary_form=True)).hexdigest()
                except OSError:
                    pass
                expected_fingerprint = hashlib.sha256(ssl.PEM_cert_to_DER_cert(
                    console_certificate.read_text(encoding="utf-8"))).hexdigest()
                raise RuntimeError(f"Unlicensed Console did not return 503: {last_readiness_error}\n"
                                   f"expected_cert={expected_fingerprint} observed_cert={observed_fingerprint}\n"
                                   f"{service_state}\n{console_logs}")
            login_status, login_body = console_request(console_port, console_ca,
                                                       "POST", "/api/console/sessions", {
                                                           "username": "smoke-admin",
                                                           "password": administrator_password,
                                                       })
            if login_status != 200 or not isinstance(login_body, dict) or len(login_body.get("token", "")) != 64:
                raise RuntimeError(f"Console administrator login failed: HTTP {login_status}")
            admin_token = login_body["token"]
            denied_status, _denied_body = console_request(
                console_port, console_ca, "GET", "/api/console/managed/relays?limit=100", token=admin_token
            )
            if denied_status != 503:
                raise RuntimeError(f"Unlicensed business API was not denied: HTTP {denied_status}")
            license_status, license_body = console_request(
                console_port, console_ca, "PUT", "/api/console/managed/license",
                {"wire": signed_license.read_text(encoding="utf-8")}, admin_token
            )
            if license_status != 200 or not isinstance(license_body, dict) or license_body.get("max_streams") != 8:
                raise RuntimeError(f"Console license import failed: HTTP {license_status}")
            ready_status, _ready_body = console_request(console_port, console_ca, "GET", "/health/ready")
            if ready_status != 204 or not (license_directory / "console.license").is_file():
                raise RuntimeError(f"Console did not activate after license import: HTTP {ready_status}")
            print("PASS unlicensed business rejection, administrator login and web license activation", flush=True)

            relay_status, relay_body = console_request(console_port, console_ca,
                                                       "POST", "/api/console/managed/relays", {
                                                           "name": "isolated-single-relay",
                                                           "public_host": "host.docker.internal",
                                                           "public_port": relay_port,
                                                       }, admin_token)
            if relay_status != 201 or not isinstance(relay_body, dict):
                raise RuntimeError(f"Relay registration failed: HTTP {relay_status}")
            relay_environment["PIXELS_RELAY_NODE_TOKEN"] = relay_body["relay_token"]
            private_text(config_root / "relay.env",
                         "".join(f"{name}={value}\n" for name, value in relay_environment.items()), 10002)
            configure_status, _configured_relay = console_request(
                console_port, console_ca, "PATCH",
                f"/api/console/managed/relays/{relay_body['relay']['id']}", {
                    "revision": relay_body["relay"]["revision"],
                    "configuration": {"draining": False, "disabled": False},
                }, admin_token)
            if configure_status != 200:
                raise RuntimeError(f"Relay activation failed: HTTP {configure_status}")
            archive_sha256 = hashlib.sha256(IMAGE_ARCHIVE.read_bytes()).hexdigest()
            checked(["bash", str(REPOSITORY_ROOT / "deploy/single_server/linux/deploy.sh"), "install",
                     str(settings), str(IMAGE_ARCHIVE), archive_sha256])
            print("PASS Compose install started Console, Relay and Backup", flush=True)
            relay_list_status = 0
            relay_list_body: object = None
            for _attempt in range(40):
                try:
                    relay_list_status, relay_list_body = console_request(
                        console_port, console_ca, "GET", "/api/console/managed/relays?limit=100",
                        token=admin_token)
                except (OSError, ssl.SSLError):
                    time.sleep(0.25)
                    continue
                relay_entries = relay_list_body if isinstance(relay_list_body, list) else []
                if relay_list_status == 200 and any(entry.get("id") == relay_body["relay"]["id"]
                                                    and entry.get("state") == "ready" and entry.get("fresh") is True
                                                    for entry in relay_entries):
                    break
                time.sleep(0.25)
            else:
                relay_logs = checked(compose + ["logs", "--no-color", "--tail", "30", "relay"])
                console_logs = checked(compose + ["logs", "--no-color", "--tail", "20", "console"])
                raise RuntimeError(f"Relay inventory did not become ready/fresh: "
                                   f"HTTP {relay_list_status} body={str(relay_list_body)[:1200]}\n"
                                   f"{console_logs}\n{relay_logs}")
            print("PASS Relay registered and ready/fresh", flush=True)

            for _attempt in range(80):
                status_file = status / "status.json"
                if status_file.is_file():
                    backup_status = json.loads(status_file.read_text(encoding="utf-8"))
                    recovery_set_id = backup_status.get("last_recovery_set_id")
                    if recovery_set_id:
                        recovery_manifest = repository / recovery_set_id / "manifest.json"
                        if recovery_manifest.is_file():
                            break
                time.sleep(0.25)
            else:
                raise RuntimeError("Backup did not publish a verified recovery point")
            archive_file = repository / recovery_set_id / "console.dump"
            recovery_record = json.loads(recovery_manifest.read_text(encoding="utf-8"))
            console_member = next(member["member"] for member in recovery_record["members"]
                                  if member["service"] == "console")
            if (recovery_record["status"] != "verified" or
                recovery_record["deployment_id"] != deployment_id or
                console_member["archive_sha256"] != hashlib.sha256(archive_file.read_bytes()).hexdigest()):
                raise RuntimeError("Backup recovery point manifest or archive hash is invalid")
            postgres_sql(postgres_container, "postgres",
                         "CREATE DATABASE pixels_console_restore OWNER pixels_console_owner;")
            checked(["docker", "run", "--rm", "--network", "pixels-server_default",
                     "--user", "10003:10003", "--volume", f"{config_root}:/etc/pixels:ro",
                     "--volume", f"{data_root}:/var/lib/pixels:ro",
                     "--env", "PGSSLMODE=verify-full",
                     "--env", "PGSSLROOTCERT=/etc/pixels/pg-ca.crt",
                     "--env", "PGPASSFILE=/etc/pixels/backup/console.pgpass", IMAGE_TAG,
                     "/opt/pixels/postgresql/18/bin/pg_restore", "--exit-on-error", "--no-owner",
                     "--no-password", "--host", "host.docker.internal", "--port", str(postgres_port),
                     "--username", "pixels_console_owner", "--dbname", "pixels_console_restore",
                     f"/var/lib/pixels/repository/{recovery_set_id}/console.dump"])
            restored_identity = checked(["docker", "exec", "-u", "postgres", postgres_container,
                                         "psql", "-X", "-At", "-U", "postgres", "-d",
                                         "pixels_console_restore", "-c",
                                         "SELECT deployment_id FROM pixels.deployment_identity"])
            if restored_identity != deployment_id:
                raise RuntimeError("Restored database deployment identity differs")
            print("PASS Backup verified manifest and restored an isolated database", flush=True)

            retained_marker = data_root / "retained.txt"
            retained_marker.write_text("retained", encoding="utf-8")
            checked(compose + ["down"])
            checked(compose + ["up", "-d", "console", "relay", "backup"])
            if retained_marker.read_text(encoding="utf-8") != "retained":
                raise RuntimeError("Compose recreate lost persistent data")
            print("PASS Compose down/up retained persistent data", flush=True)
        finally:
            if (test_root / "settings.env").is_file():
                subprocess.run(["bash", str(REPOSITORY_ROOT / "deploy/single_server/linux/deploy.sh"),
                                "uninstall", str(test_root / "settings.env")],
                               capture_output=True, text=True)
            if postgres_started:
                subprocess.run(["docker", "stop", postgres_container], capture_output=True, text=True)


if __name__ == "__main__":
    main()
