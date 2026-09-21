from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT / "scripts"))

from oem_release_profile import emit_android_json, emit_cmake, load_oem_release_profile  # noqa: E402


class OemReleaseProfileTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.profile_directory = Path(self.temporary_directory.name)
        self.assets: dict[str, Path] = {}
        for asset_name in ("windows.ico", "android-foreground.png", "android-background.png", "web-icon.png"):
            asset_path = self.profile_directory / asset_name
            asset_path.write_bytes(f"content:{asset_name}".encode())
            self.assets[asset_name] = asset_path
        self.profile_path = self.profile_directory / "oem-release-profile.json"

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    @staticmethod
    def file_sha256(path: Path) -> str:
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def valid_profile(self) -> dict[str, object]:
        def build_asset_reference(asset_name: str) -> dict[str, str]:
            return {
                "path": asset_name,
                "sha256": self.file_sha256(self.assets[asset_name]),
            }

        return {
            "schema_version": 1,
            "oem_id": "north-star",
            "release_namespace": "oem.north-star",
            "brand": {"company_name": "North Star Ltd.", "application_name": "North Star Cloud"},
            "deployment": {"trust_store_sha256": "1" * 64},
            "update": {"root_sha256": "2" * 64},
            "windows": {
                "publisher_name": "North Star Ltd.",
                "signer_certificate_sha256": "3" * 64,
                "icon": build_asset_reference("windows.ico"),
                "products": {
                    "cloud_node": {
                        "product_name": "North Star Node",
                        "install_directory_name": "North Star Node",
                        "uninstall_key": "NorthStarNode",
                        "installer_basename": "NorthStarNode",
                    },
                    "client": {
                        "product_name": "North Star Client",
                        "install_directory_name": "North Star Client",
                        "uninstall_key": "NorthStarClient",
                        "installer_basename": "NorthStarClient",
                    },
                    "remote": {
                        "product_name": "North Star Remote",
                        "install_directory_name": "North Star Remote",
                        "uninstall_key": "NorthStarRemote",
                        "installer_basename": "NorthStarRemote",
                    },
                },
            },
            "android": {
                "application_id": "com.northstar.cloud.client",
                "signer_certificate_sha256": "4" * 64,
                "icon_foreground": build_asset_reference("android-foreground.png"),
                "icon_background": build_asset_reference("android-background.png"),
            },
            "web": {"application_name": "North Star Cloud", "icon": build_asset_reference("web-icon.png")},
        }

    def write_profile(self, profile: dict[str, object]) -> None:
        self.profile_path.write_text(json.dumps(profile, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    def test_loads_complete_independent_oem_identity(self) -> None:
        self.write_profile(self.valid_profile())
        profile = load_oem_release_profile(self.profile_path)
        self.assertEqual(profile.oem_id, "north-star")
        self.assertEqual(profile.release_namespace, "oem.north-star")
        self.assertEqual(profile.company_name, "North Star Ltd.")
        self.assertEqual(profile.android_application_id, "com.northstar.cloud.client")
        self.assertRegex(profile.profile_sha256, r"^[0-9a-f]{64}$")
        cmake_variables = emit_cmake(profile, "client")
        self.assertIn("set(PX_OEM_ID [[north-star]])", cmake_variables)
        self.assertIn("set(PX_OEM_PRODUCT_NAME [[North Star Client]])", cmake_variables)
        self.assertIn("set(PX_OEM_STORAGE_DIRECTORY_NAME [[North Star Client]])", cmake_variables)
        self.assertIn(f"set(PX_OEM_BRAND_ICON [[{self.assets['web-icon.png'].as_posix()}]])", cmake_variables)
        android_configuration = json.loads(emit_android_json(profile))
        self.assertEqual(android_configuration["application_id"], "com.northstar.cloud.client")
        self.assertEqual(android_configuration["application_name"], "North Star Cloud")
        self.assertEqual(android_configuration["profile_sha256"], profile.profile_sha256)
        self.assertEqual(
            Path(android_configuration["icon_foreground_path"]),
            self.assets["android-foreground.png"],
        )

    def test_rejects_pixels_brand_and_application_identity(self) -> None:
        profile = self.valid_profile()
        profile["brand"]["application_name"] = "Pixels"  # type: ignore[index]
        self.write_profile(profile)
        with self.assertRaisesRegex(RuntimeError, "must not impersonate"):
            load_oem_release_profile(self.profile_path)

        profile = self.valid_profile()
        profile["android"]["application_id"] = "yun.pixels.client.oem"  # type: ignore[index]
        self.write_profile(profile)
        with self.assertRaisesRegex(RuntimeError, "independent lowercase reverse-DNS"):
            load_oem_release_profile(self.profile_path)

    def test_rejects_brand_text_that_cannot_be_embedded_in_native_resources(self) -> None:
        profile = self.valid_profile()
        profile["brand"]["application_name"] = 'North "Star"'  # type: ignore[index]
        self.write_profile(profile)
        with self.assertRaisesRegex(RuntimeError, "quote or backslash"):
            load_oem_release_profile(self.profile_path)

        profile = self.valid_profile()
        profile["windows"]["products"]["client"]["product_name"] = "North\\Star"  # type: ignore[index]
        self.write_profile(profile)
        with self.assertRaisesRegex(RuntimeError, "quote or backslash"):
            load_oem_release_profile(self.profile_path)

    def test_rejects_asset_tampering_and_path_escape(self) -> None:
        profile = self.valid_profile()
        self.assets["web-icon.png"].write_bytes(b"tampered")
        self.write_profile(profile)
        with self.assertRaisesRegex(RuntimeError, "asset SHA-256 does not match"):
            load_oem_release_profile(self.profile_path)

        profile = self.valid_profile()
        profile["windows"]["icon"]["path"] = "../outside.ico"  # type: ignore[index]
        self.write_profile(profile)
        with self.assertRaisesRegex(RuntimeError, "must stay within"):
            load_oem_release_profile(self.profile_path)

    def test_rejects_duplicate_windows_install_identity(self) -> None:
        profile = self.valid_profile()
        products = profile["windows"]["products"]  # type: ignore[index]
        products["remote"]["uninstall_key"] = products["client"]["uninstall_key"]  # type: ignore[index]
        self.write_profile(profile)
        with self.assertRaisesRegex(RuntimeError, "uninstall keys must be unique"):
            load_oem_release_profile(self.profile_path)


if __name__ == "__main__":
    unittest.main()
