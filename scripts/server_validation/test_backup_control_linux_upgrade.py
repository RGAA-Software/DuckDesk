#!/usr/bin/env python3
"""Exercise the packaged Linux configuration upgrade without starting business services."""

from __future__ import annotations

import json
import os
import re
import subprocess
import tempfile
import uuid
from pathlib import Path


IMAGE_NAME = os.environ.get("PIXELS_BACKUP_CONTROL_IMAGE", "pixels-server:1.0.7")
DEPLOYMENT_ID = "c4fdfa68-d9d4-4e15-9883-344bd84c415c"


def run_upgrade(configuration_root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            "docker", "run", "--rm", "--user", "0:0", "--mount",
            f"type=bind,source={configuration_root},target=/etc/pixels",
            "--entrypoint", "/opt/pixels/bin/px_console_admin", IMAGE_NAME,
            "upgrade-backup-control", "/etc/pixels", "linux",
        ],
        capture_output=True, text=True, check=False,
    )


def checked_docker(arguments: list[str]) -> str:
    result = subprocess.run(["docker", *arguments], capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError(f"Docker validation command failed: {result.stderr[-500:]}")
    return result.stdout.strip()


def main() -> None:
    if os.name != "nt" or os.environ.get("PIXELS_BACKUP_CONTROL_LINUX_TEST") != "1":
        raise SystemExit("Run on the Docker Desktop host with PIXELS_BACKUP_CONTROL_LINUX_TEST=1")
    with tempfile.TemporaryDirectory(prefix="PixelsBackupLinuxUpgrade-") as temporary_directory:
        configuration_root = Path(temporary_directory).resolve()
        (configuration_root / "setup.complete").write_text("ready\n", encoding="utf-8")
        (configuration_root / "console-ca.crt").write_text("test-ca\n", encoding="utf-8")
        (configuration_root / "console.env").write_text(
            f"PIXELS_DEPLOYMENT_ID={DEPLOYMENT_ID}\n", encoding="utf-8",
        )
        (configuration_root / "relay.env").write_text(
            f"PIXELS_DEPLOYMENT_ID={DEPLOYMENT_ID}\n"
            "PIXELS_RELAY_CONSOLE_CA_FILE=/etc/pixels/console-ca.crt\n",
            encoding="utf-8",
        )
        (configuration_root / "backup.json").write_text(
            json.dumps({"deployment_id": DEPLOYMENT_ID}), encoding="utf-8",
        )
        initial_result = run_upgrade(configuration_root)
        if initial_result.returncode:
            raise RuntimeError(f"Initial Linux upgrade failed: {initial_result.stderr[-500:]}")
        console_path = configuration_root / "console.env"
        backup_path = configuration_root / "backup.json"
        upgraded_console = console_path.read_text(encoding="utf-8")
        upgraded_backup = backup_path.read_text(encoding="utf-8")
        control_token = re.search(r"(?m)^PIXELS_CONSOLE_BACKUP_CONTROL_TOKEN=([0-9a-f]{64})$", upgraded_console)
        backup_control = json.loads(upgraded_backup)["control"]
        if not control_token or backup_control != {
            "console_url": "wss://console:4600/api/console/backup-control",
            "console_ca_file": "/etc/pixels/console-ca.crt",
            "token": control_token.group(1),
        }:
            raise RuntimeError("Linux upgrade produced inconsistent control configuration")
        repeat_result = run_upgrade(configuration_root)
        if repeat_result.returncode or console_path.read_text(encoding="utf-8") != upgraded_console \
                or backup_path.read_text(encoding="utf-8") != upgraded_backup:
            raise RuntimeError("Repeated Linux upgrade changed the control token")
        console_path.write_text(f"PIXELS_DEPLOYMENT_ID={DEPLOYMENT_ID}\n", encoding="utf-8")
        repair_result = run_upgrade(configuration_root)
        if repair_result.returncode or console_path.read_text(encoding="utf-8") != upgraded_console:
            raise RuntimeError("Partial Linux upgrade did not preserve the Backup token")
        console_path.write_text(upgraded_console.replace(control_token.group(1), "0" * 64), encoding="utf-8")
        conflict_result = run_upgrade(configuration_root)
        if conflict_result.returncode == 0:
            raise RuntimeError("Conflicting Linux control tokens were accepted")
        console_path.write_text(f"PIXELS_DEPLOYMENT_ID={DEPLOYMENT_ID}\n", encoding="utf-8")
        backup_path.write_text(json.dumps({"deployment_id": DEPLOYMENT_ID}), encoding="utf-8")
        named_volume = f"pixels-backup-upgrade-{uuid.uuid4().hex[:12]}"
        checked_docker(["volume", "create", "--label", "pixels.validation=backup-control-linux", named_volume])
        try:
            checked_docker([
                "run", "--rm", "--user", "0:0",
                "--mount", f"type=bind,source={configuration_root},target=/fixture,readonly",
                "--mount", f"type=volume,source={named_volume},target=/etc/pixels",
                "--entrypoint", "/bin/cp", IMAGE_NAME, "-a", "/fixture/.", "/etc/pixels/",
            ])
            checked_docker([
                "run", "--rm", "--user", "0:0",
                "--mount", f"type=volume,source={named_volume},target=/etc/pixels",
                "--entrypoint", "/opt/pixels/bin/px_console_admin", IMAGE_NAME,
                "upgrade-backup-control", "/etc/pixels", "linux",
            ])
            with tempfile.TemporaryDirectory(prefix="PixelsBackupLinuxVolumeRead-") as output_directory:
                checked_docker([
                    "run", "--rm", "--user", "0:0",
                    "--mount", f"type=volume,source={named_volume},target=/etc/pixels,readonly",
                    "--mount", f"type=bind,source={Path(output_directory).resolve()},target=/output",
                    "--entrypoint", "/bin/cp", IMAGE_NAME, "-a", "/etc/pixels/.", "/output/",
                ])
                volume_console = (Path(output_directory) / "console.env").read_text(encoding="utf-8")
                volume_backup = json.loads((Path(output_directory) / "backup.json").read_text(encoding="utf-8"))
                volume_token = re.search(r"(?m)^PIXELS_CONSOLE_BACKUP_CONTROL_TOKEN=([0-9a-f]{64})$", volume_console)
                if not volume_token or volume_backup["control"]["token"] != volume_token.group(1):
                    raise RuntimeError("Docker named-volume upgrade produced inconsistent control configuration")
        finally:
            volume_label = checked_docker([
                "volume", "inspect", named_volume, "--format", '{{ index .Labels "pixels.validation" }}',
            ])
            if volume_label != "backup-control-linux":
                raise RuntimeError("Refusing to remove an unexpected Docker volume")
            checked_docker(["volume", "rm", named_volume])
        print("PASS Linux image initializes, preserves, repairs, and rejects conflicting control configuration")
        print("PASS Linux image upgrades the same configuration from a Docker named volume")


if __name__ == "__main__":
    main()
