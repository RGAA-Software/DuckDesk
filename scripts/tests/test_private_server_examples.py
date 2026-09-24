from __future__ import annotations

import json
import re
import unittest
from pathlib import Path


SOURCE_ROOT = Path(__file__).resolve().parents[2]
EXAMPLE_ROOT = SOURCE_ROOT / "deploy/private_server/examples"
EXAMPLE_DEPLOYMENT_ID = "11111111-1111-4111-8111-111111111111"


def environment_fields(example_name: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    for source_line in (EXAMPLE_ROOT / example_name).read_text(encoding="utf-8").splitlines():
        if not source_line or source_line.startswith("#"):
            continue
        field_name, separator, field_value = source_line.partition("=")
        if not separator or field_name in fields:
            raise ValueError(f"Invalid or duplicate environment field: {field_name}")
        fields[field_name] = field_value
    return fields


class PrivateServerExampleTests(unittest.TestCase):
    def test_examples_cover_current_required_environment_fields(self) -> None:
        runtime_sources = {
            "console.env.example": "rust_server/px_console_server/runtime/src/config.rs",
            "relay.env.example": "rust_server/px_relay_server/src/config.rs",
            "desk.env.example": "rust_server/px_desk_server/src/config.rs",
        }
        for example_name, runtime_source in runtime_sources.items():
            with self.subTest(example=example_name):
                source_text = (SOURCE_ROOT / runtime_source).read_text(encoding="utf-8")
                required_fields = set(re.findall(r'required\("([A-Z][A-Z0-9_]+)"\)', source_text))
                example_fields = environment_fields(example_name)
                self.assertTrue(required_fields, f"No required fields found in {runtime_source}")
                self.assertEqual(required_fields - set(example_fields), set())
                self.assertEqual(example_fields["PIXELS_DEPLOYMENT_ID"], EXAMPLE_DEPLOYMENT_ID)

    def test_backup_example_is_non_operational_and_matches_private_layout(self) -> None:
        backup_example = json.loads((EXAMPLE_ROOT / "backup.json.example").read_text(encoding="utf-8"))
        self.assertEqual(backup_example["schema_version"], 2)
        self.assertEqual(backup_example["deployment_id"], EXAMPLE_DEPLOYMENT_ID)
        self.assertEqual(backup_example["schedule"]["deployment_id"], EXAMPLE_DEPLOYMENT_ID)
        self.assertEqual(backup_example["plan"]["deployment_id"], EXAMPLE_DEPLOYMENT_ID)
        self.assertEqual(backup_example["pg_dump_sha256"], "0" * 64)
        self.assertEqual(backup_example["pg_restore_sha256"], "0" * 64)
        self.assertEqual([target["state"] for target in backup_example["plan"]["targets"]],
                         ["required", "not_applicable", "required"])
        for target in backup_example["plan"]["targets"]:
            if target["state"] == "required":
                self.assertEqual(target["database"]["schema_version"], 0)
                self.assertIn(f"/etc/pixels/{EXAMPLE_DEPLOYMENT_ID}/backup/", target["database"]["password_file"])

    def test_public_examples_cannot_be_used_as_product_credentials(self) -> None:
        console_fields = environment_fields("console.env.example")
        relay_fields = environment_fields("relay.env.example")
        desk_fields = environment_fields("desk.env.example")
        self.assertEqual(console_fields["PIXELS_RELAY_APP_KEY"], "REPLACE")
        self.assertEqual(relay_fields["PIXELS_RELAY_APP_KEY"], "REPLACE")
        self.assertEqual(relay_fields["PIXELS_RELAY_CONTROL_KEY"], "REPLACE")
        self.assertEqual(relay_fields["PIXELS_RELAY_NODE_TOKEN"], "REPLACE")
        self.assertEqual(desk_fields["PIXELS_DESK_ADMIN_TOKEN_SHA256"], "REPLACE")
        relay_console_origin = relay_fields["PIXELS_RELAY_CONSOLE_CONTROL_URL"].split("/api/", 1)[0]
        self.assertEqual(console_fields["PIXELS_CONSOLE_PUBLIC_ORIGIN"],
                         relay_console_origin.replace("wss://", "https://", 1))


if __name__ == "__main__":
    unittest.main()
