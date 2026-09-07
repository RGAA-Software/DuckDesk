# Pixels native client SDK

The maintained SDK lives here, alongside `px_client` and `px_android`.
Windows and Android both build the existing `px_sdk` target from these sources.
Consumers link `px_sdk` to obtain its public include directories; do not add
`px_deps/px_client_sdk` paths or copy SDK sources into platform projects.

Shared dependencies (including `px_common`, `px_message`, `px_ft_engine`,
`px_media_record` and `px_voice_call`) remain in `src/px_deps`.
The pre-extraction sources are preserved in the repository-root
`backup/native_transport_simplification` batch, with a SHA-256 manifest.
Archived sources are never build inputs.

## Migration status

SDK extraction and native transport consolidation are implemented. The SDK no
longer builds RTC, Relay, legacy UDP/KCP, or standalone WebSocket media paths.
Its connection parameters no longer accept a transport type, Relay endpoint,
P2P flag, or ICE configuration. Windows and Android use the same UDP/FEC +
WebSocket connection code; TLS is a security setting on that reliable channel.

UDP failure reports a typed media-only failure and keeps the authenticated
control/file session intact. WS audio/video is rejected before recording and
decoding. Reconnect media by ending the session and creating a new one.
Standalone file sessions use one authenticated WebSocket without requiring UDP.
Repeated Start/Exit is idempotent; Exit is terminal for that connection object.

Windows no longer links or packages the Client RTC DLL. Render's WebRTC DLLs and
the voice APM DLL remain separate retained capabilities. Obsolete transport
diagnostics, signaling messages, credential parameters, and the no-op retry API
have been retired. Windows progress presentation no longer lives in the SDK.

The unused SDK OpenGL presentation helpers have been archived. Production
`px_sdk` no longer includes or directly links Qt and disables Qt code generation;
the Windows client keeps its own Qt/OpenGL renderer. Only the optional Windows
file-transfer test harness uses Qt Core. Frame data (`gl/raw_image.*`) remains
shared and active.

This is **not yet** an independently consumable, platform-neutral SDK:
D3D11/Vulkan/Android decoder boundaries, frame ownership, and dependency
composition still need separation. iOS/macOS adapters are not implemented.

Run `scripts/check_native_sdk_transport.ps1` to check source/build topology,
and `scripts/check_webrtc_dll_link_boundary.ps1` after Windows configuration
to verify that Native Client is isolated while Render keeps its RTC DLL boundary.

The final native product transport is UDP/FEC media with WebSocket control/files.
WebRTC remains a Web-client capability. iOS and macOS adapters are planned,
not implemented. See [the product decision](../../docs/native_client_sdk_transport_decision.md).
