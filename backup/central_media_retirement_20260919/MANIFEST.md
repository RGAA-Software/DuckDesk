# Central media retirement archive

- Base revision: `bfd6439bc8f360d1d7357eb81b4d519da05aa17e`
- Local modification status before archival: fully retired tracked paths were clean; `shared_before/` records the exact working-tree bytes before each maintained file was edited, including any then-current local content
- Retirement reason: remove ZLMediaKit live push, managed Coturn/STUN/TURN, and Relay-based WebRTC signaling while retaining Direct Host WebRTC, the existing non-WebRTC Relay, recording, and file transfer
- Archived paths retain their repository-relative layout below this directory
- Files under `shared_before/` are full pre-change snapshots of maintained files that also contain retained behavior
- Archive contents are reference-only and excluded from builds, tests, packaging, and runtime loading

## Fully retired paths

- `rust_server/px_console_server/media/`
- `rust_server/px_console_server/src/live/`
- `rust_server/px_console_server/src/rtc/`
- `rust_server/px_console_server/src/wall/`
- `rust_server/px_console_server/src/media_sidecar.rs`
- `src/px_render/architecture/sinks/live_pusher/`
- `src/px_render/architecture/sinks/live_pusher_sink.cpp`
- `src/px_render/architecture/sinks/live_pusher_sink.h`
- `src/px_render/tests/unit/test_live_pusher_sink.cpp`
- `src/px_render/network/webrtc/remote/`
- `src/px_render/tests/test_rtc_payload_authorization.cpp`
- `src/px_deps/px_message/px_signaling_message.proto`
- `src/px_deps/px_common/rtc_signal_identity.h`
- `src/px_web_client/src/client/px_rtc_conn.ts`
- `web/px_web_client/src/rtc/standard_signaling.ts`
- `web/px_web_client/test/standard_signaling.test.ts`
- `scripts/build_px_turn.bat`
- `scripts/console_live_stream_probe.mjs`
- `scripts_build/build_cpp_live_pusher_tests.bat`

## Shared-file snapshots

The complete list is recorded in `shared_before/SNAPSHOT_SHA256.txt`; each entry is the SHA-256 of the exact working-tree file copied before modification.
