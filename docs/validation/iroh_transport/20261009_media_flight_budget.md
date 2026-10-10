# In-flight media budget and capture admission — 2026-10-09

This increment addresses the remaining weak-network media stalls. It does not complete P0–P7 or claim that bytes already queued inside Relay TCP can be recalled.

## Findings and implementation

- NVENC accepts the low-bitrate reconfiguration; the prior 250 Kbps target was not silently ignored. New `iroh.media_budget` counters distinguish encoded output, successfully enqueued video datagrams, original Opus bytes and audio datagram bytes. Datagram counters exclude QUIC/IP/TCP overhead and do not prove remote delivery.
- Existing audio FEC pads each small Opus shard to a fixed approximately 1100-byte datagram. Compact audio omits only trailing zeros and records the original length; the receiver reconstructs the exact original data/parity packet before the existing jitter/FEC pipeline. Sequence, audio state and FEC behavior are preserved across direct/Relay paths. Real trailing zero bytes in Opus are restored too. Real game audio datagrams now measure approximately 61–62 Kbps rather than the approximately 650 Kbps fixed-padding cost.
- Complete-frame feedback uses the existing authorized control stream every 50 ms. Remote receipt, including fully assembled frames awaiting reference repair, releases the sender's per-stream credit. Local QUIC enqueue/completion does not release credit. No transport token, ticket, second login or authentication system was added.
- Each stream admits a bounded number of in-flight frames (8 plus baseline RTT allowance, up to 36), a 256 KiB byte window and a baseline-RTT-adjusted age bound. A first large frame is allowed. Sparse 250 ms probes prevent permanent credit deadlock after incomplete frames; retained history is capped. These are admission bounds, not a claim that outstanding bytes can never exceed 256 KiB after probes or a large IDR.
- The first candidate applied credit only after encoding. Actual direct testing exposed a 1580.4 ms stall: dropping an already encoded frame could amplify reference repair. The final candidate also checks credit before encoding, skips capture while blocked and resumes on receipt. The sequenced transport still exposes genuine encoded-frame drops and preserves its fallback bounds. Shared-texture capture always consumes/releases the producer's keyed mutex, even when encoding is skipped.
- Sustained pressure after bitrate has reached 500 Kbps or below additionally reduces capture admission (60 → 30 → 15 FPS for a 60 FPS configuration). Healthy feedback gradually restores the configured rate. The encoder's configured FPS is unchanged, preventing network-driven encoder recreation. Rate control continues to run while capture is gated.

## Focused checks and delivery

- Direct: `20261009_capture_credit_direct.xml`; private TLS Relay: `20261009_capture_credit_relay.xml`. Coverage includes exact audio reconstruction, stale feedback, count/byte/age bounds, lost-frame probes, adaptive FPS restoration, queued callback/stop behavior, reliable traffic alongside media, and actual QUIC capture gating without a reference-chain gap.
- The first local-queue test failed because its four 150 KB frames now reached the remote byte bound first. It now uses small frames to isolate the existing four-frame/deadline behavior; independent byte-bound tests remain. One initial frontend disconnect check timed out; three isolated repetitions and subsequent full direct/Relay runs passed without changing the disconnect code or weakening its assertion.
- Original dirty sources are retained under `backup/iroh_inflight_budget_20261009/` and the intermediate candidate under `backup/iroh_capture_credit_20261009/`.
- First complete candidate `iroh-flight-budget-20261009` was installed on 90 with all 314 artifacts verified. Its measured 2 Mbps/20-second Relay maximum interval was 646.6 ms, versus 2140.1 ms in the preceding feedback-only run. This is a single-run comparison, not a controlled benchmark. The normal-direct 1580.4 ms regression means this candidate was not accepted as the final fix.
- The first QoS run's final API cleanup request timed out after the owned Client had exited. Independent observation confirmed zero test QoS policies and only the desktop Render remained. The media evidence is preserved separately from that cleanup failure.

## Final capture-admission candidate

`iroh-capture-credit-20261009` is installed on 90: version 3.3.97, all 314 artifacts verified, service Running. See `20261009_capture_credit_delivery.json` and `20261009_capture_credit_operations.json`. Focused Release Client/Cloud Node EXE/DLL copies match development dist (`20261009_capture_credit_development_hashes.json`). No version bump, full-build entry point or manual installed-file replacement was used.

| Actual run | Received-frame results | Remaining qualification |
| --- | --- | --- |
| Game direct, approximately 75 seconds | Mostly 60 FPS; range 55.8–60.3, max interval 150.5 ms; two RFI requests, no IDR requests in measured windows | One short interval above 100 ms remains; not a guarantee of zero jitter |
| Game forced Relay, owned 2 Mbps TCP QoS for 20 seconds | Whole-run max 184.0 ms, restriction-related window 162.1 ms; zero RFI/IDR requests; later windows return to about 60 FPS | Startup before QoS also reduced to 36.5–47.7 FPS while adapting; this is not evidence that normal Relay always sustains 60 FPS |
| WebView forced Relay | Page loaded, scene/camera changed; mostly 58.9–60.1 FPS after startup; later max 266.0 ms and 136.1 ms, zero RFI/IDR requests | Startup max 1017.8 ms; loaded-scene jitter and bitrate/quality stability still need work |

Evidence: `20261009_capture_credit_measurements.json`, per-run JSON/screenshots, and `20261009_capture_credit_game_server.log` / `20261009_capture_credit_webview_server.log`. Windows report intervals between delivered encoded frames, not glass-to-glass latency. Local and remote clocks differ by several seconds; the QoS record uses the remote clock. These are separate practical runs, not controlled A/B benchmarks. WebView bitrate recovers into several Mbps but reduces again on an RTT spike; this increment has not established a quality target or eliminated startup adaptation.

The observed multi-second **game weak-network** stall was not reproduced in the final run. This supports the in-flight/capture-admission change, without declaring all weak-network, WebView startup or P0–P7 acceptance complete. Next media work is startup and RTT-spike bitrate/quality stability, including loaded WebView jitter; multi-Relay product management, Android and old-stack retirement remain pending.

Cleanup succeeded at 18:45:02 on 90: original Console configuration SHA restored, Console Running, owned Relay PID 53728 stopped, zero test QoS, one desktop Render PID 51580. Owned clients/application instances completed cleanup. See `20261009_capture_credit_cleanup.json`. Test assets remain only under `D:\112233`. No commit/push was made.
