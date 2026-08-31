# GammaRay C++ Smart-Pointer and Lifetime Standard

## Applicability

This standard applies to the entire native GammaRay repository: Windows client,
Panel, Render, services, SDK, RTC, plugins, shared libraries, tests and future
native modules. It is not limited to the Asio dispatcher implementation.

## Mandatory ownership model

- **Hard gate for new code:** newly added GammaRay-owned or maintained C++ code
  must not declare, store, pass, return or capture a raw pointer. This includes
  local variables, data members, container elements, function parameters/results
  and callback parameters; using a local raw pointer temporarily is not a
  workaround. Every new or added line is subject to this zero-raw-pointer gate,
  even when surrounding legacy code still uses raw pointers.
- Use `std::unique_ptr` for exclusive ownership.
- Use `std::shared_ptr` only when ownership is genuinely shared.
- Use `std::weak_ptr` for observers and asynchronous references. Call `lock()`
  at the point of use and return immediately if it fails.
- Do not store raw pointers as object state or container elements.
- Do not capture `this` or another raw pointer in a listener, timer, queued UI
  call, network/RTC callback, worker task, coroutine or delayed operation.
- Do not use manual `new`/`delete` for owned resources. Use `make_unique`,
  `make_shared` or a dedicated RAII handle with a typed deleter.
- Avoid ownership cycles. The normal asynchronous pattern is an owner-held
  `shared_ptr` plus callback-held `weak_ptr`, not callback-held `shared_ptr`.

## API and ABI boundaries

C, Windows, Qt and third-party APIs sometimes expose raw handles or pointers.
Those values may be used only transiently at the call boundary. An owned result
must be wrapped immediately in an RAII type. A borrowed boundary pointer cannot
be retained in state or captured by asynchronous work. Boundary lifetime
assumptions must be documented beside the adapter.

When a new declaration or an existing plug-in loader allocation is unavoidable
because an external ABI requires its established pointer representation, keep
it in the smallest adapter and append
`NOLINT(gammaray-raw-pointer-boundary)` with the ABI and lifetime reason on that
line. This marker is not permitted for ordinary project APIs, local variables,
stored state or asynchronous callbacks and requires code review. It must not be
used to create a new project-owned ownership model; the loader case only
preserves a pre-existing plug-in identity and process-lifetime contract.

`src/px_deps/px_webrtc_client` is a deliberate structural exception. Its
borrowed observer, SDP, track and callback pointers mirror libwebrtc APIs and
must not be mechanically converted to C++ smart pointers or used as a reason to
redesign the WebRTC object model. Keep that adapter's native lifetime contract;
apply this standard to project-owned objects and queued work around the adapter.

Other third-party source trees are read-only and retain their upstream
ownership conventions. The standard applies to GammaRay-owned modules and to
dependencies explicitly maintained by this project, including the vendored
asio2 integration. Adding another maintained dependency to this scope requires
an explicit repository decision; it must not be inferred from its location.

## QObject and plugin considerations

Qt parent ownership and C++ smart-pointer ownership are two alternative
ownership models. They must never be layered on the same object:

```cpp
// Correct: Qt owns deletion; QPointer is only a guarded observer.
QPointer<QWidget> tooltip =
    new QWidget(parent); // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns it.

// Correct: no Qt parent; unique_ptr is the sole owner.
auto detached = std::make_unique<QWidget>();
```

The following patterns are prohibited:

```cpp
auto tooltip = std::make_unique<QWidget>();
tooltip->setParent(parent);
QPointer<QWidget> observed = tooltip.get();
tooltip.release(); // Prohibited ownership transfer.

auto duplicate_owner =
    std::unique_ptr<QWidget>(new QWidget(parent)); // Two deletion authorities.
```

A Qt-parented object must be created directly at the smallest annotated Qt API
boundary, immediately given its parent, and retained only through `QPointer`
when observation is needed. Do not use `setParent()` plus `release()` to hand a
smart-owned object to Qt. Do not put a parented object into `unique_ptr` or
`shared_ptr`. Conversely, `QPointer` is non-owning and cannot be the only
lifetime authority for a parentless object. These rules apply equally to
widgets, layouts, actions, timers and project-defined `QObject` subclasses.

Qt parent ownership does not make a raw pointer safe for queued work. QObject
callbacks use a smart-owned controller/model plus a guarded Qt reference where
Qt requires one. Before every queued use, test the `QPointer`; never capture the
transient boundary pointer.

Existing plug-in instance boundaries are a compatibility exception: do not
change `GetInstance`, loader-owned library handles, ABI singleton pointers,
instance identity, unload timing, or their established creation/destruction
contract. Project-owned work around that boundary should still use safe
lifetime guards without altering the plug-in instance model.

## Change policy for legacy code

Any legacy code touched by a feature or fix must migrate the affected ownership
and callback chain. It is not acceptable to copy an existing raw-pointer pattern
into new code. Larger unrelated legacy areas may be migrated incrementally, but
each change must reduce or leave unchanged the repository's measured raw-pointer
debt and must introduce zero new asynchronous raw captures.

## Review and verification

Reviews must explicitly check construction, destruction, unregister, shutdown,
reconnect and callback ordering. Tests must include callbacks queued before
destruction, owner expiry, repeated start/stop, concurrent unregister and
shutdown invoked from inside a callback. Static checks reject newly added
raw-pointer declarations, `[this]` captures, manual ownership and
smart-pointer-to-Qt-parent `release()` transfers. A reviewed external-ABI or
direct Qt-parent construction boundary is the only annotated exception.

Run `cmake --build build_official --target check_cpp_ownership` before native
code review. The checker examines added lines in the working tree and rejects
asynchronous `this` captures, manual `new`/`delete`, and obvious raw-pointer data
members. `scripts/check_cpp_ownership.ps1 -Staged` applies the same gate to the
staged patch. `-ReportAll` inventories historical debt for incremental
migration; it is expected to fail until that debt reaches zero. Unmaintained
third-party code and `src/px_deps/px_webrtc_client` are intentionally excluded;
the maintained asio2 tree remains in scope.
