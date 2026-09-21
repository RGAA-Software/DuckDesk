from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from scripts.verify_windows_installer_release import (
    validate_installed_product,
    validate_release_directory,
    validate_upgrade_pair,
)


SIGNER_PIN = "A" * 64
PAYLOAD_PIN = "B" * 64


class WindowsInstallerReleaseVerificationTests(unittest.TestCase):
    def create_release(
        self,
        parent_directory: Path,
        version: str,
        version_code: int,
        *,
        product: str = "client",
        distribution: str = "official",
        signer_pin: str = SIGNER_PIN,
    ) -> Path:
        release_directory = parent_directory / f"{product}-{distribution}-{version}"
        release_directory.mkdir()
        product_basename = {
            "cloud_node": "PixelsCloudNode",
            "client": "PixelsClient",
            "remote": "PixelsRemote",
        }[product]
        installer_name = f"{product_basename}_{distribution}_{version}_Setup.exe"
        installer_path = release_directory / installer_name
        installer_path.write_bytes(f"signed installer {product} {distribution} {version}".encode("utf-8"))
        installer_sha256 = hashlib.sha256(installer_path.read_bytes()).hexdigest().upper()
        manifest = {
            "schema_version": 2,
            "product": product,
            "distribution": distribution,
            "company": "Pixels",
            "product_version": version,
            "product_version_code": version_code,
            "git_revision": "1" * 40,
            "signer_certificate_sha256": signer_pin,
            "payload_manifest_sha256": PAYLOAD_PIN,
            "payload_artifact_count": 12,
            "installer": {"path": installer_name, "sha256": installer_sha256},
        }
        (release_directory / "installer-manifest.json").write_text(
            json.dumps(manifest),
            encoding="utf-8",
        )
        return release_directory

    @staticmethod
    def accept_signature(_installer_path: Path, _signer_pin: str) -> None:
        return None

    def test_valid_release_and_upgrade_pair(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            release_root = Path(temporary_directory)
            previous_directory = self.create_release(release_root, "3.3.72", 30372)
            current_directory = self.create_release(release_root, "3.3.73", 30373)

            previous_release = validate_release_directory(previous_directory, self.accept_signature)
            upgrade_pair = validate_upgrade_pair(
                previous_directory,
                current_directory,
                self.accept_signature,
                approved_signer_transition=(SIGNER_PIN, SIGNER_PIN),
            )

            self.assertEqual(previous_release.product_version, "3.3.72")
            self.assertEqual(upgrade_pair["product"], "client")
            self.assertEqual(upgrade_pair["current"]["product_version_code"], 30373)

    def test_tampered_installer_is_rejected_before_signature_verification(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            release_directory = self.create_release(Path(temporary_directory), "3.3.72", 30372)
            installer_path = next(release_directory.glob("*.exe"))
            installer_path.write_bytes(b"tampered")

            with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                validate_release_directory(release_directory, self.accept_signature)

    def test_version_code_must_match_three_component_version(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            release_directory = self.create_release(Path(temporary_directory), "3.3.72", 30373)

            with self.assertRaisesRegex(RuntimeError, "version code mismatch"):
                validate_release_directory(release_directory, self.accept_signature)

    def test_upgrade_pair_rejects_distribution_switch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            release_root = Path(temporary_directory)
            previous_directory = self.create_release(release_root, "3.3.72", 30372)
            current_directory = self.create_release(
                release_root,
                "3.3.73",
                30373,
                distribution="customer",
            )

            with self.assertRaisesRegex(RuntimeError, "distributions do not match"):
                validate_upgrade_pair(
                    previous_directory,
                    current_directory,
                    self.accept_signature,
                    approved_signer_transition=(SIGNER_PIN, SIGNER_PIN),
                )

    def test_upgrade_pair_requires_external_signer_approval_and_supports_approved_rotation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            release_root = Path(temporary_directory)
            previous_directory = self.create_release(release_root, "3.3.72", 30372)
            current_directory = self.create_release(
                release_root,
                "3.3.73",
                30373,
                signer_pin="C" * 64,
            )

            with self.assertRaisesRegex(RuntimeError, "requires externally approved"):
                validate_upgrade_pair(previous_directory, current_directory, self.accept_signature)

            approved_pair = validate_upgrade_pair(
                previous_directory,
                current_directory,
                self.accept_signature,
                approved_signer_transition=(SIGNER_PIN, "C" * 64),
            )
            self.assertTrue(approved_pair["signer_transition"]["explicitly_approved"])

    def test_upgrade_pair_rejects_same_or_older_version(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            release_root = Path(temporary_directory)
            previous_directory = self.create_release(release_root, "3.3.72", 30372)
            current_directory = self.create_release(release_root, "3.3.71", 30371)

            with self.assertRaisesRegex(RuntimeError, "must be newer"):
                validate_upgrade_pair(
                    previous_directory,
                    current_directory,
                    self.accept_signature,
                    approved_signer_transition=(SIGNER_PIN, SIGNER_PIN),
                )

    def test_installed_product_requires_exact_payload_and_signed_owned_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            release_directory = self.create_release(temporary_root, "3.3.72", 30372)
            install_directory = temporary_root / "installed"
            install_directory.mkdir()
            payload_files = {
                "px_panel.exe": b"panel",
                "resources/zh-CN.txt": b"catalog",
            }
            artifacts = []
            for artifact_name, artifact_bytes in payload_files.items():
                artifact_path = install_directory / artifact_name
                artifact_path.parent.mkdir(parents=True, exist_ok=True)
                artifact_path.write_bytes(artifact_bytes)
                artifacts.append(
                    {
                        "path": artifact_name,
                        "sha256": hashlib.sha256(artifact_bytes).hexdigest().upper(),
                    }
                )
            product_manifest = {
                "schema_version": 2,
                "product": "client",
                "distribution": "official",
                "company": "Pixels",
                "product_version": "3.3.72",
                "product_version_code": 30372,
                "signer_certificate_sha256": SIGNER_PIN,
                "owned_pe": ["px_panel.exe"],
                "artifacts": artifacts,
            }
            product_manifest_path = install_directory / "product-manifest.json"
            product_manifest_path.write_text(json.dumps(product_manifest), encoding="utf-8")
            artifact_hashes = {artifact["path"]: artifact["sha256"] for artifact in artifacts}
            (install_directory / "sha256sums.json").write_text(json.dumps(artifact_hashes), encoding="utf-8")
            (install_directory / "licenses.json").write_text(json.dumps({"files": []}), encoding="utf-8")
            (install_directory / "product-edition.txt").write_text(
                "client\n3.3.72\nPixels\n",
                encoding="utf-8",
            )
            (install_directory / "Uninstall.exe").write_bytes(b"uninstaller")

            release_manifest_path = release_directory / "installer-manifest.json"
            release_manifest = json.loads(release_manifest_path.read_text(encoding="utf-8"))
            release_manifest["payload_manifest_sha256"] = hashlib.sha256(
                product_manifest_path.read_bytes()
            ).hexdigest().upper()
            release_manifest["payload_artifact_count"] = len(artifacts)
            release_manifest_path.write_text(json.dumps(release_manifest), encoding="utf-8")
            verified_signatures: list[str] = []

            def record_signature(artifact_path: Path, signer_pin: str) -> None:
                self.assertEqual(signer_pin, SIGNER_PIN)
                verified_signatures.append(artifact_path.name)

            result = validate_installed_product(
                release_directory,
                install_directory,
                record_signature,
                expected_signer_sha256=SIGNER_PIN,
            )

            self.assertEqual(result["artifact_count"], 2)
            self.assertEqual(verified_signatures, [
                "PixelsClient_official_3.3.72_Setup.exe",
                "px_panel.exe",
                "Uninstall.exe",
            ])

    def test_external_signer_pin_cannot_be_replaced_by_manifest_self_declaration(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            release_directory = self.create_release(Path(temporary_directory), "3.3.72", 30372)

            with self.assertRaisesRegex(RuntimeError, "externally approved certificate pin"):
                validate_release_directory(
                    release_directory,
                    self.accept_signature,
                    expected_signer_sha256="D" * 64,
                )


if __name__ == "__main__":
    unittest.main()
