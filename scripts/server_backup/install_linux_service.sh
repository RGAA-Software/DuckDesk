#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: install_linux_service.sh <deployment-uuid> <absolute-px_backup-path> <absolute-private-config-path>" >&2
    exit 2
fi
if [ "$(id -u)" -ne 0 ]; then
    echo "install_linux_service.sh must run as root" >&2
    exit 3
fi

deployment_id=$1
source_binary=$2
source_config=$3
if ! printf '%s\n' "$deployment_id" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'; then
    echo "deployment ID must be a lowercase UUID" >&2
    exit 4
fi
case "$source_binary" in /*) ;; *) echo "binary path must be absolute" >&2; exit 4 ;; esac
case "$source_config" in /*) ;; *) echo "config path must be absolute" >&2; exit 4 ;; esac
if [ ! -x "$source_binary" ] || [ ! -f "$source_config" ]; then
    echo "binary or private configuration is unavailable" >&2
    exit 4
fi

script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(CDPATH= cd -- "$script_directory/../.." && pwd)
unit_source="$repository_root/deploy/systemd/pixels-backup@.service"
if [ ! -f "$unit_source" ]; then
    echo "systemd unit template is unavailable" >&2
    exit 4
fi

service_name="pixels-backup@$deployment_id.service"
if systemctl list-unit-files "$service_name" --no-legend 2>/dev/null | grep -q .; then
    systemctl stop "$service_name" || true
fi

if ! getent group pixels-backup >/dev/null; then
    groupadd --system pixels-backup
fi
if ! getent passwd pixels-backup >/dev/null; then
    useradd --system --gid pixels-backup --home-dir /nonexistent --shell /usr/sbin/nologin pixels-backup
fi

install -d -o root -g root -m 0755 /opt/pixels/current/bin
install -o root -g root -m 0755 "$source_binary" /opt/pixels/current/bin/px_backup.next
mv -f /opt/pixels/current/bin/px_backup.next /opt/pixels/current/bin/px_backup

configuration_root="/etc/pixels/$deployment_id/backup"
data_root="/var/lib/pixels/$deployment_id/backup"
install -d -o root -g root -m 0755 /etc/pixels "/etc/pixels/$deployment_id"
install -d -o root -g pixels-backup -m 0750 "$configuration_root"
install -d -o root -g root -m 0755 /var/lib/pixels "/var/lib/pixels/$deployment_id"
install -d -o pixels-backup -g pixels-backup -m 0700 "$data_root"
for directory_name in repository scheduler status offsite; do
    install -d -o pixels-backup -g pixels-backup -m 0700 "$data_root/$directory_name"
done
install -o pixels-backup -g pixels-backup -m 0400 "$source_config" "$configuration_root/config.json"
install -o root -g root -m 0644 "$unit_source" /etc/systemd/system/pixels-backup@.service

systemctl daemon-reload
systemctl enable --now "$service_name"
systemctl is-active --quiet "$service_name"
echo "installed and started $service_name"
