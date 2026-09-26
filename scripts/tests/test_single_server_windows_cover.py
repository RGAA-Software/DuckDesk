"""Opt-in SCM smoke test for direct Windows Single Server overwrite installation."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import unittest
import uuid
from pathlib import Path
from unittest.mock import patch

from scripts.assemble_single_server_windows import BINARIES, assemble
from setup.make_single_server import validate_package


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
CSC = Path(r"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe")


@unittest.skipUnless(os.name == "nt" and os.getenv("PIXELS_SINGLE_SERVER_SCM_TEST") == "1",
                     "Explicit opt-in is required for Windows SCM service creation")
class SingleServerWindowsCoverTests(unittest.TestCase):
    def test_install_cover_and_uninstall(self) -> None:
        existing_services = subprocess.run([
            "powershell.exe", "-NoProfile", "-Command",
            "Get-Service -Name 'Pixels.Setup','Pixels.Console','Pixels.Relay','Pixels.Backup.*' "
            "-ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name; exit 0",
        ], capture_output=True, text=True, check=True).stdout.strip()
        if existing_services:
            self.skipTest("Existing Pixels services must not be touched by the isolated test")

        with tempfile.TemporaryDirectory(prefix="pixels-single-server-cover-") as temporary_directory:
            test_root = Path(temporary_directory)
            binary_directory = test_root / "binaries"
            static_directory = test_root / "static"
            postgresql_directory = test_root / "postgresql"
            configuration_directory = test_root / "config"
            persistent_directory = test_root / "persistent"
            installation_directory = test_root / "installed"
            for directory in (binary_directory, static_directory, postgresql_directory / "bin",
                              configuration_directory, persistent_directory):
                directory.mkdir(parents=True)

            test_service = binary_directory / "test-service.exe"
            subprocess.run([
                str(CSC), "/nologo", "/target:exe", "/reference:System.ServiceProcess.dll",
                f"/out:{test_service}",
                str(REPOSITORY_ROOT / "scripts/tests/fixtures/single_server_test_service.cs"),
            ], check=True, capture_output=True, text=True)
            for binary_name in BINARIES:
                shutil.copy2(test_service, binary_directory / binary_name)
            for tool_name in ("pg_dump.exe", "pg_restore.exe"):
                (postgresql_directory / "bin" / tool_name).write_bytes(b"MZ" + tool_name.encode())

            deployment_id = str(uuid.uuid4())
            backup_service = "Pixels.Backup." + deployment_id.replace("-", "")[:12]
            license_directory = configuration_directory / "license"
            license_directory.mkdir()
            for secret_name in ("console-tls.crt", "console-tls.key", "postgres-ca.crt",
                                "guest-source.key", "license-trust.json",
                                "workspace.key", "postgres.pgpass"):
                (configuration_directory / secret_name).write_text("fixture", encoding="utf-8")
            persistent_subdirectories = {}
            for directory_name in ("recording", "repository", "scheduler", "status"):
                persistent_subdirectories[directory_name] = persistent_directory / directory_name
                persistent_subdirectories[directory_name].mkdir()
            retained_file = persistent_directory / "retained.txt"
            retained_file.write_text("keep", encoding="utf-8")

            console_fields = {
                "PIXELS_DEPLOYMENT_ID": deployment_id,
                "PIXELS_CONSOLE_DATABASE_URL": "postgresql://fixture:fixture@localhost:5432/fixture?sslrootcert="
                + (configuration_directory / "postgres-ca.crt").as_posix(),
                "PIXELS_CONSOLE_TLS_CERT": str(configuration_directory / "console-tls.crt"),
                "PIXELS_CONSOLE_TLS_KEY": str(configuration_directory / "console-tls.key"),
                "PIXELS_CONSOLE_GUEST_SOURCE_KEY": str(configuration_directory / "guest-source.key"),
                "PIXELS_CONSOLE_LICENSE_TRUST_STORE": str(configuration_directory / "license-trust.json"),
                "PIXELS_CONSOLE_LICENSE_FILE": str(license_directory / "console.license"),
                "PIXELS_CONSOLE_WORKSPACE_KEYS": "'" + json.dumps([{
                    "id": str(uuid.uuid4()), "path": str(configuration_directory / "workspace.key"),
                }]) + "'",
                "PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY": str(persistent_subdirectories["recording"]),
            }
            (configuration_directory / "console.env").write_text(
                "".join(f"{field_name}={field_value}\n" for field_name, field_value in console_fields.items()),
                encoding="utf-8",
            )
            (configuration_directory / "relay.env").write_text(
                f"PIXELS_DEPLOYMENT_ID={deployment_id}\n"
                f"PIXELS_RELAY_CONSOLE_CA_FILE={configuration_directory / 'postgres-ca.crt'}\n",
                encoding="utf-8",
            )
            backup_configuration = {
                "schema_version": 2,
                "deployment_id": deployment_id,
                "repository_root": str(persistent_subdirectories["repository"]),
                "scheduler_root": str(persistent_subdirectories["scheduler"]),
                "status_root": str(persistent_subdirectories["status"]),
                "offsite_repository_root": None,
                "plan": {"targets": [
                    {"state": "required", "database": {
                        "service": "console", "password_file": str(configuration_directory / "postgres.pgpass"),
                    }},
                    {"state": "not_applicable", "service": "auth"},
                    {"state": "not_applicable", "service": "desk"},
                ]},
            }
            (configuration_directory / "backup.json").write_text(
                json.dumps(backup_configuration), encoding="utf-8"
            )

            try:
                for release_number in (1, 2):
                    (static_directory / "index.html").write_text(
                        f"release-{release_number}", encoding="utf-8"
                    )
                    package_directory = test_root / f"package-{release_number}"
                    with patch("scripts.assemble_single_server_windows.verify_postgresql_client",
                               return_value=["bin/pg_dump.exe", "bin/pg_restore.exe"]):
                        assemble(binary_directory, static_directory, postgresql_directory,
                                 package_directory, "1.0.3")
                    _, manifest_hash = validate_package(package_directory)
                    installation = subprocess.run([
                        "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                        str(package_directory / "install.ps1"), "-PackageRoot", str(package_directory),
                        "-ExpectedManifestSha256", manifest_hash,
                        "-ConfigRoot", str(configuration_directory),
                        "-DataRoot", str(persistent_directory),
                        "-InstallRoot", str(installation_directory),
                    ], capture_output=True, text=True)
                    self.assertEqual(installation.returncode, 0, installation.stderr + installation.stdout)
                    self.assertIn("RUNNING customer-server", installation.stdout)
                    self.assertEqual((installation_directory / "current/static/console/index.html").read_text(
                        encoding="utf-8"), f"release-{release_number}")
                    self.assertEqual(retained_file.read_text(encoding="utf-8"), "keep")
                    self.assertEqual(self._service_states(("Pixels.Console", "Pixels.Relay", backup_service)),
                                     ["Running", "Running", "Running"])

                removal = subprocess.run([
                    "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    str(installation_directory / "current/uninstall.ps1"),
                    "-InstallRoot", str(installation_directory),
                ], capture_output=True, text=True)
                self.assertEqual(removal.returncode, 0, removal.stderr + removal.stdout)
                self.assertFalse((installation_directory / "current").exists())
                self.assertEqual(retained_file.read_text(encoding="utf-8"), "keep")
                self.assertTrue((configuration_directory / "backup.json").exists())
            finally:
                for service_name in ("Pixels.Console", "Pixels.Relay", backup_service):
                    subprocess.run(["sc.exe", "stop", service_name], capture_output=True, text=True)
                    subprocess.run(["sc.exe", "delete", service_name], capture_output=True, text=True)

    @staticmethod
    def _service_states(service_names: tuple[str, ...]) -> list[str]:
        state_query = ";".join(
            f"(Get-Service -Name '{service_name}').Status.ToString()" for service_name in service_names
        )
        state_result = subprocess.run([
            "powershell.exe", "-NoProfile", "-Command", state_query,
        ], capture_output=True, text=True, check=True)
        return state_result.stdout.splitlines()


if __name__ == "__main__":
    unittest.main()
