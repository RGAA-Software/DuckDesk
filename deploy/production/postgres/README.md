# Pixels PostgreSQL production backup image

This directory defines the PostgreSQL 18.6 and pgBackRest 2.59.1 image used by the production backup profile. The PostgreSQL base image is pinned by digest and the pgBackRest Debian package is pinned by its exact version. A release pipeline must build, scan, sign, and publish the resulting image by digest; production must not rebuild a mutable tag during an incident.

`pgbackrest.conf.example` is a single-filesystem syntax example and a local functional baseline. It is not an independent backup fault domain. `pgbackrest.s3.conf.example` is a non-runnable object-repository rendering template: the deployment system must replace its endpoint, bucket and region placeholders and inject the three documented secrets without writing them to source control or logs. A production deployment must generate a private `/etc/pgbackrest/pgbackrest.conf` with one of these repository layouts:

- a dedicated pgBackRest repository host reached through its authenticated protocol; or
- an S3-compatible object repository in a separate host/account/fault domain, with TLS verification and client-side repository encryption.

Do not put static object-store keys or `repo*-cipher-pass` in this repository or ordinary logs. Prefer workload identity (`repo1-s3-key-type=auto`) when the provider supports it; otherwise render the private config from the deployment secret store with owner-only permissions. The repository encryption passphrase must be backed up separately from both PostgreSQL and the repository.

Required PostgreSQL settings are:

```text
archive_mode=on
archive_command=pgbackrest --stanza=pixels archive-push %p
```

After generating the private configuration, run `stanza-create`, then `check`, before accepting production traffic. Backups and WAL are retained as one dependency graph; never delete WAL by file age. Alert immediately when `archive_command` fails or the last archived WAL age exceeds the deployment threshold. The initial service objective is a last recoverable point no more than five minutes behind the primary.

The local destructive functional gate is:

```powershell
pwsh -NoProfile -File scripts/server_validation/postgres_pitr.ps1
```

It creates uniquely named Docker containers, volumes, a network and an image, then removes only those resources. It verifies full backup, continuous WAL, named-point recovery before a committed `DROP`, and fail-closed behavior after deleting the exact target WAL from an isolated repository copy. Its report explicitly identifies itself as a single-host functional test; it does not replace independent-host, access-control, bandwidth, retention or disaster-recovery acceptance.

The S3-compatible protocol and encryption gate is:

```powershell
pwsh -NoProfile -File scripts/server_validation/postgres_pitr_object_store.ps1
```

It uses digest-pinned MinIO and MinIO Client images in a private Docker network, generates one-run credentials and a one-run AES-256 repository passphrase, deletes the primary PostgreSQL container and data volume, rejects a restore with the wrong passphrase, and restores the committed pre-`DROP` state from S3 objects. The test uses an ephemeral self-signed TLS certificate with certificate verification disabled and keeps MinIO on the same Docker host, so it proves the object protocol and encrypted recovery path only. Production acceptance still requires verified TLS, deployment secret custody, and storage in a separate account/host/fault domain.

Reference behavior follows the [pgBackRest user guide](https://pgbackrest.org/user-guide.html), [pgBackRest configuration reference](https://pgbackrest.org/configuration.html), and [PostgreSQL 18 continuous archiving documentation](https://www.postgresql.org/docs/18/continuous-archiving.html).
