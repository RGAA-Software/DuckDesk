import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from prepare_windows_update_release import build_release_spec, write_new_json


SIGNER_PIN = "A" * 64


class WindowsUpdateReleaseTests(unittest.TestCase):
    def create_release(self, root: Path) -> Path:
        release_directory = root / "release"
        release_directory.mkdir()
        installer_name = "PixelsCloudNode_official_3.3.80_Setup.exe"
        installer_path = release_directory / installer_name
        installer_path.write_bytes(b"signed installer fixture")
        import hashlib

        installer_sha256 = hashlib.sha256(installer_path.read_bytes()).hexdigest().upper()
        (release_directory / "installer-manifest.json").write_text(
            json.dumps(
                {
                    "schema_version": 2,
                    "product": "cloud_node",
                    "distribution": "official",
                    "company": "Pixels",
                    "product_version": "3.3.80",
                    "product_version_code": 30380,
                    "git_revision": "a" * 40,
                    "signer_certificate_sha256": SIGNER_PIN,
                    "payload_manifest_sha256": "B" * 64,
                    "payload_artifact_count": 20,
                    "installer": {"path": installer_name, "sha256": installer_sha256},
                }
            ),
            encoding="utf-8",
        )
        return release_directory

    def test_verified_manifest_becomes_an_immutable_release_spec(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            release_directory = self.create_release(Path(temporary_directory))
            verified_signatures: list[tuple[str, str]] = []

            def signature_verifier(installer_path: Path, signer_pin: str) -> None:
                verified_signatures.append((installer_path.name, signer_pin))

            release_spec, verified_release = build_release_spec(
                release_directory,
                SIGNER_PIN,
                "https://updates.example.test/metadata/",
                "https://updates.example.test/targets/",
                "stable",
                signature_verifier,
            )
            self.assertEqual(verified_release.product_version_code, 30380)
            self.assertEqual(verified_signatures, [("PixelsCloudNode_official_3.3.80_Setup.exe", SIGNER_PIN)])
            self.assertEqual(release_spec["platform_signer_sha256"], SIGNER_PIN.lower())
            self.assertEqual(release_spec["build_number"], 30380)
            self.assertEqual(release_spec["target"]["release_namespace"], "pixels.official")
            self.assertIsNone(release_spec["target"]["oem_id"])
            self.assertEqual(
                release_spec["target_name"],
                "windows/cloud_node/official/stable/x86_64/30380/PixelsCloudNode_official_3.3.80_Setup.exe",
            )
            self.assertEqual(release_spec["size_bytes"], len(b"signed installer fixture"))

    def test_signer_urls_and_output_are_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_path = Path(temporary_directory)
            release_directory = self.create_release(temporary_path)
            with self.assertRaises(RuntimeError):
                build_release_spec(
                    release_directory,
                    "C" * 64,
                    "https://updates.example.test/metadata/",
                    "https://updates.example.test/targets/",
                    "stable",
                    lambda _installer_path, _signer_pin: None,
                )
            with self.assertRaises(RuntimeError):
                build_release_spec(
                    release_directory,
                    SIGNER_PIN,
                    "https://updates.example.test/metadata/?token=secret",
                    "https://updates.example.test/targets/",
                    "stable",
                    lambda _installer_path, _signer_pin: None,
                )

            output_path = temporary_path / "release-spec.json"
            write_new_json(output_path, {"approved": True})
            original_bytes = output_path.read_bytes()
            with self.assertRaises(FileExistsError):
                write_new_json(output_path, {"approved": False})
            self.assertEqual(output_path.read_bytes(), original_bytes)


if __name__ == "__main__":
    unittest.main()
