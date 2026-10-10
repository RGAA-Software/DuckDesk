"""Install or upgrade a complete Linux Relay package using a stable Compose project."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys


PACKAGE_FILES = {"relay-image.tar", "compose.yaml", "relay.example.json", ".env", "deploy.py"}


def sha256(file_path):
    digest = hashlib.sha256()
    with file_path.open("rb") as file_stream:
        for file_block in iter(lambda: file_stream.read(1024 * 1024), b""):
            digest.update(file_block)
    return digest.hexdigest()


def verify_package(package_directory):
    manifest = json.loads((package_directory / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("product") != "pixels-relay" or manifest.get("platform") != "linux-x86_64":
        raise ValueError("Expected a pixels-relay linux-x86_64 package")
    if not re.fullmatch(r"pixels-relay:[a-zA-Z0-9][a-zA-Z0-9_.-]*", manifest.get("image_tag", "")):
        raise ValueError("Invalid Relay image tag")
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", manifest.get("image_id", "")):
        raise ValueError("Invalid Relay image identity")
    if not re.fullmatch(r"[0-9a-f]{64}", manifest.get("runtime_sha256", "")):
        raise ValueError("Invalid Relay runtime hash")
    if set(manifest.get("files", {})) != PACKAGE_FILES:
        raise ValueError("Incomplete Relay package manifest")
    for file_name in sorted(PACKAGE_FILES):
        file_path = package_directory / file_name
        if file_path.is_symlink() or not file_path.is_file() or sha256(file_path) != manifest["files"][file_name]:
            raise ValueError("Package SHA-256 mismatch: " + file_name)
    return manifest


def docker(arguments, environment=None, timeout=60):
    command_result = subprocess.run(
        ["docker", *arguments], env=environment, capture_output=True, text=True, timeout=timeout, check=False
    )
    if command_result.returncode:
        # Docker errors can include mounted configuration; keep secrets out of deployment reports.
        raise RuntimeError("Docker command failed: " + " ".join(arguments[:2]))
    return command_result.stdout.strip()


def deploy(package_directory, config_directory, project_name, https_port, qad_port):
    manifest = verify_package(package_directory)
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", project_name):
        raise ValueError("Invalid Compose project name")
    if not all(1 <= port <= 65535 for port in (https_port, qad_port)):
        raise ValueError("Host ports must be between 1 and 65535")
    config_directory = config_directory.resolve(strict=True)
    for file_name in ("relay.json", "relay-control.env"):
        if not (config_directory / file_name).is_file():
            raise ValueError("Missing external configuration: " + file_name)
    relay_configuration = json.loads((config_directory / "relay.json").read_text(encoding="utf-8"))
    # The packaged Compose service exposes the fixed container ports; host ports may differ.
    for bind_field in ("https_bind", "qad_bind"):
        if relay_configuration.get(bind_field) not in ("0.0.0.0:4605", "[::]:4605"):
            raise ValueError(bind_field + " must listen on the packaged container port 4605")

    environment = os.environ.copy()
    environment.update({
        "PIXELS_RELAY_IMAGE": manifest["image_tag"],
        "PIXELS_RELAY_CONFIG_DIR": str(config_directory),
        "PIXELS_RELAY_HTTPS_PORT": str(https_port),
        "PIXELS_RELAY_QAD_PORT": str(qad_port),
    })
    compose = ["compose", "--project-name", project_name, "--env-file", str(package_directory / ".env"),
               "--file", str(package_directory / "compose.yaml")]
    # Validate Compose/config availability before loading an image or replacing an existing service.
    docker([*compose, "config", "--quiet"], environment)
    docker(["load", "--input", str(package_directory / "relay-image.tar")], timeout=300)
    image = json.loads(docker(["image", "inspect", manifest["image_tag"]]))[0]
    if image["Id"] != manifest["image_id"] or image["Os"] != "linux" or image["Architecture"] != "amd64":
        raise ValueError("Loaded image does not match the package identity/platform")
    runtime_digest = docker([
        "run", "--rm", "--network", "none", "--entrypoint", "sha256sum", manifest["image_id"],
        "/opt/pixels/bin/px_relay"
    ]).split()[0]
    if runtime_digest != manifest["runtime_sha256"]:
        raise ValueError("Loaded image runtime SHA-256 mismatch")
    docker([*compose, "up", "--detach", "--no-build", "--pull", "never", "relay"], environment, timeout=120)
    container_id = docker([*compose, "ps", "--all", "--quiet", "relay"], environment)
    if not container_id or len(container_id.splitlines()) != 1:
        raise RuntimeError("Expected exactly one Relay container for this project")
    container = json.loads(docker(["inspect", container_id]))[0]
    if container["Image"] != manifest["image_id"] or container["State"]["Status"] != "running":
        raise RuntimeError("Relay container is not running the packaged image")
    installed_digest = docker(["exec", container_id, "sha256sum", "/opt/pixels/bin/px_relay"]).split()[0]
    if installed_digest != manifest["runtime_sha256"]:
        raise ValueError("Running Relay runtime SHA-256 mismatch")
    return {
        "project": project_name, "container_id": container_id, "image_id": manifest["image_id"],
        "runtime_sha256": installed_digest, "state": "running",
        "console_readiness": "not_checked", "config_directory": str(config_directory),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true", help="Verify package files without calling Docker")
    parser.add_argument("--config-directory", type=Path, help="Existing configuration directory; never overwritten")
    parser.add_argument("--project-name", help="Stable Compose project name; use the existing name for upgrades")
    parser.add_argument("--https-port", type=int, default=4605)
    parser.add_argument("--qad-port", type=int, default=4605)
    arguments = parser.parse_args()
    package_directory = Path(__file__).resolve().parent
    if not arguments.verify_only and (not arguments.config_directory or not arguments.project_name):
        parser.error("Deployment requires --config-directory and --project-name")
    try:
        if arguments.verify_only:
            manifest = verify_package(package_directory)
            report = {"verified": True, "image_id": manifest["image_id"], "files": len(PACKAGE_FILES)}
        else:
            report = deploy(package_directory, arguments.config_directory, arguments.project_name,
                            arguments.https_port, arguments.qad_port)
        print(json.dumps(report, indent=2))
    except (OSError, ValueError, KeyError, IndexError, TypeError, RuntimeError, subprocess.TimeoutExpired) as error:
        print("Relay deployment failed: " + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
