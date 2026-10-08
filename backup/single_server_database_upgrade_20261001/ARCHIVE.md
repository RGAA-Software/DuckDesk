# Windows Single Server database upgrade repair

Base revision: `eeaf061f8c4c15d823846ad4ac85a90a689e47ba`.
Both archived files had local modifications; their complete working-tree contents
were copied before this repair. This batch is reference-only and excluded from builds.

- `rust_server/px_console_server/runtime/src/setup_install.rs`: generated owner
  credentials were discarded; fresh setup now preserves offline migration credentials.
- `deploy/single_server/windows/install.ps1`: program-only rollback is retired once
  a versioned database migration has started. Upgrades now validate, back up and migrate.
