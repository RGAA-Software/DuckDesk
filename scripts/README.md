# Scripts

This directory contains helper scripts for building, packaging, and diagnosing Pixels.

## `list_pixels_render.ps1` / `list_pixels_render.bat`

List all running `px_render.exe` processes with their full command-line arguments.

### PowerShell

```powershell
# Default: list px_render.exe
.\list_pixels_render.ps1

# List a different process
.\list_pixels_render.ps1 -Name "px_panel.exe"
.\list_pixels_render.ps1 -Name "px_client.exe"
```

### Batch (double-click)

Run `list_pixels_render.bat` directly. It defaults to `px_render.exe` and keeps the window open with `pause`.

### Output

```text
Found 1 px_render.exe process(es):
================================================================================
PID              : 30644
Name             : px_render.exe
Parent PID       : 18960
Executable Path  : <repo>\build_official\cloud_node\dist\px_render.exe
Start Time       : 2026-06-12 15:38:24
Command Line     : <repo>/build_official/cloud_node/dist/px_render.exe
                   --app_mode=desktop
                   --panel_server_host=127.0.0.1
                   --panel_server_port=4999
                   --service_server_host=127.0.0.1
                   --service_server_port=4603
                   ...
--------------------------------------------------------------------------------
```

## Server packaging scripts

Auth, Console and Desk build complete versioned releases under `output/<server>/releases/<run-id>/`.
These are release-only entry points, not routine development commands. They bump their own server version,
build the matching web/server/schema tools, and verify all copied artifact hashes.
No signing keys, TLS private keys or deployed configuration are generated, copied or overwritten.

- `package_px_auth_server.bat`: px_auth, px_auth_admin, px_db and static web assets.
  [Configuration, explicit initialization and tests](../docs/px_auth_server_runtime_config.md).
- `package_px_desk_server.bat`: px_desk, px_db and static web assets.
  [Configuration and tests](../docs/px_desk_web_overview.md).
- `package_px_console_server.bat`: PostgreSQL `px_console`, `px_console_admin`, `px_db` and static web assets.
  [Configuration, initialization and upgrade](../docs/px_console_server_runtime_config.md).
- `ensure_tls_cert.bat`: retired Console helper; not used by the Auth/Console/Desk release paths.

Focused tests: `pwsh -NoProfile -File scripts/server_validation/postgres.ps1 Test -Linux`.
Explicit query generation after reviewed SQL changes: `PrepareQueries`; it is not an acceptance run.
Current incomplete stage gates are tracked in [database execution status](../docs/server_database_execution_status.md).

## Other scripts

- `collect_dist.py` — Collect one product/distribution's declared artifacts into its isolated `dist/` directory.
- `prepare_windows_distribution.py` — Validate approved deployment trust material and prepare fail-closed Official/Customer package policy.
- `../scripts_build/build_cpp_common.bat` — Build the `px_common` target through the supported focused-build entry point.
- `build_tc_tests.bat` / `run_tc_tests.bat` — Build and run tc tests.
