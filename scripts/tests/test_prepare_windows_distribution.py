from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPOSITORY_ROOT / "scripts" / "prepare_windows_distribution.py"


class PrepareWindowsDistributionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.update_root = self.root / "update-root.json"
        self.update_root.write_text(
            json.dumps(
                {
                    "signed": {
                        "_type": "root",
                        "spec_version": "1.0.0",
                        "version": 1,
                        "expires": "2030-01-01T00:00:00Z",
                        "keys": {"test-key": {"keytype": "ed25519"}},
                        "roles": {
                            role: {"keyids": ["test-key"], "threshold": 1}
                            for role in ("root", "snapshot", "targets", "timestamp")
                        },
                    },
                    "signatures": [{"keyid": "test-key", "sig": "test-signature"}],
                },
                separators=(",", ":"),
            ),
            encoding="utf-8",
        )
        self.windows_icon = self.root / "windows.ico"
        self.android_foreground = self.root / "android-foreground.png"
        self.android_background = self.root / "android-background.png"
        self.web_icon = self.root / "web-icon.png"
        for asset_path, asset_bytes in (
            (self.windows_icon, b"test-windows-icon"),
            (self.android_foreground, b"test-android-foreground"),
            (self.android_background, b"test-android-background"),
            (self.web_icon, b"test-web-icon"),
        ):
            asset_path.write_bytes(asset_bytes)
        self.oem_profile = self.root / "oem-release-profile.json"
        self.write_oem_profile()
        self.environment = os.environ.copy()
        self.environment.pop("PIXELS_OEM_RELEASE_PROFILE", None)
        self.environment.update(
            {
                "PIXELS_UPDATE_ROOT_FILE": str(self.update_root),
                "PIXELS_OFFICIAL_CONSOLE_URL": "https://console.pixels.example:8443",
            }
        )

    @staticmethod
    def file_sha256(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def write_oem_profile(self, *, update_root_sha256: str | None = None) -> None:
        profile = {
            "schema_version": 1,
            "oem_id": "acme-cloud",
            "release_namespace": "oem.acme-cloud",
            "brand": {"company_name": "Acme Systems", "application_name": "Acme Cloud"},
            "update": {"root_sha256": update_root_sha256 or self.file_sha256(self.update_root)},
            "windows": {
                "publisher_name": "Acme Systems",
                "signer_certificate_sha256": "1" * 64,
                "icon": {"path": self.windows_icon.name, "sha256": self.file_sha256(self.windows_icon)},
                "products": {
                    "cloud_node": {
                        "product_name": "Acme Cloud Node",
                        "install_directory_name": "Acme Cloud Node",
                        "uninstall_key": "AcmeCloudNode",
                        "installer_basename": "AcmeCloudNode",
                    },
                    "client": {
                        "product_name": "Acme Client",
                        "install_directory_name": "Acme Client",
                        "uninstall_key": "AcmeClient",
                        "installer_basename": "AcmeClient",
                    },
                    "remote": {
                        "product_name": "Acme Remote",
                        "install_directory_name": "Acme Remote",
                        "uninstall_key": "AcmeRemote",
                        "installer_basename": "AcmeRemote",
                    },
                },
            },
            "android": {
                "application_id": "com.acme.cloud.client",
                "signer_certificate_sha256": "2" * 64,
                "icon_foreground": {"path": self.android_foreground.name, "sha256": self.file_sha256(self.android_foreground)},
                "icon_background": {"path": self.android_background.name, "sha256": self.file_sha256(self.android_background)},
            },
            "web": {
                "application_name": "Acme Cloud",
                "icon": {"path": self.web_icon.name, "sha256": self.file_sha256(self.web_icon)},
            },
        }
        self.oem_profile.write_text(json.dumps(profile, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def run_script(
        self,
        distribution: str,
        *arguments: str,
        environment: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--product", "client", "--distribution", distribution, *arguments],
            cwd=REPOSITORY_ROOT,
            env=environment or self.environment,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_official_and_customer_stage_only_update_root(self) -> None:
        for distribution in ("official", "customer"):
            with self.subTest(distribution=distribution):
                output_directory = self.root / distribution
                result = self.run_script(distribution, "--output-dir", str(output_directory))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(list(output_directory.iterdir()), [output_directory / "update-root.json"])
                self.assertEqual((output_directory / "update-root.json").read_bytes(), self.update_root.read_bytes())

    def test_pixels_distributions_require_canonical_official_origin(self) -> None:
        missing_origin = self.environment | {"PIXELS_OFFICIAL_CONSOLE_URL": ""}
        self.assertNotEqual(self.run_script("customer", "--validate-only", environment=missing_origin).returncode, 0)
        invalid_origin = self.environment | {"PIXELS_OFFICIAL_CONSOLE_URL": "https://CONSOLE.pixels.example:443"}
        self.assertNotEqual(self.run_script("official", "--validate-only", environment=invalid_origin).returncode, 0)

    def test_oem_requires_profile_and_matching_update_root(self) -> None:
        oem_environment = self.environment | {
            "PIXELS_OFFICIAL_CONSOLE_URL": "",
            "PIXELS_OEM_RELEASE_PROFILE": str(self.oem_profile),
        }
        result = self.run_script("oem", "--validate-only", environment=oem_environment)
        self.assertEqual(result.returncode, 0, result.stderr)

        self.write_oem_profile(update_root_sha256="3" * 64)
        result = self.run_script("oem", "--validate-only", environment=oem_environment)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("TUF root SHA-256", result.stderr)

    def test_distribution_flavors_reject_foreign_identity_inputs(self) -> None:
        official_environment = self.environment | {"PIXELS_OEM_RELEASE_PROFILE": str(self.oem_profile)}
        self.assertNotEqual(self.run_script("official", "--validate-only", environment=official_environment).returncode, 0)
        oem_environment = self.environment | {"PIXELS_OEM_RELEASE_PROFILE": str(self.oem_profile)}
        self.assertNotEqual(self.run_script("oem", "--validate-only", environment=oem_environment).returncode, 0)

    def test_rejects_missing_or_incomplete_update_root(self) -> None:
        missing_root = self.environment | {"PIXELS_UPDATE_ROOT_FILE": ""}
        self.assertNotEqual(self.run_script("official", "--validate-only", environment=missing_root).returncode, 0)
        self.update_root.write_text("{}", encoding="utf-8")
        self.assertNotEqual(self.run_script("official", "--validate-only").returncode, 0)


if __name__ == "__main__":
    unittest.main()
