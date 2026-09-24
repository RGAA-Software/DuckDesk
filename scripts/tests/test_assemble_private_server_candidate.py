from __future__ import annotations

import argparse
import json
import tempfile
import unittest
from pathlib import Path

from scripts.assemble_private_server_candidate import assemble, sha256


class PrivateServerCandidateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.root = Path(self.temporary_directory.name)
        self.sources = self.root / "sources"
        self.sources.mkdir()
        for executable_name in ("px_console", "px_console_admin", "px_db", "px_relay"):
            (self.sources / executable_name).write_bytes(b"\x7fELF" + executable_name.encode("ascii"))
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
        self.assertEqual(manifest["artifacts"]["bin/px_console"], sha256(candidate / "bin/px_console"))
        self.assertEqual(manifest["artifacts"]["static/console/index.html"], sha256(candidate / "static/console/index.html"))
        self.assertEqual(json.loads((candidate / "sha256.json").read_text(encoding="utf-8")), manifest)
        self.assertFalse((candidate / "bin/px_auth").exists())

    def test_rejects_windows_binary_and_removes_partial_candidate(self) -> None:
        (self.sources / "px_relay").write_bytes(b"MZWindows")
        with self.assertRaisesRegex(ValueError, "Linux executable"):
            assemble(self.arguments())
        self.assertFalse((self.root / "candidate").exists())

    def test_rejects_auth_signer_binary_as_an_input(self) -> None:
        auth_signer = self.sources / "px_auth"
        auth_signer.write_bytes(b"\x7fELFauth")
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


if __name__ == "__main__":
    unittest.main()
