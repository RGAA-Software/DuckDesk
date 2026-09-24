from __future__ import annotations

import argparse
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from scripts.assemble_private_server_candidate import assemble, sha256
from scripts.server_private.verify_pg_toolchain import REQUIRED_ARTIFACTS, SOURCE_SHA256


def create_fake_pg_toolchain(toolchain_directory: Path) -> Path:
    toolchain_directory.mkdir()
    artifact_hashes = {}
    for artifact_name in sorted(REQUIRED_ARTIFACTS):
        artifact_path = toolchain_directory / artifact_name
        artifact_path.parent.mkdir(parents=True, exist_ok=True)
        artifact_content = (b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\x3e\x00" + artifact_name.encode("ascii")
                            if artifact_name.startswith("bin/") else artifact_name.encode("ascii"))
        artifact_path.write_bytes(artifact_content)
        if artifact_name.startswith("bin/"):
            artifact_path.chmod(0o755)
        artifact_hashes[artifact_name] = hashlib.sha256(artifact_content).hexdigest()
    manifest = {
        "schema_version": 1,
        "product": "pixels-postgresql-client-toolchain",
        "postgresql_version": "18.6",
        "platform": "linux-x86_64-glibc-2.31",
        "source_url": "https://ftp.postgresql.org/pub/source/v18.6/postgresql-18.6.tar.bz2",
        "download_url": "https://mirrors.aliyun.com/postgresql/source/v18.6/postgresql-18.6.tar.bz2",
        "source_sha256": SOURCE_SHA256,
        "artifacts": artifact_hashes,
    }
    (toolchain_directory / "sha256.json").write_text(json.dumps(manifest), encoding="utf-8")
    return toolchain_directory


class PrivateServerCandidateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.root = Path(self.temporary_directory.name)
        self.sources = self.root / "sources"
        self.sources.mkdir()
        for executable_name in ("px_console", "px_console_admin", "px_db", "px_relay"):
            (self.sources / executable_name).write_bytes(
                b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\x3e\x00" + executable_name.encode("ascii")
            )
        self.console_static = self.sources / "console-static"
        self.console_static.mkdir()
        (self.console_static / "index.html").write_text("Pixels", encoding="utf-8")

    def arguments(self) -> argparse.Namespace:
        return argparse.Namespace(
            console=self.sources / "px_console",
            console_admin=self.sources / "px_console_admin",
            database_admin=self.sources / "px_db",
            relay=self.sources / "px_relay",
            console_static=self.console_static,
            desk=None,
            desk_static=None,
            output=self.root / "candidate",
        )

    def test_assembles_hash_checked_customer_files_without_auth(self) -> None:
        manifest = assemble(self.arguments())
        candidate = self.root / "candidate"
        self.assertFalse(manifest["contains_auth_signer"])
        self.assertEqual(manifest["distribution"], "customer")
        self.assertEqual(manifest["platform"], "linux-x86_64")
        self.assertEqual(manifest["component_versions"]["relay"], "3.2.1")
        self.assertEqual(manifest["artifacts"]["bin/px_console"], sha256(candidate / "bin/px_console"))
        self.assertEqual(manifest["artifacts"]["static/console/index.html"], sha256(candidate / "static/console/index.html"))
        self.assertEqual(json.loads((candidate / "sha256.json").read_text(encoding="utf-8")), manifest)
        self.assertFalse((candidate / "bin/px_auth").exists())

    def test_rejects_windows_binary_and_removes_partial_candidate(self) -> None:
        (self.sources / "px_relay").write_bytes(b"MZWindows")
        with self.assertRaisesRegex(ValueError, "Linux x86-64 executable"):
            assemble(self.arguments())
        self.assertFalse((self.root / "candidate").exists())

    def test_optional_desk_has_its_own_version_and_static_assets(self) -> None:
        desk_executable = self.sources / "px_desk"
        desk_executable.write_bytes(b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\x3e\x00desk")
        desk_static = self.sources / "desk-static"
        desk_static.mkdir()
        (desk_static / "index.html").write_text("Pixels Desk", encoding="utf-8")
        arguments = self.arguments()
        arguments.desk = desk_executable
        arguments.desk_static = desk_static
        manifest = assemble(arguments)
        self.assertEqual(manifest["component_versions"]["desk"], "3.2.9")
        self.assertIn("bin/px_desk", manifest["artifacts"])
        self.assertIn("static/desk/index.html", manifest["artifacts"])

    def test_rejects_wrong_linux_architecture(self) -> None:
        (self.sources / "px_relay").write_bytes(b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\xb7\x00")
        with self.assertRaisesRegex(ValueError, "Linux x86-64 executable"):
            assemble(self.arguments())
        self.assertFalse((self.root / "candidate").exists())

    def test_rejects_auth_signer_binary_as_an_input(self) -> None:
        auth_signer = self.sources / "px_auth"
        auth_signer.write_bytes(b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\x3e\x00auth")
        arguments = self.arguments()
        arguments.console = auth_signer
        with self.assertRaisesRegex(ValueError, "forbids"):
            assemble(arguments)
        self.assertFalse(arguments.output.exists())

    def test_rejects_input_nested_under_output(self) -> None:
        arguments = self.arguments()
        arguments.output = self.console_static / "candidate"
        with self.assertRaisesRegex(ValueError, "must be separate"):
            assemble(arguments)
        self.assertFalse(arguments.output.exists())

    def test_rejects_existing_output_without_changing_it(self) -> None:
        candidate = self.root / "candidate"
        candidate.mkdir()
        marker = candidate / "operator-file"
        marker.write_text("keep", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "already exists"):
            assemble(self.arguments())
        self.assertEqual(marker.read_text(encoding="utf-8"), "keep")

    def test_formal_release_has_independent_suite_version_and_locked_components(self) -> None:
        arguments = self.arguments()
        arguments.suite_version = "1.0.0"
        arguments.backup = self.sources / "px_backup"
        arguments.backup.write_bytes(b"\x7fELF\x02\x01" + b"\x00" * 12 + b"\x3e\x00backup")
        arguments.pg_toolchain = create_fake_pg_toolchain(self.root / "toolchain")
        manifest = assemble(arguments)
        self.assertEqual(manifest["schema_version"], 2)
        self.assertEqual(manifest["product"], "pixels-private-server")
        self.assertEqual(manifest["suite_version"], "1.0.0")
        self.assertEqual(manifest["build_profile"], "optimized-release")
        self.assertEqual(manifest["component_versions"]["console"], "3.2.21")
        self.assertEqual(manifest["component_versions"]["backup"], "0.1.0")
        self.assertIn("postgresql/18/bin/pg_dump", manifest["artifacts"])

    def test_rejects_invalid_formal_suite_version_before_creating_output(self) -> None:
        arguments = self.arguments()
        arguments.suite_version = "1.0.0-preview"
        with self.assertRaisesRegex(ValueError, "suite version"):
            assemble(arguments)
        self.assertFalse(arguments.output.exists())


if __name__ == "__main__":
    unittest.main()
