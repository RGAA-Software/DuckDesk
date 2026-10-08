# Hardware sampling independence

Base revision: eeaf061f8c4c15d823846ad4ac85a90a689e47ba

Preserved full pre-change implementations before replacing the WMI-dependent combined sampler with independent GPU/CPU/memory/disk probes.
Reference only; excluded from compilation, tests, packaging and runtime loading.

- Original: `rust_client/px_service/src/node_telemetry.rs`; local modification status: clean.
- Original: `rust_client/px_service/src/node_gpu_telemetry.rs`; local modification status: clean.
