#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 || $(id -u) -ne 0 || $(ps -p 1 -o comm=) != systemd
      || "${PIXELS_PG_ISOLATED_TEST:-}" != 1 || -z "${PIXELS_TEST_CONSOLE_RUNTIME_URL:-}"
      || -z "${PIXELS_TEST_CONSOLE_OWNER_URL:-}" ]]; then
    echo "usage (as root with isolated PostgreSQL): private_console_systemd.sh <candidate-directory> <test-fixture-binary> <postgres-tls-root>" >&2
    exit 2
fi
candidate_directory=$1
fixture_binary=$2
postgres_tls_root=$3
[[ "$candidate_directory" = /* && "$fixture_binary" = /* && "$postgres_tls_root" = /* ]] || {
    echo "paths must be absolute" >&2; exit 2;
}
python3 "$candidate_directory/tools/verify_candidate.py" "$candidate_directory"
[[ -x "$fixture_binary" ]] || { echo "isolated license fixture is unavailable" >&2; exit 2; }
[[ -f "$postgres_tls_root" && ! -L "$postgres_tls_root" ]] || { echo "isolated PostgreSQL TLS root is unavailable" >&2; exit 2; }
[[ "$PIXELS_TEST_CONSOLE_RUNTIME_URL" != *\?* ]] || { echo "isolated runtime DSN must not contain query parameters" >&2; exit 2; }

deployment_id=$PIXELS_DEPLOYMENT_ID
[[ "$deployment_id" =~ ^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$ ]] || exit 2
configuration_root="/etc/pixels/$deployment_id"
secret_directory="$configuration_root/console-secrets"
data_root="/var/lib/pixels/$deployment_id"
cache_directory="$data_root/console/cache"
release_root="/opt/pixels/private/$deployment_id"
service_name="pixels-private-console@$deployment_id.service"
unit_destination=/etc/systemd/system/pixels-private-console@.service
fixture_directory=$(mktemp -d)
source_environment="$fixture_directory/console.env"
original_unit="$fixture_directory/original-console-unit"
for private_root in "$configuration_root" "$data_root" "$release_root"; do
    [[ ! -e "$private_root" ]] || { echo "test deployment path already exists" >&2; exit 2; }
done
if [[ -f "$unit_destination" ]]; then cp -p "$unit_destination" "$original_unit"; fi

cleanup() {
    cleanup_status=$?
    set +e
    systemctl disable --now "$service_name" >/dev/null 2>&1
    systemctl reset-failed "$service_name" >/dev/null 2>&1
    if [[ -f "$original_unit" ]]; then cp -p "$original_unit" "$unit_destination"; else rm -f -- "$unit_destination"; fi
    systemctl daemon-reload
    for private_root in "$configuration_root" "$data_root" "$release_root"; do
        if [[ -d "$private_root" && ! -L "$private_root" ]]; then
            resolved_root=$(realpath -e -- "$private_root")
            case "$resolved_root" in
                "/etc/pixels/$deployment_id"|"/var/lib/pixels/$deployment_id"|"/opt/pixels/private/$deployment_id")
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

if ! getent group pixels-console >/dev/null; then groupadd --system pixels-console; fi
if ! getent passwd pixels-console >/dev/null; then
    useradd --system --gid pixels-console --home-dir /nonexistent --shell /usr/sbin/nologin pixels-console
fi
install -d -o root -g root -m 0711 /etc/pixels "$configuration_root"
install -d -o root -g root -m 0700 "$secret_directory"
install -d -o root -g root -m 0755 /var/lib/pixels "$data_root"
install -d -o root -g root -m 0700 "$data_root/console" "$cache_directory"

workspace_key_id=$(tr -d '\n' </proc/sys/kernel/random/uuid)
PIXELS_CONSOLE_GUEST_SOURCE_KEY="$secret_directory/guest-source.key" \
PIXELS_CONSOLE_WORKSPACE_KEY="$secret_directory/workspace.key" \
PIXELS_CONSOLE_WORKSPACE_KEY_ID="$workspace_key_id" \
    "$candidate_directory/bin/px_console_admin" generate-secrets
"$fixture_binary" "$deployment_id" "$secret_directory"
install -o root -g root -m 0600 "$postgres_tls_root" "$secret_directory/postgres-ca.crt"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
    -subj '/CN=localhost' -addext 'subjectAltName=DNS:localhost' \
    -keyout "$secret_directory/console-tls.key" -out "$secret_directory/console-tls.crt" >/dev/null 2>&1
PIXELS_DEPLOYMENT_ID="$deployment_id" \
PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY="$cache_directory" \
    "$candidate_directory/bin/px_console_admin" initialize-recording-cache
printf '%s' 'isolated-console-systemd-password' >"$secret_directory/admin-password"
chmod 0600 "$secret_directory/admin-password"
PIXELS_DATABASE_URL="$PIXELS_TEST_CONSOLE_OWNER_URL" \
PIXELS_CONSOLE_LOCAL_DEVELOPMENT=1 \
PIXELS_CONSOLE_INITIAL_USERNAME=systemd-admin \
PIXELS_CONSOLE_INITIAL_PASSWORD_FILE="$secret_directory/admin-password" \
    "$candidate_directory/bin/px_console_admin" bootstrap
chown -R pixels-console:pixels-console "$secret_directory" "$data_root/console"
runuser -u pixels-console -- env \
    PIXELS_DEPLOYMENT_ID="$deployment_id" \
    PIXELS_CONSOLE_LICENSE_TRUST_STORE="$secret_directory/license-trust.json" \
    PIXELS_CONSOLE_LICENSE_FILE="$secret_directory/console.license" \
    "$candidate_directory/bin/px_console_admin" validate-license
verified_database_url="$PIXELS_TEST_CONSOLE_RUNTIME_URL?sslrootcert=$secret_directory/postgres-ca.crt"
runuser -u pixels-console -- env \
    PIXELS_PG_LOCAL_DEVELOPMENT=0 \
    PIXELS_DEPLOYMENT_ID="$deployment_id" \
    PIXELS_DATABASE_URL="$verified_database_url" \
    "$candidate_directory/bin/px_db" check console
if runuser -u pixels-console -- env \
    PIXELS_PG_LOCAL_DEVELOPMENT=0 \
    PIXELS_DEPLOYMENT_ID="$deployment_id" \
    PIXELS_DATABASE_URL="$PIXELS_TEST_CONSOLE_RUNTIME_URL?sslrootcert=$secret_directory/console-tls.crt" \
    "$candidate_directory/bin/px_db" check console >/dev/null 2>&1; then
    echo "Console database accepted an unrelated TLS root" >&2
    exit 1
fi

console_port=$(python3 -c 'import socket; listener=socket.socket(); listener.bind(("127.0.0.1", 0)); print(listener.getsockname()[1]); listener.close()')
console_origin="https://localhost:$console_port"
console_curl=(curl --silent --noproxy '*' --resolve "localhost:$console_port:127.0.0.1" --cacert "$secret_directory/console-tls.crt")
wait_for_console_ready() {
    local attempt_number
    for attempt_number in {1..30}; do
        if [[ $("${console_curl[@]}" --output /dev/null --write-out '%{http_code}' "$console_origin/health/ready") == 204 ]]; then
            return 0
        fi
        sleep 0.2
    done
    echo "installed Console did not become ready" >&2
    return 1
}
workspace_keys="[{\"id\":\"$workspace_key_id\",\"path\":\"$secret_directory/workspace.key\"}]"
printf '%s\n' \
    "PIXELS_DEPLOYMENT_ID=$deployment_id" \
    'PIXELS_CONSOLE_LOCAL_DEVELOPMENT=0' \
    "PIXELS_CONSOLE_DATABASE_URL=$verified_database_url" \
    "PIXELS_CONSOLE_LISTEN=127.0.0.1:$console_port" \
    "PIXELS_CONSOLE_STATIC_DIRECTORY=/opt/pixels/private/$deployment_id/current-console/static/console" \
    "PIXELS_CONSOLE_TLS_CERT=$secret_directory/console-tls.crt" \
    "PIXELS_CONSOLE_TLS_KEY=$secret_directory/console-tls.key" \
    "PIXELS_CONSOLE_PUBLIC_ORIGIN=$console_origin" \
    'PIXELS_CONSOLE_REGISTRATION=1' \
    'PIXELS_CONSOLE_GUESTS=1' \
    'PIXELS_CONSOLE_SESSION_LIFETIME_SECONDS=3600' \
    'PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS=3600' \
    "PIXELS_CONSOLE_GUEST_SOURCE_KEY=$secret_directory/guest-source.key" \
    "PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY=$workspace_key_id" \
    "PIXELS_CONSOLE_WORKSPACE_KEYS='$workspace_keys'" \
    "PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY=$cache_directory" \
    'PIXELS_CONSOLE_RECORDING_CACHE_BYTES=1073741824' \
    'PIXELS_CONSOLE_RECORDING_CACHE_DOWNLOADS=4' \
    'PIXELS_CONSOLE_RECORDING_CACHE_TTL_SECONDS=86400' \
    'PIXELS_CONSOLE_DISTRIBUTION=customer' \
    'PIXELS_CONSOLE_RELEASE_NAMESPACE=pixels.customer' \
    "PIXELS_CONSOLE_LICENSE_TRUST_STORE=$secret_directory/license-trust.json" \
    "PIXELS_CONSOLE_LICENSE_FILE=$secret_directory/console.license" >"$source_environment"
chmod 0600 "$source_environment"

/bin/sh "$candidate_directory/tools/install_linux_component.sh" console "$deployment_id" "$candidate_directory" "$source_environment"
systemctl is-active --quiet "$service_name"
main_pid=$(systemctl show "$service_name" --property=MainPID --value)
[[ $(ps -o user= -p "$main_pid" | xargs) == pixels-console ]] || { echo "Console did not run as its dedicated identity" >&2; exit 1; }
[[ $(stat -c '%U:%G:%a' "$configuration_root/private-console.env") == pixels-console:pixels-console:400 ]] || {
    echo "installed Console environment permissions are invalid" >&2; exit 1;
}
wait_for_console_ready
index_response=$("${console_curl[@]}" --fail "$console_origin/")
[[ "$index_response" == *'<div id="app"></div>'* ]] || { echo "installed Console did not serve its packaged page" >&2; exit 1; }
systemctl restart "$service_name"
wait_for_console_ready

wrong_license_directory="$fixture_directory/wrong-license"
install -d -m 0700 "$wrong_license_directory"
"$fixture_binary" "$(cat /proc/sys/kernel/random/uuid)" "$wrong_license_directory"
license_before=$(sha256sum "$secret_directory/console.license" | cut -d ' ' -f 1)
pid_before=$(systemctl show "$service_name" --property=MainPID --value)
if /bin/sh "$candidate_directory/tools/renew_console_license.sh" "$deployment_id" \
    "$wrong_license_directory/license-trust.json" "$wrong_license_directory/console.license"; then
    echo "Console accepted a replacement license for another deployment" >&2
    exit 1
fi
[[ $(sha256sum "$secret_directory/console.license" | cut -d ' ' -f 1) == "$license_before" ]] || {
    echo "rejected license changed the installed file" >&2; exit 1;
}
[[ $(systemctl show "$service_name" --property=MainPID --value) == "$pid_before" ]] || {
    echo "rejected license restarted Console" >&2; exit 1;
}

replacement_directory="$fixture_directory/replacement-license"
install -d -m 0700 "$replacement_directory"
"$fixture_binary" "$deployment_id" "$replacement_directory"
/bin/sh "$candidate_directory/tools/renew_console_license.sh" "$deployment_id" \
    "$replacement_directory/license-trust.json" "$replacement_directory/console.license"
[[ $(sha256sum "$secret_directory/console.license" | cut -d ' ' -f 1) != "$license_before" ]] || {
    echo "Console license replacement did not publish new bytes" >&2; exit 1;
}
wait_for_console_ready

invalid_environment="$fixture_directory/invalid-console.env"
sed "s|^PIXELS_CONSOLE_LICENSE_FILE=.*|PIXELS_CONSOLE_LICENSE_FILE=$secret_directory/missing.license|" \
    "$source_environment" >"$invalid_environment"
chmod 0600 "$invalid_environment"
if /bin/sh "$candidate_directory/tools/install_linux_component.sh" console "$deployment_id" "$candidate_directory" "$invalid_environment"; then
    echo "Console accepted a missing license during cover install" >&2
    exit 1
fi
[[ $(sha256sum "$configuration_root/private-console.env" | cut -d ' ' -f 1) == \
    $(sha256sum "$source_environment" | cut -d ' ' -f 1) ]] || {
    echo "Console did not restore its previous private environment" >&2; exit 1;
}
systemctl is-active --quiet "$service_name"
wait_for_console_ready

/bin/sh "$candidate_directory/tools/uninstall_linux_component.sh" console "$deployment_id"
[[ $(systemctl is-active "$service_name" 2>/dev/null || true) != active ]] || { echo "Console remained active" >&2; exit 1; }
[[ -f "$configuration_root/private-console.env" && -d "$release_root/releases" && -d "$cache_directory" ]] || {
    echo "Console unregister removed preserved state" >&2; exit 1;
}
echo "PASS PRIVATE CONSOLE SYSTEMD: isolated PXLIC2, verify-full PG, rejected wrong-deployment renewal, successful license replacement, HTTPS, restart, rollback and unregister"
