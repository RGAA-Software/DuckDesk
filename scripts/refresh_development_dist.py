#!/usr/bin/env python3
"""Create or refresh the manifest of a focused development distribution."""

from __future__ import annotations

import argparse
import json
import tomllib
from pathlib import Path, PurePosixPath

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


def create_development_manifest(distribution: Path, product_config: dict[str, object]) -> dict[str, object]:
    stamp_path = distribution.parent / "product-build.json"
    build_stamp = json.loads(stamp_path.read_text(encoding="utf-8"))
    product = product_config["product"]
    expected_identity = {
        "schema_version": 2,
        "product": product,
        "distribution": "development",
        "release_namespace": None,
        "oem_id": None,
        "oem_profile_sha256": None,
        "edition": product_config["edition"],
        "company": "Pixels",
        "product_version": product_config["product_version"],
        "product_version_code": product_config["product_version_code"],
        "cmake_binary_dir": f"build_official/{product}/cmake",
    }
    if any(build_stamp.get(identity_field) != expected_value for identity_field, expected_value in expected_identity.items()):
        raise RuntimeError("cannot create a development manifest from a mismatched product build stamp")
    return {
        "schema_version": 4,
        "product": product,
        "distribution": "development",
        "release_namespace": None,
        "oem_id": None,
        "oem_profile_sha256": None,
        "company": "Pixels",
        "git_revision": build_stamp["git_revision"],
        "windows_code_signing": "unsigned",
    }


def owned_pe_inventory(product_config: dict[str, object], relative_paths: set[str]) -> list[str]:
    artifact_config_path = Path(__file__).resolve().parents[1] / "packaging" / "artifact_groups.toml"
    with artifact_config_path.open("rb") as configuration_file:
        artifact_config = tomllib.load(configuration_file)
    declared_paths: set[str] = set()
    for group_name in product_config["package_groups"]:
        for artifact in artifact_config["groups"][group_name]:
            destination = PurePosixPath(artifact["destination"])
            if artifact.get("owned_pe", False):
                declared_paths.add(destination.as_posix())
            for owned_name in artifact.get("owned_pe_include", []):
                declared_paths.add((destination / owned_name).as_posix())
    return sorted(declared_paths & relative_paths)


def refresh(distribution: Path) -> dict[str, str]:
    distribution = distribution.resolve()
    product = distribution.parent.name
    if product not in {"client", "cloud_node", "remote"}:
        raise RuntimeError("development distribution has an invalid Windows product identity")
    if distribution.name != "dist":
        raise RuntimeError("development distribution path does not match its product identity")

    product_config_path = Path(__file__).resolve().parents[1] / "packaging" / "products" / f"{product}.toml"
    with product_config_path.open("rb") as configuration_file:
        product_config = tomllib.load(configuration_file)
    if product_config.get("product") != product:
        raise RuntimeError("development product configuration does not match its distribution")
    manifest_path = distribution / "product-manifest.json"
    manifest = (
        json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest_path.is_file()
        else create_development_manifest(distribution, product_config)
    )
    if manifest.get("schema_version") != 4 or manifest.get("distribution") != "development":
        raise RuntimeError("only a schema 4 development distribution can be refreshed in place")
    if manifest.get("product") != product or manifest.get("company") != "Pixels" or any(
        manifest.get(identity_field) is not None for identity_field in ("release_namespace", "oem_id", "oem_profile_sha256")
    ):
        raise RuntimeError("development distribution manifest has a mismatched product identity")
    manifest["edition"] = product_config["edition"]
    manifest["product_version"] = product_config["product_version"]
    manifest["product_version_code"] = product_config["product_version_code"]
    manifest["capabilities"] = product_config["capabilities"]
    manifest["package_groups"] = product_config["package_groups"]
    manifest["windows_code_signing"] = "unsigned"

    hashes = {
        path.relative_to(distribution).as_posix(): sha256(path)
        for path in distribution_files(distribution)
    }
    licenses = sorted(path for path in hashes if "license" in path.lower() or path.endswith("SOURCE.md"))
    manifest["owned_pe"] = owned_pe_inventory(product_config, set(hashes))
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
