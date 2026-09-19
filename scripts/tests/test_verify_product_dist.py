from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


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
            "runtime/PX_TURN.EXE",
            "configuration/turnserver.conf",
            "plugins/libmk_api.dll",
        ):
            with self.subTest(retired_path=retired_path):
                with self.assertRaisesRegex(RuntimeError, "ZLMediaKit/Coturn"):
                    VERIFY_PRODUCT_DIST.verify_retired_central_media_absent({"px_render.exe", retired_path})

    def test_rejects_retired_component_directories(self) -> None:
        for retired_path in ("ZLMediaKit/config.ini", "third_party/coturn/LICENSE"):
            with self.subTest(retired_path=retired_path):
                with self.assertRaisesRegex(RuntimeError, "ZLMediaKit/Coturn"):
                    VERIFY_PRODUCT_DIST.verify_retired_central_media_absent({"px_render.exe", retired_path})


if __name__ == "__main__":
    unittest.main()
