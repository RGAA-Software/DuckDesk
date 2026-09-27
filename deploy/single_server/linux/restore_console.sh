#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || ! "$1" =~ ^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$ ]]; then
    echo 'usage: ./restore_console.sh <recovery-set-uuid>' >&2
    exit 2
fi
deployment_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$deployment_directory"
docker compose run --rm --no-deps -T --user 0:0 --entrypoint /bin/bash backup \
    /opt/pixels/restore_console.sh "$1"
read -r -p 'Restore this backup into a new isolated Console database? Type RESTORE: ' confirmation
[[ "$confirmation" == RESTORE ]] || { echo 'Cancelled; no database was created.'; exit 0; }
read -r -p 'PostgreSQL administrator role: ' postgres_user
[[ "$postgres_user" =~ ^[a-z_][a-z0-9_]*$ ]] || { echo 'Invalid PostgreSQL role.' >&2; exit 2; }
read -r -s -p 'PostgreSQL administrator password: ' postgres_password
echo
[[ -n "$postgres_password" ]] || { echo 'Password is required.' >&2; exit 2; }
backup_was_running=$(docker compose ps --status running --services backup)
if [[ "$backup_was_running" == backup ]]; then docker compose stop backup; fi
restart_backup() { if [[ "$backup_was_running" == backup ]]; then docker compose start backup >/dev/null; fi; }
trap restart_backup EXIT
printf '%s\n' "$postgres_password" | docker compose run --rm --no-deps -T --user 0:0 --entrypoint /bin/bash backup \
    /opt/pixels/restore_console.sh "$1" execute "$postgres_user"
unset postgres_password
