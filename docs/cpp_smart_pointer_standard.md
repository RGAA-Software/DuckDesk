# GammaRay Modern C++ Ownership, Initialization, and Lifetime Standard

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

Synchronous non-owning access should use a reference, `std::span`, or
`std::reference_wrapper` when absence is not valid. A nullable relationship must
use an appropriate smart pointer, `std::optional`, `QPointer` at a Qt-parent
boundary, or a typed RAII handle. It must not be represented by a project raw
pointer, integer address, `void*`, or `std::any` ownership bag.

## Deterministic initialization

Every project-owned C++ declaration must have a deterministic value before its
first use. This is a hard gate for new code and for legacy code in the scope of a
change.

- Initialize scalar, enum, atomic, handle, array, smart-pointer, and state members
  at the declaration whenever the value does not depend on constructor input.
- Use value initialization (`{}`) for local values and aggregate state. Do not
  declare an uninitialized local and rely on a later branch to assign it.
- A constructor must establish the complete class invariant. It must not publish
  the object, register a callback, start a thread, or spawn a coroutine before the
  object is fully initialized and, when needed, managed by `shared_ptr`.
- Fallible construction uses a factory returning a typed result or smart pointer.
  Asynchronous activation uses `StartAsync`; destructors and `StopAsync` must be
  safe after every partial initialization failure.
- Use `std::optional<T>` or an explicit state enum for absence and lifecycle state.
  Do not use uninitialized storage, undocumented sentinel integers, or an invalid
  enum value as hidden state.
- Initialize members in declaration order. Do not depend on constructor initializer
  list order or on zero-filled allocator/debug-build behavior.
- Do not use `memset` to initialize a non-trivial C++ object. Use constructors,
  member initializers, or typed factory functions.
- Output-only values required by a C, Win32, Qt, or third-party API are transient
  boundary exceptions. Value-initialize their storage before the call, validate the
  result, and immediately wrap any acquired resource in its RAII owner.

Example:

```cpp
enum class ConnectionState {
    kStopped,
    kConnecting,
    kReady,
    kStopping,
};

class ConnectionWorkflow final {
public:
    static PxResult<std::shared_ptr<ConnectionWorkflow>> Create(const std::shared_ptr<PxAsyncRuntime>& runtime);

private:
    std::weak_ptr<PxAsyncRuntime> runtime_;
    std::atomic<ConnectionState> state_{ConnectionState::kStopped};
    std::uint64_t generation_{0};
    std::optional<std::chrono::steady_clock::time_point> deadline_{};
};
```

## Architecture and design constraints

Good design in this repository means that ownership, responsibility, dependency
direction, and lifecycle are visible in types. Patterns are tools, not a reason to
add abstraction.

- A composition root creates concrete modules and owns their dependency graph.
  Business code must not discover dependencies through a service locator, generic
  plugin registry, mutable global singleton, or string/UUID lookup.
- Dependencies are supplied explicitly through constructors or typed factories.
  Optional capabilities use a typed optional object, not a generic property bag.
- Use an Adapter at C, Win32, Qt, WebRTC, asio2, and other external boundaries.
  Borrowed ABI values remain inside the adapter and are converted immediately to
  owned values, smart pointers, or typed RAII handles.
- Use explicit workflow/state-machine objects for start, reconnect, timeout,
  cancellation, and shutdown. Do not distribute one lifecycle transition across
  unrelated callbacks and boolean flags.
- Use Strategy only for real replaceable algorithms such as encoder backends. Use
  Observer only with RAII registration tokens, weak callback lifetime, snapshot
  dispatch, and defined unregister-during-dispatch behavior.
- Prefer composition over inheritance for built-in modules. A virtual interface is
  justified only by a stable extension point or multiple independently replaceable
  implementations; otherwise use a concrete type and capability-specific methods.
- Keep interfaces small, typed, and cohesive. Generic `OnMessage(void*)`,
  `std::any` service bags, catch-all managers, and speculative extension layers are
  prohibited in new project code.
- A class owns one coherent responsibility. Split transport, protocol parsing,
  domain decisions, persistence, and diagnostics at explicit boundaries; do not use
  file length alone as the reason for a split.
- Resource cleanup follows reverse dependency order. Start/stop are idempotent,
  partial start is rollback-safe, and no destructor depends on another object that
  has already been destroyed.

## Formatting

Project-authored C++ uses the repository `.clang-format` and a hard 150-column
limit. Keep code on one line while it fits within 150 columns; wrap when it exceeds
that limit or when a deliberate multiline table, initializer, fluent expression,
or algorithmic grouping is materially clearer. Do not retain legacy 80-column
wrapping in newly written code merely for consistency with nearby code.

Generated sources, vendored/read-only third-party trees, URLs, and unavoidable
external literals are exempt. Formatting a touched file must not become a
repository-wide mechanical rewrite; format the changed declarations and logical
blocks only.

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

The Client `clipboard.dll`, `ft.dll`, and `record.dll` boundaries are explicitly
retired by product decision. Their implementations are internal, statically
linked Client modules and are not covered by the compatibility exception. This
authorization is limited to those three Client features; Render plug-ins and all
other established plug-in ABIs keep their existing contracts.

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

Review must also reject a newly added scalar, enum, atomic, handle, state member,
or local value that has no deterministic initializer. Every constructor must be
checked against declaration order and partial-failure cleanup. Architecture review
must identify the composition owner, injected dependencies, external Adapter,
lifecycle workflow/state machine, and RAII cleanup token where those concepts
apply; naming a design pattern without enforcing these invariants is insufficient.

Run `clang-format --style=file --dry-run --Werror` on changed project-owned C++
files. The root configuration enforces the 150-column policy. Do not include
generated or read-only third-party sources in a mechanical formatting pass.

Run `cmake --build build_official --target check_cpp_ownership` before native
code review. The checker examines added lines in the working tree and rejects
asynchronous `this` captures, manual `new`/`delete`, and obvious raw-pointer data
members. `scripts/check_cpp_ownership.ps1 -Staged` applies the same gate to the
staged patch. `-ReportAll` inventories historical debt for incremental
migration; it is expected to fail until that debt reaches zero. Unmaintained
third-party code and `src/px_deps/px_webrtc_client` are intentionally excluded;
the maintained asio2 tree remains in scope.
