from __future__ import annotations

import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
NINJA_ENTRY_POINTS = (
    "scripts/build_cpp_target.bat",
    "scripts/build_miniaudio_pid_test.bat",
    "scripts_build/build_cpp_sdk_standalone.bat",
    "scripts_build/build_cpp_workspace_demo.bat",
    "scripts_build/build_official_tests.bat",
)


class NinjaMsvcCodepageTests(unittest.TestCase):
    def test_windows_ninja_entry_points_select_utf8_console(self) -> None:
        for relative_path in NINJA_ENTRY_POINTS:
            with self.subTest(entry_point=relative_path):
                script_text = (REPOSITORY_ROOT / relative_path).read_text(encoding="utf-8")
                self.assertIn("chcp 65001 >nul", script_text)
                self.assertIn("-G Ninja", script_text)


if __name__ == "__main__":
    unittest.main()
