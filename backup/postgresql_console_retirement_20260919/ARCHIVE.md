# PostgreSQL Console legacy retirement archive

- Archive batch: `postgresql_console_retirement_20260919`
- Base revision: `4b5db61ce`
- Original paths:
  - `rust_server/px_console_server/Cargo.toml`, `build.rs`, and `src/**`
  - `rust_server/px_console.toml`
  - `rust_server/px_auth.toml`
  - `rust_server/px_auth_server/app_credential.txt`
  - `scripts/publish_px_console_public.py`
  - `scripts/deploy/px_auth_cn_remote.py`
  - `scripts/dedup_console_records.js`
  - `scripts/run_rtc_lan_case.ps1`
  - `scripts/run_rtc_app_multi_session_lan_case.ps1`
  - `scripts/run_rtc_lan_stability.ps1`
  - `scripts/run_rtc_multi_session_lan_case.ps1`
- Local modification status before archive: clean
- Retirement reason: the PostgreSQL `px_console_runtime` product composition root replaced the MongoDB/Redis/legacy-license Console crate. Keeping the old crate as a workspace member retained a second backend and obsolete authorization behavior. The root TOML files, plaintext credential handoff, MongoDB deduplication utility, and old publish/migration scripts targeted that retired composition and could deploy it accidentally. The RTC scripts exercised the retired `/api/v1` and MongoDB fixture, so they are not valid Direct Host WebRTC evidence; DB5 must use the current resource-session and descriptor flow instead.
- Active code retained in place: `rust_server/px_console_server/runtime`, `storage`, and `migrations`

The archived tree is reference-only. It is outside every Cargo workspace, build script, test target, package manifest, and runtime loading path. Do not add a forwarding facade or compatibility executable that compiles this archive.
