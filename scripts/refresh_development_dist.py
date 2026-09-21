#!/usr/bin/env python3
"""Refresh hashes in an existing runnable development product distribution."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

try:
    from scripts.collect_dist import GENERATED_MANIFESTS, sha256, write_json_atomic
except ModuleNotFoundError:
    from collect_dist import GENERATED_MANIFESTS, sha256, write_json_atomic


RUNTIME_OUTPUT_DIRECTORIES = {"px_logs"}


def is_runtime_output(relative_path: str) -> bool:
    parts = Path(relative_path).parts
    return bool(parts) and parts[0].lower() in RUNTIME_OUTPUT_DIRECTORIES


def distribution_files(distribution: Path) -> list[Path]:
    return sorted(
        path
        for path in distribution.rglob("*")
        if path.is_file()
        and path.name not in GENERATED_MANIFESTS
        and not is_runtime_output(path.relative_to(distribution).as_posix())
    )


def refresh(distribution: Path) -> dict[str, str]:
    distribution = distribution.resolve()
    manifest_path = distribution / "product-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 2 or manifest.get("distribution") != "development":
        raise RuntimeError("only a schema 2 development distribution can be refreshed in place")
    product = manifest.get("product")
    if product not in {"client", "cloud_node", "remote"}:
        raise RuntimeError("development distribution has an invalid Windows product identity")
    if distribution.name != "dist" or distribution.parent.name != product:
        raise RuntimeError("development distribution path does not match its product identity")

    hashes = {
        path.relative_to(distribution).as_posix(): sha256(path)
        for path in distribution_files(distribution)
    }
    licenses = sorted(path for path in hashes if "license" in path.lower() or path.endswith("SOURCE.md"))
    manifest["artifacts"] = [{"path": path, "sha256": digest} for path, digest in hashes.items()]
    write_json_atomic(distribution / "sha256sums.json", hashes)
    write_json_atomic(distribution / "licenses.json", {"files": licenses})
    write_json_atomic(manifest_path, manifest)
    return hashes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dist_dir", type=Path)
    arguments = parser.parse_args()
    hashes = refresh(arguments.dist_dir)
    print(f"Refreshed development distribution manifest: {arguments.dist_dir} ({len(hashes)} artifacts).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
