from __future__ import annotations

import unittest

from scripts import publish_windows_node_public

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
