# Rust unused services retirement archive

- Archive batch: `rust_unused_services_retirement_20260916`
- Base revision: `d8ef006480c61e30da55a298bf5d4876f2083f60`
- Local modification status before archive: clean for both archived directories
- Retirement reason: `px_stat_server` and `px_updater` have no active callers, release build entry points, packaging integration, or deployment integration in the current product

Original paths are preserved beneath this archive batch:

- `rust_server/px_stat_server/`
- `rust_server/px_updater/`

These sources are reference-only. They are excluded from the active Rust workspace, builds, packaging, tests, deployment, and runtime loading.
