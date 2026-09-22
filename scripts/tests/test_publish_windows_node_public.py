from __future__ import annotations

import importlib.util
import json
import unittest
from pathlib import Path
from unittest import mock

from scripts import publish_windows_node_public


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPOSITORY_ROOT / "scripts" / "public_console_identity.py"
SPECIFICATION = importlib.util.spec_from_file_location("public_console_identity", MODULE_PATH)
assert SPECIFICATION and SPECIFICATION.loader
PUBLIC_CONSOLE_IDENTITY = importlib.util.module_from_spec(SPECIFICATION)
SPECIFICATION.loader.exec_module(PUBLIC_CONSOLE_IDENTITY)


class FakeIdentityResponse:
    def __init__(self, identity: dict[str, str]) -> None:
        self.response_bytes = json.dumps(identity).encode("utf-8")

    def __enter__(self) -> FakeIdentityResponse:
        return self

    def __exit__(self, exception_type: object, exception: object, traceback: object) -> None:
        return None

    def read(self, maximum_bytes: int) -> bytes:
        return self.response_bytes[:maximum_bytes]


class ConsoleIdentityPreflightTest(unittest.TestCase):
    def test_accepts_only_current_identity_generation(self) -> None:
        response = FakeIdentityResponse(
            {
                "certificate_wire": "PXDC2.certificate.signature",
                "descriptor_wire": "PXDD2.descriptor.signature",
            }
        )
        with mock.patch.object(PUBLIC_CONSOLE_IDENTITY.urllib.request, "urlopen", return_value=response):
            PUBLIC_CONSOLE_IDENTITY.verify_current_console_identity("https://console.example:4600")

    def test_rejects_retired_identity_generation(self) -> None:
        response = FakeIdentityResponse(
            {
                "certificate_wire": "PXDC1.certificate.signature",
                "descriptor_wire": "PXDD1.descriptor.signature",
            }
        )
        with mock.patch.object(PUBLIC_CONSOLE_IDENTITY.urllib.request, "urlopen", return_value=response):
            with self.assertRaisesRegex(RuntimeError, "PXDC2"):
                PUBLIC_CONSOLE_IDENTITY.verify_current_console_identity("https://console.example:4600")

    def test_rejects_insecure_or_credentialed_origins_before_network_access(self) -> None:
        for invalid_origin in (
            "http://console.example:4600",
            "https://operator:secret@console.example:4600",
        ):
            with self.subTest(invalid_origin=invalid_origin):
                with self.assertRaisesRegex(RuntimeError, "HTTPS origin"):
                    PUBLIC_CONSOLE_IDENTITY.verify_current_console_identity(invalid_origin)


class NodePublishTransactionTest(unittest.TestCase):
    def test_service_start_failure_is_inside_the_rollback_transaction(self) -> None:
        deployment_script = publish_windows_node_public.service_script(
            "C:/Program Files/Pixels Cloud Node",
            "cloud_node",
            "development",
            "A" * 64,
            "B" * 64,
            "C" * 64,
        )
        transaction_body, rollback_and_cleanup = deployment_script.split("} catch {", 1)
        rollback_body, cleanup_body = rollback_and_cleanup.split("} finally {", 1)
        self.assertIn("if ($serviceWasRunning) { Start-InstalledService }", transaction_body)
        self.assertIn("px_service.exe", rollback_body)
        self.assertIn("px_service.toml", rollback_body)
        self.assertIn("product-manifest.json", rollback_body)
        self.assertIn("Start-InstalledService", rollback_body)
        self.assertNotIn("Start-InstalledService", cleanup_body)

    def test_render_and_rtc_are_rolled_back_before_the_old_service_restarts(self) -> None:
        deployment_script = publish_windows_node_public.render_script(
            "C:/Program Files/Pixels Cloud Node",
            "cloud_node",
            "development",
            "A" * 64,
            "B" * 64,
        )
        rollback_body = deployment_script.split("} catch {", 1)[1].split("} finally {", 1)[0]
        render_restore_offset = rollback_body.index("px_render.exe")
        rtc_restore_offset = rollback_body.index("px_render_rtc.dll")
        service_restart_offset = rollback_body.index("Start-InstalledService")
        self.assertLess(render_restore_offset, service_restart_offset)
        self.assertLess(rtc_restore_offset, service_restart_offset)


if __name__ == "__main__":
    unittest.main()
