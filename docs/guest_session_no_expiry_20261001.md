# Persistent guest sessions — 2026-10-01

User decision: remove the one-hour guest-session lifetime, not increase it or renew it periodically.

## Contract

- Guest identities have no `expires_at` field or lifetime setting. They remain valid until
  logout, explicit revocation, administrator blocking or recovery-security invalidation.
- Public application visibility remains authoritative. No new anonymous management access.
- User login expiry, connection descriptor expiry, source-ban duration, licensing and
  instance/workspace ownership remain independent and unchanged.
- All guest admission checks, application-owner checks, resource-session checks and
  workspace frontend checks use the same revocation/blocking rules, without guest expiry.
- Web and Android consume the new contract. A rejected Web guest is not automatically
  replaced by another guest identity to retry the forbidden operation.
- Client application queries return an explicit failure, not an empty directory. Refresh
  failure keeps the previous cards and instance bindings and shows a localized error.
  A successful empty response still clears the directory.

## Database update and delivery

Migration `0034_persistent_guest_sessions.sql` removes the guest expiry column and its
dependent constraint/index. It does not delete application, node, instance, guest or
revocation data. Unrevoked sessions previously rejected only because of age no longer
expire. Revoked sessions stay revoked; stopped instances are not restarted.

SQLx metadata was regenerated against fresh isolated PostgreSQL using the repository
`PrepareQueries` entry point. Eleven superseded generated cache files were replaced;
they can be reproduced from the archived SQL or the base revision. Retired source
branches, including dirty working-tree contents, are under
`backup/guest_session_no_expiry_20261001/`.

This change requires updating Console before testing the no-expiry behavior online.
A real deployment must use a newly built complete Server package/Compose images,
not a copied executable.

### Initial deployment preflight on node 90

The complete Windows Server `1.0.21` fast Release Setup was incrementally built,
all 34 package artifacts were SHA-256 checked, and the Setup was transferred with
Pixels MCP to `C:\Users\Administrator\Desktop\PixelsServer_1.0.21_Setup.exe`.
Setup SHA-256: `5fc025f934966502b3fecb62f633aa6f5a1b23ae9220b53f47124ce99a3e4bd6`.

Installation of that candidate was **not executed**. Read-only checks identified a pre-existing upgrade
gap: node 90 then ran Server `1.0.20`, Console schema 33, with 3 applications and 1 device;
the existing Setup upgrade flow does not call the database migrator. Initial setup
generates a random `pixels_console_owner` password but does not persist it, while
the active configuration and backup password file contain only runtime/backup
credentials. Neither the active nor archived configuration contains an owner
credential, and a passwordless owner connection was explicitly rejected by the
existing SCRAM authentication rules.

The schema must be migrated to 34 before starting the new Console. Existing services,
data and PostgreSQL authentication were left unchanged. Continuing requires either
an existing owner credential or explicit approval to recover that offline migration
credential and repair the package upgrade workflow. Do not directly install this
candidate against schema 33, reset database passwords implicitly, weaken `pg_hba.conf`,
or replace SQLx migration checks with hand-written ledger entries.

The user subsequently approved recovery of the offline owner credential and repair of
the Windows upgrade workflow. This initial blocker is resolved; implementation and
focused acceptance are recorded in `docs/single_server_windows_database_upgrade_20261001.md`.
The actual complete-package installation advanced schema 33 to 34, preserved all three
application identities and device code `934886467`, and verified an existing guest
token across the restart with no expiry field. Separate complete-package overwrite
installation then verified ordinary upgrades without administrator credentials and
removed the obsolete lifetime environment setting. No business password or PostgreSQL
authentication-policy change was made. Diagnostic guests were explicitly logged out.

## Verification

- PostgreSQL guest suite: **9 passed**, including a 30-day-old identity, logout,
  administrator/source blocks, concurrent issuance and immutable audit behavior.
  Report: `test-results/server_validation/pg-20261001-010345-283d72e7`.
- Console HTTP directory suite: **8 passed**, including no expiry field, public
  applications and explicit operator blocks.
  Report: `test-results/server_validation/pg-20261001-010806-3df58c62`.
- Windows Panel product Release tests: **50 passed**; C++ ownership and naming gates passed.
- Console Web: **64 passed**, TypeScript checking and touched-file ESLint passed.
- Android network Release tests: **47 passed**, including renewal with the same guest
  after 30 days. The temporary Gradle init script enabled Release unit tests only;
  no Debug product or APK was built.
- Client Panel incrementally built and published into `build_official/client/dist`.
  Build/dist SHA-256: `ACBF51640F35883068EF8D7F154472552454D91CC376E2A9B839208B3C2FECDD`.
  Development manifest refreshed; no full build or version bump.
- Runtime-wide strict Clippy is blocked by pre-existing warnings in `px_backup`
  (`large_enum_variant`), `setup_install.rs` (`needless_borrow`), and runtime `lib.rs`
  (`too_many_arguments`). Those unrelated implementations were not redesigned or suppressed.
