from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts.assemble_single_server_windows import BINARIES, assemble
from setup.make_single_server import validate_package


class SingleServerWindowsPackageTests(unittest.TestCase):
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
            for tool_name in ("pg_dump.exe", "pg_restore.exe"):
                (postgresql / "bin" / tool_name).write_bytes(b"MZ" + tool_name.encode())
            output = root / "package"
            with patch("scripts.assemble_single_server_windows.verify_postgresql_client",
                       return_value=["bin/pg_dump.exe", "bin/pg_restore.exe"]):
                assemble(binaries, static, postgresql, output, "1.0.3")
            version, manifest_hash = validate_package(output)
            self.assertEqual(version, "1.0.3")
            self.assertEqual(len(manifest_hash), 64)
            manifest = json.loads((output / "sha256.json").read_text(encoding="utf-8"))
            self.assertIn("stage_setup.ps1", manifest["files"])
            self.assertIn("assets/license-trust.json", manifest["files"])
            self.assertNotIn("bin/px_desk.exe", manifest["files"])
            self.assertNotIn("bin/px_auth.exe", manifest["files"])
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
            for tool_name in ("pg_dump.exe", "pg_restore.exe"):
                (postgresql / "bin" / tool_name).write_bytes(b"MZ" + tool_name.encode())
            package = root / "package"
            with patch("scripts.assemble_single_server_windows.verify_postgresql_client",
                       return_value=["bin/pg_dump.exe", "bin/pg_restore.exe"]):
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
            for tool_name in ("pg_dump.exe", "pg_restore.exe"):
                (postgresql / "bin" / tool_name).write_bytes(b"MZ" + tool_name.encode())
            package = root / "package"
            with patch("scripts.assemble_single_server_windows.verify_postgresql_client",
                       return_value=["bin/pg_dump.exe", "bin/pg_restore.exe"]):
                assemble(binaries, static, postgresql, package, "1.0.3")
            deployment_id = "11111111-1111-4111-8111-111111111111"
            for environment_name in ("console.env", "relay.env"):
                (config / environment_name).write_text(f"PIXELS_DEPLOYMENT_ID={deployment_id}\n", encoding="utf-8")
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
                "product": "pixels-single-server", "distribution": "customer",
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
