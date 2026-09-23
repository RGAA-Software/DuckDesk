#!/usr/bin/env python3
"""Publish focused Service, Render, or browser artifacts to the public Windows node."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import re
import subprocess
import sys
import uuid
from pathlib import Path

import paramiko

ROOT = Path(__file__).resolve().parent.parent
MACHINE_FILE = ROOT / ".env" / "test_machine.md"
REMOTE_DIRECTORIES = {
    "cloud_node": "C:/Program Files/Pixels Cloud Node",
    "remote": "C:/Program Files/Pixels Remote",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--product", required=True, choices=("cloud_node", "remote"))
    parser.add_argument("--distribution", default="development", choices=("development", "official", "customer"))
    parser.add_argument("--component", required=True, choices=("service", "render", "web"))
    parser.add_argument("--preflight-only", action="store_true")
    return parser.parse_args()


def machine_value(text: str, label: str) -> str:
    match = re.search(rf"^\s*-\s*{re.escape(label)}\s*[:：]\s*(.+?)\s*$", text, re.MULTILINE | re.IGNORECASE)
    if not match:
        raise RuntimeError(f"Missing {label} in the test-machine document")
    return match.group(1).strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def encoded_powershell(script: str) -> str:
    return base64.b64encode(script.encode("utf-16-le")).decode("ascii")


def run_powershell(client: paramiko.SSHClient, script: str) -> dict[str, object]:
    command = f"powershell.exe -NoProfile -NonInteractive -EncodedCommand {encoded_powershell(script)}"
    _, stdout, stderr = client.exec_command(command, timeout=120)
    status = stdout.channel.recv_exit_status()
    output = stdout.read().decode("utf-8", errors="replace").strip()
    error = stderr.read().decode("utf-8", errors="replace").strip()
    if status != 0:
        raise RuntimeError(error or output or f"Remote command exited with {status}")
    json_line = next((line for line in reversed(output.splitlines()) if line.strip().startswith("{")), "")
    if not json_line:
        raise RuntimeError(f"Remote deployment did not return JSON: {output}")
    return json.loads(json_line)


def service_script(
    remote_directory: str,
    product: str,
    distribution: str,
    exe_hash: str,
    config_hash: str,
    descriptor_hash: str,
) -> str:
    return rf"""
$ErrorActionPreference = 'Stop'
$directory = '{remote_directory}'
$target = Join-Path $directory 'px_service.exe'
$staging = Join-Path $directory 'px_service.staged.exe'
$configTarget = Join-Path $directory 'px_service.toml'
$configStaging = Join-Path $directory 'px_service.staged.toml'
$descriptorTarget = Join-Path $directory 'product-manifest.json'
$descriptorStaging = Join-Path $directory 'product-manifest.staged.json'
if (-not (Test-Path -LiteralPath $descriptorTarget -PathType Leaf)) {{ throw 'Focused publish requires an installed current product descriptor' }}
$installedProduct = Get-Content -LiteralPath $descriptorTarget -Raw | ConvertFrom-Json
$expectedReleaseNamespace = if ('{distribution}' -eq 'development') {{ $null }} else {{ 'pixels.{distribution}' }}
if ($installedProduct.schema_version -ne 4 -or $installedProduct.company -ne 'Pixels' -or $installedProduct.product -ne '{product}' -or
    $installedProduct.distribution -ne '{distribution}' -or $installedProduct.release_namespace -ne $expectedReleaseNamespace -or
    $null -ne $installedProduct.oem_id) {{
    throw 'Installed product identity does not match the requested focused publish'
}}
if ((Get-FileHash -LiteralPath $staging -Algorithm SHA256).Hash -ne '{exe_hash}') {{ throw 'Service staging hash mismatch' }}
if ((Get-FileHash -LiteralPath $configStaging -Algorithm SHA256).Hash -ne '{config_hash}') {{ throw 'Service config staging hash mismatch' }}
if ((Get-FileHash -LiteralPath $descriptorStaging -Algorithm SHA256).Hash -ne '{descriptor_hash}') {{ throw 'Product descriptor staging hash mismatch' }}
$services = @(Get-CimInstance Win32_Service | Where-Object {{
    $_.PathName -and $_.PathName.IndexOf($target, [StringComparison]::OrdinalIgnoreCase) -ge 0
}})
if ($services.Count -ne 1) {{ throw 'Service identity is ambiguous' }}
$serviceName = $services[0].Name
$serviceWasRunning = (Get-Service -Name $serviceName).Status -eq 'Running'
$backup = Join-Path $directory ('service.before-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmss'))
[void](New-Item -ItemType Directory -Path $backup)
Copy-Item -LiteralPath $target -Destination (Join-Path $backup 'px_service.exe') -Force
Copy-Item -LiteralPath $configTarget -Destination (Join-Path $backup 'px_service.toml') -Force
Copy-Item -LiteralPath $descriptorTarget -Destination (Join-Path $backup 'product-manifest.json') -Force
function Stop-InstalledService {{
    if ((Get-Service -Name $serviceName).Status -ne 'Stopped') {{
        Stop-Service -Name $serviceName -Force
        (Get-Service -Name $serviceName).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(20))
    }}
    $processDeadline = [DateTime]::UtcNow.AddSeconds(20)
    do {{
        $serviceProcesses = @(Get-CimInstance Win32_Process | Where-Object {{
            $_.ExecutablePath -and [IO.Path]::GetFullPath($_.ExecutablePath).Equals(
                [IO.Path]::GetFullPath($target), [StringComparison]::OrdinalIgnoreCase)
        }})
        if ($serviceProcesses.Count -eq 0) {{ return }}
        Start-Sleep -Milliseconds 250
    }} while ([DateTime]::UtcNow -lt $processDeadline)
    throw 'Service process did not release the installed executable'
}}
function Start-InstalledService {{
    Start-Service -Name $serviceName
    (Get-Service -Name $serviceName).WaitForStatus('Running', [TimeSpan]::FromSeconds(20))
    Start-Sleep -Seconds 2
    if ((Get-Service -Name $serviceName).Status -ne 'Running') {{ throw 'Published Service did not remain running' }}
}}
try {{
    Stop-InstalledService
    Copy-Item -LiteralPath $staging -Destination $target -Force
    Copy-Item -LiteralPath $configStaging -Destination $configTarget -Force
    Copy-Item -LiteralPath $descriptorStaging -Destination $descriptorTarget -Force
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne '{exe_hash}') {{ throw 'Service deployment hash mismatch' }}
    if ((Get-FileHash -LiteralPath $configTarget -Algorithm SHA256).Hash -ne '{config_hash}') {{ throw 'Service config deployment hash mismatch' }}
    if ((Get-FileHash -LiteralPath $descriptorTarget -Algorithm SHA256).Hash -ne '{descriptor_hash}') {{ throw 'Product descriptor deployment hash mismatch' }}
    if ($serviceWasRunning) {{ Start-InstalledService }}
}} catch {{
    $deploymentError = $_
    Stop-InstalledService
    Copy-Item -LiteralPath (Join-Path $backup 'px_service.exe') -Destination $target -Force
    Copy-Item -LiteralPath (Join-Path $backup 'px_service.toml') -Destination $configTarget -Force
    Copy-Item -LiteralPath (Join-Path $backup 'product-manifest.json') -Destination $descriptorTarget -Force
    if ($serviceWasRunning) {{ Start-InstalledService }}
    throw $deploymentError
}} finally {{
    Remove-Item -LiteralPath $staging, $configStaging, $descriptorStaging -Force -ErrorAction SilentlyContinue
}}
[pscustomobject]@{{
    Component = 'service'
    Service = $serviceName
    State = (Get-Service -Name $serviceName).Status.ToString()
    ExeHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    ConfigHash = (Get-FileHash -LiteralPath $configTarget -Algorithm SHA256).Hash
    DescriptorHash = (Get-FileHash -LiteralPath $descriptorTarget -Algorithm SHA256).Hash
    RecoverableBackup = $backup
}} | ConvertTo-Json -Compress
"""


def render_script(
    remote_directory: str,
    product: str,
    distribution: str,
    exe_hash: str,
    rtc_hash: str,
) -> str:
    return rf"""
$ErrorActionPreference = 'Stop'
$directory = '{remote_directory}'
$target = Join-Path $directory 'px_render.exe'
$staging = Join-Path $directory 'px_render.staged.exe'
$rtcTarget = Join-Path $directory 'px_render_rtc.dll'
$rtcStaging = Join-Path $directory 'px_render_rtc.staged.dll'
$descriptorTarget = Join-Path $directory 'product-manifest.json'
if (-not (Test-Path -LiteralPath $descriptorTarget -PathType Leaf)) {{ throw 'Focused publish requires an installed current product descriptor' }}
$installedProduct = Get-Content -LiteralPath $descriptorTarget -Raw | ConvertFrom-Json
$expectedReleaseNamespace = if ('{distribution}' -eq 'development') {{ $null }} else {{ 'pixels.{distribution}' }}
if ($installedProduct.schema_version -ne 4 -or $installedProduct.company -ne 'Pixels' -or $installedProduct.product -ne '{product}' -or
    $installedProduct.distribution -ne '{distribution}' -or $installedProduct.release_namespace -ne $expectedReleaseNamespace -or
    $null -ne $installedProduct.oem_id) {{
    throw 'Installed product identity does not match the requested focused publish'
}}
if ((Get-FileHash -LiteralPath $staging -Algorithm SHA256).Hash -ne '{exe_hash}') {{ throw 'Render staging hash mismatch' }}
if ((Get-FileHash -LiteralPath $rtcStaging -Algorithm SHA256).Hash -ne '{rtc_hash}') {{ throw 'Render RTC staging hash mismatch' }}
$services = @(Get-CimInstance Win32_Service | Where-Object {{
    $_.PathName -and $_.PathName.IndexOf((Join-Path $directory 'px_service.exe'), [StringComparison]::OrdinalIgnoreCase) -ge 0
}})
if ($services.Count -ne 1) {{ throw 'Service identity is ambiguous' }}
$serviceName = $services[0].Name
$serviceWasRunning = (Get-Service -Name $serviceName).Status -eq 'Running'
$backup = Join-Path $directory ('render.before-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmss'))
[void](New-Item -ItemType Directory -Path $backup)
Copy-Item -LiteralPath $target -Destination (Join-Path $backup 'px_render.exe') -Force
Copy-Item -LiteralPath $rtcTarget -Destination (Join-Path $backup 'px_render_rtc.dll') -Force
function Stop-InstalledService {{
    if ((Get-Service -Name $serviceName).Status -ne 'Stopped') {{
        Stop-Service -Name $serviceName -Force
        (Get-Service -Name $serviceName).WaitForStatus('Stopped', [TimeSpan]::FromSeconds(20))
    }}
}}
function Start-InstalledService {{
    Start-Service -Name $serviceName
    (Get-Service -Name $serviceName).WaitForStatus('Running', [TimeSpan]::FromSeconds(20))
    Start-Sleep -Seconds 2
    if ((Get-Service -Name $serviceName).Status -ne 'Running') {{ throw 'Service did not remain running with the published Render' }}
}}
try {{
    Stop-InstalledService
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {{
        $processes = @(Get-Process -Name px_render, px_panel -ErrorAction SilentlyContinue | Where-Object {{
            $_.Path -and [IO.Path]::GetFullPath($_.Path).StartsWith([IO.Path]::GetFullPath($directory), [StringComparison]::OrdinalIgnoreCase)
        }})
        if ($processes.Count -eq 0) {{ break }}
        $processes | Stop-Process -Force
        Start-Sleep -Milliseconds 250
    }} while ([DateTime]::UtcNow -lt $deadline)
    if ($processes.Count -ne 0) {{ throw 'Render or Panel did not stop before deployment' }}
    Copy-Item -LiteralPath $staging -Destination $target -Force
    Copy-Item -LiteralPath $rtcStaging -Destination $rtcTarget -Force
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne '{exe_hash}') {{ throw 'Render deployment hash mismatch' }}
    if ((Get-FileHash -LiteralPath $rtcTarget -Algorithm SHA256).Hash -ne '{rtc_hash}') {{ throw 'Render RTC deployment hash mismatch' }}
    if ($serviceWasRunning) {{ Start-InstalledService }}
}} catch {{
    $deploymentError = $_
    Stop-InstalledService
    Copy-Item -LiteralPath (Join-Path $backup 'px_render.exe') -Destination $target -Force
    Copy-Item -LiteralPath (Join-Path $backup 'px_render_rtc.dll') -Destination $rtcTarget -Force
    if ($serviceWasRunning) {{ Start-InstalledService }}
    throw $deploymentError
}} finally {{
    Remove-Item -LiteralPath $staging, $rtcStaging -Force -ErrorAction SilentlyContinue
}}
[pscustomobject]@{{
    Component = 'render'
    Service = $serviceName
    State = (Get-Service -Name $serviceName).Status.ToString()
    ExeHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    RtcHash = (Get-FileHash -LiteralPath $rtcTarget -Algorithm SHA256).Hash
    RecoverableBackup = $backup
}} | ConvertTo-Json -Compress
"""


def web_script(
    remote_directory: str,
    product: str,
    distribution: str,
    staging_name: str,
    expected_files: dict[str, str],
) -> str:
    expected_json = json.dumps(expected_files, ensure_ascii=False, separators=(",", ":"))
    return rf"""
$ErrorActionPreference = 'Stop'
$directory = '{remote_directory}'
$staging = Join-Path $directory '{staging_name}'
$target = Join-Path $directory 'web_client'
$descriptorTarget = Join-Path $directory 'product-manifest.json'
$resolvedDirectory = [IO.Path]::GetFullPath($directory).TrimEnd('\') + '\'
$resolvedStaging = [IO.Path]::GetFullPath($staging)
$resolvedTarget = [IO.Path]::GetFullPath($target)
if (-not $resolvedStaging.StartsWith($resolvedDirectory, [StringComparison]::OrdinalIgnoreCase) -or
    -not $resolvedTarget.StartsWith($resolvedDirectory, [StringComparison]::OrdinalIgnoreCase)) {{
    throw 'Unsafe browser artifact deployment path'
}}
if (-not (Test-Path -LiteralPath $descriptorTarget -PathType Leaf)) {{ throw 'Focused publish requires an installed current product descriptor' }}
$installedProduct = Get-Content -LiteralPath $descriptorTarget -Raw | ConvertFrom-Json
$expectedReleaseNamespace = if ('{distribution}' -eq 'development') {{ $null }} else {{ 'pixels.{distribution}' }}
if ($installedProduct.schema_version -ne 4 -or $installedProduct.company -ne 'Pixels' -or $installedProduct.product -ne '{product}' -or
    $installedProduct.distribution -ne '{distribution}' -or $installedProduct.release_namespace -ne $expectedReleaseNamespace -or
    $null -ne $installedProduct.oem_id) {{
    throw 'Installed product identity does not match the requested focused publish'
}}
$expected = ConvertFrom-Json -InputObject '{expected_json}'
$expectedNames = @($expected.psobject.Properties.Name)
$actualFiles = @(Get-ChildItem -LiteralPath $staging -File -Recurse)
if ($actualFiles.Count -ne $expectedNames.Count) {{ throw 'Staged browser artifact file count mismatch' }}
foreach ($relativePath in $expectedNames) {{
    $candidate = Join-Path $staging $relativePath.Replace('/', '\')
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {{ throw "Staged browser artifact is missing: $relativePath" }}
    if ((Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash -ne $expected.psobject.Properties[$relativePath].Value) {{
        throw "Staged browser artifact hash mismatch: $relativePath"
    }}
}}
$backup = Join-Path $directory ('web_client.before-' + [DateTime]::UtcNow.ToString('yyyyMMddHHmmss'))
$previousMoved = $false
try {{
    if (Test-Path -LiteralPath $target -PathType Container) {{
        Move-Item -LiteralPath $target -Destination $backup
        $previousMoved = $true
    }}
    Move-Item -LiteralPath $staging -Destination $target
    foreach ($relativePath in $expectedNames) {{
        $published = Join-Path $target $relativePath.Replace('/', '\')
        if ((Get-FileHash -LiteralPath $published -Algorithm SHA256).Hash -ne $expected.psobject.Properties[$relativePath].Value) {{
            throw "Published browser artifact hash mismatch: $relativePath"
        }}
    }}
}} catch {{
    if ($previousMoved -and -not (Test-Path -LiteralPath $target) -and (Test-Path -LiteralPath $backup)) {{
        Move-Item -LiteralPath $backup -Destination $target
    }}
    throw
}}
[pscustomobject]@{{
    Component = 'web'
    FileCount = $expectedNames.Count
    IndexHash = (Get-FileHash -LiteralPath (Join-Path $target 'index.html') -Algorithm SHA256).Hash
    RecoverableBackup = if ($previousMoved) {{ $backup }} else {{ $null }}
}} | ConvertTo-Json -Compress
"""


def ensure_remote_directory(sftp: paramiko.SFTPClient, path: str) -> None:
    segments = [segment for segment in path.replace("\\", "/").split("/") if segment]
    if not segments:
        raise RuntimeError("Remote directory path is empty")
    current = segments[0]
    for segment in segments[1:]:
        current = f"{current}/{segment}"
        try:
            sftp.stat(current)
        except OSError:
            sftp.mkdir(current)


def main() -> int:
    args = parse_args()
    product_root = ROOT / "build_official" / args.product
    dist_directory = product_root / "dist" if args.distribution == "development" else product_root / args.distribution / "dist"
    remote_directory = REMOTE_DIRECTORIES[args.product]
    subprocess.run(
        ["python", str(ROOT / "scripts" / "verify_product_dist.py"), str(dist_directory)],
        check=True,
    )
    machine_text = MACHINE_FILE.read_text(encoding="utf-8")
    host = machine_value(machine_text, "地址")
    port = int(machine_value(machine_text, "SSH 端口").split("，", 1)[0].split(",", 1)[0])
    username = machine_value(machine_text, "用户名")
    password = machine_value(machine_text, "密码")
    if args.component == "service":
        sources = {
            "px_service.staged.exe": dist_directory / "px_service.exe",
            "px_service.staged.toml": dist_directory / "px_service.toml",
            "product-manifest.staged.json": dist_directory / "product-manifest.json",
        }
    elif args.component == "render":
        sources = {
            "px_render.staged.exe": dist_directory / "px_render.exe",
            "px_render_rtc.staged.dll": dist_directory / "px_render_rtc.dll",
        }
    else:
        web_directory = dist_directory / "web_client"
        sources = {
            source.relative_to(web_directory).as_posix(): source
            for source in sorted(web_directory.rglob("*"))
            if source.is_file()
        }
        if not sources:
            raise RuntimeError(f"Required browser distribution is empty: {web_directory}")
    for source in sources.values():
        if not source.is_file():
            raise RuntimeError(f"Required deployment input is missing: {source}")

    expected_hashes = {name: sha256(source) for name, source in sources.items()}
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(host, port=port, username=username, password=password, timeout=15, banner_timeout=15, auth_timeout=15)
    try:
        with client.open_sftp() as sftp:
            descriptor_path = f"{remote_directory}/product-manifest.json"
            try:
                with sftp.open(descriptor_path, "rb") as descriptor_file:
                    installed_descriptor = json.loads(descriptor_file.read().decode("utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as error:
                raise RuntimeError(
                    "Focused public publish requires a valid current product installation; run the product installer first"
                ) from error
            expected_identity = {
                "schema_version": 4,
                "product": args.product,
                "distribution": args.distribution,
                "release_namespace": None if args.distribution == "development" else f"pixels.{args.distribution}",
                "oem_id": None,
                "oem_profile_sha256": None,
                "company": "Pixels",
            }
            actual_identity = {key: installed_descriptor.get(key) for key in expected_identity}
            if actual_identity != expected_identity:
                raise RuntimeError(
                    f"Public node product mismatch: expected={expected_identity}, actual={actual_identity}"
                )
            if args.preflight_only:
                print(
                    json.dumps(
                        {
                            "Host": host,
                            "Product": args.product,
                            "Distribution": args.distribution,
                            "Component": args.component,
                            "Preflight": "passed",
                        },
                        ensure_ascii=False,
                        separators=(",", ":"),
                    )
                )
                return 0
            if args.component == "web":
                staging_name = f"web_client.staged-{uuid.uuid4().hex}"
                remote_staging = f"{remote_directory}/{staging_name}"
                ensure_remote_directory(sftp, remote_staging)
                for name, source in sources.items():
                    remote_path = f"{remote_staging}/{name}"
                    ensure_remote_directory(sftp, remote_path.rsplit("/", 1)[0])
                    sftp.put(str(source), remote_path)
            else:
                for name, source in sources.items():
                    sftp.put(str(source), f"{remote_directory}/{name}")
        if args.component == "service":
            result = run_powershell(
                client,
                service_script(
                    remote_directory,
                    args.product,
                    args.distribution,
                    expected_hashes["px_service.staged.exe"],
                    expected_hashes["px_service.staged.toml"],
                    expected_hashes["product-manifest.staged.json"],
                ),
            )
            if result.get("ExeHash") != expected_hashes["px_service.staged.exe"]:
                raise RuntimeError("Remote Service hash verification failed")
            if result.get("ConfigHash") != expected_hashes["px_service.staged.toml"]:
                raise RuntimeError("Remote Service config hash verification failed")
            if result.get("DescriptorHash") != expected_hashes["product-manifest.staged.json"]:
                raise RuntimeError("Remote product descriptor hash verification failed")
        elif args.component == "render":
            result = run_powershell(
                client,
                render_script(
                    remote_directory,
                    args.product,
                    args.distribution,
                    expected_hashes["px_render.staged.exe"],
                    expected_hashes["px_render_rtc.staged.dll"],
                ),
            )
            if result.get("ExeHash") != expected_hashes["px_render.staged.exe"]:
                raise RuntimeError("Remote Render hash verification failed")
            if result.get("RtcHash") != expected_hashes["px_render_rtc.staged.dll"]:
                raise RuntimeError("Remote Render RTC hash verification failed")
        else:
            result = run_powershell(
                client,
                web_script(remote_directory, args.product, args.distribution, staging_name, expected_hashes),
            )
            if result.get("FileCount") != len(expected_hashes):
                raise RuntimeError("Remote browser artifact count verification failed")
            if result.get("IndexHash") != expected_hashes.get("index.html"):
                raise RuntimeError("Remote browser index hash verification failed")
        result["Host"] = host
        result["Distribution"] = args.distribution
        print(json.dumps(result, ensure_ascii=False, separators=(",", ":")))
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"Public node publish failed: {error}", file=sys.stderr)
        raise SystemExit(1) from None
