from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.name == "nt", "Windows batch dispatcher test")
class BuildOfficialDispatcherTest(unittest.TestCase):
    def test_product_build_failure_is_returned_to_the_caller(self) -> None:
        product_exit_codes = {
            "cloud_node": 31,
            "client": 32,
            "remote": 33,
        }

        with tempfile.TemporaryDirectory() as temporary_directory:
            dispatcher_directory = Path(temporary_directory)
            shutil.copy2(
                REPOSITORY_ROOT / "scripts_build" / "build_official.bat",
                dispatcher_directory / "build_official.bat",
            )
            stub_scripts = {
                "cloud_node": "build_cloud_node.bat",
                "client": "build_client_product.bat",
                "remote": "build_remote_product.bat",
            }
            for product_name, stub_name in stub_scripts.items():
                (dispatcher_directory / stub_name).write_text(
                    f"@echo off\r\nexit /b {product_exit_codes[product_name]}\r\n",
                    encoding="ascii",
                    newline="",
                )

            for product_name, expected_exit_code in product_exit_codes.items():
                with self.subTest(product_name=product_name):
                    completed_process = subprocess.run(
                        ["cmd.exe", "/d", "/c", "build_official.bat", product_name],
                        cwd=dispatcher_directory,
                        check=False,
                        capture_output=True,
                        text=True,
                    )
                    self.assertEqual(completed_process.returncode, expected_exit_code)

    def test_invalid_arguments_return_usage_error(self) -> None:
        completed_process = subprocess.run(
            ["cmd.exe", "/d", "/c", str(REPOSITORY_ROOT / "scripts_build" / "build_official.bat"), "unknown"],
            cwd=REPOSITORY_ROOT,
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(completed_process.returncode, 2)
        self.assertIn("Usage:", completed_process.stdout)
