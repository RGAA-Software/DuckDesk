#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || $(id -u) -ne 0 || $(ps -p 1 -o comm=) != systemd || "$1" != /* ]]; then
    echo "usage (as root under systemd): private_backup_systemd.sh <absolute-candidate-directory>" >&2
    exit 2
fi
candidate_directory=$1
python3 "$candidate_directory/tools/verify_candidate.py" "$candidate_directory"
[[ -x "$candidate_directory/bin/px_backup" ]] || { echo "candidate backup executable is missing" >&2; exit 2; }

deployment_id=$(tr -d '\n' </proc/sys/kernel/random/uuid)
configuration_root="/etc/pixels/$deployment_id"
data_root="/var/lib/pixels/$deployment_id"
release_root="/opt/pixels/private/$deployment_id"
native_toolchain=false
tool_root="/opt/pixels/systemd-validation-$deployment_id"
if [[ -f "$candidate_directory/postgresql/18/sha256.json" ]]; then
    native_toolchain=true
    tool_root=/opt/pixels/postgresql/18
    /bin/sh "$candidate_directory/tools/install_pg_toolchain.sh" "$candidate_directory"
fi
tool_bin="$tool_root"
if [[ "$native_toolchain" == true ]]; then tool_bin="$tool_root/bin"; fi
service_name="pixels-private-backup@$deployment_id.service"
unit_destination=/etc/systemd/system/pixels-private-backup@.service
fixture_directory=$(mktemp -d)
source_configuration="$fixture_directory/backup-config.json"
original_unit="$fixture_directory/original-backup-unit"
for private_root in "$configuration_root" "$data_root" "$release_root"; do
    [[ ! -e "$private_root" ]] || { echo "isolated test path already exists" >&2; exit 2; }
done
if [[ "$native_toolchain" == false && -e "$tool_root" ]]; then
    echo "isolated tool path already exists" >&2
    exit 2
fi
if [[ -f "$unit_destination" ]]; then cp -p "$unit_destination" "$original_unit"; fi

cleanup() {
    cleanup_status=$?
    set +e
    systemctl disable --now "$service_name" >/dev/null 2>&1
    systemctl reset-failed "$service_name" >/dev/null 2>&1
    if [[ -f "$original_unit" ]]; then cp -p "$original_unit" "$unit_destination"; else rm -f -- "$unit_destination"; fi
    systemctl daemon-reload
    cleanup_roots=("$configuration_root" "$data_root" "$release_root")
    if [[ "$native_toolchain" == false ]]; then cleanup_roots+=("$tool_root"); fi
    for private_root in "${cleanup_roots[@]}"; do
        if [[ -d "$private_root" && ! -L "$private_root" ]]; then
            resolved_root=$(realpath -e -- "$private_root")
            case "$resolved_root" in
                "/etc/pixels/$deployment_id"|"/var/lib/pixels/$deployment_id"|"/opt/pixels/private/$deployment_id"|"/opt/pixels/systemd-validation-$deployment_id")
                    rm -r -- "$private_root"
                    ;;
            esac
        fi
    done
    rm -r -- "$fixture_directory"
    trap - EXIT
    exit "$cleanup_status"
}
trap cleanup EXIT

if [[ "$native_toolchain" == false ]]; then
    install -d -o root -g root -m 0755 "$tool_root"
    install -o root -g root -m 0755 /usr/bin/true "$tool_bin/pg_dump"
    install -o root -g root -m 0755 /usr/bin/true "$tool_bin/pg_restore"
fi
install -d -o root -g root -m 0711 /etc/pixels "$configuration_root"
install -d -o root -g root -m 0700 "$configuration_root/backup"
printf '%s' 'isolated-backup-password' >"$configuration_root/backup/console.pgpass"
chmod 0600 "$configuration_root/backup/console.pgpass"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
    -subj '/CN=isolated-postgres' \
    -keyout "$fixture_directory/postgres-ca.key" \
    -out "$configuration_root/backup/postgres-ca.crt" >/dev/null 2>&1
dump_hash=$(sha256sum "$tool_bin/pg_dump" | cut -d ' ' -f 1)
restore_hash=$(sha256sum "$tool_bin/pg_restore" | cut -d ' ' -f 1)
python3 - "$source_configuration" "$deployment_id" "$data_root/backup" "$tool_bin" "$dump_hash" "$restore_hash" <<'PY'
import json
import sys

configuration_path, deployment_id, data_root, tool_bin, dump_hash, restore_hash = sys.argv[1:]
configuration = {
    "schema_version": 2,
    "deployment_id": deployment_id,
    "repository_root": f"{data_root}/repository",
    "offsite_repository_root": None,
    "scheduler_root": f"{data_root}/scheduler",
    "status_root": f"{data_root}/status",
    "pg_dump_path": f"{tool_bin}/pg_dump",
    "pg_dump_sha256": dump_hash,
    "pg_restore_path": f"{tool_bin}/pg_restore",
    "pg_restore_sha256": restore_hash,
    "command_timeout_seconds": 60,
    "poll_interval_seconds": 1,
    "schedule": {"deployment_id": deployment_id, "anchor_unix": 4102444800, "period_seconds": 3600},
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
                    "service": "console", "host": "127.0.0.1", "port": 5432,
                    "database": "pixels_console", "username": "pixels_backup",
                    "password_file": f"/etc/pixels/{deployment_id}/backup/console.pgpass", "schema_version": 1,
                },
            },
            {"state": "not_applicable", "service": "auth", "reason": "not installed in validation deployment"},
            {"state": "not_applicable", "service": "desk", "reason": "not installed in validation deployment"},
        ],
    },
}
with open(configuration_path, "w", encoding="utf-8") as configuration_file:
    json.dump(configuration, configuration_file)
PY
chmod 0600 "$source_configuration"
chmod 0644 "$source_configuration"
if /bin/sh "$candidate_directory/tools/install_linux_component.sh" backup "$deployment_id" "$candidate_directory" "$source_configuration" >/dev/null 2>&1; then
    echo "backup installer accepted a permissive private configuration" >&2
    exit 1
fi
[[ ! -e "$release_root" ]] || { echo "rejected backup preflight changed installation state" >&2; exit 1; }
chmod 0600 "$source_configuration"
wrong_deployment_configuration="$fixture_directory/wrong-deployment.json"
python3 - "$source_configuration" "$wrong_deployment_configuration" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as configuration_file:
    configuration = json.load(configuration_file)
configuration["deployment_id"] = "00000000-0000-0000-0000-000000000001"
with open(sys.argv[2], "w", encoding="utf-8") as configuration_file:
    json.dump(configuration, configuration_file)
PY
chmod 0600 "$wrong_deployment_configuration"
if /bin/sh "$candidate_directory/tools/install_linux_component.sh" backup "$deployment_id" "$candidate_directory" "$wrong_deployment_configuration" >/dev/null 2>&1; then
    echo "backup installer accepted a different deployment" >&2
    exit 1
fi
[[ ! -e "$release_root" ]] || { echo "wrong-deployment preflight changed installation state" >&2; exit 1; }

wrong_tool_configuration="$fixture_directory/wrong-tool.json"
python3 - "$source_configuration" "$wrong_tool_configuration" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as configuration_file:
    configuration = json.load(configuration_file)
configuration["pg_dump_sha256"] = "0" * 64
with open(sys.argv[2], "w", encoding="utf-8") as configuration_file:
    json.dump(configuration, configuration_file)
PY
chmod 0600 "$wrong_tool_configuration"
if /bin/sh "$candidate_directory/tools/install_linux_component.sh" backup "$deployment_id" "$candidate_directory" "$wrong_tool_configuration" >/dev/null 2>&1; then
    echo "backup installer accepted an unpinned PostgreSQL tool" >&2
    exit 1
fi
[[ ! -e "$release_root" ]] || { echo "wrong-tool preflight changed installation state" >&2; exit 1; }

chmod 0644 "$configuration_root/backup/console.pgpass"
if /bin/sh "$candidate_directory/tools/install_linux_component.sh" backup "$deployment_id" "$candidate_directory" "$source_configuration" >/dev/null 2>&1; then
    echo "backup installer accepted a permissive database credential" >&2
    exit 1
fi
[[ ! -e "$release_root" ]] || { echo "credential preflight changed installation state" >&2; exit 1; }
chmod 0600 "$configuration_root/backup/console.pgpass"

if [[ "$native_toolchain" == false ]]; then
    chmod 0700 "$tool_bin/pg_dump"
    if /bin/sh "$candidate_directory/tools/install_linux_component.sh" backup "$deployment_id" "$candidate_directory" "$source_configuration" >/dev/null 2>&1; then
        echo "backup installer accepted a tool inaccessible to the backup identity" >&2
        exit 1
    fi
    [[ ! -e "$release_root" ]] || { echo "unreadable-tool preflight changed release state" >&2; exit 1; }
    chmod 0755 "$tool_bin/pg_dump"
fi

chmod 0600 "$configuration_root/backup/postgres-ca.crt"
if /bin/sh "$candidate_directory/tools/install_linux_component.sh" backup "$deployment_id" "$candidate_directory" "$source_configuration" >/dev/null 2>&1; then
    echo "backup installer accepted a TLS root inaccessible to the backup identity" >&2
    exit 1
fi
[[ ! -e "$release_root" ]] || { echo "unreadable-TLS preflight changed release state" >&2; exit 1; }
chmod 0644 "$configuration_root/backup/postgres-ca.crt"

/bin/sh "$candidate_directory/tools/install_linux_component.sh" backup "$deployment_id" "$candidate_directory" "$source_configuration"
systemctl is-active --quiet "$service_name"
main_pid=$(systemctl show "$service_name" --property=MainPID --value)
[[ $(ps -o user= -p "$main_pid" | xargs) == pixels-backup ]] || { echo "backup did not run as its dedicated identity" >&2; exit 1; }
[[ $(stat -c '%U:%G:%a' "$configuration_root/backup/config.json") == pixels-backup:pixels-backup:400 ]] || {
    echo "installed backup configuration permissions are invalid" >&2; exit 1;
}
[[ $(stat -c '%U:%G:%a' "$configuration_root/backup/console.pgpass") == pixels-backup:pixels-backup:400 ]] || {
    echo "installed backup database credential is not readable only by the backup identity" >&2; exit 1;
}
runuser -u pixels-backup -- test -r "$configuration_root/backup/postgres-ca.crt"
runuser -u pixels-backup -- test -x "$tool_bin/pg_dump"
for attempt_number in {1..30}; do
    [[ -f "$data_root/backup/status/status.json" ]] && break
    sleep 0.2
done
python3 - "$data_root/backup/status/status.json" "$deployment_id" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as status_file:
    status = json.load(status_file)
if status.get("schema_version") != 2 or status.get("deployment_id") != sys.argv[2]:
    raise SystemExit("backup status does not match deployment")
PY
systemctl restart "$service_name"
systemctl is-active --quiet "$service_name"
/bin/sh "$candidate_directory/tools/uninstall_linux_component.sh" backup "$deployment_id"
[[ $(systemctl is-active "$service_name" 2>/dev/null || true) != active ]] || { echo "backup remained active" >&2; exit 1; }
[[ -f "$configuration_root/backup/config.json" && -f "$data_root/backup/status/status.json" && -d "$release_root/releases" ]] || {
    echo "backup unregister removed private configuration or state" >&2; exit 1;
}
echo "PASS PRIVATE BACKUP SYSTEMD: private-config preflight, packaged status, restart and unregister"
