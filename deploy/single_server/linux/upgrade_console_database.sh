#!/usr/bin/env bash
set -euo pipefail

configuration_root=/etc/pixels
administrator=/opt/pixels/bin/px_console_admin
backup_configuration="$configuration_root/backup.json"

# Fresh initialization is performed by setup-server, not by the upgrade path.
[[ -f "$configuration_root/setup.complete" ]] || exit 0
[[ -f "$backup_configuration" && ! -L "$backup_configuration" ]] || {
    echo 'Backup configuration must be a regular file before database upgrade.' >&2
    exit 1
}
jq -e '[.plan.targets[] | select(.database.service == "console" and .database.database == "pixels_console")] | length == 1' \
    "$backup_configuration" >/dev/null
"$administrator" preflight-single-server-database "$configuration_root"
upgrade_status=$("$administrator" upgrade-single-server-database "$configuration_root")
schema_version=$(printf '%s\n' "$upgrade_status" | sed -n 's/^SCHEMA_READY version=\([1-9][0-9]*\)$/\1/p')
[[ "$schema_version" =~ ^[1-9][0-9]*$ ]] || {
    echo 'Database upgrade did not report its schema version; services remain stopped.' >&2
    exit 1
}
temporary_configuration=$(mktemp "$configuration_root/.backup-schema-XXXXXXXX")
trap 'rm -f -- "$temporary_configuration"' EXIT
jq --argjson schema_version "$schema_version" \
    '(.plan.targets[] | select(.database.service == "console" and .database.database == "pixels_console") | .database.schema_version) = $schema_version' \
    "$backup_configuration" > "$temporary_configuration"
chmod --reference="$backup_configuration" "$temporary_configuration"
chown --reference="$backup_configuration" "$temporary_configuration"
sync -f "$temporary_configuration"
mv -f -- "$temporary_configuration" "$backup_configuration"
sync -f "$configuration_root"
printf '%s\n' "$upgrade_status"
