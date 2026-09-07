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

Directory and build-target extraction is the first implementation checkpoint.
Transport simplification and platform/core separation are subsequent checkpoints:
the current SDK still contains the existing RTC/Relay implementations and the
Windows decoder/render adapter still depends on Qt. This directory move alone
does **not** establish an independently consumable, platform-neutral SDK.

The UDP path no longer falls back to WebSocket media. A typed media-only failure
keeps the authenticated control/file session intact; UDP sessions reject WS
audio/video before recording and decoding. Reconnect by ending the session and
creating a new one. Other legacy transport entry points are not yet retired.

The final native product transport is UDP/FEC media with WebSocket control/files.
WebRTC remains a Web-client capability. iOS and macOS adapters are planned,
not implemented. See [the product decision](../../docs/native_client_sdk_transport_decision.md).
