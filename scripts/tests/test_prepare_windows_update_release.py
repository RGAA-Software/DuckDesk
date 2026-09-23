import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from prepare_windows_update_release import build_release_spec, write_new_json


class WindowsUpdateReleaseTests(unittest.TestCase):
    def create_release(
        self,
        root: Path,
        *,
        distribution: str = "official",
        release_namespace: str = "pixels.official",
        oem_id: str | None = None,
        company: str = "Pixels",
        installer_basename: str = "PixelsCloudNode",
        oem_profile_sha256: str | None = None,
    ) -> Path:
        release_directory = root / "release"
        release_directory.mkdir()
        installer_name = f"{installer_basename}_{distribution}_3.3.80_Setup.exe"
        installer_path = release_directory / installer_name
        installer_path.write_bytes(b"signed installer fixture")
        installer_sha256 = hashlib.sha256(installer_path.read_bytes()).hexdigest().upper()
        (release_directory / "installer-manifest.json").write_text(
            json.dumps(
                {
                    "schema_version": 4,
                    "product": "cloud_node",
                    "distribution": distribution,
                    "release_namespace": release_namespace,
                    "oem_id": oem_id,
                    "company": company,
                    "publisher_name": company,
                    "installer_basename": installer_basename,
                    "oem_profile_sha256": oem_profile_sha256,
                    "product_version": "3.3.80",
                    "product_version_code": 30380,
                    "git_revision": "a" * 40,
                    "windows_code_signing": "unsigned",
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
            release_spec, verified_release = build_release_spec(
                release_directory,
                "https://updates.example.test/metadata/",
                "https://updates.example.test/targets/",
                "stable",
            )
            self.assertEqual(verified_release.product_version_code, 30380)
            self.assertIsNone(release_spec["platform_signer_sha256"])
            self.assertEqual(release_spec["windows_code_signing"], "unsigned")
            self.assertEqual(release_spec["build_number"], 30380)
            self.assertEqual(release_spec["target"]["release_namespace"], "pixels.official")
            self.assertIsNone(release_spec["target"]["oem_id"])
            self.assertEqual(
                release_spec["target_name"],
                "windows/cloud_node/official/stable/x86_64/30380/PixelsCloudNode_official_3.3.80_Setup.exe",
            )
            self.assertEqual(release_spec["size_bytes"], len(b"signed installer fixture"))

    def test_oem_target_name_and_catalog_identity_include_the_oem_domain(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            release_directory = self.create_release(
                Path(temporary_directory),
                distribution="oem",
                release_namespace="oem.acme-cloud",
                oem_id="acme-cloud",
                company="Acme Systems",
                installer_basename="AcmeCloudNode",
                oem_profile_sha256="C" * 64,
            )
            release_spec, verified_release = build_release_spec(
                release_directory,
                "https://updates.acme.example/metadata/",
                "https://updates.acme.example/targets/",
                "stable",
            )
            self.assertEqual(verified_release.oem_id, "acme-cloud")
            self.assertEqual(release_spec["target"]["release_namespace"], "oem.acme-cloud")
            self.assertEqual(release_spec["target"]["oem_id"], "acme-cloud")
            self.assertEqual(
                release_spec["target_name"],
                "windows/cloud_node/oem/acme-cloud/stable/x86_64/30380/AcmeCloudNode_oem_3.3.80_Setup.exe",
            )

    def test_urls_and_output_are_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_path = Path(temporary_directory)
            release_directory = self.create_release(temporary_path)
            with self.assertRaises(RuntimeError):
                build_release_spec(
                    release_directory,
                    "https://updates.example.test/metadata/?token=secret",
                    "https://updates.example.test/targets/",
                    "stable",
                )

            output_path = temporary_path / "release-spec.json"
            write_new_json(output_path, {"approved": True})
            original_bytes = output_path.read_bytes()
            with self.assertRaises(FileExistsError):
                write_new_json(output_path, {"approved": False})
            self.assertEqual(output_path.read_bytes(), original_bytes)


if __name__ == "__main__":
    unittest.main()
