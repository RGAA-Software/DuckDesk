from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from scripts.server_private.server_suite_version import read_next_version, reserve_version


class ServerSuiteVersionTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(temporary_directory.cleanup)
        self.version_file = Path(temporary_directory.name) / "server_suite_version.json"
        self.version_file.write_text('{"next_version":"1.2.9"}\n', encoding="utf-8")

    def test_reservation_advances_once_and_never_reuses_version(self) -> None:
        self.assertEqual(reserve_version(self.version_file, "1.2.9"), "1.2.9")
        self.assertEqual(read_next_version(self.version_file), "1.2.10")
        with self.assertRaisesRegex(ValueError, "changed after preflight"):
            reserve_version(self.version_file, "1.2.9")
        self.assertEqual(read_next_version(self.version_file), "1.2.10")

    def test_invalid_state_cannot_be_reserved(self) -> None:
        self.version_file.write_text('{"next_version":"1.2.9-preview"}\n', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Invalid next"):
            reserve_version(self.version_file, "1.2.9-preview")


if __name__ == "__main__":
    unittest.main()
