"""Publish the focused PX Console build to the configured public test host."""

from __future__ import annotations

import base64
import hashlib
import json
import re
from pathlib import Path

import paramiko


ROOT = Path(__file__).resolve().parent.parent
MACHINE_FILE = ROOT / ".env" / "test_machine.md"
SOURCE_EXE = ROOT / "output" / "px_console" / "px_console.exe"
SOURCE_CONFIG = ROOT / "px_console.toml"
REMOTE_DIRECTORY = "D:/software/esprit_169811/console"


def machine_value(text: str, label: str) -> str:
    match = re.search(rf"^\s*-\s*{re.escape(label)}\s*[:：]\s*(.+?)\s*$", text, re.MULTILINE | re.IGNORECASE)
    if not match:
        raise RuntimeError(f"Missing {label} in the test-machine document")
    return match.group(1)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def encoded_powershell(script: str) -> str:
    return base64.b64encode(script.encode("utf-16-le")).decode("ascii")


def run_powershell(client: paramiko.SSHClient, script: str) -> str:
    command = f"powershell.exe -NoProfile -NonInteractive -EncodedCommand {encoded_powershell(script)}"
    _, stdout, stderr = client.exec_command(command, timeout=60)
    status = stdout.channel.recv_exit_status()
    output = stdout.read().decode("utf-8", errors="replace").strip()
    error = stderr.read().decode("utf-8", errors="replace").strip()
    if status != 0:
        raise RuntimeError(error or output or f"Remote command exited with {status}")
    return output


def main() -> None:
    for source in (MACHINE_FILE, SOURCE_EXE, SOURCE_CONFIG):
        if not source.is_file():
            raise RuntimeError(f"Required deployment input is missing: {source}")
    machine_text = MACHINE_FILE.read_text(encoding="utf-8")
    host = machine_value(machine_text, "地址")
    port = int(machine_value(machine_text, "SSH 端口").split("，", 1)[0])
    username = machine_value(machine_text, "用户名")
    password = machine_value(machine_text, "密码")
    expected_exe_hash = sha256(SOURCE_EXE)
    expected_config_hash = sha256(SOURCE_CONFIG)

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(host, port=port, username=username, password=password, timeout=10, banner_timeout=10, auth_timeout=10)
    try:
        with client.open_sftp() as sftp:
            sftp.put(str(SOURCE_EXE), f"{REMOTE_DIRECTORY}/px_console.staged.exe")
            sftp.put(str(SOURCE_CONFIG), f"{REMOTE_DIRECTORY}/px_console.staged.toml")
        script = f"""
$ErrorActionPreference = 'Stop'
$directory = '{REMOTE_DIRECTORY}'
$targetExe = Join-Path $directory 'px_console.exe'
$targetConfig = Join-Path $directory 'px_console.toml'
$stagedExe = Join-Path $directory 'px_console.staged.exe'
$stagedConfig = Join-Path $directory 'px_console.staged.toml'
if ((Get-FileHash -LiteralPath $stagedExe -Algorithm SHA256).Hash -ne '{expected_exe_hash}' -or
    (Get-FileHash -LiteralPath $stagedConfig -Algorithm SHA256).Hash -ne '{expected_config_hash}') {{ throw 'staging hash mismatch' }}
Stop-ScheduledTask -TaskName 'Pixels-Console-Debug' -ErrorAction SilentlyContinue
Get-Process px_console -ErrorAction SilentlyContinue | Where-Object {{ $_.Path -eq $targetExe }} | Stop-Process -Force
$deadline = [DateTime]::UtcNow.AddSeconds(10)
while ((Get-Process px_console -ErrorAction SilentlyContinue) -and [DateTime]::UtcNow -lt $deadline) {{ Start-Sleep -Milliseconds 100 }}
if (Get-Process px_console -ErrorAction SilentlyContinue) {{ throw 'Console did not stop' }}
Copy-Item -LiteralPath $stagedExe -Destination $targetExe -Force
Copy-Item -LiteralPath $stagedConfig -Destination $targetConfig -Force
Remove-Item -LiteralPath $stagedExe, $stagedConfig -Force
if ((Get-FileHash -LiteralPath $targetExe -Algorithm SHA256).Hash -ne '{expected_exe_hash}' -or
    (Get-FileHash -LiteralPath $targetConfig -Algorithm SHA256).Hash -ne '{expected_config_hash}') {{ throw 'published hash mismatch' }}
Start-ScheduledTask -TaskName 'Pixels-Console-Debug'
Start-Sleep -Seconds 5
$process = Get-Process px_console -ErrorAction Stop | Where-Object {{ $_.Path -eq $targetExe }} | Select-Object -First 1
if (-not $process) {{ throw 'Console did not restart' }}
[pscustomobject]@{{ ProcessId=$process.Id; ExeHash=(Get-FileHash $targetExe).Hash; ConfigHash=(Get-FileHash $targetConfig).Hash; Host='{host}' }} | ConvertTo-Json -Compress
"""
        result = json.loads(run_powershell(client, script))
        if result["ExeHash"] != expected_exe_hash or result["ConfigHash"] != expected_config_hash:
            raise RuntimeError("Remote deployment hash mismatch")
        print(json.dumps(result, ensure_ascii=False))
    finally:
        client.close()


if __name__ == "__main__":
    main()
