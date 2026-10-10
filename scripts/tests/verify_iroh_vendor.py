"""Verify the reviewed, version-pinned isolated iroh source and patch identity."""
import hashlib
import json
from pathlib import Path


def main():
    repository = Path(__file__).resolve().parents[2]
    patch_directory = repository / "patches/iroh"
    manifest = json.loads((patch_directory / "manifest.json").read_text(encoding="utf-8"))
    source_directory = repository / "rust_transport/vendor/iroh-1.3.0"
    for relative, expected_hash in manifest["isolated_files"].items():
        actual_hash = hashlib.sha256((source_directory / relative).read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            raise RuntimeError("Isolated iroh source mismatch: " + relative)
    patch_hash = hashlib.sha256((patch_directory / "0001-drain-disconnected-relay-queues.patch").read_bytes()).hexdigest()
    if patch_hash != manifest["patch_sha256"]:
        raise RuntimeError("Reviewed iroh patch mismatch")
    print(f"Verified iroh {manifest['version']}: {len(manifest['isolated_files'])} files and reviewed patch")


if __name__ == "__main__":
    main()
