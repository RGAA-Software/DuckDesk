from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts.collect_dist import sha256
from scripts.refresh_development_dist import refresh


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPOSITORY_ROOT / "scripts" / "verify_product_dist.py"
SPECIFICATION = importlib.util.spec_from_file_location("verify_product_dist", MODULE_PATH)
assert SPECIFICATION and SPECIFICATION.loader
VERIFY_PRODUCT_DIST = importlib.util.module_from_spec(SPECIFICATION)
SPECIFICATION.loader.exec_module(VERIFY_PRODUCT_DIST)


class RetiredCentralMediaAuditTest(unittest.TestCase):
    def test_accepts_current_product_files(self) -> None:
        VERIFY_PRODUCT_DIST.verify_retired_central_media_absent(
            {
                "px_render.exe",
                "px_rtc.dll",
                "web_client/assets/index.js",
                "resources/deployment/deployment-policy.json",
            }
        )

    def test_rejects_retired_executable_or_configuration_names_case_insensitively(self) -> None:
        for retired_path in (
            "MediaServer.exe",
            "runtime/mediaserver",
            "runtime/PX_TURN.EXE",
            "runtime/turnserver",
            "configuration/turnserver.conf",
            "plugins/libmk_api.dll",
            "plugins/libmk_api.so",
            "plugins/mk_api.so",
        ):
            with self.subTest(retired_path=retired_path):
                with self.assertRaisesRegex(RuntimeError, "ZLMediaKit/Coturn"):
                    VERIFY_PRODUCT_DIST.verify_retired_central_media_absent({"px_render.exe", retired_path})


class DevelopmentManifestRefreshTest(unittest.TestCase):
    def test_refreshes_changed_artifacts_without_recording_runtime_logs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            product_root = Path(temporary_directory) / "client"
            distribution = product_root / "dist"
            distribution.mkdir(parents=True)
            executable = distribution / "px_client.exe"
            executable.write_bytes(b"current-client")
            runtime_log = distribution / "px_logs" / "px_client.log"
            runtime_log.parent.mkdir()
            runtime_log.write_text("runtime output", encoding="utf-8")
            (distribution / "product-manifest.json").write_text(
                json.dumps(
                    {
                        "schema_version": 3,
                        "product": "client",
                        "distribution": "development",
                        "release_namespace": None,
                        "oem_id": None,
                        "oem_profile_sha256": None,
                        "company": "Pixels",
                        "artifacts": [{"path": "px_client.exe", "sha256": "STALE"}],
                    }
                ),
                encoding="utf-8",
            )

            hashes = refresh(distribution)

            self.assertEqual(hashes, {"px_client.exe": sha256(executable)})
            manifest = json.loads((distribution / "product-manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["artifacts"], [{"path": "px_client.exe", "sha256": sha256(executable)}])
            self.assertEqual(
                json.loads((distribution / "sha256sums.json").read_text(encoding="utf-8")),
                hashes,
            )

    def test_refuses_release_distribution(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            distribution = Path(temporary_directory) / "client" / "dist"
            distribution.mkdir(parents=True)
            (distribution / "product-manifest.json").write_text(
                json.dumps({"schema_version": 3, "product": "client", "distribution": "official"}),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(RuntimeError, "development distribution"):
                refresh(distribution)

    def test_rejects_retired_component_directories(self) -> None:
        for retired_path in ("ZLMediaKit/config.ini", "third_party/coturn/LICENSE"):
            with self.subTest(retired_path=retired_path):
                with self.assertRaisesRegex(RuntimeError, "ZLMediaKit/Coturn"):
                    VERIFY_PRODUCT_DIST.verify_retired_central_media_absent({"px_render.exe", retired_path})


class DistributionUpdateTrustAuditTest(unittest.TestCase):
    def test_release_distribution_requires_update_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            distribution = Path(temporary_directory)
            policy_path = distribution / "resources/deployment/deployment-policy.json"
            policy_path.parent.mkdir(parents=True)
            policy_path.write_text(
                json.dumps(
                    {
                        "schema_version": 2,
                        "distribution": "customer",
                        "release_namespace": "pixels.customer",
                        "oem_id": None,
                        "expected_deployment_id": None,
                        "official_console_origin": None,
                        "minimum_certificate_version": 1,
                        "minimum_descriptor_revision": 1,
                        "minimum_trust_epoch": 1,
                        "protocol_version": 1,
                    }
                ),
                encoding="utf-8",
            )
            actual_files = {
                "resources/deployment/deployment-policy.json",
                "resources/deployment/deployment-trust.json",
            }
            with self.assertRaisesRegex(RuntimeError, "update trust"):
                VERIFY_PRODUCT_DIST.verify_distribution_identity(
                    distribution,
                    {
                        "distribution": "customer",
                        "release_namespace": "pixels.customer",
                        "oem_id": None,
                    },
                    actual_files,
                )
            actual_files.add("resources/update/root.json")
            actual_files.add("px_client.exe")
            manifest = {
                "distribution": "customer",
                "release_namespace": "pixels.customer",
                "oem_id": None,
                "oem_profile_sha256": None,
                "company": "Pixels",
                "owned_pe": ["px_client.exe"],
                "signer_certificate_sha256": "A" * 64,
            }
            with mock.patch.object(VERIFY_PRODUCT_DIST, "verify_file") as verify_file:
                VERIFY_PRODUCT_DIST.verify_distribution_identity(distribution, manifest, actual_files)
                verify_file.assert_called_once_with(distribution / "px_client.exe", "A" * 64)

    def test_release_distribution_requires_signer_pin_and_owned_pe_inventory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            distribution = Path(temporary_directory)
            actual_files = {
                "resources/deployment/deployment-policy.json",
                "resources/deployment/deployment-trust.json",
                "resources/update/root.json",
            }
            with self.assertRaisesRegex(RuntimeError, "signer pin"):
                VERIFY_PRODUCT_DIST.verify_distribution_identity(
                    distribution,
                    {
                        "distribution": "official",
                        "release_namespace": "pixels.official",
                        "oem_id": None,
                        "oem_profile_sha256": None,
                        "company": "Pixels",
                    },
                    actual_files,
                )

    def test_release_distribution_rejects_pixels_pe_outside_owned_inventory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            distribution = Path(temporary_directory)
            policy_path = distribution / "resources/deployment/deployment-policy.json"
            policy_path.parent.mkdir(parents=True)
            policy_path.write_text(
                json.dumps(
                    {
                        "schema_version": 2,
                        "distribution": "customer",
                        "release_namespace": "pixels.customer",
                        "oem_id": None,
                        "expected_deployment_id": None,
                        "official_console_origin": None,
                        "minimum_certificate_version": 1,
                        "minimum_descriptor_revision": 1,
                        "minimum_trust_epoch": 1,
                        "protocol_version": 1,
                    }
                ),
                encoding="utf-8",
            )
            actual_files = {
                "resources/deployment/deployment-policy.json",
                "resources/deployment/deployment-trust.json",
                "resources/update/root.json",
                "px_client.exe",
                "rdp/px_rdp_core.dll",
            }
            manifest = {
                "distribution": "customer",
                "release_namespace": "pixels.customer",
                "oem_id": None,
                "oem_profile_sha256": None,
                "company": "Pixels",
                "owned_pe": ["px_client.exe"],
                "signer_certificate_sha256": "A" * 64,
            }
            with mock.patch.object(VERIFY_PRODUCT_DIST, "verify_file"):
                with self.assertRaisesRegex(RuntimeError, "unsigned Pixels PE"):
                    VERIFY_PRODUCT_DIST.verify_distribution_identity(distribution, manifest, actual_files)

    def test_development_distribution_rejects_update_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            with self.assertRaisesRegex(RuntimeError, "update trust"):
                VERIFY_PRODUCT_DIST.verify_distribution_identity(
                    Path(temporary_directory),
                    {
                        "distribution": "development",
                        "release_namespace": None,
                        "oem_id": None,
                        "oem_profile_sha256": None,
                        "company": "Pixels",
                    },
                    {"resources/update/root.json"},
                )

    def test_oem_distribution_requires_the_same_canonical_release_domain_in_manifest_and_policy(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            distribution = Path(temporary_directory)
            policy_path = distribution / "resources/deployment/deployment-policy.json"
            policy_path.parent.mkdir(parents=True)
            policy = {
                "schema_version": 2,
                "distribution": "oem",
                "release_namespace": "oem.acme-cloud",
                "oem_id": "acme-cloud",
                "expected_deployment_id": None,
                "official_console_origin": None,
                "minimum_certificate_version": 1,
                "minimum_descriptor_revision": 1,
                "minimum_trust_epoch": 1,
                "protocol_version": 1,
            }
            policy_path.write_text(json.dumps(policy), encoding="utf-8")
            actual_files = {
                "resources/deployment/deployment-policy.json",
                "resources/deployment/deployment-trust.json",
                "resources/update/root.json",
                "px_client.exe",
            }
            manifest = {
                "distribution": "oem",
                "release_namespace": "oem.acme-cloud",
                "oem_id": "acme-cloud",
                "oem_profile_sha256": "B" * 64,
                "company": "Acme Systems",
                "owned_pe": ["px_client.exe"],
                "signer_certificate_sha256": "A" * 64,
            }
            with mock.patch.object(VERIFY_PRODUCT_DIST, "verify_file"):
                VERIFY_PRODUCT_DIST.verify_distribution_identity(distribution, manifest, actual_files)
                policy["release_namespace"] = "oem.other-brand"
                policy_path.write_text(json.dumps(policy), encoding="utf-8")
                with self.assertRaisesRegex(RuntimeError, "wrong release domain"):
                    VERIFY_PRODUCT_DIST.verify_distribution_identity(distribution, manifest, actual_files)

if __name__ == "__main__":
    unittest.main()
