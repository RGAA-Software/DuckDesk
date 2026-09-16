#!/usr/bin/env python3
"""Build an atomic product distribution from explicit artifact declarations."""

from __future__ import annotations

import argparse
import atexit
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import tomllib
import uuid
from pathlib import Path


GENERATED_MANIFESTS = {"product-manifest.json", "sha256sums.json", "licenses.json"}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def write_json_atomic(path: Path, value: object) -> None:
    descriptor, temporary_name = tempfile.mkstemp(prefix=".manifest-", suffix=".tmp", dir=path.parent)
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as output:
            json.dump(value, output, ensure_ascii=False, indent=2)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_path, path)
    finally:
        temporary_path.unlink(missing_ok=True)


def checked_relative_path(value: str, field: str) -> Path:
    path = Path(value)
    if path.is_absolute() or ".." in path.parts:
        raise RuntimeError(f"unsafe {field}: {value}")
    return path


def copy_file(source: Path, destination: Path, staging_dir: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    source_digest = sha256(source)
    destination_digest = sha256(destination)
    if source_digest != destination_digest:
        raise RuntimeError(
            f"artifact hash mismatch after copy: {source} -> {destination} "
            f"({source_digest} != {destination_digest})"
        )
    print(f"  + {destination.relative_to(staging_dir).as_posix()}")


def matching_tree_files(source: Path, patterns: list[str]) -> list[Path]:
    matches: set[Path] = set()
    for pattern in patterns:
        matches.update(path for path in source.glob(pattern) if path.is_file())
    return sorted(matches, key=lambda path: path.relative_to(source).as_posix())


def collect_artifacts(
    product_config: dict[str, object],
    artifact_config: dict[str, object],
    roots: dict[str, Path],
    staging_dir: Path,
) -> list[str]:
    selected_groups = product_config.get("package_groups")
    if not isinstance(selected_groups, list) or not selected_groups:
        raise RuntimeError("Windows product manifest must declare non-empty package_groups")
    groups = artifact_config.get("groups")
    if not isinstance(groups, dict):
        raise RuntimeError("artifact_groups.toml does not contain a groups table")

    written_by: dict[str, str] = {}
    owned_pe: list[str] = []
    for group_name in selected_groups:
        entries = groups.get(group_name)
        if not isinstance(entries, list) or not entries:
            raise RuntimeError(f"unknown or empty package group: {group_name}")
        for entry in entries:
            root_name = entry.get("root")
            if root_name not in roots:
                raise RuntimeError(f"artifact {group_name} has invalid root: {root_name}")
            source_relative = checked_relative_path(entry["source"], "artifact source")
            destination_relative = checked_relative_path(entry["destination"], "artifact destination")
            source = roots[root_name] / source_relative
            optional = bool(entry.get("optional", False))
            if not source.exists():
                if optional:
                    print(f"  - optional artifact absent: {source}")
                    continue
                raise RuntimeError(f"required artifact is missing: {source}")

            kind = entry.get("kind", "file")
            if kind == "file":
                if not source.is_file():
                    raise RuntimeError(f"artifact source is not a file: {source}")
                sources = [(source, destination_relative)]
            elif kind == "tree":
                patterns = entry.get("include")
                if not isinstance(patterns, list) or not patterns:
                    raise RuntimeError(f"tree artifact must declare include patterns: {source}")
                files = matching_tree_files(source, patterns)
                if not files:
                    raise RuntimeError(f"tree artifact matched no files: {source}")
                sources = [(path, destination_relative / path.relative_to(source)) for path in files]
            else:
                raise RuntimeError(f"unsupported artifact kind {kind}: {source}")

            for source_file, relative_output in sources:
                output_name = relative_output.as_posix()
                if output_name in GENERATED_MANIFESTS:
                    raise RuntimeError(f"artifact collides with generated manifest: {output_name}")
                if output_name in written_by and not entry.get("replace", False):
                    raise RuntimeError(
                        f"duplicate artifact destination {output_name}: {written_by[output_name]} and {group_name}"
                    )
                copy_file(source_file, staging_dir / relative_output, staging_dir)
                written_by[output_name] = group_name
                if entry.get("owned_pe", False):
                    owned_pe.append(output_name)
    return sorted(set(owned_pe))


def write_distribution_manifests(
    source_dir: Path,
    product_config: dict[str, object],
    staging_dir: Path,
    owned_pe: list[str],
) -> None:
    revision = subprocess.run(
        ["git", "-C", source_dir, "rev-parse", "--short=12", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    hashes = {
        path.relative_to(staging_dir).as_posix(): sha256(path)
        for path in sorted(staging_dir.rglob("*"))
        if path.is_file() and path.name not in GENERATED_MANIFESTS
    }
    licenses = sorted(path for path in hashes if "license" in path.lower() or path.endswith("SOURCE.md"))
    manifest = {
        "schema_version": 2,
        "product": product_config["product"],
        "edition": product_config["edition"],
        "company": product_config["company"],
        "product_version": product_config["product_version"],
        "product_version_code": product_config["product_version_code"],
        "capabilities": product_config["capabilities"],
        "package_groups": product_config["package_groups"],
        "git_revision": revision,
        "owned_pe": owned_pe,
        "artifacts": [{"path": path, "sha256": digest} for path, digest in hashes.items()],
    }
    write_json_atomic(staging_dir / "sha256sums.json", hashes)
    write_json_atomic(staging_dir / "licenses.json", {"files": licenses})
    write_json_atomic(staging_dir / "product-manifest.json", manifest)


def publish_staging_directory(staging_dir: Path, final_dir: Path) -> None:
    backup_dir = final_dir.with_name(f"{final_dir.name}.previous-{uuid.uuid4().hex}")
    previous_moved = False
    try:
        if final_dir.is_dir():
            os.replace(final_dir, backup_dir)
            previous_moved = True
        os.replace(staging_dir, final_dir)
    except Exception:
        if previous_moved and not final_dir.exists() and backup_dir.is_dir():
            os.replace(backup_dir, final_dir)
        raise
    if previous_moved:
        shutil.rmtree(backup_dir)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path, help="Product CMake binary directory")
    parser.add_argument("--source-dir", required=True, type=Path, help="Repository source root")
    parser.add_argument("--product", choices=("cloud_node", "client", "remote"), required=True)
    parser.add_argument("--dist-dir", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.resolve()
    source_dir = args.source_dir.resolve()
    final_dir = args.dist_dir.resolve()
    if final_dir in {source_dir, build_dir} or final_dir.parent == final_dir:
        raise RuntimeError(f"unsafe distribution target: {final_dir}")

    with (source_dir / "packaging" / "products" / f"{args.product}.toml").open("rb") as source:
        product_config = tomllib.load(source)
    with (source_dir / "packaging" / "artifact_groups.toml").open("rb") as source:
        artifact_config = tomllib.load(source)
    if product_config.get("product") != args.product:
        raise RuntimeError(f"product manifest identity mismatch: {args.product}")

    roots = {
        "source": source_dir,
        "build": build_dir,
        "rust_shared": source_dir / "build_official" / "shared" / "rust",
        "rdp_sdk": source_dir / ".cache" / "rdp_sdk",
        "rdp_policy": source_dir / ".cache" / "rdp_policy_build" / "Release",
    }
    final_dir.parent.mkdir(parents=True, exist_ok=True)
    staging_dir = Path(tempfile.mkdtemp(prefix=f".{final_dir.name}.staging-", dir=final_dir.parent))

    def cleanup() -> None:
        if staging_dir.is_dir():
            shutil.rmtree(staging_dir)

    atexit.register(cleanup)
    owned_pe = collect_artifacts(product_config, artifact_config, roots, staging_dir)
    write_distribution_manifests(source_dir, product_config, staging_dir, owned_pe)
    publish_staging_directory(staging_dir, final_dir)
    atexit.unregister(cleanup)
    print(f"Done: {args.product} distribution published to {final_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
