# Workspace delivery rules

- `build_official.bat` is a release-only full build. Do not run it for routine
  development, focused verification, or incremental C++ changes unless the
  user explicitly requests a release/full build.
- Routine C++ work must use the `build_cpp_*.bat` entry points, which build
  only the requested CMake targets and do not bump versions, run npm, or build
  Rust workspaces.
- The user starts and validates the Windows client from `build_official\dist`.
- A client-side build is not considered delivered until every changed runtime artifact (including executables, DLLs, language resources,
  and web assets when applicable) has been synchronized into `build_official\dist`.
- Before reporting a build ready for validation, verify that the relevant build-tree artifacts and their `build_official\dist` copies have
  matching SHA-256 hashes. If a destination file is in use, stop the corresponding process, publish the artifact, and re-run the hash check.

# Project-wide modern C++ ownership and asynchronous safety

- `src/px_render/hook_capture/win/hk_audio/InProcessLoopbackCapture.h` and
  `InProcessLoopbackCapture.cpp` are retained project implementations. Do not
  delete, rename, replace, stub out, or exclude them as dead code; changes in
  this area must preserve the class and capture path and modify the existing
  implementation in place.
- **Hard gate for all new code:** New GammaRay-owned or project-maintained C++ code must not declare, store, return, pass, or capture raw
  pointers. This includes local variables, members, container elements, function parameters/results, callback parameters, and `this`
  captures. Use smart pointers or typed RAII handles from the first ownership boundary; a temporary local raw pointer is not an acceptable
  workaround.
- This is a repository-wide rule for every C++ module, not a feature-local convention. Project C++ code must not store or capture raw
  pointers, including asynchronous `[this]` captures.
- Existing legacy code in the scope of a change must be migrated to this rule as part of that change. New code must never add more
  raw-pointer lifetime debt.
- Express exclusive ownership with `std::unique_ptr`, shared lifetime with `std::shared_ptr`, and non-owning asynchronous references with
  `std::weak_ptr` followed by `lock()` at the point of use.
- Listener, timer, network, RTC, worker-thread, and UI-queue callbacks must capture a smart pointer. Prefer `weak_ptr` so callbacks do not
  create ownership cycles, and return immediately when `lock()` fails.
- A raw pointer required by a C API, operating-system API, Qt parent API, or third-party ABI may exist only as a transient boundary value.
  It must not be retained, used to express ownership, or captured by asynchronous work; wrap owned resources immediately in an appropriate
  RAII smart handle.
- Qt parent ownership and C++ smart-pointer ownership are mutually exclusive. A `QObject` owned by a Qt parent must be created directly at
  the annotated Qt boundary and observed with `QPointer`; never place it in `unique_ptr`/`shared_ptr`, call `setParent()`, and then
  `release()`, and never leave both a Qt parent and a smart pointer responsible for deletion. A parentless `QObject` may instead remain
  exclusively smart-owned, but `QPointer` alone is never an owner.
- Do not redesign `src/px_deps/px_webrtc_client` to remove libwebrtc's borrowed ABI, observer, track, SDP or callback pointers. That adapter
  follows libwebrtc's own lifetime contract and is excluded from the repository smart-pointer migration gate; changes there require a
  separate WebRTC-specific review.
- Do not redesign existing plug-in instance boundaries (`GetInstance`, loader-owned library handles, ABI singleton pointers, or their
  established creation/destruction contract). These are compatibility exceptions and may retain their existing raw-pointer representation.
  Improvements around them must not change plug-in instance identity, ownership, unload timing, or callback ABI.
- Product decision: the Windows Client `clipboard.dll`, `ft.dll`, and `record.dll`
  boundaries are retired and are excluded from the compatibility exception above.
  Their retained implementations must be built as internal Client modules and linked
  into `px_client`; do not preserve or reintroduce `GetInstance`, runtime DLL loading,
  generic plug-in event routing, or independent Client plug-in packaging for these
  three features. This decision does not apply to Render plug-ins or any other ABI.
- Apply these rules only to GammaRay-owned code and dependencies explicitly maintained by this project (including the vendored asio2
  integration). Other third-party source trees are read-only: do not mechanically reformat, modernize, or change their ownership model.
- Code review and tests must cover destruction with queued callbacks, unregister during dispatch, shutdown from a callback, and repeated
  start/stop so that smart-pointer use is verified behaviorally rather than only syntactically.
- The complete project standard is documented in `docs/cpp_smart_pointer_standard.md` and is mandatory for all client, Panel, Render,
  service, SDK, RTC, plugin and shared-library code.

# Project-wide C++ initialization, design, and formatting rules

- Every GammaRay-owned C++ object, data member, scalar, enum, atomic, handle, and local variable must be deterministically initialized before
  first use. Prefer in-class member initializers and value initialization (`{}`); constructors must establish a complete valid state and
  must not expose or schedule work against a partially initialized object.
- A fallible or asynchronous initialization sequence must use a factory or explicit `Create`/`StartAsync` result. Destruction must remain safe
  after every partial-failure point. Absence is represented by `std::optional`, a null smart pointer, or a typed state, never by an
  uninitialized value or undocumented sentinel.
- Pointer ownership in project code is expressed only with `std::unique_ptr`, `std::shared_ptr`, `std::weak_ptr`, `QPointer` at an annotated
  Qt-parent boundary, or a typed RAII handle. Prefer values, references, `std::span`, or `std::reference_wrapper` for synchronous non-owning
  access; asynchronous access must use `weak_ptr` and `lock()`.
- Code structure must follow explicit responsibilities and dependency direction: composition roots create concrete modules,
  constructor/factory injection supplies required capabilities, external APIs stay behind adapters, and lifecycle transitions live in
  explicit state machines or workflow objects. Prefer composition over inheritance.
- Apply design patterns only where they make ownership, variability, or lifecycle clearer. Do not add service locators, mutable global
  singletons, generic `void*`/`std::any` bags, speculative interfaces, or inheritance layers for built-in modules. Interfaces must be small,
  typed, capability-specific, and backed by a real extension boundary.
- Resource acquisition, subscriptions, registrations, locks, threads, timers, library handles, and cancellation ownership must all be
  represented by RAII types. Cleanup order must be the reverse of dependency construction and repeated stop/destroy must be safe.
- Project-authored C++ uses a 150-column limit. Keep a statement on one line when it fits within 150 columns; wrap only when it exceeds the
  limit or when a deliberate multiline layout materially improves readability. Generated code, vendored third-party code, URLs, and
  unavoidable external literals are excluded.
- The repository `.clang-format` is the formatting authority for project-authored C++. Do not mechanically reformat unrelated legacy files
  or read-only third-party trees.
