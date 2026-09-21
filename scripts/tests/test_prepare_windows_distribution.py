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
        public_key = bytes(range(1, 33))
        key_id = hashlib.sha256(public_key).hexdigest()
        self.trust_store = self.root / "deployment-trust.json"
        self.trust_store.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "trust_epoch": 3,
                    "trusted_keys": [{"key_id": key_id, "public_key_hex": public_key.hex()}],
                },
                separators=(",", ":"),
            ),
            encoding="utf-8",
        )
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
                        "roles": {role: {"keyids": ["test-key"], "threshold": 1} for role in ("root", "snapshot", "targets", "timestamp")},
                    },
                    "signatures": [{"keyid": "test-key", "sig": "test-signature"}],
                },
                separators=(",", ":"),
            ),
            encoding="utf-8",
        )
        self.environment = os.environ.copy()
        self.environment.update(
            {
                "PIXELS_DEPLOYMENT_TRUST_STORE_FILE": str(self.trust_store),
                "PIXELS_UPDATE_ROOT_FILE": str(self.update_root),
                "PIXELS_DEPLOYMENT_CERTIFICATE_VERSION": "4",
                "PIXELS_DESCRIPTOR_REVISION": "7",
                "PIXELS_DEPLOYMENT_TRUST_EPOCH": "3",
                "PIXELS_EXPECTED_DEPLOYMENT_ID": "8f9cbade-f2c1-47d4-a92e-109675684b21",
                "PIXELS_OFFICIAL_CONSOLE_URL": "https://console.pixels.example:8443",
            }
        )

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def run_script(self, distribution: str, *arguments: str, environment: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--product",
                "client",
                "--distribution",
                distribution,
                *arguments,
            ],
            cwd=REPOSITORY_ROOT,
            env=environment or self.environment,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_official_policy_is_emitted_with_fixed_identity(self) -> None:
        output_directory = self.root / "official"
        result = self.run_script("official", "--output-dir", str(output_directory))
        self.assertEqual(result.returncode, 0, result.stderr)
        policy = json.loads((output_directory / "deployment-policy.json").read_text(encoding="utf-8"))
        self.assertEqual(policy["distribution"], "official")
        self.assertEqual(policy["release_namespace"], "pixels.official")
        self.assertIsNone(policy["oem_id"])
        self.assertEqual(policy["expected_deployment_id"], self.environment["PIXELS_EXPECTED_DEPLOYMENT_ID"])
        self.assertEqual(policy["official_console_origin"], self.environment["PIXELS_OFFICIAL_CONSOLE_URL"])
        self.assertEqual(policy["protocol_version"], 1)
        self.assertEqual((output_directory / "deployment-trust.json").read_bytes(), self.trust_store.read_bytes())
        self.assertEqual((output_directory / "update-root.json").read_bytes(), self.update_root.read_bytes())

    def test_customer_policy_contains_no_official_identity(self) -> None:
        output_directory = self.root / "customer"
        result = self.run_script("customer", "--matrix-customer", "--output-dir", str(output_directory))
        self.assertEqual(result.returncode, 0, result.stderr)
        policy = json.loads((output_directory / "deployment-policy.json").read_text(encoding="utf-8"))
        self.assertEqual(policy["distribution"], "customer")
        self.assertEqual(policy["release_namespace"], "pixels.customer")
        self.assertIsNone(policy["oem_id"])
        self.assertIsNone(policy["expected_deployment_id"])
        self.assertIsNone(policy["official_console_origin"])

    def test_customer_standalone_rejects_official_inputs(self) -> None:
        result = self.run_script("customer", "--validate-only")
        self.assertNotEqual(result.returncode, 0)

    def test_oem_policy_requires_and_binds_canonical_oem_id(self) -> None:
        output_directory = self.root / "oem"
        environment = self.environment | {
            "PIXELS_EXPECTED_DEPLOYMENT_ID": "",
            "PIXELS_OFFICIAL_CONSOLE_URL": "",
            "PIXELS_OEM_ID": "acme-cloud",
        }
        result = self.run_script("oem", "--output-dir", str(output_directory), environment=environment)
        self.assertEqual(result.returncode, 0, result.stderr)
        policy = json.loads((output_directory / "deployment-policy.json").read_text(encoding="utf-8"))
        self.assertEqual(policy["distribution"], "oem")
        self.assertEqual(policy["release_namespace"], "oem.acme-cloud")
        self.assertEqual(policy["oem_id"], "acme-cloud")

    def test_rejects_noncanonical_trust_store_and_origin(self) -> None:
        self.trust_store.write_bytes(self.trust_store.read_bytes() + b"\n")
        result = self.run_script("official", "--validate-only")
        self.assertNotEqual(result.returncode, 0)
        self.trust_store.write_bytes(self.trust_store.read_bytes().rstrip(b"\n"))
        invalid_environment = self.environment | {"PIXELS_OFFICIAL_CONSOLE_URL": "https://CONSOLE.pixels.example:443"}
        result = self.run_script("official", "--validate-only", environment=invalid_environment)
        self.assertNotEqual(result.returncode, 0)

    def test_validation_does_not_create_output(self) -> None:
        result = self.run_script("official", "--validate-only")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(set(self.root.iterdir()), {self.trust_store, self.update_root})

    def test_rejects_missing_or_incomplete_update_root(self) -> None:
        missing_environment = self.environment | {"PIXELS_UPDATE_ROOT_FILE": ""}
        result = self.run_script("official", "--validate-only", environment=missing_environment)
        self.assertNotEqual(result.returncode, 0)
        self.update_root.write_text("{}", encoding="utf-8")
        result = self.run_script("official", "--validate-only")
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
