#!/usr/bin/env python3
"""Isolated short Backup↔Console WSS and manual PostgreSQL recovery test."""

from __future__ import annotations

import hashlib
import http.client
import json
import os
import re
import secrets
import ssl
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT))

from single_server_windows_smoke import (
    checked,
    configure_private_directory,
    postgres_sql,
    service_state,
    wait_for_postgres,
)


PACKAGE_ROOT = Path(os.environ.get(
    "PIXELS_BACKUP_E2E_PACKAGE_ROOT",
    str(REPOSITORY_ROOT / ".cache/backup-control-e2e-package-20260927"),
))
RUNTIME_BIN_ROOT = Path(os.environ.get("PIXELS_BACKUP_E2E_BIN_ROOT", str(PACKAGE_ROOT / "bin")))


def console_request(port: int, certificate: Path, method: str, route: str,
                    payload: dict[str, object] | None = None,
                    token: str | None = None) -> tuple[int, object]:
    tls_context = ssl.create_default_context(cafile=str(certificate))
    # The generated test CA is trusted and the hostname is verified. Python 3.13's
    # additional strict AKI requirement is unrelated to this WSS control test.
    tls_context.verify_flags &= ~ssl.VERIFY_X509_STRICT
    connection = http.client.HTTPSConnection("localhost", port, context=tls_context, timeout=5)
    headers = {"Origin": f"https://localhost:{port}", "x-pixels-client-type": "admin_web"}
    if token is not None:
        headers["Authorization"] = f"Bearer {token}"
    body = None
    if payload is not None:
        headers["Content-Type"] = "application/json"
        body = json.dumps(payload).encode("utf-8")
    connection.request(method, route, body=body, headers=headers)
    response = connection.getresponse()
    response_bytes = response.read()
    connection.close()
    return response.status, json.loads(response_bytes) if response_bytes else None


def wait_for_backup_status(console_ca: Path, administrator_token: str,
                           predicate, timeout_seconds: float) -> dict[str, object]:
    deadline = time.monotonic() + timeout_seconds
    last_response: object = None
    while time.monotonic() < deadline:
        status_code, response = console_request(
            4600, console_ca, "GET", "/api/console/managed/backup", token=administrator_token,
        )
        last_response = response
        if status_code == 200 and isinstance(response, dict) and predicate(response):
            return response
        time.sleep(0.5)
    raise RuntimeError(f"Backup state did not converge: {last_response}")


def stop_process(process: subprocess.Popen[str] | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def main() -> None:
    if os.name != "nt" or os.getenv("PIXELS_BACKUP_CONTROL_E2E") != "1":
        raise SystemExit("Run elevated on Windows with PIXELS_BACKUP_CONTROL_E2E=1")
    for service_name in ("Pixels.Console", "Pixels.Relay", "Pixels.Backup.*"):
        if service_state(service_name) != "missing":
            raise RuntimeError(f"Refusing to touch a host with {service_name} installed")
    if not PACKAGE_ROOT.joinpath("bin/px_console.exe").is_file():
        raise RuntimeError("Reviewed isolated Windows candidate is missing")

    run_id = uuid.uuid4().hex[:8]
    postgres_container = f"pixels-backup-control-pg-{run_id}"
    postgres_password = secrets.token_hex(24)
    administrator_password = "Strong9!" + secrets.token_hex(12)
    console_process: subprocess.Popen[str] | None = None
    backup_process: subprocess.Popen[str] | None = None
    postgres_started = False

    with tempfile.TemporaryDirectory(prefix="PixelsBackupControl-", dir=os.environ["ProgramData"]) as temporary_name:
        test_root = Path(temporary_name)
        configure_private_directory(test_root)
        checked(["icacls.exe", str(test_root), "/remove:g", "*S-1-3-4"])
        config_root = test_root / "config"
        data_root = test_root / "data"
        pg_ca_input = test_root / "postgres-ca-input.crt"
        console_log = test_root / "console.log"
        backup_log = test_root / "backup.log"
        try:
            checked([
                "docker", "run", "--rm", "--detach", "--name", postgres_container,
                "--label", "pixels.validation=backup-control-windows",
                "-p", "127.0.0.1::5432", "-e", f"POSTGRES_PASSWORD={postgres_password}", "postgres:18.6",
            ])
            postgres_started = True
            wait_for_postgres(postgres_container)
            checked(["docker", "exec", "-u", "postgres", postgres_container, "openssl", "req",
                     "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-sha256",
                     "-subj", "/CN=pixels-backup-control-test-ca",
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
            checked(["docker", "cp", f"{postgres_container}:/var/lib/postgresql/ca.crt", str(pg_ca_input)])
            print("PASS isolated PostgreSQL 18 uses verified TLS", flush=True)

            setup_input = {
                "postgresql_host": "localhost",
                "postgresql_port": postgres_port,
                "postgresql_administrator": "postgres",
                "postgresql_password": postgres_password,
                "postgresql_ca_pem": pg_ca_input.read_text(encoding="utf-8"),
                "public_host": "localhost",
                "initial_username": "backup-admin",
                "initial_password": administrator_password,
            }
            try:
                setup_output = checked([
                    str(PACKAGE_ROOT / "bin/px_console_admin.exe"), "initialize-single-server",
                    str(config_root), str(data_root), str(PACKAGE_ROOT), str(PACKAGE_ROOT), "windows",
                ], input_text=json.dumps(setup_input))
            except RuntimeError as setup_error:
                created_paths = sorted(path.relative_to(test_root).as_posix() for path in test_root.rglob("*"))
                root_acl = checked(["icacls.exe", str(test_root)])
                config_acl = checked(["icacls.exe", str(config_root)])
                raise RuntimeError(f"{setup_error}; created paths: {created_paths}; root ACL: {root_acl}; config ACL: {config_acl}") from setup_error
            if "Single Server initialized: deployment=" not in setup_output:
                raise RuntimeError("Single Server setup did not report a deployment")
            deployment_id = setup_output.split("deployment=")[-1].strip()
            backup_configuration = json.loads((config_root / "backup.json").read_text(encoding="utf-8"))
            assert backup_configuration["control"]["console_url"] == "wss://localhost:4600/api/console/backup-control"
            assert f"PIXELS_CONSOLE_BACKUP_CONTROL_TOKEN={backup_configuration['control']['token']}" in (
                config_root / "console.env").read_text(encoding="utf-8")
            console_ca = config_root / "console-ca.crt"
            pg_ca = config_root / "postgresql-ca.crt"
            print("PASS fresh setup created private Backup control configuration", flush=True)

            with console_log.open("w", encoding="utf-8") as console_output, backup_log.open("w", encoding="utf-8") as backup_output:
                console_process = subprocess.Popen(
                    [str(RUNTIME_BIN_ROOT / "px_console.exe"), "--wait-env-file", str(config_root / "console.env")],
                    stdout=console_output, stderr=subprocess.STDOUT, text=True,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
                for _attempt in range(100):
                    if console_process.poll() is not None:
                        raise RuntimeError(f"Console exited: {console_log.read_text(encoding='utf-8')[-1200:]}")
                    try:
                        login_code, login_body = console_request(4600, console_ca, "POST", "/api/console/sessions", {
                            "username": "backup-admin", "password": administrator_password,
                        })
                        if login_code == 200 and isinstance(login_body, dict) and login_body.get("token"):
                            break
                    except OSError:
                        pass
                    time.sleep(0.2)
                else:
                    raise RuntimeError("Console administrator login did not become available")
                administrator_token = login_body["token"]
                print("PASS Console administrator login without a product license", flush=True)

                backup_environment = os.environ.copy()
                backup_environment.update({"PGSSLMODE": "verify-full", "PGSSLROOTCERT": str(pg_ca)})
                backup_process = subprocess.Popen(
                    [str(RUNTIME_BIN_ROOT / "px_backup.exe"), "run", str(config_root / "backup.json")],
                    stdout=backup_output, stderr=subprocess.STDOUT, text=True, env=backup_environment,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
                initial_state = wait_for_backup_status(
                    console_ca, administrator_token,
                    lambda response: response.get("connected") is True and
                    isinstance(response.get("status"), dict) and
                    response["status"].get("last_recovery_set_id") is not None,
                    90,
                )
                scheduled_recovery_set = initial_state["status"]["last_recovery_set_id"]
                if backup_process.poll() is not None:
                    raise RuntimeError(f"Backup exited: {backup_log.read_text(encoding='utf-8')[-1200:]}")
                print("PASS Backup WSS connected and reported a verified scheduled set", flush=True)

                trigger_code, trigger_body = console_request(
                    4600, console_ca, "POST", "/api/console/managed/backup/trigger", token=administrator_token,
                )
                if trigger_code != 200 or not isinstance(trigger_body, dict) or not trigger_body.get("task_id"):
                    raise RuntimeError(f"Manual trigger failed: HTTP {trigger_code}, {trigger_body}")
                manual_state = wait_for_backup_status(
                    console_ca, administrator_token,
                    lambda response: response.get("connected") is True and
                    isinstance(response.get("status"), dict) and
                    response["status"].get("last_recovery_set_id") not in (None, scheduled_recovery_set) and
                    response["status"].get("active_task") is None,
                    90,
                )
                manual_recovery_set = manual_state["status"]["last_recovery_set_id"]
                recovery_root = data_root / "backup/repository" / manual_recovery_set
                manifest = json.loads((recovery_root / "manifest.json").read_text(encoding="utf-8"))
                if manifest["status"] != "verified" or manifest["retention"] != ["manual"]:
                    raise RuntimeError("Manual recovery set was not verified or marked manual")
                archive = recovery_root / "console.dump"
                console_member = next(member["member"] for member in manifest["members"] if member["service"] == "console")
                if console_member["archive_sha256"] != hashlib.sha256(archive.read_bytes()).hexdigest():
                    raise RuntimeError("Manual recovery archive hash mismatch")
                print("PASS manual task completed through WSS with a verified manual recovery set", flush=True)

                inventory_code, inventory_body = console_request(
                    4600, console_ca, "GET", "/api/console/managed/backup/recovery-sets",
                    token=administrator_token,
                )
                if inventory_code != 200 or not isinstance(inventory_body, dict):
                    raise RuntimeError(f"Recovery-set inventory failed: HTTP {inventory_code}, {inventory_body}")
                listed_recovery_sets = inventory_body.get("recovery_sets", [])
                if not any(recovery_set["recovery_set_id"] == manual_recovery_set for recovery_set in listed_recovery_sets):
                    raise RuntimeError("Manual recovery set is missing from the verified inventory")
                preflight_code, preflight_body = console_request(
                    4600, console_ca, "GET",
                    f"/api/console/managed/backup/recovery-sets/{manual_recovery_set}/preflight",
                    token=administrator_token,
                )
                if (preflight_code != 200 or not isinstance(preflight_body, dict) or
                        preflight_body.get("repository_integrity") != "verified_at_snapshot" or
                        preflight_body.get("restore_admission") != "not_evaluated" or
                        preflight_body.get("recovery_set", {}).get("recovery_set_id") != manual_recovery_set):
                    raise RuntimeError(f"Read-only preflight failed: HTTP {preflight_code}, {preflight_body}")
                print("PASS inventory and read-only preflight identify the same manual recovery set", flush=True)

                browser_environment = os.environ.copy()
                browser_environment.update({
                    "PIXELS_PUBLIC_CONSOLE_URL": "https://localhost:4600",
                    "PIXELS_PUBLIC_CONSOLE_USERNAME": "backup-admin",
                    "PIXELS_PUBLIC_CONSOLE_PASSWORD": administrator_password,
                })
                browser_result = subprocess.run(
                    ["npx.cmd", "playwright", "test", "e2e-public/admin-backup.public.spec.ts",
                     "--config=playwright.public.config.ts"],
                    cwd=REPOSITORY_ROOT / "web/px_console", env=browser_environment,
                    capture_output=True, text=True, encoding="utf-8", errors="replace",
                    check=False, timeout=150,
                )
                if browser_result.returncode != 0:
                    raise RuntimeError(f"Backup browser check failed: {browser_result.stdout[-1500:]} {browser_result.stderr[-1000:]}")
                print("PASS packaged Console page displays and preflights a verified recovery set", flush=True)

                postgres_sql(postgres_container, "postgres", "CREATE DATABASE pixels_console_restore;")
                restore_environment = os.environ.copy()
                restore_environment.update({
                    "PGSSLMODE": "verify-full", "PGSSLROOTCERT": str(pg_ca),
                    "PGPASSWORD": postgres_password,
                })
                checked([
                    str(PACKAGE_ROOT / "postgresql/bin/pg_restore.exe"), "--exit-on-error", "--no-owner",
                    "--host", "localhost", "--port", str(postgres_port), "--username", "postgres",
                    "--dbname", "pixels_console_restore", str(archive),
                ], environment=restore_environment)
                restored_deployment = checked([
                    "docker", "exec", "-u", "postgres", postgres_container, "psql", "-X", "-At",
                    "-U", "postgres", "-d", "pixels_console_restore", "-c",
                    "SELECT deployment_id FROM pixels.deployment_identity",
                ])
                if restored_deployment != deployment_id:
                    raise RuntimeError("Restored deployment identity differs")
                print("PASS manual archive restored into a separate PostgreSQL database", flush=True)

                stop_process(backup_process)
                backup_process = None
                wait_for_backup_status(console_ca, administrator_token,
                                       lambda response: response.get("connected") is False, 15)
                print("PASS Console reports Backup offline after disconnect", flush=True)
        finally:
            stop_process(backup_process)
            stop_process(console_process)
            if postgres_started:
                subprocess.run(["docker", "stop", postgres_container], capture_output=True, text=True, check=False)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"FAIL Backup control end-to-end test: {error}", file=sys.stderr)
        raise
