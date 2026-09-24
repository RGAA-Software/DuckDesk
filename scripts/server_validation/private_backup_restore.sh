#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 || "$1" != /* || "${PIXELS_PG_ISOLATED_TEST:-}" != 1
      || ! "${PIXELS_TEST_CONTAINER:-}" =~ ^[a-f0-9]{12,64}$
      || ! "${PIXELS_DEPLOYMENT_ID:-}" =~ ^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$ ]]; then
    echo "usage (with isolated PostgreSQL): private_backup_restore.sh <absolute-candidate-directory> [absolute-postgresql-toolchain]" >&2
    exit 2
fi
candidate_directory=$1
source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
python3 "$candidate_directory/tools/verify_candidate.py" "$candidate_directory"
[[ -x "$candidate_directory/bin/px_backup" ]] || { echo "packaged backup executor is unavailable" >&2; exit 2; }
native_toolchain=false
if [[ $# -eq 2 ]]; then
    toolchain_directory=$2
    [[ "$toolchain_directory" == /* && -n "${PIXELS_TEST_CONSOLE_OWNER_URL:-}" ]] || {
        echo "native client test requires an absolute toolchain and isolated Console owner URL" >&2; exit 2;
    }
    python3 "$source_root/scripts/server_private/verify_pg_toolchain.py" "$toolchain_directory"
    export LD_LIBRARY_PATH="$toolchain_directory/lib"
    native_toolchain=true
fi

fixture_directory=$(mktemp -d -t pixels-private-backup-XXXXXXXX)
restore_database="pixels_p4_restore_${PIXELS_DEPLOYMENT_ID:0:8}"
container_archive="/tmp/pixels-p4-${PIXELS_DEPLOYMENT_ID:0:8}.dump"
backup_process_id=
cleanup() {
    cleanup_status=$?
    set +e
    if [[ -n "$backup_process_id" ]]; then
        kill -TERM "$backup_process_id" 2>/dev/null
        wait "$backup_process_id" 2>/dev/null
    fi
    docker exec "$PIXELS_TEST_CONTAINER" dropdb --if-exists --force -U pixels_admin "$restore_database" >/dev/null 2>&1
    docker exec "$PIXELS_TEST_CONTAINER" rm -f -- "$container_archive" >/dev/null 2>&1
    if [[ -d "$fixture_directory" && ! -L "$fixture_directory" ]]; then
        resolved_fixture=$(realpath -e -- "$fixture_directory")
        case "$resolved_fixture" in /tmp/pixels-private-backup-*) rm -r -- "$fixture_directory" ;; esac
    fi
    trap - EXIT
    exit "$cleanup_status"
}
trap cleanup EXIT

install -d -m 0700 "$fixture_directory/repository" "$fixture_directory/scheduler" "$fixture_directory/status" "$fixture_directory/tools"
if [[ "$native_toolchain" == true ]]; then
    dump_path="$toolchain_directory/bin/pg_dump"
    restore_path="$toolchain_directory/bin/pg_restore"
else
    install -m 0700 "$source_root/scripts/server_validation/pg_tool_wrappers/pg_dump" "$fixture_directory/tools/pg_dump"
    install -m 0700 "$source_root/scripts/server_validation/pg_tool_wrappers/pg_restore" "$fixture_directory/tools/pg_restore"
    dump_path="$fixture_directory/tools/pg_dump"
    restore_path="$fixture_directory/tools/pg_restore"
    printf '%s\n' 'isolated-container-tool-credential' >"$fixture_directory/console.pgpass"
    chmod 0600 "$fixture_directory/console.pgpass"
fi
dump_hash=$(sha256sum "$dump_path" | cut -d ' ' -f 1)
restore_hash=$(sha256sum "$restore_path" | cut -d ' ' -f 1)

docker exec "$PIXELS_TEST_CONTAINER" psql -X -v ON_ERROR_STOP=1 -U pixels_admin -d pixels_console -c \
    "CREATE TABLE pixels.p4_backup_probe(probe_id integer PRIMARY KEY, payload text NOT NULL); \
    ALTER TABLE pixels.p4_backup_probe OWNER TO pixels_console_owner; INSERT INTO pixels.p4_backup_probe VALUES (1, 'before');" >/dev/null

anchor_unix=$(date +%s)
python3 - "$fixture_directory" "$PIXELS_DEPLOYMENT_ID" "$anchor_unix" "$dump_path" "$dump_hash" "$restore_path" "$restore_hash" \
    "$native_toolchain" <<'PY'
import json
import os
import sys
from pathlib import Path
from urllib.parse import urlsplit

fixture_directory = Path(sys.argv[1])
deployment_id, anchor_unix, dump_path, dump_hash, restore_path, restore_hash, native_toolchain = sys.argv[2:]
database_host, database_port, database_username = "127.0.0.1", 5432, "pixels_admin"
if native_toolchain == "true":
    owner_connection = urlsplit(os.environ["PIXELS_TEST_CONSOLE_OWNER_URL"])
    if (owner_connection.scheme != "postgresql" or owner_connection.path != "/pixels_console"
            or not owner_connection.hostname or not owner_connection.port
            or not owner_connection.username or not owner_connection.password):
        raise SystemExit("isolated Console owner URL is invalid")
    database_host = owner_connection.hostname
    database_port = owner_connection.port
    database_username = owner_connection.username
    credential_descriptor = os.open(fixture_directory / "console.pgpass", os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(credential_descriptor, "w", encoding="utf-8") as credential_file:
        credential_file.write(f"{database_host}:{database_port}:*:{database_username}:{owner_connection.password}\n")
configuration = {
    "schema_version": 2,
    "deployment_id": deployment_id,
    "repository_root": str(fixture_directory / "repository"),
    "offsite_repository_root": None,
    "scheduler_root": str(fixture_directory / "scheduler"),
    "status_root": str(fixture_directory / "status"),
    "pg_dump_path": dump_path,
    "pg_dump_sha256": dump_hash,
    "pg_restore_path": restore_path,
    "pg_restore_sha256": restore_hash,
    "command_timeout_seconds": 30,
    "poll_interval_seconds": 1,
    "schedule": {"deployment_id": deployment_id, "anchor_unix": int(anchor_unix), "period_seconds": 3600},
    "retention": {"hourly": 24, "daily": 7, "weekly": 4, "monthly": 6, "pre_upgrade": 5, "manual_days": 30},
    "offsite_retention": None,
    "plan": {
        "deployment_id": deployment_id,
        "kind": "independent",
        "write_barrier_proof_file": None,
        "retention": ["hourly"],
        "previous_recovery_set_id": None,
        "targets": [
            {
                "state": "required",
                "database": {
                    "service": "console", "host": database_host, "port": database_port,
                    "database": "pixels_console", "username": database_username,
                    "password_file": str(fixture_directory / "console.pgpass"), "schema_version": 1,
                },
            },
            {"state": "not_applicable", "service": "auth", "reason": "not installed in isolated test"},
            {"state": "not_applicable", "service": "desk", "reason": "not installed in isolated test"},
        ],
    },
}
with (fixture_directory / "config.json").open("w", encoding="utf-8") as configuration_file:
    json.dump(configuration, configuration_file)
PY
chmod 0600 "$fixture_directory/config.json"

"$candidate_directory/bin/px_backup" run "$fixture_directory/config.json" >"$fixture_directory/backup.stdout" 2>"$fixture_directory/backup.stderr" &
backup_process_id=$!
recovery_set_id=
for attempt_number in {1..120}; do
    if [[ -f "$fixture_directory/status/status.json" ]]; then
        recovery_set_id=$(python3 - "$fixture_directory/status/status.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as status_file:
    status = json.load(status_file)
print(status.get("last_recovery_set_id") or "")
PY
        )
        [[ -z "$recovery_set_id" ]] || break
    fi
    if ! kill -0 "$backup_process_id" 2>/dev/null; then
        echo "packaged backup executor stopped before publishing a recovery set" >&2
        exit 1
    fi
    sleep 0.25
done
[[ "$recovery_set_id" =~ ^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$ ]] || {
    echo "packaged backup executor did not publish a verified recovery set" >&2; exit 1;
}
kill -TERM "$backup_process_id"
wait "$backup_process_id"
backup_process_id=

archive_path="$fixture_directory/repository/$recovery_set_id/console.dump"
python3 - "$fixture_directory/repository/$recovery_set_id/manifest.json" "$archive_path" "$PIXELS_DEPLOYMENT_ID" <<'PY'
import hashlib
import json
import sys
from pathlib import Path

manifest_path, archive_path = (Path(path_text) for path_text in sys.argv[1:3])
with manifest_path.open(encoding="utf-8") as manifest_file:
    manifest = json.load(manifest_file)
required_console = next(member["member"] for member in manifest["members"] if member["service"] == "console")
archive_hash = hashlib.sha256(archive_path.read_bytes()).hexdigest()
if (manifest["status"] != "verified" or manifest["deployment_id"] != sys.argv[3]
        or required_console["state"] != "required" or required_console["archive_sha256"] != archive_hash):
    raise SystemExit("packaged backup manifest or archive is invalid")
PY

docker exec "$PIXELS_TEST_CONTAINER" psql -X -v ON_ERROR_STOP=1 -U pixels_admin -d pixels_console -c \
    "UPDATE pixels.p4_backup_probe SET payload='after' WHERE probe_id=1;" >/dev/null
docker exec "$PIXELS_TEST_CONTAINER" createdb -U pixels_admin -O pixels_console_owner "$restore_database"
if [[ "$native_toolchain" == true ]]; then
    mapfile -t connection_fields < <(python3 - "$fixture_directory/config.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as configuration_file:
    database_target = json.load(configuration_file)["plan"]["targets"][0]["database"]
for field_name in ("host", "port", "username"):
    print(database_target[field_name])
PY
    )
    PGPASSFILE="$fixture_directory/console.pgpass" "$restore_path" --exit-on-error --no-owner \
        --host "${connection_fields[0]}" --port "${connection_fields[1]}" --username "${connection_fields[2]}" \
        --dbname "$restore_database" "$archive_path" >/dev/null
else
    docker cp "$archive_path" "$PIXELS_TEST_CONTAINER:$container_archive" >/dev/null
    docker exec "$PIXELS_TEST_CONTAINER" pg_restore --exit-on-error --no-owner -U pixels_admin -d "$restore_database" "$container_archive" >/dev/null
fi
restored_payload=$(docker exec "$PIXELS_TEST_CONTAINER" psql -X -At -U pixels_admin -d "$restore_database" -c \
    'SELECT payload FROM pixels.p4_backup_probe WHERE probe_id=1')
source_payload=$(docker exec "$PIXELS_TEST_CONTAINER" psql -X -At -U pixels_admin -d pixels_console -c \
    'SELECT payload FROM pixels.p4_backup_probe WHERE probe_id=1')
[[ "$restored_payload" == before && "$source_payload" == after ]] || {
    echo "isolated PostgreSQL restore did not reproduce the pre-change record" >&2; exit 1;
}
if [[ "$native_toolchain" == true ]]; then
    echo "PASS PRIVATE BACKUP RESTORE: packaged px_backup and pinned native PG18 tools restored pre-change data"
else
    echo "PASS PRIVATE BACKUP RESTORE: packaged px_backup produced a hash-verified PG18 archive and restored pre-change data"
fi
