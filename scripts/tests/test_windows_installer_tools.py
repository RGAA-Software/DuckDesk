from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts.collect_dist import collect_artifacts
from setup.make_setup import find_nsis, nsis_version, require_supported_nsis, stage_payload, validate_pinned_nsis


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


class WindowsInstallerToolTest(unittest.TestCase):
    def test_repository_relative_nsis_configuration_resolves_from_setup_directory(self) -> None:
        configured_tool = find_nsis("../tools/nsis", REPOSITORY_ROOT)
        self.assertEqual(configured_tool, (REPOSITORY_ROOT / "tools" / "nsis" / "makensis.exe").resolve())

    def test_repository_nsis_matches_the_pinned_release_toolchain(self) -> None:
        validate_pinned_nsis(REPOSITORY_ROOT, REPOSITORY_ROOT / "tools" / "nsis" / "makensis.exe")

    def test_nsis_version_parses_a_semantic_tool_version(self) -> None:
        completed = mock.Mock(stdout="v3.12\n")
        with mock.patch("setup.make_setup.subprocess.run", return_value=completed):
            self.assertEqual(nsis_version(Path("makensis.exe")), (3, 12))

    def test_signed_uninstaller_rejects_the_retired_nsis_toolchain(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            makensis = Path(temporary_directory) / "makensis.exe"
            makensis.write_bytes(b"test tool")
            completed = mock.Mock(stdout="v3.06.1\n")
            with mock.patch("setup.make_setup.subprocess.run", return_value=completed):
                with self.assertRaisesRegex(RuntimeError, "NSIS 3.11 or newer"):
                    require_supported_nsis(makensis)

    def test_staged_payload_is_an_exact_recursive_copy(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            test_root = Path(temporary_directory)
            distribution = test_root / "dist"
            (distribution / "resources").mkdir(parents=True)
            (distribution / "px_panel.exe").write_bytes(b"panel")
            (distribution / "resources" / "catalog.json").write_bytes(b"catalog")

            payload = test_root / "staging" / "app"
            stage_payload(distribution, payload)

            self.assertEqual((payload / "px_panel.exe").read_bytes(), b"panel")
            self.assertEqual((payload / "resources" / "catalog.json").read_bytes(), b"catalog")

    def test_tree_artifact_marks_only_declared_pixels_pe_as_owned(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            test_root = Path(temporary_directory)
            source_directory = test_root / "source" / "rdp"
            source_directory.mkdir(parents=True)
            (source_directory / "px_rdp_core.dll").write_bytes(b"pixels")
            (source_directory / "libcrypto.dll").write_bytes(b"third party")
            staging_directory = test_root / "staging"
            staging_directory.mkdir()
            product_config = {"package_groups": ["rdp"]}
            artifact_config = {
                "groups": {
                    "rdp": [
                        {
                            "root": "source",
                            "source": "rdp",
                            "destination": "rdp",
                            "kind": "tree",
                            "include": ["px_rdp_core.dll", "libcrypto.dll"],
                            "owned_pe_include": ["px_rdp_core.dll"],
                        }
                    ]
                }
            }

            owned_pe = collect_artifacts(
                product_config,
                artifact_config,
                {"source": test_root / "source"},
                staging_directory,
            )

            self.assertEqual(owned_pe, ["rdp/px_rdp_core.dll"])


if __name__ == "__main__":
    unittest.main()
