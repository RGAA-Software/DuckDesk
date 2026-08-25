# Asio Message Dispatcher Design and Engineering Rules

## Scope

GammaRay's native application messages use one standalone Asio distribution and
`asio2::event_dispatcher`. The dispatcher replaces the former Dexode EventBus
path in the Windows client, SDK, RTC, Panel, Render and shared native modules.

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

- Each `MessageNotifier` owns one `asio::io_context` worker.
- Producers use the bounded MPSC FIFO and never run listeners inline.
- `asio2::event_dispatcher` is accessed only by the worker, so listener order is
  deterministic and reentrant publication is appended at the FIFO tail.
- SDK, RTC and network listeners run on the serial dispatcher unless they
  explicitly provide another executor.
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

The notifier suite currently contains 19 cases and is also run as a 100-round
stress gate. Native acceptance covers account/ticket and guest/device-password
authentication over both standard and Direct RTC. The Web gate verifies host,
TURN/UDP and TURN/TCP selected candidates from `getStats()` and performs
repeated connect/exit cycles.

## Rollback boundary

Rollback is a source-level revert of the dispatcher change. Dexode is not linked
as a runtime fallback, which prevents two event systems from silently diverging.
