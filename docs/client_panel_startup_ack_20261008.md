# Client → Panel remote-connection acknowledgement (2026-10-08)

## Behavior

- Each Panel launch registers a fresh UUID and stream identity before creating px_client. The private stdin envelope carries the actual local Panel port and launch UUID.
- Client opens the existing loopback `/panel` WebSocket and sends `kCpHello` for IPC readiness only. A connected Client session sends `kCpTransportConnected`; terminal rejections send `kCpTransportRejected`. An early result is retained until IPC connects and is resent after IPC reconnects.
- Native sessions become connected on remote configuration/media readiness (file-transfer-only sessions on transport readiness); RDP waits for `SessionPhase::Connected`. This does not wait for a decoded video frame or completion of webpage loading.
- The cloud-application modal stays open through process creation and adds a localized “连接远端服务” / “Connecting to remote service” stage. Both remote-control and cloud-application workflows finish only after the matching Client success report.
- Failure, child exit, or a 45-second connection acknowledgement timeout leaves an error dialog. English and Simplified Chinese messages are supported. Only the exact child process is canceled; remote RDP accounts, profiles, sessions and workspace applications are preserved.
- Launch registration is removed by RAII. Stale/wrong-stream reports cannot complete another launch. Panel shutdown cancels pending waits before joining its worker. Stopping all clients during local-data clearing still permits later launches.

## Verification

Focused development Release builds only:

- `scripts_build/build_cpp_panel.bat client 8`: product tests, Console resource-lifecycle checks, localization/theme checks passed.
- `scripts_build/build_cpp_client.bat client 8`: all eight invoked Client suites passed (50 tests total).
- Added actual loopback WebSocket tests for Hello-only waiting, early remote result, rejection, first-result-wins, wrong stream/launch, unregister during queued delivery, shutdown wake-up, and repeated destruction with queued connect/send callbacks.
- Added workflow tests proving Launching persists until remote acknowledgement and specific failure keys survive into the dialog; launch-envelope tests reject incomplete or invalid IPC identities.
- Live development Client connected to the existing 90 node via Panel: WebView at 18:02:22, RDP at 18:03:17, remote desktop at 18:04:21, GameHook 2dadventure at 18:07:08. Each produced matching Client/Panel success logs and the dialog closed.
- WebView IPC Hello was received at 18:02:20.345; remote acknowledgement followed at 18:02:22.435. A screenshot between these events shows four successful stages with “连接远端服务” still waiting.
- Deliberately stopped the newly launched WebView Client before connection at 18:06:03. The modal stayed open with “客户端在连接远端服务成功前退出了。”
- Closed only validation-owned Client processes after checks. No server/node installation was needed for this local Panel/Client change.

Local evidence: `.cache/client-startup-live/` (screenshots, startup-only logs, artifact hashes). Final build logs: `.cache/client-startup-panel-verified.log`, `.cache/client-startup-client-delivery.log`.

## Delivered development artifacts

Build-tree and `build_official/client/dist` SHA-256 values matched:

- `px_panel.exe`: `82be5e61a32a9ac6cf183091a037ed6ca5421a12fe16541aafec89bcde08b685`
- `px_client.exe`: `168c68129cf0394a806fc0260b9d51699ae56178cd5b9be831b6e5de52d81dff`

Pre-change implementation snapshots and provenance are retained under `backup/client_panel_startup_ack_20261008/`.
