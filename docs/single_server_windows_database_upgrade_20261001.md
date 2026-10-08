# Windows Single Server schema upgrades

Scope: Windows Server Setup. No client/node credential, license, publisher, Console-address
or PostgreSQL authentication-policy change is introduced.

## Offline identity

Fresh setup saves the generated Console schema-owner connection in
`<ConfigRoot>/database-upgrade/console-owner.url` before initializing its schema.
The directory and file are private to the provisioning identity (Windows:
Administrators/System). Console, Relay and Backup service identities receive no access.
Only the offline administration executable reads this file. It checks the owner role,
Console database name, runtime host/port and database deployment identity before using it.
The PostgreSQL superuser password is not saved.

## Package upgrade

1. Verify the complete package identity, exact file inventory and SHA-256 manifest.
2. Read-only database preflight checks the offline owner role and the complete installed
   migration ledger against a checksum-exact prefix of the incoming migrations.
   Missing credentials, wrong deployment, dirty/checksum-modified migrations and
   downgrades stop before services are stopped.
3. Stop this deployment's Console/Relay/Backup services. For a pending schema update,
   make a custom-format PostgreSQL archive and private configuration snapshot using
   the package's verified PostgreSQL tools. Verify its archive inventory and hashes.
4. Run the existing SQLx migration path as the separate restricted schema owner.
   Runtime connections keep their original credentials and cannot migrate. A still-live
   runtime schema lease refuses the migration.
5. Publish the complete program package, update Backup's expected Console schema version
   atomically, remove the obsolete guest lifetime environment setting, and start/check
   all three services. User-login, descriptor, source-block and license expiry settings
   are not changed. Repeated retired-setting cleanup does not rewrite the file.

After a database migration attempt, any subsequent failure retains the new/staged and
previous programs, configuration and verified database snapshots with services stopped.
It does not start an old binary against an uncertain/new schema. Restoring business
operation then requires investigating the retained installation log and choosing the
matching package/database state; database data is never automatically overwritten.
When no migration was attempted, the existing program/configuration rollback still applies.

Snapshots are under `<DataRoot>/database-upgrades/<uuid>/`; their manifest records
deployment identity, schema version, archive SHA-256 and configuration-file hashes.
These are offline pre-upgrade snapshots, not independently approved Backup recovery sets.
They are retained; no pruning or deletion is performed by this repair.
Setup records its result/output in `<DataRoot>/server-install.log`.

## Explicit missing-credential recovery

The previous initializer discarded the randomly generated owner password. Recovery is
an operator-approved action, not a startup/upgrade fallback. Only an explicit Setup
`/RECOVEROWNER` invocation may request it; the PostgreSQL administrator connection is
supplied through the temporary `PIXELS_SETUP_DATABASE_URL` process environment.
The installer makes a verified snapshot first and then verifies the current database
deployment/role before changing **only** `pixels_console_owner`'s password. It stores
the private replacement before changing PostgreSQL; an existing credential is never
overwritten, and failure with a pending file requires operator inspection rather than
an automatic retry/reset. Normal subsequent upgrades need neither this option nor a
PostgreSQL superuser connection. No `pg_hba.conf`, business-role or account-password
change is allowed by the recovery operation.

## Verification

- Owner preflight unit tests: exact old/current prefixes accepted; empty, failed,
  checksum-modified, missing and future histories rejected.
- Endpoint-binding unit test: wrong host, port, database, role, password or URL options rejected.
- Isolated PostgreSQL Release admin suite: 4 passed, including private explicit owner
  recovery, no overwrite, unchanged runtime password, read-only preflight with live
  runtime pools and migration rejection until those pools are closed.
  Report: `test-results/server_validation/pg-20261001-070816-0a176412`.
- Windows package/upgrade tests: 10 passed, including PowerShell 5.1 atomic Backup schema
  update, rejection without changing the existing file, repeatable retired-setting
  cleanup and orchestration/failure gates.
- `px_pg` strict Release Clippy passed. Existing runtime-wide unrelated warnings remain
  documented with the guest-session change; they were not suppressed.
- Retired program-only upgrade branches were archived with their dirty pre-change
  contents in `backup/single_server_database_upgrade_20261001/`.

## Node 90 installation

With explicit user approval, Server Setup `1.0.22` backed up the database/configuration,
recovered only the offline owner's password, applied migration 34 and started all three
services. Ordinary full-Setup overwrite installation then verified that no PostgreSQL
administrator environment/recovery option is needed. The final candidate is `1.0.24`
(`fast-release`, complete Setup, not a formal optimized publication), available on
90's Administrator desktop as `PixelsServer_1.0.24_Setup.exe`.
Setup SHA-256: `121d0974761c63902e375b09b5aadf0966598acc908cd001c704d215b588ce93`.
Manifest SHA-256: `97fdc24459b71fa4ba87f761a6b591e2eae7892a6f842e97c8bb8c69f808020a`.
All 35 installed payload artifacts matched their package/build hashes.

The two verified pre-upgrade snapshots are retained under
`C:\ProgramData\Pixels\Server\data\database-upgrades\`:

- `a9d90eee4c414c5a8941a34b7b6381ed`, 595400-byte archive,
  SHA-256 `15491d625c6faf856748f51043f3781593ed783541e2750ed78634640c4dcdc3`.
- `265dd1a291924725b95a2cca67d4cdf7`, 595330-byte archive,
  SHA-256 `87b3148dc3f826aee4c59bc66ac39e2782c57e0e7e17e2ed47e5f9ad7a4e9dde`.

Read-only SQL/HTTP acceptance confirmed schema 34, no guest expiry column/field,
three applications with their original identities, device `934886467`, ready node
heartbeats and matching Backup schema 34. A guest issued before the migration kept
the same identity/token afterward; final fresh-guest application and instance queries
also returned HTTP 200. `/health/live` and `/health/ready` returned HTTP 204.
Diagnostic guests were logged out. No cloud application was launched by these checks.
No node/client package was reinstalled, data deleted, business password reset or
PostgreSQL host-authentication rule changed.

The 1.0.23 overwrite check found that NSIS append-mode requires seeking to the end
explicitly; 1.0.24 adds that seek so subsequent install-log blocks no longer overwrite
the previous prefix. Earlier partial log text was retained; the original 1.0.22 command
result and the snapshot manifests record the complete recovery/migration evidence.
