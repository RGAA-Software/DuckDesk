from __future__ import annotations

import tempfile
import unittest
import zipfile
from pathlib import Path

from scripts.audit_android_retired_media import audit_archive, retired_entries


class AndroidRetiredMediaAuditTest(unittest.TestCase):
    def test_accepts_current_runtime_entries(self) -> None:
        self.assertEqual(
            retired_entries(
                [
                    "AndroidManifest.xml",
                    "lib/arm64-v8a/libpixels_android_core.so",
                    "assets/update/root.json",
                ]
            ),
            [],
        )

    def test_rejects_retired_names_and_directories_case_insensitively(self) -> None:
        entries = [
            "lib/arm64-v8a/libmk_api.so",
            "assets/ZLMediaKit/config.ini",
            "third_party/COTURN/LICENSE",
        ]
        self.assertEqual(retired_entries(entries), sorted(entries))

    def test_audits_real_zip_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            archive_path = Path(temporary_directory) / "Pixels-test.apk"
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr("lib/arm64-v8a/libpixels_android_core.so", b"pixels")
            audit_archive(archive_path)
            with zipfile.ZipFile(archive_path, "a") as archive:
                archive.writestr("assets/coturn/turnserver.conf", b"retired")
            with self.assertRaisesRegex(RuntimeError, "ZLMediaKit/Coturn"):
                audit_archive(archive_path)


if __name__ == "__main__":
    unittest.main()
