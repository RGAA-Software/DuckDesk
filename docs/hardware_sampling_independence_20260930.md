# Independent Windows node hardware sampling

Implemented scope: Windows Service. GPU is the priority business lane; CPU, physical memory and fixed disks each have independent background lanes.

## Collection boundaries

- GPU inventory and metrics use NVIDIA NVML directly: device UUID, name, PCI location, dedicated/used memory, GPU utilization and encoder utilization.
- DXGI/D3DKMT is used only to prove the NVML device's exact physical Render binding. Matching uses PCI location and the existing normalized PNP SHA-256 key, never model name or enumeration index. Ambiguous bindings remain ineligible. A nonzero PCI domain unsupported by the Windows mapping is not guessed.
- CPU uses native processor count and system time counters; memory uses `GlobalMemoryStatusEx`; disks use native fixed-drive enumeration and free-space queries. None uses WMI or loads NVML.
- Each lane runs as a hidden, internal invocation of the same `px_service.exe`, not an extra installed service or executable. Typed JSON over redirected pipes is transient IPC, not a new settings/persistence file.
- Each worker has its own five-second deadline. Timeout kills and waits for that worker; cancellation also kills the worker. GPU workers have above-normal priority, machine workers below-normal priority. A blocked synchronous driver query cannot retain the main Service thread or the other probes.

## Business and reporting

- Deployment preparation and launch reservation rechecks call only `sample_gpu()`. They neither start nor await CPU/memory/disk collection.
- GPU calls serialize independently and share at most one second of actual sampled results. Failure replaces only GPU state; unsupported metrics are unknown, never synthesized as zero. Exact identity, revision, binding and resource-headroom checks remain enforced.
- Aggregate reporting completes GPU first, then starts CPU/memory/disk independently. Each background lane refreshes five seconds after its previous result and can be cancelled when the sampler is destroyed. Individual failures clear only that lane. Hardware results older than fifteen seconds are omitted.
- Reports preserve the GPU sample timestamp instead of refreshing cached admission data. The existing telemetry envelope already supports `Partial` reports with only GPU or only machine fields; no schema migration or alternate scheduling contract is introduced. Console GPU scheduling already accepts `Ready` or `Partial` and still requires exact binding and valid GPU metrics.
- GPUs with unsupported identity/binding/metrics are not admitted. One card's query failure does not erase usable cards; complete enumeration/driver failure makes the GPU lane unavailable.
- On systems with more than 64 logical processors, inventory count remains valid but CPU utilization is unknown rather than reporting one Windows processor group's load as the whole-machine load.

## Verification and deployment boundary

Release tests cover blocked CPU versus fresh GPU/memory/disk, GPU-only business sampling, GPU failure with retained machine results, hardware expiry, final-owner cancellation, repeated sampler start/stop, exact worker termination, typed IPC lane validation and ambiguous GPU identity rejection. Existing deployment/reservation tests operate on a GPU-only snapshot.

Local verification on 2026-09-30: 110 Service Release tests passed; the separately enabled physical-NVIDIA test also passed on an RTX 3060. All four hidden CLI worker modes exited successfully. NVIDIA inventory, memory, GPU/encoder load and the exact Render PNP binding were confirmed. Release Clippy passed with warnings denied; the entire active Service source directory passed the readable-naming gate. This is local evidence, not an installation/function acceptance result for node 90.

Focused builds use `scripts_build/build_rust_service.bat cloud_node`, publish the development artifact and refresh its SHA-256 manifest. A real installation on node 90 still requires a newly built complete Cloud Node Setup; copying only Service executables is not installation acceptance.

The retired WMI-based sampler preimages are preserved intact under `backup/hardware_sampling_independence_20260930/` and excluded from runtime/build discovery.

## Node 90 deployment acceptance

On 2026-09-30, the existing official build cache was reused through `repackage_windows_product_incremental.bat cloud_node`; no clean/full-build entry point was run. The complete Cloud Node 3.3.95 Setup was uploaded through Pixels MCP, its SHA-256 was checked on the target, and the silent covering installation exited with code 0. All 314 installed artifacts matched the package manifest; the Panel reported version 3.3.95 and Service was running.

Installed Service SHA-256: `6F0966B220B5441642B6BC7DA5E712038368245985E56C5B2AC496DB24370651`.

All four independent probe modes succeeded on node 90. NVML reported an NVIDIA GeForce RTX 4090 with valid memory, GPU/encoder utilization and an exact, ready Render binding. CPU, memory and disk also returned valid native counters. Service authenticated to the configured Console with the existing node/device identities and a new generation; the checked post-install log contained no sampling or node-control errors. Interactive game/webview/RDP launch and picture/input acceptance remain separate from these installation and sampling checks.
