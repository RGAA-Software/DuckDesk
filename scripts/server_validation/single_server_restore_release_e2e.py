#!/usr/bin/env python3
"""Opt-in, isolated restore test for the released Windows and Linux Single Server packages."""

from __future__ import annotations

import hashlib
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
RELEASE_VERSION = os.getenv("PIXELS_RESTORE_RELEASE_VERSION", "1.0.11")
RELEASE = ROOT / "build_official/private_server/official" / RELEASE_VERSION
WINDOWS_PACKAGE = RELEASE / "windows/package"
LINUX_BUNDLE = RELEASE / f"PixelsServer_{RELEASE_VERSION}_Linux.tar.gz"
CSC = Path(r"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe")


def run(arguments: list[str], *, input_text: str | None = None, input_bytes: bytes | None = None,
        environment: dict[str, str] | None = None) -> str:
    if input_text is not None and input_bytes is not None:
        raise ValueError("Provide one stdin representation")
    result = subprocess.run(arguments, input=input_bytes if input_bytes is not None else
                            input_text.encode("utf-8") if input_text is not None else None,
                            capture_output=True, env=environment, timeout=120)
    stdout = result.stdout.decode("utf-8", errors="replace")
    stderr = result.stderr.decode("utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(f"{arguments[0]} failed ({result.returncode}): {stdout[-800:]} {stderr[-1200:]}")
    return stdout.strip()


def postgres_sql(container: str, database: str, statement: str) -> str:
    return run(["docker", "exec", "-i", "-u", "postgres", container, "psql", "-X", "-A", "-t",
                "-v", "ON_ERROR_STOP=1", "-U", "postgres", "-d", database], input_text=statement)


def service_running(service_name: str) -> bool:
    result = subprocess.run(["sc.exe", "query", service_name], capture_output=True, text=True)
    return result.returncode == 0 and re.search(r"STATE\s+:\s+4\s+RUNNING", result.stdout) is not None


def wait_for_service(service_name: str, expected_running: bool) -> None:
    for _attempt in range(40):
        if service_running(service_name) == expected_running:
            return
        time.sleep(0.25)
    raise RuntimeError(f"Service state did not converge: {service_name}")


def manifest(deployment_id: str, recovery_set_id: str, archive: Path) -> dict[str, object]:
    return {
        "schema_version": 3, "recovery_set_id": recovery_set_id, "deployment_id": deployment_id,
        "kind": "independent", "status": "verified",
        "security_evidence": {"state": "unavailable", "reason": "independent_backup"},
        "members": [
            {"service": "console", "member": {"state": "required", "database": "pixels_console",
                                              "schema_version": 1, "archive_file": "console.dump",
                                              "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest()}},
            {"service": "auth", "member": {"state": "not_applicable"}},
            {"service": "desk", "member": {"state": "not_applicable"}},
        ],
    }


def backup_config(deployment_id: str, repository: str, status: str, hostname: str, port: int) -> dict[str, object]:
    return {
        "schema_version": 2, "deployment_id": deployment_id, "repository_root": repository,
        "status_root": status, "plan": {
            "kind": "independent", "deployment_id": deployment_id,
            "targets": [
                {"service": "console", "state": "required", "database": {
                    "database": "pixels_console", "host": hostname, "port": port}},
                {"service": "auth", "state": "not_applicable"},
                {"service": "desk", "state": "not_applicable"},
            ],
        },
    }


def check_databases(container: str, deployment_id: str, recovery_set_id: str) -> None:
    target = "pixels_console_restore_" + recovery_set_id.replace("-", "")
    live_value = postgres_sql(container, "pixels_console", "SELECT value FROM pixels.restore_probe;")
    restored_value = postgres_sql(container, target, "SELECT value FROM pixels.restore_probe;")
    restored_identity = postgres_sql(container, target, "SELECT deployment_id FROM pixels.deployment_identity;")
    if (live_value, restored_value, restored_identity) != ("after-backup", "before-backup", deployment_id):
        raise RuntimeError("Live/isolated database contents or deployment identity differ")


def test_windows(test_root: Path, container: str, deployment_id: str, archive: Path,
                 ca_certificate: Path, postgres_password: str, port: int) -> None:
    recovery_set_id = str(uuid.uuid4())
    install_root = test_root / "windows-installed"
    shutil.copytree(WINDOWS_PACKAGE, install_root / "current")
    config_root = test_root / "windows-config"
    repository = test_root / "windows-repository"
    status_root = test_root / "windows-status"
    recovery_directory = repository / recovery_set_id
    for directory in (config_root, recovery_directory, status_root):
        directory.mkdir(parents=True)
    shutil.copy2(ca_certificate, config_root / "postgresql-ca.crt")
    shutil.copy2(archive, recovery_directory / "console.dump")
    (recovery_directory / "manifest.json").write_text(
        json.dumps(manifest(deployment_id, recovery_set_id, recovery_directory / "console.dump")), encoding="utf-8")
    (config_root / "backup.json").write_text(json.dumps(backup_config(
        deployment_id, str(repository), str(status_root), "localhost", port)), encoding="utf-8")
    (status_root / "status.json").write_text(
        json.dumps({"deployment_id": deployment_id, "active_task": None}), encoding="utf-8")
    password_file = config_root / "postgres.pgpass"
    escaped_password = postgres_password.replace("\\", "\\\\").replace(":", "\\:")
    password_file.write_text(f"localhost:{port}:*:postgres:{escaped_password}\n", encoding="utf-8")
    backup_service = "Pixels.Backup." + deployment_id.replace("-", "")[:12]
    fixture = test_root / "px_backup.exe"
    run([str(CSC), "/nologo", "/target:exe", "/reference:System.ServiceProcess.dll",
         f"/out:{fixture}", str(ROOT / "scripts/tests/fixtures/single_server_test_service.cs")])
    run(["sc.exe", "create", backup_service, "binPath=", f'"{fixture}" service "{config_root / "backup.json"}"',
         "start=", "demand"])
    try:
        run(["sc.exe", "start", backup_service])
        wait_for_service(backup_service, True)
        command = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                   str(install_root / "current/restore_console.ps1"), "-InstallRoot", str(install_root),
                   "-ConfigRoot", str(config_root), "-RecoverySetId", recovery_set_id]
        if "Preflight only" not in run(command):
            raise RuntimeError("Windows packaged restore preflight did not complete")
        result = run(command + ["-Execute", "-PgUser", "postgres", "-PgPasswordFile", str(password_file)])
        if "Restored and verified" not in result:
            raise RuntimeError("Windows packaged restore did not confirm verification")
        wait_for_service(backup_service, True)
        check_databases(container, deployment_id, recovery_set_id)
        print("PASS Windows packaged restore: isolated data, untouched live DB, Backup restarted", flush=True)
    finally:
        subprocess.run(["sc.exe", "stop", backup_service], capture_output=True, text=True)
        subprocess.run(["sc.exe", "delete", backup_service], capture_output=True, text=True)


def wsl_path(path: Path) -> str:
    return run(["wsl.exe", "-d", "Ubuntu-20.04", "--exec", "wslpath", "-a", str(path)])


def test_linux(test_root: Path, container: str, deployment_id: str, archive: Path,
               ca_certificate: Path, postgres_password: str, port: int) -> None:
    recovery_set_id = str(uuid.uuid4())
    bundle_root = test_root / "linux-bundle"
    bundle_root.mkdir()
    with tarfile.open(LINUX_BUNDLE, "r:gz") as bundle:
        expected_prefix = f"PixelsServer_{RELEASE_VERSION}_Linux/"
        if any(not member.name.startswith(expected_prefix) for member in bundle.getmembers() if member.name != expected_prefix[:-1]):
            raise RuntimeError("Unexpected Linux bundle member")
        bundle.extractall(bundle_root, filter="data")
    deployment_root = bundle_root / f"PixelsServer_{RELEASE_VERSION}_Linux"
    run(["docker", "load", "--input", str(deployment_root / f"pixels-server-{RELEASE_VERSION}.tar")])
    (deployment_root / "compose.override.yaml").write_text(
        'services:\n  backup:\n    user: "0:0"\n    command: ["sleep", "infinity"]\n', encoding="utf-8")
    project_name = "pixels-restore-" + uuid.uuid4().hex[:10]
    compose = ["wsl.exe", "-d", "Ubuntu-20.04", "--exec", "env",
               f"COMPOSE_PROJECT_NAME={project_name}", "docker", "compose", "-f", wsl_path(deployment_root / "compose.yaml"),
               "-f", wsl_path(deployment_root / "compose.override.yaml")]
    config_fixture = test_root / "linux-config"
    data_fixture = test_root / "linux-data"
    recovery_directory = data_fixture / "backup/repository" / recovery_set_id
    status_directory = data_fixture / "backup/status"
    recovery_directory.mkdir(parents=True)
    status_directory.mkdir(parents=True)
    config_fixture.mkdir()
    shutil.copy2(ca_certificate, config_fixture / "postgresql-ca.crt")
    shutil.copy2(archive, recovery_directory / "console.dump")
    (recovery_directory / "manifest.json").write_text(
        json.dumps(manifest(deployment_id, recovery_set_id, recovery_directory / "console.dump")), encoding="utf-8")
    (config_fixture / "backup.json").write_text(json.dumps(backup_config(
        deployment_id, "/var/lib/pixels/backup/repository", "/var/lib/pixels/backup/status",
        "host.docker.internal", port)), encoding="utf-8")
    (status_directory / "status.json").write_text(
        json.dumps({"deployment_id": deployment_id, "active_task": None}), encoding="utf-8")
    try:
        run(compose + ["up", "-d", "--no-deps", "backup"])
        backup_container = run(compose + ["ps", "-q", "backup"])
        if not backup_container:
            raise RuntimeError("Isolated backup container did not start")
        run(["docker", "cp", str(config_fixture) + "/.", backup_container + ":/etc/pixels/"])
        run(["docker", "cp", str(data_fixture) + "/.", backup_container + ":/var/lib/pixels/"])
        script = wsl_path(deployment_root / "restore_console.sh")
        result = run(["wsl.exe", "-d", "Ubuntu-20.04", "--exec", "env",
                      f"COMPOSE_PROJECT_NAME={project_name}", "bash", script, recovery_set_id],
                     input_bytes=f"RESTORE\npostgres\n{postgres_password}\n".encode("utf-8"))
        if "Restored and verified" not in result:
            raise RuntimeError(f"Linux packaged restore did not confirm verification: {result[-1600:]}")
        if backup_container not in run(compose + ["ps", "--status", "running", "-q", "backup"]):
            raise RuntimeError("Linux Backup was not restarted")
        check_databases(container, deployment_id, recovery_set_id)
        print("PASS Linux packaged restore: isolated data, untouched live DB, Backup restarted", flush=True)
    finally:
        subprocess.run(compose + ["down", "--volumes"], capture_output=True, text=True)


def main() -> None:
    if os.name != "nt" or os.getenv("PIXELS_RESTORE_RELEASE_E2E") != "1":
        raise SystemExit("Run elevated on Windows with PIXELS_RESTORE_RELEASE_E2E=1")
    from setup.make_single_server import validate_package
    validate_package(WINDOWS_PACKAGE)
    existing_services = run(["powershell.exe", "-NoProfile", "-Command",
                             "Get-Service -Name 'Pixels.Setup','Pixels.Console','Pixels.Relay','Pixels.Backup.*' "
                             "-ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name; exit 0"])
    if existing_services:
        raise RuntimeError("Existing Pixels services are present; refusing to create a test service")
    deployment_id = str(uuid.uuid4())
    postgres_container = "pixels-restore-pg-" + deployment_id[:8]
    postgres_password = secrets.token_hex(20) + ":\\safe"
    cache_root = ROOT / ".cache"
    cache_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="single-restore-e2e-", dir=cache_root) as temporary_directory:
        test_root = Path(temporary_directory)
        postgres_started = False
        try:
            run(["docker", "run", "--rm", "--detach", "--name", postgres_container,
                 "--label", "pixels.validation=single-server-restore", "-p", "127.0.0.1::5432",
                 "-e", f"POSTGRES_PASSWORD={postgres_password}", "postgres:18.6"])
            postgres_started = True
            for _attempt in range(40):
                if subprocess.run(["docker", "exec", postgres_container, "pg_isready", "-U", "postgres"],
                                  capture_output=True).returncode == 0:
                    break
                time.sleep(0.25)
            else:
                raise RuntimeError("Temporary PostgreSQL did not become ready")
            run(["docker", "exec", "-u", "postgres", postgres_container, "openssl", "req", "-x509",
                 "-newkey", "rsa:2048", "-nodes", "-days", "1", "-sha256", "-subj", "/CN=restore-test-ca",
                 "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign,cRLSign",
                 "-keyout", "/var/lib/postgresql/ca.key", "-out", "/var/lib/postgresql/ca.crt"])
            run(["docker", "exec", "-u", "postgres", postgres_container, "openssl", "req", "-newkey",
                 "rsa:2048", "-nodes", "-sha256", "-subj", "/CN=localhost",
                 "-addext", "subjectAltName=DNS:localhost,DNS:host.docker.internal",
                 "-keyout", "/var/lib/postgresql/server.key", "-out", "/var/lib/postgresql/server.csr"])
            run(["docker", "exec", "-u", "postgres", postgres_container, "openssl", "x509", "-req",
                 "-in", "/var/lib/postgresql/server.csr", "-CA", "/var/lib/postgresql/ca.crt",
                 "-CAkey", "/var/lib/postgresql/ca.key", "-CAcreateserial", "-copy_extensions", "copy",
                 "-days", "1", "-sha256", "-out", "/var/lib/postgresql/server.crt"])
            postgres_sql(postgres_container, "postgres", "ALTER SYSTEM SET ssl='on';")
            postgres_sql(postgres_container, "postgres", "ALTER SYSTEM SET ssl_cert_file='/var/lib/postgresql/server.crt';")
            postgres_sql(postgres_container, "postgres", "ALTER SYSTEM SET ssl_key_file='/var/lib/postgresql/server.key';")
            run(["docker", "restart", postgres_container])
            port = int(run(["docker", "port", postgres_container, "5432/tcp"]).split(":")[-1])
            ca_certificate = test_root / "postgresql-ca.crt"
            run(["docker", "cp", postgres_container + ":/var/lib/postgresql/ca.crt", str(ca_certificate)])
            postgres_sql(postgres_container, "postgres", """
CREATE ROLE pixels_console_owner LOGIN;
CREATE ROLE pixels_console_runtime LOGIN;
CREATE DATABASE pixels_console OWNER pixels_console_owner;
REVOKE ALL ON DATABASE pixels_console FROM PUBLIC;
GRANT CONNECT ON DATABASE pixels_console TO pixels_console_runtime;
""")
            postgres_sql(postgres_container, "pixels_console", f"""
SET ROLE pixels_console_owner;
CREATE SCHEMA pixels;
CREATE TABLE pixels.deployment_identity (service text, deployment_id uuid);
CREATE TABLE pixels._sqlx_migrations (version bigint, success boolean);
CREATE TABLE pixels.restore_probe (value text);
INSERT INTO pixels.deployment_identity VALUES ('console', '{deployment_id}');
INSERT INTO pixels._sqlx_migrations VALUES (1, true);
INSERT INTO pixels.restore_probe VALUES ('before-backup');
""")
            run(["docker", "exec", "-u", "postgres", postgres_container, "pg_dump", "-Fc", "-U", "postgres",
                 "-d", "pixels_console", "-f", "/tmp/console.dump"])
            archive = test_root / "console.dump"
            run(["docker", "cp", postgres_container + ":/tmp/console.dump", str(archive)])
            postgres_sql(postgres_container, "pixels_console",
                         "UPDATE pixels.restore_probe SET value='after-backup';")
            test_windows(test_root, postgres_container, deployment_id, archive, ca_certificate, postgres_password, port)
            test_linux(test_root, postgres_container, deployment_id, archive, ca_certificate, postgres_password, port)
        finally:
            if postgres_started:
                subprocess.run(["docker", "stop", postgres_container], capture_output=True, text=True)


if __name__ == "__main__":
    main()
