#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || $(id -u) -ne 0 || $(ps -p 1 -o comm=) != systemd
      || "${PIXELS_PG_ISOLATED_TEST:-}" != 1 || -z "${PIXELS_TEST_DESK_RUNTIME_URL:-}" ]]; then
    echo "usage (as root with isolated PostgreSQL): private_desk_systemd.sh <absolute-candidate-directory>" >&2
    exit 2
fi
candidate_directory=$1
[[ "$candidate_directory" = /* ]] || { echo "candidate path must be absolute" >&2; exit 2; }
python3 "$candidate_directory/tools/verify_candidate.py" "$candidate_directory"

deployment_id=$PIXELS_DEPLOYMENT_ID
[[ "$deployment_id" =~ ^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$ ]] || exit 2
configuration_root="/etc/pixels/$deployment_id"
data_root="/var/lib/pixels/$deployment_id"
release_root="/opt/pixels/private/$deployment_id"
service_name="pixels-private-desk@$deployment_id.service"
unit_destination=/etc/systemd/system/pixels-private-desk@.service
fixture_directory=$(mktemp -d)
source_environment="$fixture_directory/desk.env"
original_unit="$fixture_directory/original-desk-unit"
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

desk_port=$(python3 -c 'import socket; listener=socket.socket(); listener.bind(("127.0.0.1", 0)); print(listener.getsockname()[1]); listener.close()')
admin_digest=$(printf '%s' 'private-desk-systemd-test-token' | sha256sum | cut -d ' ' -f 1)
printf '%s\n' \
    "PIXELS_DEPLOYMENT_ID=$deployment_id" \
    "PIXELS_DATABASE_URL=$PIXELS_TEST_DESK_RUNTIME_URL" \
    'PIXELS_DESK_LOCAL_DEVELOPMENT=1' \
    "PIXELS_DESK_LISTEN=127.0.0.1:$desk_port" \
    "PIXELS_DESK_ADMIN_TOKEN_SHA256=$admin_digest" \
    "PIXELS_DESK_STATIC_DIRECTORY=/opt/pixels/private/$deployment_id/current-desk/static/desk" >"$source_environment"
chmod 0600 "$source_environment"

/bin/sh "$candidate_directory/tools/install_linux_component.sh" desk "$deployment_id" "$candidate_directory" "$source_environment"
systemctl is-active --quiet "$service_name"
main_pid=$(systemctl show "$service_name" --property=MainPID --value)
[[ $(ps -o user= -p "$main_pid" | xargs) == pixels-desk ]] || { echo "Desk did not run as its dedicated identity" >&2; exit 1; }
[[ $(stat -c '%U:%G:%a' "$configuration_root/private-desk.env") == pixels-desk:pixels-desk:400 ]] || {
    echo "installed Desk environment permissions are invalid" >&2; exit 1;
}
[[ $(curl --silent --output /dev/null --write-out '%{http_code}' "http://127.0.0.1:$desk_port/health/ready") == 204 ]] || {
    echo "installed Desk did not become ready" >&2; exit 1;
}
index_response=$(curl --fail --silent "http://127.0.0.1:$desk_port/")
[[ "$index_response" == *'<div id="app"></div>'* ]] || { echo "installed Desk did not serve its packaged portal" >&2; exit 1; }

/bin/sh "$candidate_directory/tools/uninstall_linux_component.sh" desk "$deployment_id"
[[ $(systemctl is-active "$service_name" 2>/dev/null || true) != active ]] || { echo "Desk remained active" >&2; exit 1; }
[[ -f "$configuration_root/private-desk.env" && -d "$release_root/releases" ]] || {
    echo "Desk unregister removed preserved state" >&2; exit 1;
}
echo "PASS PRIVATE DESK SYSTEMD: isolated PG readiness, packaged portal, dedicated identity and unregister"
