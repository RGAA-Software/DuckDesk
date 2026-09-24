from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.assemble_private_server_candidate import assemble, sha256
from scripts.tests.test_assemble_private_server_candidate import create_fake_pg_toolchain
from scripts.verify_private_server_candidate import verify


class PrivateServerVerifierTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.root = Path(self.temporary_directory.name)
        sources = self.root / "sources"
        sources.mkdir()
        for executable_name in ("px_console", "px_console_admin", "px_db", "px_relay"):
            (sources / executable_name).write_bytes(
                b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\x3e\x00" + executable_name.encode("ascii")
            )
        static_directory = sources / "static"
        static_directory.mkdir()
        (static_directory / "index.html").write_text("Pixels", encoding="utf-8")
        self.candidate = self.root / "candidate"
        assemble(
            argparse.Namespace(
                console=sources / "px_console",
                console_admin=sources / "px_console_admin",
                database_admin=sources / "px_db",
                relay=sources / "px_relay",
                console_static=static_directory,
                desk=None,
                desk_static=None,
                output=self.candidate,
            )
        )

    def add_fake_pg_toolchain(self, package_directory: Path, manifest: dict[str, object]) -> None:
        source_toolchain = self.root / "source-toolchain"
        if not source_toolchain.exists():
            create_fake_pg_toolchain(source_toolchain)
        destination_toolchain = package_directory / "postgresql" / "18"
        shutil.copytree(source_toolchain, destination_toolchain)
        for artifact_path in destination_toolchain.rglob("*"):
            if artifact_path.is_file():
                artifact_name = artifact_path.relative_to(package_directory).as_posix()
                manifest["artifacts"][artifact_name] = sha256(artifact_path)

    def test_accepts_intact_candidate(self) -> None:
        self.assertEqual(verify(self.candidate)["distribution"], "customer")

    def test_rejects_changed_binary(self) -> None:
        with (self.candidate / "bin/px_relay").open("ab") as relay_binary:
            relay_binary.write(b"modified")
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            verify(self.candidate)

    def test_rejects_unlisted_file(self) -> None:
        (self.candidate / "bin/px_auth").write_text("forbidden", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "file list"):
            verify(self.candidate)

    def test_rejects_wrong_identity(self) -> None:
        manifest_path = self.candidate / "sha256.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["distribution"] = "official"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "identity"):
            verify(self.candidate)

    def test_rejects_missing_component_version(self) -> None:
        manifest_path = self.candidate / "sha256.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        del manifest["component_versions"]["relay"]
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "versions"):
            verify(self.candidate)

    def test_accepts_formal_release_and_rejects_version_tampering(self) -> None:
        manifest_path = self.candidate / "sha256.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        backup_path = self.candidate / "bin" / "px_backup"
        backup_path.write_bytes(b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\x3e\x00backup")
        backup_unit = self.candidate / "systemd" / "pixels-private-backup@.service"
        shutil.copyfile(Path(__file__).resolve().parents[2] / "deploy/systemd/pixels-private-backup@.service", backup_unit)
        manifest["artifacts"]["bin/px_backup"] = sha256(backup_path)
        manifest["artifacts"]["systemd/pixels-private-backup@.service"] = sha256(backup_unit)
        self.add_fake_pg_toolchain(self.candidate, manifest)
        manifest["component_versions"]["backup"] = "0.1.0"
        manifest.update({
            "schema_version": 2,
            "product": "pixels-private-server",
            "suite_version": "1.0.0",
            "build_profile": "optimized-release",
        })
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        self.assertEqual(verify(self.candidate)["suite_version"], "1.0.0")
        manifest["suite_version"] = "latest"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "suite version"):
            verify(self.candidate)

    def test_candidate_cannot_claim_formal_profile(self) -> None:
        manifest_path = self.candidate / "sha256.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest["build_profile"] = "optimized-release"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "must not claim"):
            verify(self.candidate)

    def test_rejects_nested_postgresql_toolchain_tampering(self) -> None:
        manifest_path = self.candidate / "sha256.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        backup_path = self.candidate / "bin" / "px_backup"
        backup_path.write_bytes(b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\x3e\x00backup")
        backup_unit = self.candidate / "systemd" / "pixels-private-backup@.service"
        shutil.copyfile(Path(__file__).resolve().parents[2] / "deploy/systemd/pixels-private-backup@.service", backup_unit)
        manifest["artifacts"]["bin/px_backup"] = sha256(backup_path)
        manifest["artifacts"]["systemd/pixels-private-backup@.service"] = sha256(backup_unit)
        manifest["component_versions"]["backup"] = "0.1.0"
        self.add_fake_pg_toolchain(self.candidate, manifest)
        pg_dump_path = self.candidate / "postgresql" / "18" / "bin" / "pg_dump"
        pg_dump_path.write_bytes(pg_dump_path.read_bytes() + b"tampered")
        manifest["artifacts"]["postgresql/18/bin/pg_dump"] = sha256(pg_dump_path)
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaises(subprocess.CalledProcessError):
            verify(self.candidate)

    def test_formal_upgrade_rejects_downgrade_and_same_version_replacement(self) -> None:
        installed_directory = self.root / "installed"
        incoming_directory = self.root / "incoming"
        shutil.copytree(self.candidate, installed_directory)
        shutil.copytree(self.candidate, incoming_directory)

        def formalize(package_directory: Path, suite_version: str, binary_suffix: bytes) -> None:
            backup_path = package_directory / "bin" / "px_backup"
            backup_path.write_bytes(b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\x3e\x00backup")
            backup_unit = package_directory / "systemd" / "pixels-private-backup@.service"
            shutil.copyfile(Path(__file__).resolve().parents[2] / "deploy/systemd/pixels-private-backup@.service", backup_unit)
            console_path = package_directory / "bin" / "px_console"
            console_path.write_bytes(console_path.read_bytes() + binary_suffix)
            manifest_path = package_directory / "sha256.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest.update({
                "schema_version": 2,
                "product": "pixels-private-server",
                "suite_version": suite_version,
                "build_profile": "optimized-release",
            })
            manifest["component_versions"]["backup"] = "0.1.0"
            manifest["artifacts"]["bin/px_backup"] = sha256(backup_path)
            manifest["artifacts"]["systemd/pixels-private-backup@.service"] = sha256(backup_unit)
            manifest["artifacts"]["bin/px_console"] = sha256(console_path)
            self.add_fake_pg_toolchain(package_directory, manifest)
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        formalize(installed_directory, "1.0.1", b"-installed")
        formalize(incoming_directory, "1.0.1", b"-changed")
        upgrade_tool = incoming_directory / "tools" / "check_upgrade.py"

        def check_install() -> subprocess.CompletedProcess[str]:
            return subprocess.run(
                [sys.executable, str(upgrade_tool), str(installed_directory), str(incoming_directory)],
                text=True, capture_output=True, check=False,
            )

        self.assertIn("cannot change its artifacts", check_install().stderr)
        incoming_manifest_path = incoming_directory / "sha256.json"
        incoming_manifest = json.loads(incoming_manifest_path.read_text(encoding="utf-8"))
        incoming_manifest["suite_version"] = "1.0.0"
        incoming_manifest_path.write_text(json.dumps(incoming_manifest), encoding="utf-8")
        self.assertIn("downgrade is forbidden", check_install().stderr)
        incoming_manifest["suite_version"] = "1.0.2"
        incoming_manifest_path.write_text(json.dumps(incoming_manifest), encoding="utf-8")
        self.assertEqual(check_install().returncode, 0)


if __name__ == "__main__":
    unittest.main()
