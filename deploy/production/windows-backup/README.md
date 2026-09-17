# Pixels Windows PostgreSQL backup package

This directory defines the reviewed PostgreSQL client payload used by the native Windows
`px_backup` service. The PostgreSQL archive is downloaded at release time and is not committed.

## Pinned input

`postgresql-client-18.6.json` pins the EDB PostgreSQL 18.6 Windows x64 binary archive by HTTPS URL,
archive SHA-256, and the size and SHA-256 of every admitted runtime file. The package builder extracts
only `pg_dump`, `pg_restore`, `psql`, `createdb`, their exact DLL closure, and the applicable license
files. An unexpected, missing, linked, or modified file fails the build or installation.

The archive currently expected by the manifest is:

```text
postgresql-18.6-3-windows-x64-binaries.zip
SHA-256 59f8ce701c63c2ed623c665a5e51b3ef6f2e37ccf837b68ffeed0742d0ae6abd
```

## Build

Build `px_backup` from the locked Rust workspace, then create the self-contained release directory:

```powershell
cargo build --offline --locked --release --manifest-path rust_server/Cargo.toml -p px_backup
pwsh -NoProfile -File scripts/server_backup/build_windows_package.ps1 `
  -BackupBinary rust_server/target/release/px_backup.exe `
  -PostgreSqlArchive C:\ReleaseInputs\postgresql-18.6-3-windows-x64-binaries.zip
```

The builder writes a versioned directory below `output/px_backup/releases` and prints both its path
and the SHA-256 of `package-manifest.json`. Preserve that manifest digest in the reviewed release
metadata; the installer requires it and does not discover or trust an arbitrary local package.

## Install or upgrade

Run elevated PowerShell 7 with the reviewed package path, manifest digest, and an absolute private
schema-2 configuration path:

```powershell
pwsh -NoProfile -File <package>\install.ps1 `
  -PackageRoot <package> `
  -ExpectedPackageManifestSha256 <64-lowercase-hex> `
  -ConfigPath C:\SecureInput\backup-config.json
```

The installer copies the release into `Program Files\Pixels\Backup\releases\<package-id>`, rewrites
the configuration to the installed and hashed PostgreSQL tools, stores runtime configuration below
`ProgramData\Pixels\<deployment-id>\backup`, and registers
`Pixels.Backup.<deployment-short-id>`. Each deployment uses its own unrestricted virtual service SID.
The protected release is read-only to that identity; configuration and credentials are read-only;
repository, scheduler, status, and configured offsite roots are writable. Their closed ACL contains
only the service SID, LocalSystem, and Administrators.

Installing the same or a newer reviewed package is the upgrade operation. The existing service is
stopped, the new versioned image and configuration are verified, and startup must remain stable. If
startup fails, the prior image path, configuration bytes, and running state are restored. Releases
are not selected by directory enumeration and no legacy layout is imported.

Production release transport still requires the outer Pixels installer/package signature and the
deployment secret manager. The manifest digest authenticates the reviewed payload at this boundary;
it is not a replacement for release signing or credential provisioning.

## Uninstall

```powershell
pwsh -NoProfile -File <package>\uninstall.ps1 -DeploymentId <uuid>
```

Uninstall stops and deletes only the selected SCM registration. Configuration, scheduler state,
status, recovery sets, offsite data, and versioned package files are intentionally retained so an
uninstall cannot silently destroy recovery material. Their removal is a separate, explicit data
retirement operation.

## Validation

The elevated validation uses the real pinned client against the digest-pinned PostgreSQL 18.6
container. It performs dump/list/create/restore/query, rejects an injected DLL before SCM creation,
tests first and covering installation, verifies rollback after a bad configuration, and verifies
uninstall with data retention:

```powershell
pwsh -NoProfile -File scripts/server_validation/windows_backup_package.ps1 `
  -PostgreSqlArchive C:\ReleaseInputs\postgresql-18.6-3-windows-x64-binaries.zip
```

Every run uses unique Docker, service, directory, and report identities and removes only those exact
test resources.
