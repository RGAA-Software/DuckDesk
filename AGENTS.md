# Workspace delivery rules

- The user starts and validates the Windows client from `build_official\dist`.
- A client-side build is not considered delivered until every changed runtime artifact (including executables, DLLs, language resources, and web assets when applicable) has been synchronized into `build_official\dist`.
- Before reporting a build ready for validation, verify that the relevant build-tree artifacts and their `build_official\dist` copies have matching SHA-256 hashes. If a destination file is in use, stop the corresponding process, publish the artifact, and re-run the hash check.

# Project-wide modern C++ ownership and asynchronous safety

- This is a repository-wide rule for every C++ module, not a feature-local convention. Project C++ code must not store or capture raw pointers, including asynchronous `[this]` captures.
- Existing legacy code in the scope of a change must be migrated to this rule as part of that change. New code must never add more raw-pointer lifetime debt.
- Express exclusive ownership with `std::unique_ptr`, shared lifetime with `std::shared_ptr`, and non-owning asynchronous references with `std::weak_ptr` followed by `lock()` at the point of use.
- Listener, timer, network, RTC, worker-thread, and UI-queue callbacks must capture a smart pointer. Prefer `weak_ptr` so callbacks do not create ownership cycles, and return immediately when `lock()` fails.
- A raw pointer required by a C API, operating-system API, Qt parent API, or third-party ABI may exist only as a transient boundary value. It must not be retained, used to express ownership, or captured by asynchronous work; wrap owned resources immediately in an appropriate RAII smart handle.
- Do not redesign `src/px_deps/px_webrtc_client` to remove libwebrtc's borrowed ABI, observer, track, SDP or callback pointers. That adapter follows libwebrtc's own lifetime contract and is excluded from the repository smart-pointer migration gate; changes there require a separate WebRTC-specific review.
- Do not redesign existing plug-in instance boundaries (`GetInstance`, loader-owned library handles, ABI singleton pointers, or their established creation/destruction contract). These are compatibility exceptions and may retain their existing raw-pointer representation. Improvements around them must not change plug-in instance identity, ownership, unload timing, or callback ABI.
- Apply these rules only to GammaRay-owned code and dependencies explicitly maintained by this project (including the vendored asio2 integration). Other third-party source trees are read-only: do not mechanically reformat, modernize, or change their ownership model.
- Code review and tests must cover destruction with queued callbacks, unregister during dispatch, shutdown from a callback, and repeated start/stop so that smart-pointer use is verified behaviorally rather than only syntactically.
- The complete project standard is documented in `docs/cpp_smart_pointer_standard.md` and is mandatory for all client, Panel, Render, service, SDK, RTC, plugin and shared-library code.
