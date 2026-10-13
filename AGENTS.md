# Workspace delivery rules

- Unified acceptance live checkpoint (2026-10-13 night): T1/T2 passed; Company Client E849DAD0… and Panel0BBF191A…
  are incrementally aligned,44 candidate files checked and317 settings-file hashes retained. T3a original command
  136e708d-1a3b-4c14-b233-5d43949fb70a/task f3aac060-0f9a-43dd-a808-607906832969 remains running for1800 seconds.
  Supervisor49200/Client53952, client creation2026-10-13T15:06:25.473789Z, actualLAN UDP10.0.0.90:4613, not NAT evidence.
  Source90 owned page65500/created15:03:30Z/port48778 and prior owned app145233…/deployment2a244… are active;
  normal close/own resource cleanup and fixture-disable restoration remain required. Never restart because a poll expires.
  Use bounded64KiB log reads during live samples; initial GetContent tail timed out without a network failure.
  PAB commands support service/user selections; desktop_user is for dedicated desktop tools, not process.exec.

- Unified acceptance long-task decision (2026-10-13 evening): the user requested one continuous, complete
  acceptance task. Execute docs/iroh_transport_acceptance_task_20261013.md; resume from
  docs/validation/iroh_transport/20261013_acceptance_task_state.json and status.md. T1 Company identity preparation
  is current; Company connected and initial read-only inventory passed, with no update/test yet. Its Client
  90703A5B…/Panel A96F421B… differs from the latest candidate; inspect manifest/configuration before T2.
  Workspace is D:\112233\unified-acceptance-20261013. Proceed through the matrix,
  fix evidenced functional failures, repeat only affected gates, and deliver one final classified report/cleanup.
  Reuse passed evidence; Company starts with one Client, at most two, optional XR one. Local validation/Android
  and paused investigations stay excluded. Missing mandatory environment is not a pass; conditional Remote,
  complex-NAT, scale and external-publication items retain explicit separate statuses.

- Latest validation-host / remaining-work planning decision (2026-10-13 evening): Company (559038160) is available
  again and is the primary receiving peer; start with one Client, at most two if there is headroom. Do not use the
  local machine as a validation peer or start local Client/probe/test workloads. 90 and XR remain available under
  their existing workspace constraints; BJ remains the Linux Relay, CN remains an independent low-bandwidth Server.
  The remaining-work plan is docs/iroh_transport_remaining_work_plan_20261013.md. It is planning only: Company
  has not yet been reconnected or updated. Read its installed identity before incremental delivery; reuse passed gates.
  Do not uninstall Company's existing Client to force Remote installation. Android and paused investigations remain excluded.

- CN Relay held-connection observation (2026-10-13): reuse the completed 600-second low-load sample in
  docs/validation/iroh_transport/20261013_cn_relay_soak.md: 484 reliable round trips and 484 datagrams each direction,
  held QUIC/stream, endpoint release and 0 reported connections. 19 configuration hashes and all 11 containers retained.
  Initial 90-second exit was the probe deadline, not a Relay fault; bounded timeout is configurable, old defaults retained.
  This is not media, throughput, complex NAT or long-term stability acceptance. No product/runtime configuration changed.
  Relay management reads require the Page limit query, e.g. /api/console/managed/relays?limit=100, and return an array.

- CN Linux Server delivery (2026-10-13): complete fast-release 1.0.56 Compose installation/overwrite accepted
  on CN with an independent PostgreSQL 18.6 volume and loopback 5432. Reuse
  docs/validation/iroh_transport/20261013_cn_linux_server_report.md. Log-directory provisioning and partial-setup
  retry defects are fixed; logs 10001:10001/700, schema 42 and 18 immutable configuration hashes retained.
  Managed iroh Relay 4605 passed low-rate reliable/datagram checks, not video FPS or throughput capacity.
  Original 7 business containers unchanged; BJ/90 not migrated. CN is a separate 4-stream starter deployment.
  Keep Company/Android and paused investigations excluded; do not repeat completed installation gates.

- Linux Server target decision (2026-10-13, latest): the user selected CN instead of BJ for Linux Server.
  BJ was inspected only; no Server installation or new services were started there. Preserve BJ Relay on 4605
  and its 90 Console registration/configuration. CN has approximately 4 Mbps bandwidth: use it for Server
  installation/overwrite and low-bandwidth connectivity, without throughput or 60 FPS acceptance targets.
  CN needs TCP 4600 for Console, SSH access and TCP/UDP 4605 for bundled Relay connectivity when tested.
  Keep PostgreSQL 5432 local. Do not migrate or redirect the existing 90 deployment.

- Latest Windows installation candidates (2026-10-13): user-authorized optimized complete Setup candidates
  network-publication-20261013-1882ac301 are accepted: Server then Cloud Node on 90, Client first install then
  same-version overwrite locally. Four Setup exits0, all payload SHA checks and six optimized Client CTest pass.
  Reuse docs/validation/iroh_transport/20261013_windows_publication_report.md; Server/Node configuration hashes,
  schema42,80-stream license,RDP10 and saved Client preferences retained. Remote has a complete candidate only;
  do not uninstall another product to force its independent acceptance. Versions and external update sources
  were not advanced; development Client/Panel artifacts retained. One first-sample later presentation gap 193 ms
  remains unexplained, and is not a reason to automatically reopen paused tuning during installation delivery.
  Company/Android remain excluded; the latest CN installation entry above supersedes the earlier missing Linux target.

- Post-baseline network follow-up (2026-10-13): baseline 7eaeb13a6 was pushed. Relay names now travel as separate
  display metadata; Client resolves only its actual selected Relay URL and retains address/RTT and unknown-name
  fallback. Original Render wire fields remain unchanged when names are absent. Console displays reported bandwidth
  and budget usage. Linux Compose now stops Console/Backup before schema migration and atomically syncs the Backup
  schema; Linux empty backup.env initialization failure is fixed. Reuse the real Linux-container schema41→42,
  idempotency, invalid-plan and real two-Client preference evidence in
  docs/validation/iroh_transport/20261013_network_followup_report.md. Subsequent Windows candidates and CN Linux
  installation are recorded above; these earlier missing delivery steps are now complete. Do not repeat passed gates.

- Validation host decision (2026-10-13, latest): do not use Company for further tests. Its owned test Client was
  stopped and this MCP session disconnected. Use the local machine, 90 (428358431), or XRServer03 (947158375).
  The user explicitly reauthorized updating Console and small validation on 90, overriding earlier 90 exclusions.
  Keep 90 development files under D:\112233 and XR files under D:\uploadsoft\rd\pixels.

- Client settings / Relay bandwidth follow-up (2026-10-13): real Client process restarts retained statistics,
  audio, language, theme, fullscreen and 30 FPS preferences. Console schema42, backup plan and web were incrementally
  updated on 90, with all 35 Server and 315 Cloud Node manifest entries verified. Healthy held Windows Relay media
  continued while a new source and 90 Client selected BJ after the configured bandwidth threshold was crossed.
  Reuse docs/validation/iroh_transport/20261013_settings_bandwidth_rollout_report.md; do not repeat these gates.
  Bandwidth is candidate filtering, not shaping/reservation or fair distribution. Android and paused stutter/NIC
  investigations remain outside this batch. All six owned instances/resources and temporary receivers were cleaned.

- Windows/Linux transport closeout (2026-10-13): the follow-up RDP/audio/admission changes are consolidated into
  optimized complete candidates. Full Setup overwrite passed for Server then Cloud Node on 90, and Client on
  Company, followed by one-at-a-time GameHook/Relay, WebView/direct, RDP/Relay and desktop/direct output checks.
  Remote has a verified complete candidate, without a separate installation in this batch. Reuse the installed
  identities and evidence in docs/validation/iroh_transport/20261013_transport_final_report.md. Section 0.9 and
  these follow-up deliveries are closed; do not restart completed gates or expand ancillary acceptance.
  Basic two-NAT connectivity already passed; complex NAT, scale and long stability remain explicit gaps.
  Android and the paused short-stutter/NIC/saturated mixed-load investigations remain excluded or stopped.

- Game first-admission follow-up (2026-10-13): failed batch407 was a delayed test handoff; the existing pending
  resource session closed before Render admission. Client now suppresses hello while disconnected/unready and
  reports final dial failure as terminal, with separate localized admission-rejection text. Reuse the database
  timeline and bounded batch409 evidence in docs/validation/iroh_transport/20261013_game_admission_report.md.
  Complete build/transfer preparation before requesting connection information, then launch immediately in the
  same script. Do not treat this fixture failure as UDP loss or restart the paused video/NIC stress investigation.

- Native audio completion (2026-10-13): SDL audio initialization and owned subsystem lifetime are fixed in
  Client. WebView output frequencies/input passed over actual Relay and UDP direct; GameHook playback and
  Company installed-Client PCM submission were verified. Reuse `docs/validation/iroh_transport/20261013_native_audio_input_report.md`.
  Old test executables and Rust incremental caches were cleaned at the user's request; product dist, current
  audio regression target, source/backup, environment configuration and validation evidence remain intact.

- Connection warm-up decision (2026-10-13): the user accepts temporary picture instability immediately after
  connection. Record startup and first-frame statistics separately; do not require them to meet steady-state
  smoothness thresholds. Continue to report gaps during established, normally rendering sessions honestly.

- Small media validation (2026-10-13): the user authorized GameHook/WebView validation with at most two local
  Clients and one Company Client, roughly 10 Mbps total configured video ceiling. Use owned test applications
  and a loopback animation page to avoid changing existing application settings or confusing website load with
  media transport. Keep RDP capacity 10 and the 80-stream license. Relay draining is a temporary maintenance test;
  restore its baseline and clean only the owned test processes, resources and applications after validation.

- Distributed validation update (2026-10-13): the user reauthorized a small test with at most three Client
  processes on the local machine and two on Company. This supersedes the Company exclusion only for this bounded
  batch; do not increase either receiver's count. Stop Company clients if the user reports load again. The RDP
  deployment stays at capacity 10, and existing installation/transport evidence remains reusable.

- RDP validation limit and Company load decision (2026-10-13, superseding the 20-route RDP target): the user
  limited RDP to at most 10 and requested relief after Company became sluggish. The 90 RDP deployment is now
  capacity=10/revision=3. Stop this batch's Company test clients and do not use Company for further multi-client
  or heavy load validation without new user direction. Preserve the passed ten-user connection/frame evidence;
  do not expand RDP back to 20 or restart this load test. GameHook/WebView capacities and the 80-stream license
  remain unchanged. Existing Windows accounts and disconnected sessions are retained.

- RDP workspace pool update (2026-10-13, superseding the singleton notes below): the same RDP application/node
  now owns persistent slots `(application_id, node_id, workspace_slot)`, each with a distinct Windows user.
  Capacity is configurable from 1 to 64; the current 90 deployment is configured for 20. Each workspace still
  permits one active frontend. Ordinary launch may allocate another workspace when an existing instance is busy;
  it must never take over the occupied desktop. Preserve original workspace identity and credentials in slot 1,
  and retain all accounts, profiles and disconnected sessions on stop. Reuse the implementation and focused
  evidence in docs/validation/iroh_transport/20261013_rdp_workspace_pool.md; configured capacity and database
  concurrency tests are not proof that twenty real Windows desktops have been simultaneously accepted.

- RDP concurrency clarification (2026-10-12): the user clarified that multiple instances of the same RDP application
  on one node should use distinct Windows users and sessions. The one-active-frontend rule applies to each workspace,
  not to the whole application or node. The current application/node singleton mapping and RDP capacity=1 checks
  are implementation limitations to address, not a reason to require twenty machines or duplicate application entries.
  Preserve existing accounts, SIDs, profiles and disconnected sessions when extending workspace allocation.

- Multi-stream configuration update (2026-10-12): the current Console has an Auth-issued 80-stream license.
  The user authorized increasing runtime scheduling capacity: 90 and XR Cloud Nodes now have max_instances=64;
  their existing GameHook/WebView deployments have capacity=20, with disabled flags and GPU bindings preserved.
  The existing RDP deployment remains capacity=1 because it represents one workspace with one active frontend.
  Twenty distinct RDP workspaces are not delivered by this configuration batch; do not claim all types have twenty running instances.
  Reuse the capacity baseline and verification in docs/validation/iroh_transport/20261012_multistream_capacity.json.

- Video investigation deferral (2026-10-12, superseding the evening start below): the user accepted the current
  un-reproduced short-video-stutter investigation as staged progress and asked to defer further handling.
  Preserve delivered diagnostics and evidence; the root cause remains unconfirmed, so do not claim a permanent fix.
  Resume only on an actual recurrence or explicit user direction. Do not repeat captures or tuning as a prerequisite
  for the Windows/Linux network mainline. Android and the stopped NIC/saturated mixed-load stress remain excluded.

- Evening validation update (2026-10-12): the user explicitly started the deferred short-video-stutter investigation.
  Company (559038160), 90 (428358431) and XRServer03 (947158375) are available. Reuse existing delivery evidence,
  investigate actual media waits with logs and upstream references, and keep Android and the stopped NIC/saturated mixed-load stress excluded.

- Remote availability update (2026-10-12, superseding the afternoon exclusion): the user confirmed
  "Use 90; it is available now" for the paired Console/node rollout and subsequent mainline validation.
  Device 428358431 may be used again; stage development and transfer artifacts only under `D:\112233`.
  Keep Android excluded and leave the evening video-stutter/NIC/saturated mixed-load investigation deferred.

- Transport sequencing decision (2026-10-12 afternoon): defer the remaining short video-stutter tuning to the evening return.
  Continue the Windows/Linux network mainline with multi-Relay load visibility and actual endpoint-distribution review.
  Reuse the completed functional/installation evidence; do not restart the video, NIC or saturated mixed-load investigation during this batch.
  Android remains excluded and 90 remains unavailable for tests. The evening return has no fixed automatic start time.

- Remote validation decision (2026-10-12): do not use 90 (428358431) for further tests; the user needs it for other work.
  Use local/Company peers and XRServer03 (947158375) for subsequent Relay measurements. XR development, transfers and temporary files
  belong under `D:\uploadsoft\rd\pixels`; this does not relocate installed products or runtime logs. Measure actual Relay throughput
  and delivered media performance; poor-network XR acceptance does not require 60 FPS.

- Build parallelism decision (2026-10-11): use 18 build jobs for every subsequent compilation; do not use all available cores.
  Pass the job count explicitly to focused C++ build entry points and to other compilers when invoked.

- Current transport task scope (2026-10-11): complete the Windows/Linux iroh transport delivery as one continuous task.
  Android implementation and acceptance are excluded from this task, rather than a final phase within it.
  Preserve all data paths still consumed by Android; global legacy transport retirement is deferred with Android.
  The current task and completion criteria are in section 0.9 of `docs/iroh_transport_execution_plan_20261008.md`.
  Reuse passed evidence, finish each required development/delivery step without asking the user to restart each batch,
  and keep the stopped mixed-load/NIC stress investigation outside the current task's completion prerequisites.

- Transport scope correction (2026-10-10): keep this long task focused on media transport, iroh direct/relay, reliable streams/datagrams,
  multi-Relay allocation/recovery, Windows/Linux delivery and Android last. Validate existing business features only as transport consumers
  or when affected by a concrete change; independent recording/playback and exhaustive clipboard/UI acceptance are not transport milestones.
  Reuse passed evidence. Game Hook text clipboard is delivered; do not reopen it without a regression. Track missing NAT, capacity and
  stability evidence honestly without turning ancillary product work into a prerequisite for core network development.

- Clipboard scope decision (2026-10-10, superseding the earlier exclusion): Game Hook applications also support bidirectional text clipboard. Include their clipboard round trip in transport acceptance. Keep ordinary desktop, WebView text editing and RDP clipboard flows in their existing scopes.

- Transport progress continuity (2026-10-10): before resuming transport work, read the current entry in
  `docs/validation/iroh_transport/status.md`. Update it after each deployment/validation batch with installed identities,
  persistent configuration changes, evidence and the next unfinished step; do not repeat completed gates without a new reason.

- Transport delivery order (2026-10-10): implement Android last. Prioritize the formal iroh Relay and Windows flows;
  Relay must support both Windows and Linux. The user will supply additional Relay hosts for cross-host validation.
  Do not retire data paths still used by Android before its final migration and acceptance.

- Transport implementation priority (2026-10-08): prioritize the media stutter fix and working iroh direct/relay, reliable-stream and
  datagram functionality. Reuse existing login and resource-session authorization; do not introduce short-lived transport tokens,
  extra tickets, a second authentication system, or token-specific acceptance work. Keep focused regression and real connectivity
  validation, without expanding ancillary requirements ahead of the core functionality.

- Remote development directory decision (2026-10-08): use `D:\112233` as the development workspace on the 90 Windows machine
  (Pixels device code `428358431`, updated by the user). Stage source checkouts, development/test scripts, temporary artifacts and product installers there;
  do not copy development files to its Desktop. This does not relocate installed products or their runtime data/log directories.

- Windows release decision (2026-09-23): Windows products are intentionally distributed without Authenticode code signing or RFC 3161
  timestamping. Private-deployment operators accept the Windows unknown-publisher/security warning. Windows packaging, installation and
  upgrade gates rely on immutable product/publisher identity, exact SHA-256 manifests, isolated outputs and installer lifecycle checks;
  they must not require, synthesize or claim a Windows signer certificate. Android signing requirements remain unchanged.

- `scripts_build\build_official.bat` is a release-only full build. Do not run it for routine
  development, focused verification, or incremental C++ changes unless the
  user explicitly requests a release/full build.
- Routine C++ work must use the `scripts_build\build_cpp_*.bat` entry points, which build
  only the requested CMake targets and do not bump versions, run npm, or build
  Rust workspaces.
- The user starts and validates the Windows client from `build_official\<product>\dist`.
- A client-side build is not considered delivered until every changed runtime artifact (including executables, DLLs, language resources,
  and web assets when applicable) has been synchronized into `build_official\<product>\dist`.
- Before reporting a build ready for validation, verify that the relevant build-tree artifacts and their `build_official\<product>\dist` copies have
  matching SHA-256 hashes. If a destination file is in use, stop the corresponding process, publish the artifact, and re-run the hash check.
- Focused development builds use `build_official\<product>\{cmake,dist}` with `PX_DISTRIBUTION=development`. Release-only full builds produce one
  Pixels-owned package per Windows product under `build_official\<product>\official\...`; the retained `official` release identity means Pixels
  publisher/update trust, not a fixed runtime Console. Do not copy it back into development dist or treat development dist as installable.
- Build-configuration decision (2026-09-22): project development, focused verification and device checks use Release configuration only;
  do not generate or use Debug or RelWithDebInfo product artifacts. Routine builds use a fast, incremental Release profile with lightweight
  optimization. Only final publication/full-release entry points enable the fully optimized Release profile. Both profiles retain Release
  runtime semantics; development artifacts may be labeled `fast-release` and are not installable Pixels publication artifacts.
- Android short device checks use the single Pixels fast Release APK. Final Android publication uses
  `scripts_build\build_android_product.bat release` and preflights/builds one signed Pixels APK with one Android version.
- Remote incremental delivery decision (2026-10-12): after a complete package baseline has already been transferred/installed,
  subsequent development updates transfer only changed executables and any required changed DLLs/resources; verify source and destination
  SHA-256 and record the resulting runtime identities. Do not retransmit the complete package for these incremental updates.
  This supersedes the complete-package requirement below for authorized development updates; formal installation acceptance still uses Setup.

- Installation/upgrade acceptance decision (2026-09-27): every real installation test must use the freshly built complete product package,
  including Server applications. On Windows, install or overwrite with the product Setup; on Linux, deploy the built container images with
  Compose; on Android, overwrite-install the built APK. Verify the package/image identity and SHA-256 before deployment, then verify the
  installed/running version, service readiness and focused function. Do not substitute manual executable/DLL/web-asset copying for a package
  installation. This does not turn routine focused development builds into mandatory full release builds when no installation is requested.

# Project-wide modern C++ ownership and asynchronous safety

- Greenfield server decision (2026-09-16): the existing environment contains development data only. Build the new PostgreSQL/server/client
  contracts directly, with no Mongo data migration/import, legacy protocol/configuration/schema compatibility, adapter facade, dual write,
  or runtime fallback. Old API shapes, IDs, package/config formats and test expectations do not constrain the new design; update affected
  producers, consumers and tests together. Existing product invariants (authorization, Job ownership, RDP preservation, Android target
  identity) still apply. Versioned fresh-schema initialization and future upgrades between formal releases remain required; they do not
  authorize support for the retired development baseline. This does not authorize deleting unrelated databases or Windows workspaces.

- Licensing simplification decision (2026-09-22): retain only the signed `PXLIC2` server-side license contract for customer authorization.
  Auth issues the license; Console verifies it and enforces enabled services, expiry and the licensed concurrent-stream limit. Service,
  Render, Panel, Windows Client, Web Client and Android must not parse, store or enforce the license. Retire the custom `PXDC2` deployment
  certificate, `PXDD2` descriptor, `PXDP1` challenge, deployment trust stores, identity watermarks and credential-before-challenge gates from
  the active product; do not replace them with another product PKI. Connections use normal HTTPS/TLS. Pixels Cloud Node, Client, Remote
  and Android each have one Pixels-owned package. Their settings explicitly select the fixed official Console or a custom private HTTPS
  Console; the custom-address input rejects the known official address. The Pixels package keeps one publisher/update identity independent
  of the selected Console. OEM-branded releases remain separate products. Upgrades
  are operationally ordered Server first, then independently overwrite each node/client; do not build a distributed activation transaction.
  From the first formal baseline onward, Server supports the declared current-client upgrade window through versioned APIs. This decision
  does not restore retired development protocols or ports.

- Network port decision (2026-09-14): port 20371 is fully retired and must not appear as a runtime default, fallback, probe target,
  test endpoint or deployment assumption. Consume the authoritative endpoint reported through current node configuration and Console
  connection descriptors. Current package defaults use desktop Render 4601 and dynamically allocate application Render ports from
  4613–4998; each actual Render port carries TCP/WS and UDP on the same port number. Do not restore compatibility fallback to 20371.
- Media transport decision (2026-09-19): keep the existing Relay name, protocol, routes and non-WebRTC data-forwarding behavior. Remove
  only its central WebRTC SDP/ICE signaling role; do not rename Relay or add a compatibility alias. WebRTC remains supported only as
  Direct Host WebRTC: Windows Client, Web Client and applicable Android flows connect directly to the authoritative Render host/port and
  negotiate with Render on that direct endpoint. Do not route WebRTC signaling or media through Relay and do not add a hidden fallback.
  ZLMediaKit push/live playback, Coturn, STUN/TURN configuration, TURN media relay and their packaged sidecars are retired from the active
  product and must be preserved under a new repository-root `backup/` archive batch before removal. Recording and file transfer remain
  active features. The cross-component scope and DB0-DB5 gates are authoritative in
  `docs/direct_host_webrtc_scope_plan_20260919.md`.
- Android Cloud Apps decision (2026-09-14): implement the independent “云应用” / “Cloud Apps” top-level tab directly on the current
  Console identity and endpoint model. Delete the old device-nested Application Library flow; do not retain a compatibility page,
  forwarding facade, dual navigation, legacy DataStore import or old endpoint fallback. Android identifies itself to Console as
  `client_type=android`; it must not impersonate `panel`. Use a distinct cloud-application session target rather than a device/account
  target with fallback identifiers.
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
- **Hard gate for all new code:** New Pixels-owned or project-maintained C++ code must not declare, store, return, pass, or capture raw
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
- Apply these rules only to Pixels-owned code and dependencies explicitly maintained by this project (including the vendored asio2
  integration). Other third-party source trees are read-only: do not mechanically reformat, modernize, or change their ownership model.
- Code review and tests must cover destruction with queued callbacks, unregister during dispatch, shutdown from a callback, and repeated
  start/stop so that smart-pointer use is verified behaviorally rather than only syntactically.
- The complete project standard is documented in `docs/cpp_smart_pointer_standard.md` and is mandatory for all client, Panel, Render,
  service, SDK, RTC, plugin and shared-library code.

# Project-wide C++ initialization, design, and formatting rules

- Readable naming decision (2026-09-17): every project-owned identifier must communicate its domain role. This applies to production code,
  tests, fixtures, closures, callback captures, scripts and configuration helpers in every maintained language. Do not introduce
  context-free names such as `a`, `b`, `c`, `x`, `y`, `tmp`, `data`, `obj`, `item`, `thing`, `foo` or `bar`; do not hide meaningful
  objects behind type-only abbreviations such as `f` for fixture, `s` for store, `r` for request/result or `b` for barrier. Prefer names
  such as `fixture`, `activity_store`, `open_request`, `start_barrier`, `candidate_index`, `payload_bytes` or another precise business role.
  A short name is acceptable only when it is the established domain term and remains unambiguous in that scope (for example `x`/`y`
  coordinates in a geometry formula); convenience, conventional loop spelling and small scope are not sufficient justification.
  Existing code touched by a change must be brought into compliance. Generated code, read-only third-party trees and repository `backup/`
  archives remain excluded from mechanical renaming. Review and automated checks must treat human-readable naming as a delivery gate.

- Coding-style decision (2026-09-17): project-owned C++ follows the Google C++ Style Guide; Rust follows the official Rust Style Guide
  and rustfmt; TypeScript follows Microsoft's TypeScript Coding Guidelines for general application code. See `docs/coding_style.md`.
  This supersedes the previous C++ LLVM formatting rule: use Google formatting with 4-space indentation and a 150-column project limit.
  The user's explicit four-space indentation override applies to normal and continuation indentation; use spaces, never tabs.
  Existing project ownership, deterministic initialization, async safety, localization/theme and protected ABI rules remain mandatory;
  adopting a style guide does not permit raw pointers, unrelated third-party changes or a repository-wide mechanical rewrite.
  New code and logically modified blocks must use the new style. Generated code, archives and read-only external references are excluded.

- Every Pixels-owned C++ object, data member, scalar, enum, atomic, handle, and local variable must be deterministically initialized before
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
- New features and migrations must be decomposed into cohesive modules with one clear reason to change. Do not combine platform/window
  integration, rendering backends, UI composition, persistence, network/business workflows, and process lifecycle in one large source file
  or god class. Keep the composition root thin, split files at real responsibility boundaries, and enforce dependency direction through
  narrow typed interfaces. File splitting alone is not architecture; avoid both monoliths and speculative one-method abstraction layers.
- New user-facing UI must be localization-ready from its first implementation. English and Simplified Chinese are the initial required
  catalogs; user-facing text uses typed localization keys instead of literals scattered through pages. A language change must reuse the
  same UI and business flow, and missing-key/catalog parity checks must be testable without launching the UI.
- New desktop UI must support Pixels light and dark themes from its first implementation. Theme colors come from shared semantic tokens;
  pages must not embed independent palettes. Theme changes reuse the same widgets and business state and must not accumulate DPI/style
  scaling. Persistence belongs to the application settings adapter, not individual pages.
- Apply design patterns only where they make ownership, variability, or lifecycle clearer. Do not add service locators, mutable global
  singletons, generic `void*`/`std::any` bags, speculative interfaces, or inheritance layers for built-in modules. Interfaces must be small,
  typed, capability-specific, and backed by a real extension boundary.
- Resource acquisition, subscriptions, registrations, locks, threads, timers, library handles, and cancellation ownership must all be
  represented by RAII types. Cleanup order must be the reverse of dependency construction and repeated stop/destroy must be safe.
- Project-authored C++ uses a 150-column project limit with the user's 4-space indentation override. Keep a statement on one line when it fits within 150 columns;
  wrap when required or when a deliberate multiline layout materially improves readability. Generated code, vendored third-party code, URLs, and
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

- User-approved exception (2026-10-10): maintain the minimal iroh 1.3.0 disconnected-Relay send-queue fix in the isolated
  `rust_transport/vendor/iroh-1.3.0` copy, with reviewed patches and original/modified hashes under `patches/iroh/`.
  Cargo registry caches and original reference checkouts remain read-only. This exception does not authorize unrelated dependency changes.

Local reference checkouts (original references confirmed on 2026-09-07; session/RDP references added on 2026-09-08):

- RustDesk: `D:/GoCloud/rustdesk` — connection establishment, NAT traversal/relay, media queues/recovery, session and file-transfer architecture. User updated this path on 2026-10-09; it supersedes `D:/source/rustdesk`. Use this external checkout for all RustDesk references and do not clone a second copy under this repository.
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

Transport reference revisions verified on 2026-10-09 (re-read HEAD when making later implementation comparisons):
- RustDesk: `7aa98d43cf1962a7a29ec16ffef42974377ef11e`.
- Moonlight Qt: `2e13ed9977bc31c73caf8428f08f58d793313ece`.
  Its populated media core is `D:/source/moonlight-qt/moonlight-common-c/moonlight-common-c`, revision
  `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac`; frame assembly/recovery entry points are `src/RtpVideoQueue.c` and `src/VideoDepacketizer.c`.
- Sunshine: `3cba9baebac882b336be3ebe129ee612cb189853`.
Prioritize these references for frame assembly, bounded media queues, loss/reference recovery, FEC and pacing investigations.

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
- Superseding RDP decisions (2026-09-08): preserve native RDP encoding; Pixels provides provisioning, authorization, proxy transport
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
  RTC, KCP, Relay or public P2P. Windows Client/Console and the configured public Windows node are the first validation scope; other RDP clients are not yet supported.
