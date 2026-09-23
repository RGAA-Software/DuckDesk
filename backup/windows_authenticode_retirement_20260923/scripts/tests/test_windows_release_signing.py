from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts.windows_release_signing import load_configuration, normalized_hex, signing_arguments


class WindowsReleaseSigningConfigurationTest(unittest.TestCase):
    def signing_environment(self, signtool: Path) -> dict[str, str]:
        return {
            "PIXELS_WINDOWS_SIGNTOOL": str(signtool),
            "PIXELS_WINDOWS_SIGNING_CERT_SHA1": "01:" * 19 + "01",
            "PIXELS_WINDOWS_SIGNING_CERT_SHA256": "AB " * 31 + "AB",
            "PIXELS_WINDOWS_SIGNING_STORE": "local_machine",
            "PIXELS_WINDOWS_TIMESTAMP_URL": "https://timestamp.example.test/rfc3161",
        }

    def test_load_configuration_normalizes_pins_and_uses_https_timestamp(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            signtool = Path(temporary_directory) / "signtool.exe"
            signtool.write_bytes(b"test tool")
            with mock.patch.dict(os.environ, self.signing_environment(signtool), clear=False):
                configuration = load_configuration()

            self.assertEqual(configuration.certificate_sha1, "01" * 20)
            self.assertEqual(configuration.certificate_sha256, "AB" * 32)
            self.assertTrue(configuration.machine_store)
            command = signing_arguments(configuration, Path("release.exe"))
            self.assertIn("/sm", command)
            self.assertEqual(command[-1], "release.exe")

    def test_configuration_rejects_insecure_timestamp_or_ambiguous_store(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            signtool = Path(temporary_directory) / "signtool.exe"
            signtool.write_bytes(b"test tool")
            environment = self.signing_environment(signtool)
            environment["PIXELS_WINDOWS_TIMESTAMP_URL"] = "http://timestamp.example.test"
            with mock.patch.dict(os.environ, environment, clear=False):
                with self.assertRaisesRegex(RuntimeError, "HTTPS"):
                    load_configuration()

            environment["PIXELS_WINDOWS_TIMESTAMP_URL"] = "https://timestamp.example.test"
            environment["PIXELS_WINDOWS_SIGNING_STORE"] = "automatic"
            with mock.patch.dict(os.environ, environment, clear=False):
                with self.assertRaisesRegex(RuntimeError, "current_user or local_machine"):
                    load_configuration()

    def test_normalized_hex_removes_only_formatting_separators(self) -> None:
        self.assertEqual(normalized_hex("aa:BB 01"), "AABB01")


if __name__ == "__main__":
    unittest.main()
