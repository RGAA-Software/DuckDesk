# Windows runtime environment diagnostics

Status: implemented baseline on 2026-09-15.

## Implemented checks

- Windows version reported by the local system-information channel.
- Windows Audio and Audio Endpoint Builder service state, followed by a WASAPI default-render-endpoint activation probe.
- Visual C++ v14 x64 runtime modules: `msvcp140.dll`, `vcruntime140.dll`, and `vcruntime140_1.dll`.
- Legacy DirectX optional components represented by XInput 1.3 (`xinput1_3.dll`). This is intentionally not presented as a complete
  DirectX capability test.
- Windows automatic-logon configuration, including a configured user and legal-notice policy conflicts. Password material is never read or
  displayed.
- Active power-plan AC and, when a battery is present, DC display and sleep timeouts.
- Active High Performance or Ultimate Performance power plan.
- Pending restart signals from Component Based Servicing, Windows Update, and pending file rename operations.

Checks run asynchronously on the Panel worker. A refresh already in progress is not duplicated. Results use typed states and actions, and
actions only open Windows settings or official Microsoft guidance/download locations; diagnostics do not silently mutate machine-wide
settings.

## Prioritized follow-up checks

1. Service startup-chain health: SCM registration, configured executable path, service state, local management port, last exit status, and
   whether Panel can request a safe service start when it is installed but stopped.
2. End-to-end capture and encoder self-test: create a real capture source, select an encoder, encode a short synthetic frame sequence, and
   report the exact failing capability rather than inferring readiness from DLL or GPU presence.
3. Interactive-session readiness: active console/session identity, desktop availability, DDA initialization, GDI fallback availability, and
   virtual-display health where the selected mode requires it.
4. Network readiness: required local port conflicts, Windows Firewall policy, DNS resolution, Console/Relay reachability, TLS validation,
   and UDP reachability. These results must distinguish local configuration failures from an unavailable remote service.
5. Storage health: free space for the system volume, logs, recordings, cache, and crash dumps, with thresholds derived from actual product
   workloads.
6. Clock health: Windows Time service state and measured clock skew against an authenticated product endpoint, because excessive skew can
   break certificates and authorization tokens.
7. RDP-mode readiness after that mode is implemented: TermService state, authorized Windows account, target-session identity, reconnectable
   session state, proxy/tunnel availability, and one-active-frontend admission state. Ordinary diagnostics must not log off users or clean
   up workspace applications.

## Design constraints

- Prefer an actual capability probe over registry or file-presence inference.
- Keep probing read-only. Repair is an explicit user action with a separately reported result.
- Do not expose credentials, automatic-logon secrets, tokens, or private keys in UI details or logs.
- Preserve mode boundaries: game-hook and webview checks must not introduce RDP account/session prerequisites.
- Keep machine-local checks in a Windows adapter and keep the page as a pure view over typed snapshots.
