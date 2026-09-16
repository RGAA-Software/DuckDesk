#!/usr/bin/env python3
"""Validate the fixed product boundaries encoded by the release manifests."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from set_product_version import load_manifest  # noqa: E402


EXPECTED_EDITIONS = {
    "cloud_node": "CLOUD_NODE",
    "client": "CLIENT",
    "remote": "REMOTE",
}
EXPECTED_CAPABILITIES = {
    "cloud_node": {
        "desktop_client",
        "desktop_host",
        "file_transfer",
        "rdp_client",
        "rdp_host",
        "cloud_app_catalog",
        "cloud_app_host",
        "game_hook",
        "webview_host",
        "browser_remote",
        "virtual_display",
        "joystick",
        "system_information",
    },
    "client": {
        "desktop_client",
        "file_transfer",
        "rdp_client",
        "cloud_app_catalog",
        "system_information",
    },
    "remote": {
        "desktop_client",
        "desktop_host",
        "file_transfer",
        "rdp_client",
        "rdp_host",
        "browser_remote",
        "virtual_display",
        "joystick",
        "system_information",
    },
    "android": {
        "desktop_client",
        "file_transfer",
        "rdp_client",
        "cloud_app_catalog",
        "system_information",
    },
}
EXPECTED_PACKAGE_GROUPS = {
    "cloud_node": {"desktop_client", "host_common", "rdp_host", "render_common", "browser_remote", "webview_host", "game_hook"},
    "client": {"desktop_client"},
    "remote": {"desktop_client", "host_common", "rdp_host", "render_common", "browser_remote"},
}


def main() -> int:
    for product, expected_capabilities in EXPECTED_CAPABILITIES.items():
        manifest = load_manifest(product)
        actual_capabilities = set(manifest["capabilities"])
        if actual_capabilities != expected_capabilities:
            missing = sorted(expected_capabilities - actual_capabilities)
            extra = sorted(actual_capabilities - expected_capabilities)
            raise RuntimeError(f"{product} capability mismatch; missing={missing}, extra={extra}")
        if manifest.get("company") != "Pixels":
            raise RuntimeError(f"{product} company must be Pixels")
        if product == "android":
            if "edition" in manifest:
                raise RuntimeError("Android must not declare a Windows Edition")
        elif manifest.get("edition") != EXPECTED_EDITIONS[product]:
            raise RuntimeError(f"{product} has an invalid Windows Edition")
        elif set(manifest.get("package_groups", [])) != EXPECTED_PACKAGE_GROUPS[product]:
            raise RuntimeError(f"{product} package group boundary is invalid")

    print("All four product manifests are valid and preserve the confirmed capability boundaries.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
