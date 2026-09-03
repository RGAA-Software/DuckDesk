# Client Built-in Modules Migration

## Decision and scope

The Windows Client clipboard, file-transfer, and media-recording features are
product capabilities, not independently deployable extensions. Their existing
implementations are retained, but the `clipboard.dll`, `ft.dll`, and `record.dll`
runtime boundaries are retired. The three features are linked into `px_client`
as internal modules.

This change is limited to `src/px_client`. Render plug-ins and all other plug-in
ABIs remain unchanged.

## Target architecture

`ClientModuleManager` is the composition root for three explicit modules:

- `ClientClipboardModule`
- `ClientFileTransferModule`
- `ClientMediaRecordingModule`

Their sources live under `src/px_client/modules`; no Client feature module is
configured through the legacy `src/px_client/plugins` build entry point.

The manager owns modules with smart pointers, routes only relevant protocol
messages to each module, synchronizes typed settings, and stops modules before
releasing them. Modules call a typed `ClientModuleServices` interface for media
or file-channel sends, application notifications, and file-transfer audit
events. There is no directory scan, `QLibrary`, `GetInstance`, ABI singleton,
generic plug-in event hierarchy, or message broadcast.

The recording feature module and its FFmpeg remux engine are statically linked
into `px_client.exe`. The RTC implementation remains isolated in
`px_client_rtc.dll`, which the SDK loads at runtime. Its private `webrtc.lib`
dependency is not propagated into the Client executable's link interface.

The file-transfer-only Client mode creates only the file-transfer module. Normal
Client mode creates all three modules.

## Ownership and shutdown

- The workspace and context share the module manager; the context observes it
  weakly so it cannot form an ownership cycle.
- The manager owns each module using `shared_ptr`; module callbacks capture
  `weak_ptr` and return when the module has expired.
- Parentless top-level Qt windows have one smart owner. Qt-parented children are
  observed with `QPointer` and are never smart-owned at the same time.
- `Stop()` is idempotent. It rejects new queued work, stops worker threads and
  file-transfer sessions, closes top-level windows, and then releases state.

## Message routing

- Recording receives only encoded video and audio frame messages.
- Clipboard receives only clipboard metadata, response, and virtual-file chunk
  messages.
- File transfer receives only `kFileAction` and `kFileResponse` messages.
- Other Client protocol handling remains in `BaseWorkspace`.

## Migration stages

1. Add the module manager, typed settings/services, and static CMake targets.
2. Convert recording, clipboard, and file transfer without rewriting their
   recorder, clipboard platform, virtual-file, transfer engine, or UI cores.
3. Replace workspace/context/UI plug-in lookups and broadcast routing.
4. Remove the three Client DLL exports, loader, ABI event bridge, packaging, and
   obsolete DLL build dependencies.
5. Replace DLL unload tests with repeated start/stop, queued-callback shutdown,
   reconnect, file-transfer-only, and targeted-routing tests.
6. Run focused C++ builds/tests, publish changed runtime files to
   `build_official/dist`, and compare SHA-256 hashes.

## Acceptance criteria

- `px_client` starts without `deps/ct_plugins` and exposes all three features.
- No Client code scans or loads the three retired DLLs.
- File-transfer-only mode starts only file transfer.
- Recording, clipboard, file transfer, reconnect, cancellation, and exit-busy
  behavior remain intact.
- Queued callbacks cannot access a stopped or destroyed module.
- Build and packaging scripts neither build nor copy the retired Client plug-in
  DLLs or the temporary recording-core DLL.
- Focused tests and the C++ ownership gate pass.

## Implementation status (2026-09-03)

All six migration stages are complete. The three implementations now live
under `src/px_client/modules`, are CMake `STATIC` targets, and are linked by
`px_client`. The old Client loader, plug-in interfaces, generic event router,
DLL entry points, DLL lifecycle tests, and Client plug-in packaging have been
retired. The recording core is also statically linked into the main executable.
Upgrade publishing removes stale copies of all historical Client plug-in DLL
names, the temporary `px_client_recording_core.dll`, and the empty
`deps/ct_plugins` directory.

Focused verification uses `build_cpp_client_module_tests.bat`. It covers
repeated start/stop, queued work rejected during shutdown, stop invoked inside
worker/timer callbacks, concurrent post/stop, file-transfer reconnect, embedded
Qt resource registration, clipboard virtual-file streaming, and the statically
linked recording-core create/stop path. The final focused test run passed 5/5 tests,
and `check_cpp_ownership` passed. `build_cpp_client.bat` built and published the
Client artifacts; build-tree and `build_official/dist` SHA-256 values matched.
The release-only `build_official.bat` was not run.
