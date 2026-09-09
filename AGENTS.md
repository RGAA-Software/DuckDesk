# Workspace delivery rules

- `scripts_build\build_official.bat` is a release-only full build. Do not run it for routine
  development, focused verification, or incremental C++ changes unless the
  user explicitly requests a release/full build.
- Routine C++ work must use the `scripts_build\build_cpp_*.bat` entry points, which build
  only the requested CMake targets and do not bump versions, run npm, or build
  Rust workspaces.
- The user starts and validates the Windows client from `build_official\dist`.
- A client-side build is not considered delivered until every changed runtime artifact (including executables, DLLs, language resources,
  and web assets when applicable) has been synchronized into `build_official\dist`.
- Before reporting a build ready for validation, verify that the relevant build-tree artifacts and their `build_official\dist` copies have
  matching SHA-256 hashes. If a destination file is in use, stop the corresponding process, publish the artifact, and re-run the hash check.

# Project-wide modern C++ ownership and asynchronous safety

- Game Hook product decision (2026-09-09): only hook Apps launched by this product. Admission is an AND condition:
  membership in this launch's private Windows Job AND a matching normalized full executable path. A matching basename,
  matching full path alone, or an independently restarted process never authorizes adoption, injection or cleanup.
  Assign the suspended root to the Job before execution; configured view executables must also be descendants in that Job.
  Do not restore Steam URL discovery/adoption. Preserve Windows path spaces, Unicode and argument quoting.
  Service cleanup must not sweep game/view processes by executable path or adopt a replacement Render by port.
  Stopping an already Stopped instance is an idempotent success; do not downgrade it to Failed or touch reused PID/port resources.

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

# Retired code archival during Native/WebRTC simplification

- User decision (2026-09-07): preserve retired implementations under the repository-root `backup/` directory instead of deleting them.
  This supersedes deletion wording in earlier Native SDK and Android plans for this simplification work.
- Preserve repository-relative paths beneath a named archive batch, for example
  `backup/native_transport_simplification/src/px_deps/px_client_sdk/connection/`.
- For a fully retired file, archive the file intact. Before removing retired branches from a still-maintained file, archive its full
  pre-change contents. Preserve uncommitted contents as well; never reconstruct the backup solely from Git HEAD.
- Record the original path, base revision, local modification status, retirement reason and archive batch. Never overwrite an existing
  backup; create a new batch when necessary.
- Archived code is reference-only: exclude it from build discovery, compilation, tests, packaging and runtime loading. Do not create an
  executable compatibility layer or maintain a second product implementation in `backup/`.
- Code still required by Native or WebRTC consumers remains active. External reference checkouts and read-only third-party trees retain
  their existing protections.

# Local upstream source references

Local reference checkouts (original references confirmed on 2026-09-07; session/RDP references added on 2026-09-08):

- RustDesk: `D:/source/rustdesk` — connection establishment, NAT traversal/relay, session and file-transfer architecture. Path updated and verified on 2026-09-08; use this external checkout for all RustDesk references and do not clone a second copy under this repository.
- Sunshine: `D:/source/Sunshine` — host-side media transport, UDP packetization, FEC and pacing.
- Moonlight Qt: `D:/source/moonlight-qt` — client-side SDK integration, media reception and platform adaptation; inspect its shared-core
  submodules when populated.
- Dolit streamer: `D:/dolit/streamer` — capture inside Windows RDP sessions, desktop readiness and DDA/GDI recovery.
- Dolit AppGuard: `D:/dolit/dlAppGuard` — user provisioning, loopback RDP login, session supervision and exact lifecycle reference.
- Project-owned Qt RDP client: `D:/dolit/rdp` — substantial but unfinished implementation; prioritize reuse of its protocol lifecycle,
  graphics/input and clipboard handling for the new RDP mode. Feature/validation inventory: `docs/rdp_qt_client_reuse_inventory.md`.
  Its FreeRDP source is `D:/dolit/rdp/FreeRDP`. Existing GUI integration and local patches do not establish headless bridging or 60 FPS.

Prefer inspecting these local sources for implementation comparisons. Record the checkout revision when making version-sensitive claims;
do not assume a local checkout matches the latest upstream release. Treat these repositories as read-only references unless the user
explicitly requests changes to them. Their availability does not expand the current implementation scope.

# Enterprise isolated desktop planning

- User approval (2026-09-09): maintain a minimal, version-pinned FreeRDP decoder patch for the RDP mode.
  Keep the original `D:/dolit/rdp` reference and pristine upstream checkout read-only. Store reviewed patches in
  `patches/freerdp/`, apply them only to an isolated build copy, and record base revision plus patch/runtime hashes.
  This exception permits the targeted decoder fix, not unrelated third-party modernization or ownership redesign.

- User decision (2026-09-08): existing game-hook and webview modes do not create Windows users. Preserve their current user environment
  and app-instance lifecycle; do not add an RDP-login prerequisite for them.
- Latest user direction (2026-09-08): add an RDP application mode parallel to game-hook and webview. FreeRDP receives the target user's
  session graphics and carries input; do not additionally capture that desktop with DDA/GDI or fall back to host desktop/input.
  Dedicated users and Windows sessions apply to this new mode; it does not change game-hook/webview user provisioning.
- Current design and validation stages: `docs/rdp_application_mode_design.md`. Earlier keepalive-plus-capture design, repository revisions
  and FreeRDP/60-FPS references remain in `docs/enterprise_windows_session_isolation_plan.md` as historical research.
  Neither document is evidence of an implemented or benchmarked capability. Public FreeRDP callbacks are the first integration candidate;
  modifying its internals remains conditional on concrete limitations, and reference checkouts remain read-only.
- Superseding RDP decisions (2026-09-08): preserve native RDP encoding; GammaRay provides provisioning, authorization, proxy transport
  and session management, with RDP decoding/composition/display in the Client. Do not decode and re-encode video in Render.
  The user permits reuse of the project-owned Qt demo; reuse must still meet project C++ standards, and does not authorize unrelated edits.
- Clarified RDP lifecycle (2026-09-08): Render, proxy processes and RDP connections may stop when access ends. Preserve Windows accounts,
  profiles and logged-in sessions: ordinary disconnect/stop, idle time, ticket expiry or transport failure must not trigger logoff,
  account/profile deletion or termination of workspace applications. Explicit destructive administration is a separate authorized action.
  On the next authorized visit, start the required runtime and reconnect to the existing session when available; verify identity and state
  instead of blindly creating another session or relaunching applications. Do not keep reconnecting after an intentional stop.
  No always-on Render or backend RDP protocol endpoint is required. A full RDP tunnel remains a candidate, subject to authentication and
  channel-policy design; preserving a Windows session does not require preserving the old transport connection.
  Render must retain the existing last-client-disconnect grace/timeout exit behavior: reconnecting during the grace period cancels the
  pending exit, and remaining clients prevent it. Do not introduce immediate exit, an RDP-specific idle timer or an always-on runtime.
  Closing Render and its RDP transport must not be implemented as Windows session logoff or workspace-application cleanup.
  The RDP mode is planned for one active frontend per workspace/session, not existing multi-viewer broadcast semantics. Its sole
  client's departure triggers the same grace/timeout behavior. Multiple channels for that client are not multiple occupants.
  User-confirmed admission policy: reject a second client when the workspace is busy. Explicit takeover is not in the first version.
  Do not introduce automatic takeover or a server-wide single-user lock; other modes remain unchanged.
  Section 0 of `docs/rdp_application_mode_design.md` supersedes its retained earlier re-encoding design and cleanup assumptions.
- RDP implementation entry point: `docs/rdp_application_mode_implementation_plan.md`. Validate the existing FreeRDP proxy and an opaque
  tunnel baseline before freezing authentication/transport boundaries. Do not silently expose production Windows credentials to clients.
  A dedicated reliable RDP protocol carrier is a proposed mode-specific extension, not restoration of Native WS video fallback,
  RTC, KCP, Relay or public P2P. Windows Client/Console and node 10.0.0.90 are the first validation scope; other RDP clients are not yet supported.
