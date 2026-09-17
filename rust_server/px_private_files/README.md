# Private files and immutable cache IO

Shared by Auth private-material loading and Console cache IO. Auth's checked-handle/ACL
implementation lives in `src/private.rs`; it is not duplicated in the service.

`CacheRoot` requires an explicitly provisioned private local directory and a deployment UUID.
It retains a single-process owner lock; blob guards retain that owner, use nonblocking locks,
and derive all child names from UUIDs. The baseline accepts Windows fixed NTFS and Linux ext
filesystem roots; acceptance runs use NTFS/ext4. Network/FUSE/overlay/tmpfs roots are not accepted.

Writers are one-attempt objects with bounded chunks, exact size/SHA-256, sync and no-overwrite
publication. Cancelled/collected IDs cannot be reused. Readers validate actual content and hold
a shared lock. Exact removal preserves lock tombstones and never scans/deletes unknown names.
All IO is synchronous: the composition layer must use a bounded blocking executor, not block
async request workers or hold SQL transactions during IO.

This library does **not** authorize media requests or prove PostgreSQL cache state. A coordinator
must check current grants, recording/root identity, attempt leases, references and pin/retention
rules while holding the physical guard. Its typed publication proof is necessary, not sufficient,
to commit a database Ready reference. No HTTP static serving path is provided.

Run focused native checks through:

```powershell
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 TestSuite -Suite files
```

`Test -Linux` runs the same eight IO/process cases on Windows and WSL/Linux, plus Auth key
permissions and the full PG/service regression. The `px_cache_probe` binary is test-only,
requires `integration-probe`, and is not a product/deployment executable.

Scope and remaining coordinator/media acceptance: [cache contract](../../docs/postgresql_cache_file_contract.md).
