from __future__ import annotations

import hashlib
import json
import tarfile
import tempfile
import unittest
from pathlib import Path

from scripts.assemble_single_server_linux_bundle import assemble


class SingleServerLinuxBundleTests(unittest.TestCase):
    def test_offline_bundle_contains_only_three_service_compose(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            image_archive = root / "image.tar"
            image_archive.write_bytes(b"verified-image-archive")
            image_hash = hashlib.sha256(image_archive.read_bytes()).hexdigest()
            output = root / "PixelsServer_1.0.3_Linux.tar.gz"
            assemble(image_archive, image_hash, "1.0.3", output)
            with tarfile.open(output, "r:gz") as bundle:
                prefix = "PixelsServer_1.0.3_Linux/"
                manifest_file = bundle.extractfile(prefix + "sha256.json")
                compose_file = bundle.extractfile(prefix + "compose.yaml")
                self.assertIsNotNone(manifest_file)
                self.assertIsNotNone(compose_file)
                manifest = json.load(manifest_file)
                compose = compose_file.read().decode("utf-8")
            self.assertEqual(manifest["files"]["pixels-server-1.0.3.tar"], image_hash)
            self.assertEqual(manifest["distribution"], "customer")
            for service in ("setup:", "console:", "relay:", "backup:"):
                self.assertIn(service, compose)
            self.assertIn("pixels-server:1.0.3", compose)
            self.assertIn("127.0.0.1:4700:4700", compose)
            self.assertNotIn("env_file:", compose)
            self.assertNotIn("PIXELS_CONFIG_DIR", compose)
            self.assertNotIn("settings.env.example", manifest["files"])
            for forbidden_service in ("postgres:", "redis:", "desk:", "auth:"):
                self.assertNotIn(forbidden_service, compose)
            self.assertTrue(output.with_suffix(".gz.sha256").is_file())


if __name__ == "__main__":
    unittest.main()
