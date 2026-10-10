# Adaptive background file credit and residual jitter investigation

## Evidence before the adaptive change

90 initially ran `iroh-file-credit-20261010`; its installed Render hash was rechecked. Captures A/B use the actual Windows Client, game view dragging and 64 MiB incompressible upload/download, with TCP 4605 headers captured on both hosts. Pktmon uses multi-file mode, 96-byte truncation and TCPIP component conversion (Client 75, 90 51). Both nonempty remote B parts are included. Capture hashes are in `20261010_credit_capture_summary.json`.

- **A failed with a hardware/network interruption.** Local NDIS event 10400 at 22:30:03 reports that the Realtek Gaming GbE hardware stopped responding to driver commands and was reset, the sixteenth reset since initialization. The installed driver is 10.43.723.2020 (2020-07-23). The Client stopped receiving Relay traffic, retried sends, timed out refreshing Console, and exhausted the existing eight-second readmission window. The 90-second file harness failed. The driver version alone does not prove a particular driver bug. No driver, NIC setting or application grace policy was changed.
- **B completed without a new NDIS reset:** 46.321 seconds, matching upload/download SHA, 51.9–60.4 delivery FPS, 18 delivery gaps above 100 ms, maximum 149.2 ms. Five media events correlate with TCP sequence holes, duplicate ACKs and repeated transmissions of the missing segment on 90. At 22:33:57.384, the 149.152 ms media interval coincides with an 81.124 ms ACK stall, 30 duplicate ACKs and three transmissions. Other gaps are not automatically labeled TCP loss. Two sub-millisecond hole associations without duplicate ACK/retransmission evidence are explicitly excluded from the five confirmed associations.
- Render also logged capture admission blocked for 83–100 ms, with the video window reduced to roughly 32 KiB while file credit stayed at 256 KiB. This motivates giving background file traffic less credit under delay growth. It does not establish that every remaining gap has the same cause.
- **Timing interpretation correction:** `source_gap_us` uses the sender's packetization submission timestamps, not a standalone game-render timestamp. Its pauses may include capture admission/backpressure. It must not be described as proof that the game renderer itself paused. Assembly calls in the examined events remain microseconds.

TCP sequence matching and durations measured on each host avoid subtracting unsynchronized host clocks. Capture-level packet-drop counters are not ETW event-loss counters or proof of physical network loss. No packet payload is decrypted. A's owned partial upload was SHA-checked against the local source prefix and removed together with its owned digest; both hosts' Pktmon sessions and temporary filters were stopped/removed.

## Implementation

`FileSendWindow` samples reservation-to-cumulative-receipt delay with one bounded optional sample. Relative to its minimum observed delay, growth of at least 50 ms reduces the window from 256 KiB to 128 KiB (two normal 120 KiB blocks to one). Growth at or below 25 ms for at least one second restores the normal window; the intermediate band resets recovery hysteresis. These are application bulk-credit thresholds, not changes to QUIC congestion control or media assembly limits.

Existing outstanding credit is not revoked. Legal oversized messages still progress alone, with the existing 1 MiB maximum. Invalid or duplicate receipts do not alter latency samples. Credit remains receipt-driven, with the existing writable-wait/shutdown behavior. Input, control and datagrams do not share file credit. No auth, protocol framing or third-party dependency changes are needed.

This is a project policy inferred from the observed mixed-load budget imbalance. It is not claimed to copy a Moonlight file-transfer implementation. Read-only references checked this batch: Sunshine `3cba9baebac882b336be3ebe129ee612cb189853`, `src/stream.cpp` bounded packet batches and within-frame pacing; Moonlight common core `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac`, `src/RtpVideoQueue.c` bounded loss/recovery behavior. Existing project packetization/pacing already follows those references; adding assembly buffering would not cure TCP HOL.

## Verification and delivery

Release tests: 26 cases passed across file-flow/transport/frontend; three external-Relay environment cases skipped, reusing their previous evidence. New deterministic tests check delayed-credit shrink without discarding debt, delayed recovery hysteresis, invalid/duplicate receipts and maximum-message progress. Existing real-QUIC tests cover input/datagrams during file backpressure, both file directions and callback shutdown. Pre-change sources archived under `backup/iroh_adaptive_file_credit_20261010/`.

Complete candidate `iroh-adaptive-credit-20261010` was installed on 90 through Setup, with 314 matching installed artifacts. Identities are in `20261010_adaptive_credit_{build,installation}.json`.

## Experiment outcome: rejected

Two completed real Relay runs transferred 64 MiB in each direction with matching SHA, taking 63.236 and 56.257 seconds. They still recorded 78 gaps above 100 ms / maximum 519 ms, and 22 / maximum 178.3 ms. The latency-driven shrink actually occurred, but these results do **not** establish a stable performance gain. The network conditions were not a replayed identical loss trace, so this is not proof that the algorithm caused every worse gap either. The added policy is withdrawn rather than shipping an unproven tuning change.

One earlier attempt was aborted by the harness's Console read-only connected-state GET timing out while the Client was already transferring. It is preserved as `20261010_adaptive_credit_console_timeout.*`, not counted as a functional/performance success. The harness now allows two retries for failed GET observations only; mutations are not replayed. Neither completed run used a retry. The aborted upload's 35,880,960-byte partial file matched the local source prefix and was removed with its owned digest.

Experimental sources and tests are retained under `backup/iroh_adaptive_credit_rejected_20261010/`; active source has been restored to the fixed peer-receipt window. Copying the archived originals preserved older timestamps, which initially caused Ninja to reuse an object built against the experimental class layout; that test failed with heap corruption. Touching the restored dependencies and rebuilding the affected targets resolved the stale-build mismatch; all three focused CTests then passed. The failed build was never published. Failure and retry evidence are retained.

A fresh complete baseline package `iroh-credit-baseline-20261010` has replaced the experiment on 90 through Setup. Cloud Node 3.3.97, all 314 artifacts verified; installed Render SHA-256 `83EE1980EDEA6620DA8C0155E5A99598DCC321F6E476D785A378CBAB1FC491FF`. Development Client/Render/embedded Client are republished with matching build/dist hashes. Both modified production source files match their pre-experiment archive byte-for-byte. Identities are in `20261010_credit_baseline_{build,installation}.json`.

Post-restore actual 64 MiB upload/download SHA passes:

| Transport | Seconds | Delivery FPS | Gaps >100 ms | Maximum gap ms |
|---|---:|---:|---:|---:|
| 90 Relay | 37.607 | 48.8–60.3 | 8 | 277.1 |
| Direct | 25.525 | 56.9–60.2 | 1 | 169.5 |

The direct gap includes a 133.133 ms sender submission interval. This is not a claim that all source-side latency or Relay jitter has been resolved. Both completed post-restore runs required zero observation retries. Functional checks pass; M3 performance remains open.

Cleanup: no active business sessions, owned Client/instance or remote test file remains. Three services Running, only desktop Render PID 19200, both Relays fresh/ready/undrained. Pktmon stopped and filters empty on both hosts. No additional NDIS 10400 was recorded from 22:33 through the end of this batch. No driver, NIC, persistent network configuration or application grace period was modified. Summary and cleanup: `20261010_adaptive_credit_summary.json`, `20261010_credit_baseline_cleanup.log`, `20261010_credit_baseline_after.json`.

Next: retain fixed receipt credit, distinguish remaining sender capture-admission/encoding pauses using their own timing, and record NIC resets as environment failures. Reuse this rejected experiment rather than repeating it; retain TCP ordered-retransmission limitations, missing NAT/long-run evidence and Android-last delivery order.
