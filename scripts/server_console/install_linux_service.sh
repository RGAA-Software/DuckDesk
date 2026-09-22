#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: install_linux_service.sh <deployment-uuid> <absolute-px_console-path> <absolute-private-environment-path>" >&2
    exit 2
fi
if [ "$(id -u)" -ne 0 ]; then
    echo "install_linux_service.sh must run as root" >&2
    exit 3
fi

deployment_id=$1
source_binary=$2
source_environment=$3
if ! printf '%s\n' "$deployment_id" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'; then
    echo "deployment ID must be a lowercase UUID" >&2
    exit 4
fi
case "$source_binary" in /*) ;; *) echo "binary path must be absolute" >&2; exit 4 ;; esac
case "$source_environment" in /*) ;; *) echo "environment path must be absolute" >&2; exit 4 ;; esac
if [ ! -f "$source_binary" ] || [ ! -x "$source_binary" ] || [ -L "$source_binary" ]; then
    echo "Console binary must be an executable regular file, not a symbolic link" >&2
    exit 4
fi
if [ ! -f "$source_environment" ] || [ -L "$source_environment" ]; then
    echo "private Console environment must be a regular file, not a symbolic link" >&2
    exit 4
fi
if find "$source_environment" -prune -perm /077 -print -quit | grep -q .; then
    echo "private Console environment must not grant group or other permissions" >&2
    exit 4
fi
if ! awk '
    /^[[:space:]]*($|#)/ { next }
    /^[A-Z][A-Z0-9_]*=/ { next }
    { exit 1 }
' "$source_environment"; then
    echo "private Console environment contains an invalid assignment" >&2
    exit 4
fi
if ! grep -Eq "^PIXELS_DEPLOYMENT_ID=(\"$deployment_id\"|'$deployment_id'|$deployment_id)$" "$source_environment"; then
    echo "private Console environment is not bound to the requested deployment" >&2
    exit 4
fi

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/../.." && pwd)
unit_source="$repository_root/deploy/systemd/pixels-console@.service"
if [ ! -f "$unit_source" ]; then
    echo "systemd unit template is unavailable" >&2
    exit 4
fi

service_name="pixels-console@$deployment_id.service"
other_active_instance=$(systemctl list-units --type=service --state=active 'pixels-console@*.service' --no-legend 2>/dev/null |
    awk -v requested="$service_name" '$1 != requested { print $1; exit }')
if [ -n "$other_active_instance" ]; then
    echo "another Pixels Console instance is active: $other_active_instance" >&2
    exit 5
fi
if systemctl is-active --quiet "$service_name"; then
    systemctl stop "$service_name"
    if systemctl is-active --quiet "$service_name"; then
        echo "existing Pixels Console instance did not stop" >&2
        exit 5
    fi
fi

if ! getent group pixels-console >/dev/null; then
    groupadd --system pixels-console
fi
if ! getent passwd pixels-console >/dev/null; then
    useradd --system --gid pixels-console --home-dir /nonexistent --shell /usr/sbin/nologin pixels-console
fi

install -d -o root -g root -m 0755 /opt/pixels/current/bin
install -o root -g root -m 0755 "$source_binary" /opt/pixels/current/bin/px_console.next
mv -f /opt/pixels/current/bin/px_console.next /opt/pixels/current/bin/px_console

configuration_root="/etc/pixels/$deployment_id"
data_root="/var/lib/pixels/$deployment_id/console"
install -d -o root -g root -m 0755 /etc/pixels
install -d -o root -g pixels-console -m 0750 "$configuration_root"
install -d -o root -g root -m 0755 /var/lib/pixels "/var/lib/pixels/$deployment_id"
install -d -o pixels-console -g pixels-console -m 0700 "$data_root"
install -o pixels-console -g pixels-console -m 0400 "$source_environment" "$configuration_root/console.env.next"
mv -f "$configuration_root/console.env.next" "$configuration_root/console.env"
install -o root -g root -m 0644 "$unit_source" /etc/systemd/system/pixels-console@.service

systemctl daemon-reload
systemctl enable --now "$service_name"
systemctl is-active --quiet "$service_name"
echo "installed and started $service_name"
