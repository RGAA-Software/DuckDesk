# Guest session expiry retirement

- Base revision: `eeaf061f8c4c15d823846ad4ac85a90a689e47ba`.
- Archive batch: `guest_session_no_expiry_20261001`.
- Reason: user removed the fixed guest-session lifetime; retain logout, revocation,
  administrator blocks, timed source bans, user-session and descriptor expiry.
- Original paths are preserved beneath this directory. Files were copied intact
  from the working tree before editing, including all uncommitted contents.
- Initially modified paths: `deploy/private_server/examples/console.env.example`,
  `docs/px_console_server_runtime_config.md`, runtime `config.rs`, `guest_source.rs`,
  `secrets.rs`, `setup_install.rs`, tests `directory_api.rs`, `process.rs`,
  `support/runtime_fixture.rs`, the five archived server-validation scripts,
  and Panel `panel_console_session.cpp`, `panel_console_session.h`,
  `product_cloud_applications_port.cpp`, `tests/panel_product_tests.cpp`.
  All other archived paths were clean at the base revision.
- Reference only: excluded from active build, tests, packaging and runtime loading.
