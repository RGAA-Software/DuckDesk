#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 || ! "$1" =~ ^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$ ]]; then
    echo 'usage: restore_console.sh <recovery-set-uuid> [execute <postgres-admin-user>]' >&2
    exit 2
fi
recovery_set_id=$1
if [[ $# -gt 1 && ( $# -ne 3 || "$2" != execute || ! "$3" =~ ^[a-z_][a-z0-9_]*$ ) ]]; then
    echo 'The execute mode requires a PostgreSQL administrator role.' >&2
    exit 2
fi

backup_config=/etc/pixels/backup.json
status_file=/var/lib/pixels/backup/status/status.json
tool_root=/opt/pixels/postgresql/18
[[ -f "$backup_config" && ! -L "$backup_config" ]] || {
    echo 'Private Backup configuration is unavailable.' >&2; exit 1;
}
jq -e '.schema_version == 2 and .plan.kind == "independent" and .deployment_id == .plan.deployment_id and
    (.plan.targets | length) == 3 and
    ([.plan.targets[] | select(.service == "console" and .state == "required" and .database.database == "pixels_console")] | length) == 1 and
    ([.plan.targets[] | select(.service == "auth" and .state == "not_applicable")] | length) == 1 and
    ([.plan.targets[] | select(.service == "desk" and .state == "not_applicable")] | length) == 1' \
    "$backup_config" >/dev/null
deployment_id=$(jq -r '.deployment_id' "$backup_config")
repository_root=$(jq -r '.repository_root' "$backup_config")
database_host=$(jq -r '.plan.targets[] | select(.service == "console").database.host' "$backup_config")
database_port=$(jq -r '.plan.targets[] | select(.service == "console").database.port' "$backup_config")
[[ "$repository_root" == /* && "$database_port" =~ ^[0-9]+$ ]] || { echo 'Backup target is invalid.' >&2; exit 1; }
set_directory="$repository_root/$recovery_set_id"
manifest_file="$set_directory/manifest.json"
archive_file="$set_directory/console.dump"
[[ -f "$manifest_file" && ! -L "$manifest_file" && -f "$archive_file" && ! -L "$archive_file" ]] || {
    echo 'Recovery set files are unavailable.' >&2; exit 1;
}
jq -e --arg recovery_set_id "$recovery_set_id" --arg deployment_id "$deployment_id" '
    .schema_version == 3 and .recovery_set_id == $recovery_set_id and .deployment_id == $deployment_id and
    .kind == "independent" and (.status == "verified" or .status == "offsite_verified" or .status == "restore_tested") and
    .security_evidence == {state:"unavailable", reason:"independent_backup"} and
    (.members | length) == 3 and
    ([.members[] | select(.service == "console" and .member.state == "required" and
        .member.database == "pixels_console" and .member.archive_file == "console.dump")] | length) == 1 and
    ([.members[] | select(.service == "auth" and .member.state == "not_applicable")] | length) == 1 and
    ([.members[] | select(.service == "desk" and .member.state == "not_applicable")] | length) == 1
' "$manifest_file" >/dev/null
archive_hash=$(jq -r '.members[] | select(.service == "console").member.archive_sha256' "$manifest_file")
schema_version=$(jq -r '.members[] | select(.service == "console").member.schema_version' "$manifest_file")
[[ "$archive_hash" =~ ^[0-9a-f]{64}$ ]] || { echo 'Archive digest is invalid.' >&2; exit 1; }
[[ "$schema_version" =~ ^[1-9][0-9]*$ ]] || { echo 'Console schema version is invalid.' >&2; exit 1; }
printf '%s  %s\n' "$archive_hash" "$archive_file" | sha256sum --check --status || {
    echo 'Console archive SHA-256 differs.' >&2; exit 1;
}
for tool_name in createdb pg_restore psql; do
    expected_hash=$(jq -r --arg tool "bin/$tool_name" '.artifacts[$tool]' "$tool_root/sha256.json")
    [[ "$expected_hash" =~ ^[0-9a-f]{64}$ && -x "$tool_root/bin/$tool_name" ]] || {
        echo "Packaged PostgreSQL tool is unavailable: $tool_name" >&2; exit 1;
    }
    printf '%s  %s\n' "$expected_hash" "$tool_root/bin/$tool_name" | sha256sum --check --status || {
        echo "Packaged PostgreSQL tool differs: $tool_name" >&2; exit 1;
    }
done
target_database="pixels_console_restore_${recovery_set_id//-/}"
echo "Verified Console backup $recovery_set_id; isolated target: $target_database"
[[ -f "$status_file" && ! -L "$status_file" ]] || { echo 'Backup status is unavailable.' >&2; exit 1; }
jq -e --arg deployment_id "$deployment_id" '.deployment_id == $deployment_id and .active_task == null' "$status_file" >/dev/null || {
    echo 'Backup is active or its status differs.' >&2; exit 1;
}
if [[ $# -eq 1 ]]; then
    echo 'Preflight only; the live Console database was not changed.'
    exit 0
fi
postgres_user=$3
IFS= read -r postgres_password || { echo 'PostgreSQL administrator password is required on stdin.' >&2; exit 1; }
[[ -n "$postgres_password" && "$postgres_password" != *$'\n'* ]] || { echo 'PostgreSQL password is invalid.' >&2; exit 1; }
temporary_directory=$(mktemp -d -t pixels-console-restore-XXXXXXXX)
trap 'rm -f -- "$temporary_directory/pgpass"; rmdir -- "$temporary_directory"' EXIT
chmod 0700 "$temporary_directory"
escaped_password=${postgres_password//\\/\\\\}
escaped_password=${escaped_password//:/\\:}
printf '%s:%s:*:%s:%s\n' "$database_host" "$database_port" "$postgres_user" "$escaped_password" >"$temporary_directory/pgpass"
unset postgres_password escaped_password
chmod 0600 "$temporary_directory/pgpass"
export PGPASSFILE="$temporary_directory/pgpass" PGSSLMODE=prefer
connection=(--host "$database_host" --port "$database_port" --username "$postgres_user" --no-password)
"$tool_root/bin/createdb" "${connection[@]}" --maintenance-db=postgres --template=template0 --owner=pixels_console_owner "$target_database"
"$tool_root/bin/psql" "${connection[@]}" -X --set=ON_ERROR_STOP=1 --dbname postgres \
    --command "REVOKE ALL ON DATABASE $target_database FROM PUBLIC; GRANT CONNECT ON DATABASE $target_database TO pixels_console_runtime"
"$tool_root/bin/pg_restore" "${connection[@]}" --exit-on-error --no-owner --role=pixels_console_owner --dbname "$target_database" "$archive_file"
identity=$("$tool_root/bin/psql" "${connection[@]}" -X -A -t --set=ON_ERROR_STOP=1 --dbname "$target_database" \
    --command "SELECT identity.service || '|' || identity.deployment_id::text || '|' || pg_catalog.pg_get_userbyid(database_record.datdba) || '|' || COUNT(migration.version)::text || '|' || COALESCE(BOOL_AND(migration.success), FALSE)::text FROM pixels.deployment_identity AS identity CROSS JOIN pg_catalog.pg_database AS database_record LEFT JOIN pixels._sqlx_migrations AS migration ON TRUE WHERE database_record.datname = current_database() GROUP BY identity.service, identity.deployment_id, database_record.datdba")
[[ "$identity" == "console|$deployment_id|pixels_console_owner|$schema_version|true" ]] || {
    echo 'Restored Console identity, owner or migration state differs; isolated database retained for inspection.' >&2; exit 1;
}
echo "Restored and verified $target_database. The live pixels_console database and Console configuration were not changed."
