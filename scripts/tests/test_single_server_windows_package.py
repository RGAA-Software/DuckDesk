from __future__ import annotations

import json
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import uuid
from pathlib import Path
from unittest.mock import patch

from scripts.assemble_single_server_windows import BINARIES, assemble
from setup.make_single_server import validate_package


class SingleServerWindowsPackageTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows recovery entry uses PowerShell")
    def test_console_recovery_preflight_verifies_archive_without_creating_database(self) -> None:
        power_shells = [shell for name in ("powershell", "pwsh") if (shell := shutil.which(name))]
        if not power_shells:
            self.skipTest("PowerShell is unavailable")
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            config_root = root / "config"
            install_root = root / "installed"
            recovery_set_id = str(uuid.uuid4())
            deployment_id = str(uuid.uuid4())
            repository = root / "repository"
            recovery_set = repository / recovery_set_id
            recovery_set.mkdir(parents=True)
            config_root.mkdir()
            tool_directory = install_root / "current" / "postgresql" / "bin"
            tool_directory.mkdir(parents=True)
            archive = recovery_set / "console.dump"
            archive.write_bytes(b"console-only-test-archive")
            archive_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
            (config_root / "postgresql-ca.crt").write_text("test-ca", encoding="ascii")
            backup_config = {
                "schema_version": 2, "deployment_id": deployment_id,
                "repository_root": str(repository), "plan": {
                    "kind": "independent", "deployment_id": deployment_id,
                    "targets": [
                        {"service": "console", "state": "required", "database": {
                            "database": "pixels_console", "host": "localhost", "port": 5432,
                        }},
                        {"service": "auth", "state": "not_applicable"},
                        {"service": "desk", "state": "not_applicable"},
                    ],
                },
            }
            (config_root / "backup.json").write_text(json.dumps(backup_config), encoding="utf-8")
            manifest = {
                "schema_version": 3, "recovery_set_id": recovery_set_id, "deployment_id": deployment_id,
                "kind": "independent", "status": "verified",
                "security_evidence": {"state": "unavailable", "reason": "independent_backup"},
                "members": [
                    {"service": "console", "member": {"state": "required", "database": "pixels_console",
                                                      "schema_version": 42, "archive_file": "console.dump",
                                                      "archive_sha256": archive_hash}},
                    {"service": "auth", "member": {"state": "not_applicable"}},
                    {"service": "desk", "member": {"state": "not_applicable"}},
                ],
            }
            (recovery_set / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            package_files = {}
            for tool_name in ("createdb.exe", "pg_restore.exe", "psql.exe"):
                tool = tool_directory / tool_name
                tool.write_bytes(b"MZ" + tool_name.encode("ascii"))
                package_files[f"postgresql/bin/{tool_name}"] = hashlib.sha256(tool.read_bytes()).hexdigest()
            (install_root / "current" / "sha256.json").write_text(
                json.dumps({"files": package_files}), encoding="utf-8",
            )
            script = Path(__file__).resolve().parents[2] / "deploy/single_server/windows/restore_console.ps1"
            commands = [[power_shell, "-NoProfile", "-File", str(script), "-ConfigRoot", str(config_root),
                         "-InstallRoot", str(install_root), "-RecoverySetId", recovery_set_id]
                        for power_shell in power_shells]
            for command in commands:
                accepted = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
                self.assertEqual(accepted.returncode, 0, accepted.stderr)
                self.assertIn("Preflight only", accepted.stdout)
            self.assertFalse((root / f"pixels_console_restore_{recovery_set_id.replace('-', '')}").exists())
            archive.write_bytes(b"tampered")
            for command in commands:
                rejected = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
                self.assertNotEqual(rejected.returncode, 0)
                self.assertIn("SHA-256 differs", rejected.stderr)

    def test_three_services_and_tools_without_desk_or_auth(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            binaries = root / "binaries"
            static = root / "static"
            postgresql = root / "postgresql"
            for directory in (binaries, static, postgresql):
                directory.mkdir()
            for binary_name in BINARIES:
                (binaries / binary_name).write_bytes(b"MZ" + binary_name.encode())
            (static / "index.html").write_text("<html>Console</html>", encoding="utf-8")
            (postgresql / "bin").mkdir()
            for tool_name in ("pg_dump.exe", "pg_restore.exe", "createdb.exe", "psql.exe"):
                (postgresql / "bin" / tool_name).write_bytes(b"MZ" + tool_name.encode())
            output = root / "package"
            with patch("scripts.assemble_single_server_windows.verify_postgresql_client",
                       return_value=["bin/pg_dump.exe", "bin/pg_restore.exe", "bin/createdb.exe", "bin/psql.exe"]):
                assemble(binaries, static, postgresql, output, "1.0.3")
            version, manifest_hash = validate_package(output)
            self.assertEqual(version, "1.0.3")
            self.assertEqual(len(manifest_hash), 64)
            manifest = json.loads((output / "sha256.json").read_text(encoding="utf-8"))
            self.assertIn("stage_setup.ps1", manifest["files"])
            self.assertIn("restore_console.ps1", manifest["files"])
            self.assertIn("restore_console.md", manifest["files"])
            self.assertIn("assets/license-trust.json", manifest["files"])
            self.assertNotIn("bin/px_desk.exe", manifest["files"])
            self.assertNotIn("bin/px_auth.exe", manifest["files"])
            retired_manifest = dict(manifest)
            retired_manifest["distribution"] = "customer"
            (output / "sha256.json").write_text(json.dumps(retired_manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "identity"):
                validate_package(output)
            (output / "sha256.json").write_text(json.dumps(manifest), encoding="utf-8")
            (output / "bin" / "px_relay.exe").write_bytes(b"tampered")
            with self.assertRaises(ValueError):
                validate_package(output)

    def test_nsis_installer_compiles_from_verified_package(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            binaries = root / "binaries"
            static = root / "static"
            postgresql = root / "postgresql"
            for directory in (binaries, static, postgresql):
                directory.mkdir()
            for binary_name in BINARIES:
                (binaries / binary_name).write_bytes(b"MZ" + binary_name.encode())
            (static / "index.html").write_text("<html>Console</html>", encoding="utf-8")
            (postgresql / "bin").mkdir()
            for tool_name in ("pg_dump.exe", "pg_restore.exe", "createdb.exe", "psql.exe"):
                (postgresql / "bin" / tool_name).write_bytes(b"MZ" + tool_name.encode())
            package = root / "package"
            with patch("scripts.assemble_single_server_windows.verify_postgresql_client",
                       return_value=["bin/pg_dump.exe", "bin/pg_restore.exe", "bin/createdb.exe", "bin/psql.exe"]):
                assemble(binaries, static, postgresql, package, "1.0.3")
            setup = root / "PixelsServer_1.0.3_Setup.exe"
            repository_root = Path(__file__).resolve().parents[2]
            result = subprocess.run([
                sys.executable, str(repository_root / "setup" / "make_single_server.py"),
                "--package", str(package), "--output", str(setup),
            ], check=True, capture_output=True, text=True)
            self.assertNotIn("warning", result.stdout.lower())
            self.assertTrue(setup.is_file())
            self.assertTrue(setup.with_suffix(".exe.sha256").is_file())

    @unittest.skipUnless(os.name == "nt", "Windows preflight uses the Windows service manager")
    def test_install_preflight_does_not_change_services(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            binaries = root / "binaries"
            static = root / "static"
            postgresql = root / "postgresql"
            config = root / "config"
            persistent_data = root / "persistent-data"
            for directory in (binaries, static, postgresql / "bin", config, persistent_data):
                directory.mkdir(parents=True)
            for binary_name in BINARIES:
                (binaries / binary_name).write_bytes(b"MZ" + binary_name.encode())
            (static / "index.html").write_text("<html>Console</html>", encoding="utf-8")
            for tool_name in ("pg_dump.exe", "pg_restore.exe", "createdb.exe", "psql.exe"):
                (postgresql / "bin" / tool_name).write_bytes(b"MZ" + tool_name.encode())
            package = root / "package"
            with patch("scripts.assemble_single_server_windows.verify_postgresql_client",
                       return_value=["bin/pg_dump.exe", "bin/pg_restore.exe", "bin/createdb.exe", "bin/psql.exe"]):
                assemble(binaries, static, postgresql, package, "1.0.3")
            deployment_id = "11111111-1111-4111-8111-111111111111"
            (config / "console.env").write_text(
                f"PIXELS_DEPLOYMENT_ID={deployment_id}\nPIXELS_CONSOLE_DISTRIBUTION=official\n"
                "PIXELS_CONSOLE_RELEASE_NAMESPACE=pixels.official\n", encoding="utf-8",
            )
            (config / "relay.env").write_text(f"PIXELS_DEPLOYMENT_ID={deployment_id}\n", encoding="utf-8")
            (config / "backup.json").write_text(json.dumps({
                "schema_version": 2, "deployment_id": deployment_id,
                "plan": {"targets": [
                    {"state": "required", "database": {"service": "console"}},
                    {"state": "not_applicable", "service": "auth"},
                    {"state": "not_applicable", "service": "desk"},
                ]},
            }), encoding="utf-8")
            _, manifest_hash = validate_package(package)
            result = subprocess.run([
                "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(package / "install.ps1"),
                "-PackageRoot", str(package), "-ExpectedManifestSha256", manifest_hash,
                "-ConfigRoot", str(config), "-DataRoot", str(persistent_data),
                "-InstallRoot", str(root / "install"), "-PreflightOnly",
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("PREFLIGHT_OK", result.stdout)
            self.assertFalse((root / "install").exists())
            official_environment = (config / "console.env").read_text(encoding="utf-8")
            (config / "console.env").write_text(
                official_environment.replace("official", "customer"), encoding="utf-8",
            )
            retired_distribution = subprocess.run([
                "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(package / "install.ps1"),
                "-PackageRoot", str(package), "-ExpectedManifestSha256", manifest_hash,
                "-ConfigRoot", str(config), "-DataRoot", str(persistent_data),
                "-InstallRoot", str(root / "install"), "-PreflightOnly",
            ], capture_output=True, text=True)
            self.assertNotEqual(retired_distribution.returncode, 0)
            self.assertIn("Official release identity", retired_distribution.stderr)
            (config / "console.env").write_text(official_environment, encoding="utf-8")
            installation = root / "install"
            installation.mkdir()
            (installation / "deployment.id").write_text(deployment_id + "\n", encoding="utf-8")
            repeat_preflight = subprocess.run([
                "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(package / "install.ps1"),
                "-PackageRoot", str(package), "-ExpectedManifestSha256", manifest_hash,
                "-ConfigRoot", str(config), "-DataRoot", str(persistent_data),
                "-InstallRoot", str(installation), "-PreflightOnly",
            ], capture_output=True, text=True)
            self.assertEqual(repeat_preflight.returncode, 0, repeat_preflight.stderr)
            self.assertIn("PREFLIGHT_OK", repeat_preflight.stdout)
            (installation / "deployment.id").write_text(
                "22222222-2222-4222-8222-222222222222\n", encoding="utf-8"
            )
            mismatch = subprocess.run([
                "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(package / "install.ps1"),
                "-PackageRoot", str(package), "-ExpectedManifestSha256", manifest_hash,
                "-ConfigRoot", str(config), "-DataRoot", str(persistent_data),
                "-InstallRoot", str(installation), "-PreflightOnly",
            ], capture_output=True, text=True)
            self.assertNotEqual(mismatch.returncode, 0)
            self.assertIn("Another Pixels Server deployment", mismatch.stderr)

    @unittest.skipUnless(os.name == "nt", "Windows uninstall uses the Windows service manager")
    def test_uninstall_preserves_data_without_private_configuration(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            installation = root / "installation"
            current_release = installation / "current"
            persistent_data = root / "persistent-data"
            current_release.mkdir(parents=True)
            persistent_data.mkdir()
            (persistent_data / "recovery-point.txt").write_text("retained", encoding="utf-8")
            (installation / "deployment.id").write_text(
                "11111111-1111-4111-8111-111111111111\n", encoding="utf-8"
            )
            old_release = installation / ("previous-" + "a" * 32)
            old_release.mkdir()
            (old_release / "px_console.exe").write_bytes(b"MZold")
            (current_release / "sha256.json").write_text(json.dumps({
                "product": "pixels-single-server", "distribution": "official",
                "platform": "windows-x86_64",
            }), encoding="utf-8")
            repository_root = Path(__file__).resolve().parents[2]
            uninstall_script = current_release / "uninstall.ps1"
            shutil.copy2(repository_root / "deploy" / "single_server" / "windows" / "uninstall.ps1",
                         uninstall_script)
            result = subprocess.run([
                "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(uninstall_script),
                "-InstallRoot", str(installation),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(current_release.exists())
            self.assertFalse(old_release.exists())
            self.assertFalse((installation / "deployment.id").exists())
            self.assertEqual((persistent_data / "recovery-point.txt").read_text(encoding="utf-8"), "retained")


if __name__ == "__main__":
    unittest.main()
