# Asio Notify Concurrency Migration and Acceptance Plan

## Purpose

This document is the executable plan for completing GammaRay's native message
notification migration. The work is not complete merely because Dexode is no
longer linked: the maintained `asio2` dispatcher, the common runtime, every
project-owned listener and every shutdown path must be concurrency-safe and
lifetime-safe.

The authoritative ownership rules are in `docs/cpp_smart_pointer_standard.md`.
The current audit and known debt are in
`docs/cpp_ownership_common_audit_20260825.md`. This plan converts those rules
and findings into implementation phases and acceptance evidence.

## Non-negotiable boundaries

- Project-owned and project-maintained C++ must not store ownership as raw
  pointers or capture raw `this` in asynchronous work. Use `unique_ptr`,
  `shared_ptr` and guarded `weak_ptr::lock()`.
- New project-owned or maintained C++ code must not declare a raw pointer at
  all, including locals, returns and callback parameters. An unavoidable
  external-ABI signature is isolated in an adapter and carries the reviewed
  `NOLINT(gammaray-raw-pointer-boundary)` annotation defined by the project
  standard.
- Existing libwebrtc borrowed-pointer APIs in
  `src/px_deps/px_webrtc_client` are excluded. They must not be redesigned as
  part of this migration.
- Existing plugin instance, loader and ABI singleton ownership contracts are
  excluded. Their identity and unload timing must not change.
- Unmaintained third-party source is read-only. The vendored `asio2`
  integration is explicitly maintained by GammaRay and is in scope.
- Video and audio frames do not travel through the application message bus.
  Only control, lifecycle, state and low-rate statistics messages use it.
- Default behavior stays ordered and serial. Parallel execution is opt-in and
  must be selected by an explicit lane.

## Current state on 2026-08-26

- Native targets use `MessageNotifier`; Dexode is not linked.
- `MessageNotifier` now owns isolated Asio control/state contexts, a
  configurable bounded worker context and one bounded ingress FIFO. Control,
  state, worker and guarded external/UI lanes are explicit at product call sites.
- Client, Panel, Render, SDK, encoder, relay, Android native code and legacy
  Service consumers use guarded weak/QPointer lifetime boundaries and
  idempotent shutdown.
- Safe `asio2::event_dispatcher` dispatch and iteration now use lock-in/
  snapshot-out semantics and have focused concurrent mutation tests. The
  unused legacy address-returning compatibility API is explicitly not a safe
  concurrent interface.
- `MessageLooper`, its obsolete tests/build entries, and the unused vendored
  Dexode EventBus tree have been removed.
- Common `Thread`, `Data`, `File` and `SharedPreference` ownership and shutdown
  paths have been repaired and covered by focused tests.
- Windows migration targets compile and the Android official Release APK builds.
- The authoritative `build_official` whole-project Windows build passes. Every
  changed runtime artifact is published to `build_official\\dist`; build-tree,
  dist and machine-90 SHA-256 values match.
- The complete C++ suite passes 377 tests with four designed environment/long-
  duration skips. Focused concurrency suites and the full suite passed the
  required 10-round gate.
- Machine 90 passed the 10/10 virtual-display RTC lifecycle gate (50/50 stages),
  followed by a clean smoke using the final official artifacts. Authentication,
  Windows/Web clients and the reproducible LAN RTC/TURN paths are accepted.

Therefore the current state is **migration and current-environment delivery
complete**. The evidence and exact limits are recorded in
`docs/asio_notify_concurrency_acceptance_report_20260826.md`. True cross-public-
network TURN/NAT tests remain an explicit environment gap and are not claimed.

## Post-acceptance hardening on 2026-08-29

The acceptance baseline remains valid, but subsequent repository-wide ownership
audits continue to close legacy asynchronous paths found outside the original
notification inventory. These are treated as Phase 7 hardening and receive the
same focused-build, 10-round lifecycle and dist-publication gates.

- Relay plug-in monitoring and SDK callbacks now use an owned shared runtime,
  cancellable worker and generation invalidation.
- MiniAudio default-device reinitialization now uses an owned cancellable
  `jthread`; Stop joins it before releasing the device.
- The outer WAS audio plug-in now delegates capture, retry and event delivery
  to `WasAudioCaptureRuntime`. Capture callbacks and the retry worker hold only
  weak/shared ownership and no longer retain the loader-owned plug-in instance.
- The maintained WAS audio backends now share a synchronized callback channel.
  MiniAudio gives its C ABI a short-lived bridge that locks a weak owner, while
  process-loopback runs on a shared state `jthread` and uses WRL `ComPtr` plus
  RAII event handles for asynchronous activation. Neither backend worker or C
  callback retains a capture object. Callback replacement during an in-flight
  dispatch, pending reinit cancellation, real process-loopback activation,
  repeated Stop and DLL unload each pass ten rounds.
- GDI capture workers now use `jthread` stop tokens and lock a weak capture only
  for one iteration at a time. Destruction calls the same idempotent stop/join
  barrier, including safe self-thread detach. Screen DC, memory DC and bitmap
  ownership use typed RAII deleters, and the memory DC is released before its
  selected bitmap. Ten active-destroy/DLL-unload rounds and ten repeated
  start/stop rounds pass. In non-interactive test sessions, failed BitBlt keeps
  the legacy fallback-frame behavior but is throttled to avoid a full-CPU loop.
- Client monitor refresh now uses the cancellable context delay lane, and its
  redundant detached exit watchdog has been removed. Panel exit/uninstall now
  uses a tested staged sequence instead of UI sleeps and a detached thread.
- Media recorder and live pusher now use independently owned runtimes. Recorder
  Stop has a tested FIFO finalize barrier; live push owns its FFmpeg state with
  RAII handles and bounds RTMP blocking operations with interrupt deadlines.
- The shared Opus encoder path now uses an independently owned runtime and
  guarded event-delivery channel; encoder/cache/debug state no longer belongs
  to a loader-owned plug-in object.
- On 2026-08-30 the Render voice-call plug-in was reduced to a synchronous ABI
  adapter. `VoiceCallRuntime` now owns session state, endpoint lifetime,
  consent correlation, RTC PCM envelopes and the bounded non-RTC transport.
  Endpoint, transport and delayed device-failure callbacks retain only weak or
  shared state. Voice media reaches Render through owned routing events rather
  than retained plug-in/network addresses. The packet transport also supports
  stop from its delivery callback without a self-join.
- Voice focused evidence now includes 39 core tests, six runtime concurrency
  tests, three packet-transport tests, five client-protocol tests, and ten DLL
  create/destroy/unload rounds. The environment-gated real WASAPI smoke was
  repeated ten times, and the real WASAPI -> APM -> Opus endpoint path passed
  its separately enabled run. The endpoint and both maintained backends now
  route callbacks through shared state, use spans for owned audio buffers, and
  stop/join their workers without retaining an endpoint or back-end object.
  Tests cover callback-initiated Stop, repeated start/stop, destruction followed
  by a late callback, and accepted-media shutdown without late delivery.
  `px_client.exe`, `voice_call.dll`, and `px_voice_apm.dll` are publication-gated
  by matching build/dist SHA-256 hashes. This is lifecycle hardening evidence,
  not a replacement for the two-machine audio-quality and device matrix.
- The Render application/encoder queued-task inventory now uses guarded weak
  ownership for maintained queues. Hook Audio mixer/share workers additionally
  use independent shared state and pass callback-destruction tests. Panel's
  single-instance named-pipe listener now has cancellable overlapped I/O and a
  synchronous stop barrier. Panel authorization refresh no longer stores its
  companion address or queues a `this` capture: queued refresh work locks a weak
  AuthManager and storage lifetime is shared independently. The next in-scope
  batches continue with remaining active Panel service/network workers; disabled
  legacy clipboard code is retained only until that migration branch is removed.
  libwebrtc adapters and established plug-in instance ABI boundaries remain
  excluded.

## Target execution architecture

```text
concurrent publishers
        |
        v
bounded ingress queue + per-lane counters/backpressure
        |
        +--> control lane: serial strand, strict FIFO
        +--> state lane: serial strand, latest-value coalescing allowed
        +--> worker lane: bounded pool, independent slow work only
        +--> UI lane: guarded application-provided UI executor

media frames --> dedicated bounded media pipelines (outside this bus)
```

### Runtime

A shared `AsioRuntime` owns the `io_context`, work guard and a configurable
bounded worker set. Ownership is entirely smart-pointer based. Runtime stop is
idempotent, supports drain and cancel, never joins its own worker, and does not
allow new public work after stop begins.

### Lanes

- `kControl`: default lane. Connection/authentication/session/input/file
  commands that require order execute on one strand.
- `kState`: ordered state and statistics. Replaceable updates may coalesce by
  message type and key.
- `kWorker`: explicitly independent bounded work. No callback may assume
  ordering relative to another worker callback.
- `kUi`: listener supplies a guarded UI executor. The listener state is checked
  once before posting and again at execution.

Cross-lane order is not implicit. A workflow requiring order must remain on one
lane or post an explicit completion message back to the control lane.

### Dispatcher semantics

The maintained `asio2::event_dispatcher` uses a listener snapshot:

1. Lock the event map and listener list.
2. Copy strong node references for listeners active at the snapshot point.
3. Release all container locks.
4. Invoke callbacks from the snapshot.

This prevents iterator/list lifetime invalidation and permits a callback to
remove itself without deadlock. A listener removed after the snapshot may run
at most once in that already-started dispatch. Later dispatches must not invoke
it. Callback order is the snapshot order.

## Implementation phases

### Phase 1 - freeze rules and baseline

- Keep this plan, the design document and ownership audit consistent.
- Record the current build/test baseline and do not treat pre-existing
  unrelated untracked files as migration output.
- Add a repository checker for newly introduced stored raw pointers and async
  raw-`this` captures in maintained code, with explicit WebRTC/plugin/third-
  party exclusions.

Exit evidence: documents are present, `git diff --check` passes, checker scope
and exclusions are tested.

### Phase 2 - make maintained asio2 dispatch concurrency-safe

- Replace escaped map-element use with lock-in/snapshot-out dispatch and
  iteration.
- Make listener count/empty observation synchronized.
- Preserve self-unregister, unregister-another-listener, clear and listener
  ordering behavior.
- Add direct concurrent append/remove/clear/dispatch tests, including callback
  self-removal and dispatcher destruction after all operations join.

Exit evidence: focused tests pass under normal and sanitizer-capable builds;
no callback is executed through an invalidated container address.

### Phase 3 - introduce the shared Asio runtime and message lanes

- Add `AsioRuntime` with configurable worker count and smart ownership.
- Keep the existing `MessageNotifier` API source-compatible; default listener
  and publish behavior maps to `kControl`.
- Add explicit lane selection, per-lane limits/statistics, state coalescing and
  bounded worker execution.
- Ensure listener installation/removal is serialized independently of callback
  execution and all already-posted external/UI callbacks re-check state.
- Specify and implement drain/cancel behavior for every lane.

Exit evidence: FIFO control tests, cross-lane progress tests, bounded worker
tests, per-lane backpressure/statistics and all shutdown tests pass.

### Phase 4 - repair common ownership foundations

- Make `Thread` lifetime and stop/join behavior RAII-safe, including stop from
  its own callback.
- Replace `Data` implicit ownership copying with explicit value or unique
  ownership semantics.
- Replace `File` implicit handle copying with move-only RAII semantics.
- Repair `SharedPreference` locking and callback lifetime so work cannot outlive
  the owner.

Exit evidence: copy/move static assertions, destruction with queued work,
callback-triggered shutdown and repeated start/stop tests.

### Phase 5 - migrate Client

- Replace direct notifier listeners with guarded context factories.
- Remove maintained async raw-`this` captures in workspace, network, UI queue,
  timers and connection/session paths touched by notification flow.
- Keep account mode on ticket authentication and guest mode on device-password
  authentication for Windows and Web clients.
- Verify forced Relay, Direct, RTC and UDP Direct modes remain mutually
  exclusive and obey the selected mode.

Exit evidence: Client targets compile; destruction, reconnect and concurrent
Web/Windows session tests pass without crash, hang or black frame.

### Phase 6 - migrate Render

- Replace detached/raw-owner work in notification, encoder and exit paths.
- Route control/state messages to their defined lanes; retain frame transport
  on the media pipeline.
- Prove encoder and render shutdown cannot race a queued callback.

Exit evidence: Render and encoder targets compile; start/stop, disconnect and
reconnect tests pass with video/audio continuity.

### Phase 7 - migrate Panel, SDK and remaining native consumers

- Migrate Panel QObject listeners to guarded UI executors.
- Migrate SDK/native Android/relay/service listeners and timers using weak
  ownership without changing plugin or WebRTC ABI boundaries.
- Audit the repository again and resolve all in-scope notification lifetime
  findings.

Exit evidence: all native targets compile and the scoped checker reports no
new or remaining prohibited notification captures/storage.

### Phase 8 - remove obsolete event systems

- Confirm there is no source, build, package or ABI consumer of
  `MessageLooper` and remove it and its obsolete tests/build entries.
- Remove the unused vendored Dexode tree and any stale package references.
- Keep Android GreenRobot EventBus explicitly documented as a separate Java/
  Kotlin scope unless a separate migration is approved.

Exit evidence: repository searches and generated build files contain no native
Dexode/MessageLooper dependency; full build passes.

### Phase 9 - complete verification and delivery

- Run focused tests after every phase.
- Run the relevant suite 10 consecutive rounds for routine and final stability
  acceptance. The former 100-round gate was retired on 2026-08-26.
- Run `build_official` as the authoritative whole-project Windows build.
- Synchronize every changed runtime artifact into `build_official\dist` and
  prove source/destination SHA-256 equality.
- Validate locally and on `10.0.0.90`: account/ticket and guest/password;
  Windows and Web client; RTC/Direct/Relay selection; concurrent clients;
  video, audio, input, clipboard, files and multiple displays; disconnect,
  exit, reconnect and repeated connection cycles.
- Public-network-only TURN relay cases remain separately identified when no
  true cross-network environment exists; all locally reproducible TURN UDP,
  TURN TCP fallback and forced-route cases must still run.

Exit evidence: test logs, 10/10 summaries, full build result, dist hash manifest
and machine-90 acceptance record. A build-tree binary alone is not delivered.

## Required test matrix

### Dispatcher

- Concurrent dispatch with append/remove/clear.
- Self-unregister and unregister another listener inside a callback.
- Clear inside a callback.
- Listener removed after snapshot executes at most once.
- Ordering for append/prepend/insert.
- Exception propagation policy remains unchanged.

### Runtime and notifier

- Concurrent producers: no loss, duplication or per-producer reorder.
- Reentrant publish: FIFO tail, no deadlock.
- Control and state lane ordering.
- Slow worker listener does not block control or UI progress.
- Queue limits, rejection, coalescing and high-water statistics per lane.
- External executor throws; listener throws; later listeners continue.
- Unregister before dispatch, during dispatch and after external post.
- Owner destruction with queued callbacks.
- Drain/cancel from external thread and from callback.
- Repeated construct/start/stop/destruct.

### Product integration

- Guest/device-password and account/ticket authentication.
- Windows-to-host, Web-to-host and concurrent Web plus Windows sessions.
- Forced connection mode obeyed exactly.
- Video/audio/input/clipboard/file/multiple-display operation.
- Network interruption, host exit, client exit, reconnect and continuous cycles.
- Render/encoder shutdown during active and queued work.

## Completion definition

The migration is complete only when every phase has its exit evidence, all
maintained notification callbacks satisfy the smart-pointer standard, the
obsolete native event systems are gone, the 10-round gate passes,
the official build succeeds, `build_official\dist` hashes match, and local plus
machine-90 acceptance is recorded. Partial compilation or a passing focused
unit test is not completion.
