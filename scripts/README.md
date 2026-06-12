# Scripts

This directory contains helper scripts for building, packaging, and diagnosing the GammaRay project.

## `list_gammaray_render.ps1` / `list_gammaray_render.bat`

List all running `GammaRayRender.exe` processes with their full command-line arguments.

### PowerShell

```powershell
# Default: list GammaRayRender.exe
.\list_gammaray_render.ps1

# List a different process
.\list_gammaray_render.ps1 -Name "GammaRay.exe"
.\list_gammaray_render.ps1 -Name "GammaRayClientInner.exe"
```

### Batch (double-click)

Run `list_gammaray_render.bat` directly. It defaults to `GammaRayRender.exe` and keeps the window open with `pause`.

### Output

```text
Found 1 GammaRayRender.exe process(es):
================================================================================
PID              : 30644
Name             : GammaRayRender.exe
Parent PID       : 18960
Executable Path  : D:\source\GoCloud\GammaRayPremium\build_official\dist\GammaRayRender.exe
Start Time       : 2026-06-12 15:38:24
Command Line     : D:/source/GoCloud/GammaRayPremium/build_official/dist/GammaRayRender.exe
                   --app_mode=desktop
                   --panel_server_host=127.0.0.1
                   --panel_server_port=20369
                   --service_server_host=127.0.0.1
                   --service_server_port=20375
                   ...
--------------------------------------------------------------------------------
```

## Other scripts

- `collect_dist.py` — Collect build artifacts from `build_official/` into `build_official/dist/`.
- `build_tc_common.bat` / `build_tc_common_new.bat` — Build `tc_common` libraries.
- `build_tc_tests.bat` / `run_tc_tests.bat` — Build and run tc tests.
