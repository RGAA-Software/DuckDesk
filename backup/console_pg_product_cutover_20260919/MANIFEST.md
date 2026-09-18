# Console PostgreSQL product cutover archive

- Base revision: `01fdf8cb2`
- Archived on: 2026-09-19
- Local modification status before archival: unchanged from the base revision
- Reason: the scripts built and packaged the retired Mongo/Redis Console and copied legacy TOML, certificate, upload and authorization-cache layouts. The active product now builds only the PostgreSQL composition root.
- Reference-only: this directory is excluded from build, test, packaging and runtime discovery.

| Original path | SHA-256 |
|---|---|
| `scripts/package_px_console_server.bat` | `A0FCA3118039B79F5D3E1DE24CC5C396D3EA365916353B39AB4EC018B377365C` |
| `scripts_build/build_px_console_server.bat` | `D0E2FE6DDF3A53CB23C3B3D880D5752C6BEE0D95953D0742D4A350C13CA677D4` |
| `scripts_build/build_console_web.bat` | `D3C8D985E466E72CB7D72A0465DCA1DA94DA1A5B9E9A6B327B7C9D27DE677060` |
