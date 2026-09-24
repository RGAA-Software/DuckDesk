#!/usr/bin/env bash
set -euo pipefail
trap 'echo "private systemd validation failed at line $LINENO" >&2' ERR

if [[ $# -lt 1 || $# -gt 2 || "$1" != /* || ( $# -eq 2 && "$2" != /* ) ]]; then
    echo "usage (as root with isolated PostgreSQL): private_backup_native_systemd.sh <absolute-candidate-directory> [isolated-license-fixture]" >&2
    exit 2
fi
[[ $(id -u) == 0 ]] || { echo "systemd Backup validation requires root" >&2; exit 2; }
[[ $(ps -p 1 -o comm=) == systemd ]] || { echo "systemd Backup validation requires systemd" >&2; exit 2; }
[[ "${PIXELS_PG_ISOLATED_TEST:-}" == 1 ]] || { echo "isolated PostgreSQL test flag is required" >&2; exit 2; }
[[ "${PIXELS_TEST_CONTAINER:-}" =~ ^[a-f0-9]{12,64}$ ]] || { echo "isolated PostgreSQL container identity is invalid" >&2; exit 2; }
[[ "${PIXELS_DEPLOYMENT_ID:-}" =~ ^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$ ]] || {
    echo "isolated deployment identity is invalid" >&2; exit 2;
}
[[ -n "${PIXELS_TEST_CONSOLE_OWNER_URL:-}" ]] || { echo "isolated Console owner URL is unavailable" >&2; exit 2; }
candidate_directory=$1
fixture_binary=${2:-}
python3 "$candidate_directory/tools/verify_candidate.py" "$candidate_directory"
/bin/sh "$candidate_directory/tools/install_pg_toolchain.sh" "$candidate_directory"
if [[ -n "$fixture_binary" && ! -x "$fixture_binary" ]]; then
    echo "isolated license fixture is unavailable" >&2
    exit 2
fi

deployment_id=$PIXELS_DEPLOYMENT_ID
console_database=pixels_console
desk_database=pixels_desk
bootstrap_retained=0
configuration_root="/etc/pixels/$deployment_id"
data_root="/var/lib/pixels/$deployment_id"
release_root="/opt/pixels/private/$deployment_id"
service_name="pixels-private-backup@$deployment_id.service"
integrated_components=(console relay desk)
unit_destination=/etc/systemd/system/pixels-private-backup@.service
restore_database="pixels_p4_systemd_${deployment_id:0:8}"
for private_root in "$configuration_root" "$data_root" "$release_root"; do
    [[ ! -e "$private_root" ]] || { echo "isolated deployment path already exists" >&2; exit 2; }
done
if [[ -n "$fixture_binary" ]]; then
    for component in console relay desk backup; do
        offline_dropin="/etc/systemd/system/pixels-private-$component@$deployment_id.service.d"
        [[ ! -e "$offline_dropin" && ! -L "$offline_dropin" ]] || {
            echo "isolated network-policy path already exists" >&2; exit 2;
        }
    done
fi
fixture_directory=$(mktemp -d -t pixels-native-backup-XXXXXXXX)
offline_probe_pid=
source_configuration="$fixture_directory/backup-config.json"
original_unit="$fixture_directory/original-backup-unit"
if [[ -f "$unit_destination" ]]; then cp -p "$unit_destination" "$original_unit"; fi
if [[ -n "$fixture_binary" ]]; then
    for component in "${integrated_components[@]}"; do
        component_unit="/etc/systemd/system/pixels-private-$component@.service"
        if [[ -f "$component_unit" ]]; then
            cp -p "$component_unit" "$fixture_directory/original-$component-unit"
        fi
    done
fi

cleanup() {
    cleanup_status=$?
    set +e
    if [[ -n "$offline_probe_pid" ]]; then
        kill "$offline_probe_pid" >/dev/null 2>&1
        wait "$offline_probe_pid" >/dev/null 2>&1
    fi
    systemctl disable --now "$service_name" >/dev/null 2>&1
    systemctl reset-failed "$service_name" >/dev/null 2>&1
    if [[ -n "$fixture_binary" ]]; then
        for component in "${integrated_components[@]}"; do
            systemctl disable --now "pixels-private-$component@$deployment_id.service" >/dev/null 2>&1
            systemctl reset-failed "pixels-private-$component@$deployment_id.service" >/dev/null 2>&1
            offline_dropin="/etc/systemd/system/pixels-private-$component@$deployment_id.service.d"
            if [[ -d "$offline_dropin" && ! -L "$offline_dropin" ]]; then
                resolved_dropin=$(realpath -e -- "$offline_dropin")
                if [[ "$resolved_dropin" == "$offline_dropin" ]]; then rm -r -- "$offline_dropin"; fi
            fi
            component_unit="/etc/systemd/system/pixels-private-$component@.service"
            original_component_unit="$fixture_directory/original-$component-unit"
            if [[ -f "$original_component_unit" ]]; then
                cp -p "$original_component_unit" "$component_unit"
            else
                rm -f -- "$component_unit"
            fi
        done
    fi
    offline_backup_dropin="/etc/systemd/system/$service_name.d"
    if [[ -n "$fixture_binary" && -d "$offline_backup_dropin" && ! -L "$offline_backup_dropin" ]]; then
        resolved_dropin=$(realpath -e -- "$offline_backup_dropin")
        if [[ "$resolved_dropin" == "$offline_backup_dropin" ]]; then rm -r -- "$offline_backup_dropin"; fi
    fi
    if [[ -f "$original_unit" ]]; then cp -p "$original_unit" "$unit_destination"; else rm -f -- "$unit_destination"; fi
    systemctl daemon-reload
    if [[ "$bootstrap_retained" == 1 ]]; then
        for database_name in "$console_database" "$desk_database"; do
            docker exec "$PIXELS_TEST_CONTAINER" dropdb --if-exists --force -U pixels_admin "$database_name" >/dev/null 2>&1
        done
    fi
    docker exec "$PIXELS_TEST_CONTAINER" dropdb --if-exists --force -U pixels_admin "$restore_database" >/dev/null 2>&1
    for private_root in "$configuration_root" "$data_root" "$release_root"; do
        if [[ -d "$private_root" && ! -L "$private_root" ]]; then
            resolved_root=$(realpath -e -- "$private_root")
            case "$resolved_root" in
                "/etc/pixels/$deployment_id"|"/var/lib/pixels/$deployment_id"|"/opt/pixels/private/$deployment_id")
                    rm -r -- "$resolved_root"
                    ;;
            esac
        fi
    done
    if [[ -d "$fixture_directory" && ! -L "$fixture_directory" ]]; then
        resolved_fixture=$(realpath -e -- "$fixture_directory")
        case "$resolved_fixture" in /tmp/pixels-native-backup-*) rm -r -- "$resolved_fixture" ;; esac
    fi
    trap - EXIT
    exit "$cleanup_status"
}
trap cleanup EXIT

if [[ -n "$fixture_binary" ]]; then
    PIXELS_PG_INTEGRATED_TEST=1 bash "$(dirname -- "$0")/private_candidate_bootstrap.sh" \
        "$candidate_directory" --retain-for-systemd
    bootstrap_retained=1
    console_database="pixels_console_candidate_${deployment_id:0:8}"
    desk_database="pixels_desk_candidate_${deployment_id:0:8}"
    [[ "$PIXELS_TEST_CONSOLE_OWNER_URL" == */pixels_console \
        && "$PIXELS_TEST_CONSOLE_RUNTIME_URL" == */pixels_console \
        && "$PIXELS_TEST_DESK_OWNER_URL" == */pixels_desk \
        && "$PIXELS_TEST_DESK_RUNTIME_URL" == */pixels_desk ]] || {
        echo "isolated database URLs do not target their expected source databases" >&2; exit 2;
    }
    export PIXELS_TEST_CONSOLE_OWNER_URL="${PIXELS_TEST_CONSOLE_OWNER_URL%/pixels_console}/$console_database"
    export PIXELS_TEST_CONSOLE_RUNTIME_URL="${PIXELS_TEST_CONSOLE_RUNTIME_URL%/pixels_console}/$console_database"
    export PIXELS_TEST_DESK_OWNER_URL="${PIXELS_TEST_DESK_OWNER_URL%/pixels_desk}/$desk_database"
    export PIXELS_TEST_DESK_RUNTIME_URL="${PIXELS_TEST_DESK_RUNTIME_URL%/pixels_desk}/$desk_database"
fi
export PIXELS_TEST_CONSOLE_DATABASE_NAME="$console_database"

postgres_data_directory=$(docker exec -u postgres "$PIXELS_TEST_CONTAINER" psql -X -U pixels_admin -d postgres -Atc 'SHOW data_directory')
[[ "$postgres_data_directory" =~ ^/var/lib/postgresql/[A-Za-z0-9/._-]+$ ]] || {
    echo "isolated PostgreSQL data directory is unexpected" >&2; exit 1;
}
postgres_tls_root="$postgres_data_directory/p4-backup-root.crt"
postgres_tls_key="$postgres_data_directory/p4-backup-root.key"
postgres_tls_cert="$postgres_data_directory/p4-backup-server.crt"
postgres_tls_server_key="$postgres_data_directory/p4-backup-server.key"
postgres_tls_request="$postgres_data_directory/p4-backup-server.csr"
docker exec -u postgres "$PIXELS_TEST_CONTAINER" openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
    -subj '/CN=p4-backup-isolated-root' -addext 'basicConstraints=critical,CA:TRUE' \
    -addext 'keyUsage=critical,keyCertSign,cRLSign' -keyout "$postgres_tls_key" -out "$postgres_tls_root" >/dev/null 2>&1
docker exec -u postgres "$PIXELS_TEST_CONTAINER" openssl req -newkey rsa:2048 -nodes -sha256 \
    -subj '/CN=127.0.0.1' -addext 'subjectAltName=IP:127.0.0.1' \
    -addext 'basicConstraints=critical,CA:FALSE' -addext 'extendedKeyUsage=serverAuth' \
    -keyout "$postgres_tls_server_key" -out "$postgres_tls_request" >/dev/null 2>&1
docker exec -u postgres "$PIXELS_TEST_CONTAINER" openssl x509 -req -in "$postgres_tls_request" \
    -CA "$postgres_tls_root" -CAkey "$postgres_tls_key" -CAcreateserial \
    -out "$postgres_tls_cert" -days 1 -sha256 -copy_extensions copy >/dev/null 2>&1
docker exec -u postgres "$PIXELS_TEST_CONTAINER" chmod 0600 "$postgres_tls_server_key"
for postgres_setting in \
    "ALTER SYSTEM SET ssl_cert_file = '$postgres_tls_cert'" \
    "ALTER SYSTEM SET ssl_key_file = '$postgres_tls_server_key'" \
    'ALTER SYSTEM SET ssl = on'; do
    docker exec -u postgres "$PIXELS_TEST_CONTAINER" psql -X -v ON_ERROR_STOP=1 -U pixels_admin -d postgres -c "$postgres_setting" >/dev/null
done
docker restart "$PIXELS_TEST_CONTAINER" >/dev/null
for readiness_attempt in {1..40}; do
    if docker exec "$PIXELS_TEST_CONTAINER" pg_isready -U pixels_admin -d postgres >/dev/null 2>&1; then break; fi
    sleep 0.25
done
[[ $(docker exec -u postgres "$PIXELS_TEST_CONTAINER" psql -X -U pixels_admin -d postgres -Atc 'SHOW ssl') == on ]] || {
    echo "isolated PostgreSQL TLS did not become active" >&2; exit 1;
}

install -d -o root -g root -m 0711 /etc/pixels "$configuration_root"
install -d -o root -g root -m 0700 "$configuration_root/backup"
docker cp "$PIXELS_TEST_CONTAINER:$postgres_tls_root" "$configuration_root/backup/postgres-ca.crt" >/dev/null
chmod 0644 "$configuration_root/backup/postgres-ca.crt"
if [[ -n "$fixture_binary" ]]; then
    [[ -n "${PIXELS_TEST_CONSOLE_RUNTIME_URL:-}" && -n "${PIXELS_TEST_DESK_RUNTIME_URL:-}" ]] || {
        echo "integrated test database URLs are unavailable" >&2; exit 2;
    }
    if ! getent group pixels-console >/dev/null; then groupadd --system pixels-console; fi
    if ! getent passwd pixels-console >/dev/null; then
        useradd --system --gid pixels-console --home-dir /nonexistent --shell /usr/sbin/nologin pixels-console
    fi
    install -d -o root -g root -m 0700 "$configuration_root/console-secrets"
    install -d -o root -g root -m 0755 "$data_root"
    install -d -o root -g root -m 0700 "$data_root/console" "$data_root/console/cache"
    workspace_key_id=$(tr -d '\n' </proc/sys/kernel/random/uuid)
    PIXELS_CONSOLE_GUEST_SOURCE_KEY="$configuration_root/console-secrets/guest-source.key" \
    PIXELS_CONSOLE_WORKSPACE_KEY="$configuration_root/console-secrets/workspace.key" \
    PIXELS_CONSOLE_WORKSPACE_KEY_ID="$workspace_key_id" \
        "$candidate_directory/bin/px_console_admin" generate-secrets
    "$fixture_binary" "$deployment_id" "$configuration_root/console-secrets"
    install -o root -g root -m 0600 "$configuration_root/backup/postgres-ca.crt" \
        "$configuration_root/console-secrets/postgres-ca.crt"
    openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
        -subj '/CN=localhost' -addext 'subjectAltName=DNS:localhost' \
        -keyout "$configuration_root/console-secrets/console-tls.key" \
        -out "$configuration_root/console-secrets/console-tls.crt" >/dev/null 2>&1
    PIXELS_DEPLOYMENT_ID="$deployment_id" \
    PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY="$data_root/console/cache" \
        "$candidate_directory/bin/px_console_admin" initialize-recording-cache
    printf '%s' 'isolated-integrated-admin-password' >"$configuration_root/console-secrets/admin-password"
    chmod 0600 "$configuration_root/console-secrets/admin-password"
    PIXELS_DATABASE_URL="$PIXELS_TEST_CONSOLE_OWNER_URL" \
    PIXELS_CONSOLE_LOCAL_DEVELOPMENT=1 \
    PIXELS_CONSOLE_INITIAL_USERNAME=integrated-admin \
    PIXELS_CONSOLE_INITIAL_PASSWORD_FILE="$configuration_root/console-secrets/admin-password" \
        "$candidate_directory/bin/px_console_admin" bootstrap
    chown -R pixels-console:pixels-console "$configuration_root/console-secrets" "$data_root/console"
    console_runtime_url="$PIXELS_TEST_CONSOLE_RUNTIME_URL?sslrootcert=$configuration_root/console-secrets/postgres-ca.crt"
    PIXELS_PG_LOCAL_DEVELOPMENT=0 PIXELS_DEPLOYMENT_ID="$deployment_id" \
    PIXELS_DATABASE_URL="$console_runtime_url" \
        "$candidate_directory/bin/px_db" check console
    PIXELS_PG_LOCAL_DEVELOPMENT=1 PIXELS_DEPLOYMENT_ID="$deployment_id" \
    PIXELS_DATABASE_URL="$PIXELS_TEST_DESK_RUNTIME_URL" \
        "$candidate_directory/bin/px_db" check desk
    console_port=$(python3 -c 'import socket; listener=socket.socket(); listener.bind(("127.0.0.1", 0)); print(listener.getsockname()[1]); listener.close()')
    desk_port=$(python3 -c 'import socket; listener=socket.socket(); listener.bind(("127.0.0.1", 0)); print(listener.getsockname()[1]); listener.close()')
    relay_port=$(python3 -c 'import socket; listener=socket.socket(); listener.bind(("127.0.0.1", 0)); print(listener.getsockname()[1]); listener.close()')
    console_origin="https://localhost:$console_port"
    workspace_keys="[{\"id\":\"$workspace_key_id\",\"path\":\"$configuration_root/console-secrets/workspace.key\"}]"
    console_environment="$fixture_directory/console.env"
    printf '%s\n' \
        "PIXELS_DEPLOYMENT_ID=$deployment_id" \
        'PIXELS_CONSOLE_LOCAL_DEVELOPMENT=0' \
        "PIXELS_CONSOLE_DATABASE_URL=$console_runtime_url" \
        "PIXELS_CONSOLE_LISTEN=127.0.0.1:$console_port" \
        "PIXELS_CONSOLE_STATIC_DIRECTORY=$release_root/current-console/static/console" \
        "PIXELS_CONSOLE_TLS_CERT=$configuration_root/console-secrets/console-tls.crt" \
        "PIXELS_CONSOLE_TLS_KEY=$configuration_root/console-secrets/console-tls.key" \
        "PIXELS_CONSOLE_PUBLIC_ORIGIN=$console_origin" \
        'PIXELS_CONSOLE_REGISTRATION=1' 'PIXELS_CONSOLE_GUESTS=1' \
        'PIXELS_CONSOLE_SESSION_LIFETIME_SECONDS=3600' 'PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS=3600' \
        "PIXELS_CONSOLE_GUEST_SOURCE_KEY=$configuration_root/console-secrets/guest-source.key" \
        "PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY=$workspace_key_id" \
        "PIXELS_CONSOLE_WORKSPACE_KEYS='$workspace_keys'" \
        "PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY=$data_root/console/cache" \
        'PIXELS_CONSOLE_RECORDING_CACHE_BYTES=1073741824' 'PIXELS_CONSOLE_RECORDING_CACHE_DOWNLOADS=4' \
        'PIXELS_CONSOLE_RECORDING_CACHE_TTL_SECONDS=86400' \
        'PIXELS_CONSOLE_DISTRIBUTION=customer' 'PIXELS_CONSOLE_RELEASE_NAMESPACE=pixels.customer' \
        "PIXELS_CONSOLE_LICENSE_TRUST_STORE=$configuration_root/console-secrets/license-trust.json" \
        "PIXELS_CONSOLE_LICENSE_FILE=$configuration_root/console-secrets/console.license" >"$console_environment"
    chmod 0600 "$console_environment"
    /bin/sh "$candidate_directory/tools/install_linux_component.sh" console "$deployment_id" "$candidate_directory" "$console_environment"
    desk_environment="$fixture_directory/desk.env"
    desk_admin_digest=$(printf '%s' 'integrated-desk-test-token' | sha256sum | cut -d ' ' -f 1)
    printf '%s\n' "PIXELS_DEPLOYMENT_ID=$deployment_id" \
        "PIXELS_DATABASE_URL=$PIXELS_TEST_DESK_RUNTIME_URL" 'PIXELS_DESK_LOCAL_DEVELOPMENT=1' \
        "PIXELS_DESK_LISTEN=127.0.0.1:$desk_port" "PIXELS_DESK_ADMIN_TOKEN_SHA256=$desk_admin_digest" \
        "PIXELS_DESK_STATIC_DIRECTORY=$release_root/current-desk/static/desk" >"$desk_environment"
    chmod 0600 "$desk_environment"
    /bin/sh "$candidate_directory/tools/install_linux_component.sh" desk "$deployment_id" "$candidate_directory" "$desk_environment"
    relay_environment="$fixture_directory/relay.env"
    printf '%s\n' "PIXELS_DEPLOYMENT_ID=$deployment_id" "PIXELS_RELAY_LISTEN=127.0.0.1:$relay_port" \
        'PIXELS_RELAY_APP_KEY=integration-relay-app-key' \
        'PIXELS_RELAY_CONTROL_KEY=integration-relay-control-key-32' \
        "PIXELS_RELAY_CONSOLE_CONTROL_URL=wss://localhost:$console_port/api/console/relay-control" \
        "PIXELS_RELAY_NODE_TOKEN=$(printf '%064d' 0)" 'PIXELS_RELAY_MAX_ROOMS=1024' >"$relay_environment"
    chmod 0600 "$relay_environment"
    /bin/sh "$candidate_directory/tools/install_linux_component.sh" relay "$deployment_id" "$candidate_directory" "$relay_environment"
    for component in "${integrated_components[@]}"; do
        systemctl is-active --quiet "pixels-private-$component@$deployment_id.service"
    done
    console_curl=(curl --silent --noproxy '*' --resolve "localhost:$console_port:127.0.0.1" \
        --cacert "$configuration_root/console-secrets/console-tls.crt")
    for readiness_attempt in {1..30}; do
        [[ $("${console_curl[@]}" --output /dev/null --write-out '%{http_code}' "$console_origin/health/ready") == 204 ]] && break
        sleep 0.2
    done
    [[ $("${console_curl[@]}" --output /dev/null --write-out '%{http_code}' "$console_origin/health/ready") == 204 ]] || {
        echo "integrated Console is not ready" >&2; exit 1;
    }
    [[ $(curl --silent --output /dev/null --write-out '%{http_code}' "http://127.0.0.1:$desk_port/health/ready") == 204 ]] || {
        echo "integrated Desk is not ready" >&2; exit 1;
    }
    [[ $("${console_curl[@]}" --fail "$console_origin/") == *'<div id="app"></div>'* ]] || exit 1
    [[ $(curl --fail --silent "http://127.0.0.1:$desk_port/") == *'<div id="app"></div>'* ]] || exit 1
    login_response=$("${console_curl[@]}" --fail -H 'Content-Type: application/json' \
        -H 'x-pixels-client-type: admin_web' -H "Origin: $console_origin" \
        --data-binary '{"username":"integrated-admin","password":"isolated-integrated-admin-password"}' \
        "$console_origin/api/console/sessions")
    printf '%s' "$login_response" | python3 -c 'import json,sys; response=json.load(sys.stdin); assert response["client_type"] == "admin_web" and len(response["token"]) == 64'
    /bin/sh "$candidate_directory/tools/install_linux_component.sh" console "$deployment_id" "$candidate_directory" "$console_environment"
    invalid_environment="$fixture_directory/invalid-console.env"
    sed "s|^PIXELS_CONSOLE_LICENSE_FILE=.*|PIXELS_CONSOLE_LICENSE_FILE=$configuration_root/console-secrets/missing.license|" \
        "$console_environment" >"$invalid_environment"
    chmod 0600 "$invalid_environment"
    if /bin/sh "$candidate_directory/tools/install_linux_component.sh" console "$deployment_id" "$candidate_directory" "$invalid_environment" >/dev/null 2>&1; then
        echo "integrated Console accepted an invalid cover installation" >&2; exit 1;
    fi
    systemctl is-active --quiet "pixels-private-console@$deployment_id.service"
    console_ready=0
    for readiness_attempt in {1..30}; do
        if [[ $("${console_curl[@]}" --output /dev/null --write-out '%{http_code}' "$console_origin/health/ready") == 204 ]]; then
            console_ready=1
            break
        fi
        sleep 0.2
    done
    [[ "$console_ready" == 1 ]] || { echo "integrated Console was not ready after cover rollback" >&2; exit 1; }
fi
python3 - "$configuration_root/backup/console.pgpass" <<'PY'
import os
import sys
from urllib.parse import urlsplit

credential_path = sys.argv[1]
owner_connection = urlsplit(os.environ["PIXELS_TEST_CONSOLE_OWNER_URL"])
if (owner_connection.scheme != "postgresql" or owner_connection.path != "/" + os.environ["PIXELS_TEST_CONSOLE_DATABASE_NAME"]
        or owner_connection.hostname != "127.0.0.1" or not owner_connection.port
        or owner_connection.username != "pixels_console_owner" or not owner_connection.password):
    raise SystemExit("isolated Console owner URL is invalid")
credential_descriptor = os.open(credential_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
with os.fdopen(credential_descriptor, "w", encoding="utf-8") as credential_file:
    credential_file.write(f"127.0.0.1:{owner_connection.port}:*:pixels_console_owner:{owner_connection.password}\n")
PY

docker exec "$PIXELS_TEST_CONTAINER" psql -X -v ON_ERROR_STOP=1 -U pixels_admin -d "$console_database" -c \
    "CREATE TABLE pixels.p4_systemd_probe(probe_id integer PRIMARY KEY, payload text NOT NULL); \
    ALTER TABLE pixels.p4_systemd_probe OWNER TO pixels_console_owner; INSERT INTO pixels.p4_systemd_probe VALUES (1, 'before');" >/dev/null

anchor_unix=$(date +%s)
python3 - "$source_configuration" "$configuration_root" "$data_root" "$deployment_id" "$anchor_unix" \
    "$candidate_directory/postgresql/18/sha256.json" <<'PY'
import json
import os
import sys
from urllib.parse import urlsplit

configuration_path, configuration_root, data_root, deployment_id, anchor_unix, toolchain_manifest_path = sys.argv[1:]
owner_connection = urlsplit(os.environ["PIXELS_TEST_CONSOLE_OWNER_URL"])
with open(toolchain_manifest_path, encoding="utf-8") as manifest_file:
    toolchain_manifest = json.load(manifest_file)
configuration = {
    "schema_version": 2,
    "deployment_id": deployment_id,
    "repository_root": f"{data_root}/backup/repository",
    "offsite_repository_root": None,
    "scheduler_root": f"{data_root}/backup/scheduler",
    "status_root": f"{data_root}/backup/status",
    "pg_dump_path": "/opt/pixels/postgresql/18/bin/pg_dump",
    "pg_dump_sha256": toolchain_manifest["artifacts"]["bin/pg_dump"],
    "pg_restore_path": "/opt/pixels/postgresql/18/bin/pg_restore",
    "pg_restore_sha256": toolchain_manifest["artifacts"]["bin/pg_restore"],
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
            {"state": "required", "database": {
                "service": "console", "host": "127.0.0.1", "port": owner_connection.port,
                "database": os.environ["PIXELS_TEST_CONSOLE_DATABASE_NAME"], "username": "pixels_console_owner",
                "password_file": f"{configuration_root}/backup/console.pgpass", "schema_version": 1,
            }},
            {"state": "not_applicable", "service": "auth", "reason": "not installed in isolated test"},
            {"state": "not_applicable", "service": "desk", "reason": "not installed in isolated test"},
        ],
    },
}
with open(configuration_path, "w", encoding="utf-8") as configuration_file:
    json.dump(configuration, configuration_file)
PY
chmod 0600 "$source_configuration"
/bin/sh "$candidate_directory/tools/install_linux_component.sh" backup "$deployment_id" "$candidate_directory" "$source_configuration"
systemctl is-active --quiet "$service_name"
recovery_set_id=
for status_attempt in {1..120}; do
    status_path="$data_root/backup/status/status.json"
    if [[ -f "$status_path" ]]; then
        recovery_set_id=$(python3 - "$status_path" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as status_file:
    status = json.load(status_file)
print(status.get("last_recovery_set_id") or "")
PY
        )
        [[ -z "$recovery_set_id" ]] || break
    fi
    sleep 0.25
done
[[ "$recovery_set_id" =~ ^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$ ]] || {
    journalctl --no-pager --unit "$service_name" --lines 30 >&2
    echo "systemd Backup did not publish a recovery set" >&2; exit 1;
}
systemctl stop "$service_name"
archive_path="$data_root/backup/repository/$recovery_set_id/console.dump"
python3 - "$data_root/backup/repository/$recovery_set_id/manifest.json" "$archive_path" "$deployment_id" <<'PY'
import hashlib
import json
import sys
from pathlib import Path

manifest_path, archive_path = (Path(path_text) for path_text in sys.argv[1:3])
with manifest_path.open(encoding="utf-8") as manifest_file:
    manifest = json.load(manifest_file)
console_member = next(member["member"] for member in manifest["members"] if member["service"] == "console")
if (manifest["status"] != "verified" or manifest["deployment_id"] != sys.argv[3]
        or console_member["archive_sha256"] != hashlib.sha256(archive_path.read_bytes()).hexdigest()):
    raise SystemExit("systemd Backup archive or manifest is invalid")
PY
database_port=$(python3 - "$source_configuration" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as configuration_file:
    print(json.load(configuration_file)["plan"]["targets"][0]["database"]["port"])
PY
)
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
    -subj '/CN=unrelated-isolated-root' -keyout "$fixture_directory/wrong-root.key" \
    -out "$configuration_root/backup/wrong-root.crt" >/dev/null 2>&1
chmod 0644 "$configuration_root/backup/wrong-root.crt"
if runuser -u pixels-backup -- env LD_LIBRARY_PATH=/opt/pixels/postgresql/18/lib PGSSLMODE=verify-full \
    PGSSLROOTCERT="$configuration_root/backup/wrong-root.crt" PGPASSFILE="$configuration_root/backup/console.pgpass" \
    /opt/pixels/postgresql/18/bin/pg_dump --schema-only --no-password \
    --host 127.0.0.1 --port "$database_port" --username pixels_console_owner \
    --dbname "$console_database" --file "$data_root/backup/status/wrong-root.dump" 2>"$fixture_directory/wrong-root.stderr"; then
    echo "PostgreSQL backup accepted an unrelated TLS root" >&2
    exit 1
fi
if ! grep -Eiq 'certificate|SSL' "$fixture_directory/wrong-root.stderr"; then
    echo "wrong-root rejection did not exercise PostgreSQL TLS validation" >&2
    exit 1
fi
docker exec "$PIXELS_TEST_CONTAINER" psql -X -v ON_ERROR_STOP=1 -U pixels_admin -d "$console_database" -c \
    "UPDATE pixels.p4_systemd_probe SET payload='after' WHERE probe_id=1;" >/dev/null
docker exec "$PIXELS_TEST_CONTAINER" createdb -U pixels_admin -O pixels_console_owner "$restore_database"
runuser -u pixels-backup -- env LD_LIBRARY_PATH=/opt/pixels/postgresql/18/lib PGSSLMODE=verify-full \
    PGSSLROOTCERT="$configuration_root/backup/postgres-ca.crt" PGPASSFILE="$configuration_root/backup/console.pgpass" \
    /opt/pixels/postgresql/18/bin/pg_restore --exit-on-error --no-owner \
    --host 127.0.0.1 --port "$database_port" --username pixels_console_owner \
    --dbname "$restore_database" "$archive_path" >/dev/null
restored_payload=$(docker exec "$PIXELS_TEST_CONTAINER" psql -X -At -U pixels_admin -d "$restore_database" -c \
    'SELECT payload FROM pixels.p4_systemd_probe WHERE probe_id=1')
source_payload=$(docker exec "$PIXELS_TEST_CONTAINER" psql -X -At -U pixels_admin -d "$console_database" -c \
    'SELECT payload FROM pixels.p4_systemd_probe WHERE probe_id=1')
[[ "$restored_payload" == before && "$source_payload" == after ]] || {
    echo "systemd Backup restore did not reproduce pre-change data" >&2; exit 1;
}
if [[ -n "$fixture_binary" ]]; then
    offline_probe_host=$(python3 -c 'import json,subprocess; interfaces=json.loads(subprocess.check_output(["ip","-j","-4","addr","show"])); print(next(address["local"] for interface in interfaces if interface["ifname"] != "lo" for address in interface["addr_info"] if address["scope"] == "global"))')
    offline_probe_port=$(python3 -c 'import socket; listener=socket.socket(); listener.bind(("0.0.0.0", 0)); print(listener.getsockname()[1]); listener.close()')
    python3 -m http.server "$offline_probe_port" --bind "$offline_probe_host" >/dev/null 2>&1 &
    offline_probe_pid=$!
    probe_connection='import socket,sys; connection=socket.create_connection((sys.argv[1],int(sys.argv[2])),timeout=2); connection.close()'
    offline_probe_ready=0
    for readiness_attempt in {1..20}; do
        if python3 -c "$probe_connection" "$offline_probe_host" "$offline_probe_port" >/dev/null 2>&1; then
            offline_probe_ready=1
            break
        fi
        sleep 0.1
    done
    [[ "$offline_probe_ready" == 1 ]] || { echo "isolated non-loopback probe did not start" >&2; exit 1; }
    if ! systemd-run --wait --collect --unit="pixels-network-allowed-$deployment_id" \
        /usr/bin/python3 -c "$probe_connection" "$offline_probe_host" "$offline_probe_port" >/dev/null 2>&1; then
        echo "unrestricted control could not reach the isolated non-loopback listener" >&2; exit 1;
    fi
    if systemd-run --wait --collect --unit="pixels-network-denied-$deployment_id" \
        --property=IPAddressDeny=any --property=IPAddressAllow=localhost \
        /usr/bin/python3 -c "$probe_connection" "$offline_probe_host" "$offline_probe_port" >/dev/null 2>&1; then
        echo "loopback-only network policy did not deny non-loopback access" >&2; exit 1;
    fi
    kill "$offline_probe_pid" >/dev/null 2>&1
    wait "$offline_probe_pid" >/dev/null 2>&1 || true
    offline_probe_pid=
    for component in console relay desk backup; do
        offline_dropin="/etc/systemd/system/pixels-private-$component@$deployment_id.service.d"
        install -d -o root -g root -m 0755 "$offline_dropin"
        printf '%s\n' '[Service]' 'IPAddressDeny=any' 'IPAddressAllow=localhost' >"$offline_dropin/offline-test.conf"
        chmod 0644 "$offline_dropin/offline-test.conf"
    done
    systemctl daemon-reload
    for component in console relay desk backup; do
        offline_service="pixels-private-$component@$deployment_id.service"
        systemctl restart "$offline_service"
        systemctl is-active --quiet "$offline_service"
        [[ $(systemctl show "$offline_service" --property=IPAddressDeny --value) == *'0.0.0.0/0'* ]] || {
            echo "$component did not apply offline network denial" >&2; exit 1;
        }
        [[ $(systemctl show "$offline_service" --property=IPAddressAllow --value) == *'127.0.0.0/8'* ]] || {
            echo "$component did not retain loopback database access" >&2; exit 1;
        }
    done
    offline_console_ready=0
    for readiness_attempt in {1..30}; do
        if [[ $("${console_curl[@]}" --output /dev/null --write-out '%{http_code}' "$console_origin/health/ready") == 204 ]]; then
            offline_console_ready=1
            break
        fi
        sleep 0.2
    done
    [[ "$offline_console_ready" == 1 ]] || { echo "offline Console did not become ready" >&2; exit 1; }
    [[ $(curl --silent --output /dev/null --write-out '%{http_code}' "http://127.0.0.1:$desk_port/health/ready") == 204 ]] || {
        echo "offline Desk did not become ready" >&2; exit 1;
    }
    offline_login_response=$("${console_curl[@]}" --fail -H 'Content-Type: application/json' \
        -H 'x-pixels-client-type: admin_web' -H "Origin: $console_origin" \
        --data-binary '{"username":"integrated-admin","password":"isolated-integrated-admin-password"}' \
        "$console_origin/api/console/sessions")
    printf '%s' "$offline_login_response" | python3 -c 'import json,sys; response=json.load(sys.stdin); assert response["client_type"] == "admin_web" and len(response["token"]) == 64'
fi
/bin/sh "$candidate_directory/tools/uninstall_linux_component.sh" backup "$deployment_id"
if [[ -n "$fixture_binary" ]]; then
    for component in "${integrated_components[@]}"; do
        systemctl is-active --quiet "pixels-private-$component@$deployment_id.service"
    done
    echo "PASS PRIVATE FULL SYSTEMD: four services shared one deployment; login, cover rollback, verified restore and loopback-only offline operation"
else
    echo "PASS PRIVATE BACKUP NATIVE SYSTEMD: verify-full PG18, scheduled archive, hash verification and pre-change restore"
fi
