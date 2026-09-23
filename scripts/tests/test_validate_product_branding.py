from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "validate_product_branding.py"
MODULE_SPEC = importlib.util.spec_from_file_location("validate_product_branding", SCRIPT_PATH)
assert MODULE_SPEC is not None and MODULE_SPEC.loader is not None
BRANDING_MODULE = importlib.util.module_from_spec(MODULE_SPEC)
MODULE_SPEC.loader.exec_module(BRANDING_MODULE)


class ValidateProductBrandingTests(unittest.TestCase):
    def create_required_inputs(self, repository_root: Path) -> None:
        for relative_name in BRANDING_MODULE.TEXT_PRODUCT_FILES:
            input_path = repository_root / relative_name
            input_path.parent.mkdir(parents=True, exist_ok=True)
            input_path.write_text("Pixels\n", encoding="utf-8")
        for relative_name in BRANDING_MODULE.REQUIRED_BINARY_FILES:
            input_path = repository_root / relative_name
            input_path.parent.mkdir(parents=True, exist_ok=True)
            input_path.write_bytes(b"Pixels icon")

    def test_current_branding_inputs_pass(self) -> None:
        with tempfile.TemporaryDirectory() as repository_directory:
            repository_root = Path(repository_directory)
            self.create_required_inputs(repository_root)

            self.assertEqual([], BRANDING_MODULE.validate(repository_root))

    def test_missing_panel_icon_fails_without_retired_rc_template(self) -> None:
        with tempfile.TemporaryDirectory() as repository_directory:
            repository_root = Path(repository_directory)
            self.create_required_inputs(repository_root)
            panel_icon_path = repository_root / "src/px_panel/icon.ico"
            panel_icon_path.unlink()

            self.assertEqual(
                ["missing product branding input: src/px_panel/icon.ico"],
                BRANDING_MODULE.validate(repository_root),
            )

    def test_retired_brand_in_text_input_fails(self) -> None:
        with tempfile.TemporaryDirectory() as repository_directory:
            repository_root = Path(repository_directory)
            self.create_required_inputs(repository_root)
            product_template_path = repository_root / BRANDING_MODULE.TEXT_PRODUCT_FILES[0]
            product_template_path.write_text("CompanyName=RGAA\n", encoding="utf-8")

            errors = BRANDING_MODULE.validate(repository_root)

            self.assertEqual(1, len(errors))
            self.assertIn("retired RGAA product branding", errors[0])


if __name__ == "__main__":
    unittest.main()
