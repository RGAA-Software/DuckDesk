#!/usr/bin/env python3
"""Validate Pixels-owned product-facing brand metadata."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


TEXT_PRODUCT_FILES = (
    "cmake/product_icon.rc.in",
    "src/px_base/icon.rc.in",
    "src/px_client/icon.rc.in",
    "src/px_render/rd_icon.rc.in",
    "src/px_render/network/webrtc/webrtc_transport_types.h",
    "src/px_render/architecture/modules/render_module.cpp",
    "src/px_panel/ui_imgui/about_settings_page.cpp",
    "setup/make_setup.nsi",
    "src/px_android/app/build.gradle",
    "src/px_android/scripts/build_release.ps1",
)

REQUIRED_BINARY_FILES = ("src/px_panel/icon.ico",)

RETIRED_BRAND = re.compile(r"rgaa", re.IGNORECASE)


def validate(repo_root: Path) -> list[str]:
    errors: list[str] = []
    for relative_name in TEXT_PRODUCT_FILES:
        path = repo_root / relative_name
        if not path.is_file():
            errors.append(f"missing product branding input: {relative_name}")
            continue
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            if RETIRED_BRAND.search(line):
                errors.append(f"retired RGAA product branding: {relative_name}:{line_number}")
    for relative_name in REQUIRED_BINARY_FILES:
        if not (repo_root / relative_name).is_file():
            errors.append(f"missing product branding input: {relative_name}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()

    errors = validate(args.repo_root.resolve())
    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1
    input_count = len(TEXT_PRODUCT_FILES) + len(REQUIRED_BINARY_FILES)
    print(f"OK: Pixels branding validated across {input_count} product-facing inputs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
