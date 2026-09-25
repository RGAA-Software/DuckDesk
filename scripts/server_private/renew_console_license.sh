#!/bin/sh
set -eu

if [ "$#" -ne 3 ] || [ "$(id -u)" -ne 0 ]; then
    echo "usage (as root): renew_console_license.sh <deployment-uuid> <new-trust-store> <new-license>" >&2
    exit 2
fi

deployment_id=$1
trust_source=$2
license_source=$3
if ! printf '%s\n' "$deployment_id" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'; then
    echo "deployment ID must be a lowercase UUID" >&2
    exit 2
fi
configuration_root="/etc/pixels/$deployment_id"
environment_file="$configuration_root/private-console.env"
service_name="pixels-private-console@$deployment_id.service"
console_admin="/opt/pixels/private/$deployment_id/current-console/bin/px_console_admin"
if [ -L "$configuration_root" ] || [ ! -d "$configuration_root" ] || [ -L "$environment_file" ] || [ ! -f "$environment_file" ] ||
   [ ! -x "$console_admin" ] || ! systemctl is-active --quiet "$service_name" || ! id pixels-console >/dev/null 2>&1; then
    echo "an installed, active Customer Console is required" >&2
    exit 3
fi
if ! grep -Fxq "PIXELS_DEPLOYMENT_ID=$deployment_id" "$environment_file" ||
   ! grep -Fxq 'PIXELS_CONSOLE_DISTRIBUTION=customer' "$environment_file" ||
   ! grep -Fxq 'PIXELS_CONSOLE_RELEASE_NAMESPACE=pixels.customer' "$environment_file"; then
    echo "Console configuration is not bound to this Customer deployment" >&2
    exit 3
fi

setting_path() {
    setting_name=$1
    setting_lines=$(grep -E "^${setting_name}=" "$environment_file" || true)
    [ "$(printf '%s\n' "$setting_lines" | grep -c .)" -eq 1 ] || return 1
    printf '%s\n' "${setting_lines#*=}"
}
trust_target=$(setting_path PIXELS_CONSOLE_LICENSE_TRUST_STORE) || { echo "missing or repeated trust path" >&2; exit 3; }
license_target=$(setting_path PIXELS_CONSOLE_LICENSE_FILE) || { echo "missing or repeated license path" >&2; exit 3; }
for target_path in "$trust_target" "$license_target"; do
    case "$target_path" in "$configuration_root"/*) ;; *) echo "license target escaped the deployment" >&2; exit 3 ;; esac
    [ ! -L "$target_path" ] || { echo "license target must not be a symbolic link" >&2; exit 3; }
    target_parent=$(dirname -- "$target_path")
    resolved_parent=$(realpath -e -- "$target_parent")
    case "$resolved_parent" in "$configuration_root"|"$configuration_root"/*) ;; *) echo "license parent escaped the deployment" >&2; exit 3 ;; esac
done
for source_path in "$trust_source" "$license_source"; do
    [ -f "$source_path" ] && [ ! -L "$source_path" ] || { echo "candidate must be a regular file" >&2; exit 3; }
done
[ "$trust_source" != "$trust_target" ] && [ "$license_source" != "$license_target" ] && [ "$trust_target" != "$license_target" ] || {
    echo "candidate and installed paths must be distinct" >&2; exit 3;
}

staging_directory=$(mktemp -d "$configuration_root/.license-update.XXXXXXXX")
chmod 0711 "$staging_directory"
staged_trust="$staging_directory/trust.next"
staged_license="$staging_directory/license.next"
old_trust="$staging_directory/trust.previous"
old_license="$staging_directory/license.previous"
swapped=0
stopped=0
cleanup() {
    result=$?
    trap - EXIT
    if [ "$result" -ne 0 ] && [ "$swapped" -eq 1 ]; then
        if [ -f "$old_trust" ]; then cp -p -- "$old_trust" "$trust_target"; else rm -f -- "$trust_target"; fi
        if [ -f "$old_license" ]; then cp -p -- "$old_license" "$license_target"; else rm -f -- "$license_target"; fi
    fi
    if [ "$result" -ne 0 ] && [ "$stopped" -eq 1 ]; then systemctl start "$service_name" || true; fi
    resolved_staging=$(realpath -e -- "$staging_directory")
    case "$resolved_staging" in "$configuration_root"/.license-update.*) rm -r -- "$staging_directory" ;; esac
    exit "$result"
}
trap cleanup EXIT

install -o pixels-console -g pixels-console -m 0400 "$trust_source" "$staged_trust"
install -o pixels-console -g pixels-console -m 0400 "$license_source" "$staged_license"
runuser -u pixels-console -- env PIXELS_DEPLOYMENT_ID="$deployment_id" \
    PIXELS_CONSOLE_LICENSE_TRUST_STORE="$staged_trust" PIXELS_CONSOLE_LICENSE_FILE="$staged_license" \
    "$console_admin" validate-license
if [ -f "$trust_target" ]; then cp -p -- "$trust_target" "$old_trust"; fi
if [ -f "$license_target" ]; then cp -p -- "$license_target" "$old_license"; fi

stopped=1
systemctl stop "$service_name"
if systemctl is-active --quiet "$service_name"; then
    echo "Console did not stop" >&2
    exit 4
fi
swapped=1
mv -f -- "$staged_trust" "$trust_target"
mv -f -- "$staged_license" "$license_target"
systemctl start "$service_name"
sleep 2
systemctl is-active --quiet "$service_name"
runuser -u pixels-console -- env PIXELS_DEPLOYMENT_ID="$deployment_id" \
    PIXELS_CONSOLE_LICENSE_TRUST_STORE="$trust_target" PIXELS_CONSOLE_LICENSE_FILE="$license_target" \
    "$console_admin" validate-license
stopped=0
echo "PASS Customer Console license replacement: $deployment_id"
