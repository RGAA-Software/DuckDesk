# Asio Message Dispatcher Design and Engineering Rules

## Scope

The target architecture for GammaRay's native application messages uses one
standalone Asio distribution and `asio2::event_dispatcher`. It replaces the
former Dexode EventBus path in the Windows client, SDK, RTC, Panel, Render and
shared native modules.

This document covers native C++ only. Android Java/Kotlin currently continues
to use GreenRobot EventBus and is a separate migration scope.

## Implementation status (2026-08-26)

The source migration and current-environment delivery are complete. The final
whole-product build, repeated tests, published-artifact hash checks and
machine-90 runtime acceptance all passed.

Completed:

- Native CMake targets no longer link `Dexode::EventBus`.
- Client, native Android context, SDK connection implementations, Panel,
  Render, relay and encoder code use the common `MessageNotifier` API.
- `MessageNotifier` owns isolated Asio control, state and bounded worker
  runtimes. Dispatcher mutation and dispatch stay on the control runtime;
  state callbacks are ordered and worker callbacks are explicitly parallel.
  Its 31 focused tests cover FIFO, reentrancy, concurrent producers, lane
  isolation/backpressure, cancellation, callback exceptions and shutdown.
- The maintained `asio2::event_dispatcher` now snapshots listeners under lock
  and invokes them after unlocking. Five focused tests cover self-remove,
  clear-from-callback, removal after snapshot, ordering and concurrent
  append/remove/clear/dispatch.
- Guarded UI listener factories exist in Client and Panel contexts.
- Client, Panel, Render, SDK, encoder, relay, Android native code and the legacy
  Service paths now register notification callbacks through guarded weak/QPointer
  lifetime boundaries and perform idempotent shutdown.
- The obsolete native `MessageLooper` implementation and its tests/build entries
  have been removed.
- The unused vendored Dexode EventBus tree has been removed. Android GreenRobot
  EventBus remains a deliberately separate Java/Kotlin mechanism.
- The authoritative Windows build and Android official Release APK build pass.
- Shutdown, destruction-with-queued-callbacks, concurrent unregister and
  repeated start/stop coverage passes the required 10-round gate.
- Changed runtime artifacts are published to `build_official\\dist`; source,
  dist and machine-90 SHA-256 values match. The final evidence is in
  `docs/asio_notify_concurrency_acceptance_report_20260826.md`.

Accordingly, implementation and current-environment delivery are complete.
Cross-public-network TURN/NAT remains separately blocked by environment and is
not claimed by this design acceptance.

## Concurrency status (2026-08-26)

The dispatcher and product-consumer migration are implemented, and the scoped
full-product concurrency acceptance has passed.

What exists today:

- Multiple producer threads may publish concurrently. Queue insertion is
  protected and the focused tests cover concurrent producers.
- One `MessageNotifier` owns isolated Asio control and state contexts plus a
  configurable bounded worker context (two worker threads by default).
- Listener installation, removal, ingress FIFO and dispatch are serialized on
  the control context.
- Reentrant publication is supported and is appended to the FIFO tail.
- State callbacks preserve order without blocking control callbacks.
- Worker callbacks run in parallel without blocking control or state, have an
  independent bound and can safely install/uninstall listeners.
- Drain and cancel work from control, state and worker callbacks without
  self-join.

Intentional compatibility boundaries and optional follow-ups:

- Known UI, state and independent worker consumers use explicit lanes.
  Unchanged consumers intentionally remain on the ordered control lane; this is
  the specified compatibility behavior, not an unclassified migration gap.
- Per-product fairness tuning beyond the bounded-lane and starvation tests is a
  performance enhancement rather than a migration blocker.
- The legacy `find_listeners()` address-returning compatibility API is not used
  by GammaRay and remains unsuitable for concurrent external use; safe
  dispatch/iteration APIs use snapshots.

The target must not simply run all callbacks in parallel. Control transitions
that require order stay on a serial strand; independent and potentially slow
work uses explicit bounded executors. UI callbacks use the guarded UI executor,
and video/audio payloads stay outside the application message bus. The accepted
implementation provides the executor/lane policy, safe concurrent dispatcher
access, per-lane backpressure and shutdown/drain coverage.

## Lifetime rule

The repository-wide requirements are defined in
`docs/cpp_smart_pointer_standard.md` and apply to every native module.

Raw pointers are forbidden as stored state and in asynchronous callback
captures. Owned objects use `std::unique_ptr` or `std::shared_ptr`. Timers,
network callbacks, RTC callbacks, worker tasks, message listeners and UI tasks
capture `std::weak_ptr`, call `lock()` immediately before use, and abandon the
operation when the object has expired. Unavoidable C, Windows, Qt or third-party
ABI pointers are transient boundary values only and cannot escape into queued
work.

## Execution model

- Each `MessageNotifier` owns a shared-lifetime `AsioRuntime` with isolated
  control and state contexts plus a configurable worker context.
- Producers use the bounded MPSC FIFO and never run listeners inline.
- `asio2::event_dispatcher` mutation and dispatch are accessed on control;
  listener snapshots make its maintained direct APIs safe against concurrent
  mutation.
- SDK, RTC and network listeners remain on ordered control unless they select
  the ordered state or bounded worker lane.
- Qt consumers provide a guarded executor. It posts work to the QObject's event
  loop and checks listener lifetime again immediately before invocation.
- Replaceable timer/state messages use `PublishLatestAppMessage`; commands and
  transitions use `PublishAppMessage`/the compatible `SendAppMessage` API.
- Video and audio payload streams remain on their dedicated bounded media
  queues. The application message queue carries control and state, not every
  media frame.

## Backpressure and failure isolation

The default pending limit is 4096. Full queues reject new non-coalesced messages
without blocking producers and increment metrics. Listener exceptions are
caught per callback, slow callbacks are logged, and one faulty listener cannot
terminate the worker or suppress later listeners.

Available statistics are posted, dispatched, rejected, coalesced, callback
exceptions, high-water mark and current pending count.

## Shutdown

Drain shutdown stops accepting public messages and waits until the FIFO is
actually empty and no callback is running, including work published
reentrantly by a listener. It then invalidates listener states and joins the
worker. Cancel shutdown
invalidates listeners immediately and clears queued work. Shutdown requested by
a listener cannot self-join; ownership of the worker thread is handed to a
smart-lifetime reaper. UI callbacks posted before unregister or shutdown
re-check their listener state and become no-ops.

## Required tests

- Dedicated asynchronous dispatch thread and FIFO ordering.
- Reentrant publication without deadlock.
- Multiple concurrent producers with no loss, duplication or per-producer
  reordering.
- Slow-listener producer isolation and listener exception isolation.
- Queue bound rejection and latest-state coalescing.
- Listener unregister before and after dispatch.
- External/UI executor routing and cancellation of already-posted callbacks.
- Drain/cancel shutdown, shutdown from a callback and repeated destruction.
- Client, RTC DLL, Panel and Render compilation, followed by direct and standard
  RTC runtime acceptance from `build_official\\dist`.

The notifier suite currently contains 31 cases and the dispatcher concurrency
suite contains five. Both passed the routine 10-round gate on 2026-08-25. The
complete stability gate also runs 10 rounds; the former 100-round requirement
was retired by the project owner on 2026-08-26. Native acceptance
covers account/ticket and guest/device-password
authentication over both standard and Direct RTC. The Web gate verifies host,
TURN/UDP and TURN/TCP selected candidates from `getStats()` and performs
repeated connect/exit cycles.

## Rollback boundary

Rollback is a source-level revert of the dispatcher change. Dexode is not linked
as a runtime fallback, which prevents two event systems from silently diverging.
