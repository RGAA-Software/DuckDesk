#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || $(id -u) -ne 0 || $(ps -p 1 -o comm=) != systemd ]]; then
    echo "usage (as root under systemd): private_server_systemd.sh <absolute-candidate-directory>" >&2
    exit 2
fi
candidate_directory=$1
[[ "$candidate_directory" = /* ]] || { echo "candidate path must be absolute" >&2; exit 2; }
python3 "$candidate_directory/tools/verify_candidate.py" "$candidate_directory"

deployment_id=$(tr -d '\n' </proc/sys/kernel/random/uuid)
configuration_root="/etc/pixels/$deployment_id"
data_root="/var/lib/pixels/$deployment_id"
release_root="/opt/pixels/private/$deployment_id"
service_name="pixels-private-relay@$deployment_id.service"
unit_destination=/etc/systemd/system/pixels-private-relay@.service
fixture_directory=$(mktemp -d)
source_environment="$fixture_directory/relay.env"
original_unit="$fixture_directory/original-relay-unit"
for private_root in "$configuration_root" "$data_root" "$release_root"; do
    [[ ! -e "$private_root" ]] || { echo "test deployment path already exists" >&2; exit 2; }
done
if [[ -f "$unit_destination" ]]; then cp -p "$unit_destination" "$original_unit"; fi

cleanup() {
    cleanup_status=$?
    set +e
    systemctl disable --now "$service_name" >/dev/null 2>&1
    systemctl reset-failed "$service_name" >/dev/null 2>&1
    if [[ -f "$original_unit" ]]; then
        cp -p "$original_unit" "$unit_destination"
    else
        rm -f -- "$unit_destination"
    fi
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

relay_token=$(printf '%064d' 0)
write_environment() {
    relay_listen=$1
    relay_rooms=$2
    printf '%s\n' \
        "PIXELS_DEPLOYMENT_ID=$deployment_id" \
        "PIXELS_RELAY_LISTEN=$relay_listen" \
        'PIXELS_RELAY_APP_KEY=integration-relay-app-key' \
        'PIXELS_RELAY_CONTROL_KEY=integration-relay-control-key-32' \
        'PIXELS_RELAY_CONSOLE_CONTROL_URL=ws://127.0.0.1:9/api/console/relay-control' \
        "PIXELS_RELAY_NODE_TOKEN=$relay_token" \
        "PIXELS_RELAY_MAX_ROOMS=$relay_rooms" >"$source_environment"
}

write_environment '127.0.0.1:0' 1024
chmod 0644 "$source_environment"
if /bin/sh "$candidate_directory/tools/install_linux_component.sh" relay "$deployment_id" "$candidate_directory" "$source_environment" >/dev/null 2>&1; then
    echo "installer accepted an over-permissive environment" >&2
    exit 1
fi
[[ ! -e "$release_root" ]] || { echo "rejected preflight changed installation state" >&2; exit 1; }

chmod 0600 "$source_environment"
/bin/sh "$candidate_directory/tools/install_linux_component.sh" relay "$deployment_id" "$candidate_directory" "$source_environment"
systemctl is-active --quiet "$service_name"
main_pid=$(systemctl show "$service_name" --property=MainPID --value)
[[ $(ps -o user= -p "$main_pid" | xargs) == pixels-relay ]] || { echo "Relay did not run as its dedicated identity" >&2; exit 1; }
[[ $(stat -c '%U:%G:%a' "$configuration_root/private-relay.env") == pixels-relay:pixels-relay:400 ]] || {
    echo "installed Relay environment permissions are invalid" >&2; exit 1;
}
original_release=$(readlink "$release_root/current-relay")

write_environment '127.0.0.1:0' 2048
/bin/sh "$candidate_directory/tools/install_linux_component.sh" relay "$deployment_id" "$candidate_directory" "$source_environment"
systemctl is-active --quiet "$service_name"
grep -Fxq 'PIXELS_RELAY_MAX_ROOMS=2048' "$configuration_root/private-relay.env"
[[ $(readlink "$release_root/current-relay") == "$original_release" ]] || { echo "same candidate changed release identity" >&2; exit 1; }

write_environment 'invalid-address' 2048
if /bin/sh "$candidate_directory/tools/install_linux_component.sh" relay "$deployment_id" "$candidate_directory" "$source_environment" >/dev/null 2>&1; then
    echo "installer accepted a failing Relay startup" >&2
    exit 1
fi
systemctl is-active --quiet "$service_name"
grep -Fxq 'PIXELS_RELAY_LISTEN=127.0.0.1:0' "$configuration_root/private-relay.env"
[[ $(readlink "$release_root/current-relay") == "$original_release" ]] || { echo "failed upgrade did not restore release" >&2; exit 1; }

systemctl disable "$service_name" >/dev/null
systemctl is-active --quiet "$service_name"
write_environment 'invalid-address' 4096
if /bin/sh "$candidate_directory/tools/install_linux_component.sh" relay "$deployment_id" "$candidate_directory" "$source_environment" >/dev/null 2>&1; then
    echo "installer accepted a failing Relay startup from disabled-active state" >&2
    exit 1
fi
systemctl is-active --quiet "$service_name" || { echo "rollback lost disabled-active Relay state" >&2; exit 1; }
[[ $(systemctl is-enabled "$service_name" 2>/dev/null || true) != enabled ]] || {
    echo "rollback changed disabled Relay to enabled" >&2; exit 1;
}
grep -Fxq 'PIXELS_RELAY_MAX_ROOMS=2048' "$configuration_root/private-relay.env"

systemctl stop "$service_name"
write_environment 'invalid-address' 8192
if /bin/sh "$candidate_directory/tools/install_linux_component.sh" relay "$deployment_id" "$candidate_directory" "$source_environment" >/dev/null 2>&1; then
    echo "installer accepted a failing Relay startup from disabled-inactive state" >&2
    exit 1
fi
[[ $(systemctl is-active "$service_name" 2>/dev/null || true) != active ]] || {
    echo "rollback started a previously inactive Relay" >&2; exit 1;
}
[[ $(systemctl is-enabled "$service_name" 2>/dev/null || true) != enabled ]] || {
    echo "rollback enabled a previously disabled Relay" >&2; exit 1;
}

/bin/sh "$candidate_directory/tools/uninstall_linux_component.sh" relay "$deployment_id"
[[ $(systemctl is-active "$service_name" 2>/dev/null || true) != active ]] || { echo "Relay remained active after unregister" >&2; exit 1; }
[[ -f "$configuration_root/private-relay.env" && -d "$release_root/releases" && -d "$data_root/relay" ]] || {
    echo "unregister removed preserved state" >&2; exit 1;
}
echo "PASS PRIVATE RELAY SYSTEMD: preflight, dedicated identity, cover install, active/inactive rollback and unregister"
