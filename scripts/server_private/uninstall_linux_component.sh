#!/bin/sh
set -eu
if [ "$#" -ne 2 ] || [ "$(id -u)" -ne 0 ]; then
    echo "usage (as root): uninstall_linux_component.sh <console|relay|backup> <deployment-uuid>" >&2
    exit 2
fi
component=$1
deployment_id=$2
case "$component" in console|relay|backup) ;; *) echo "unknown component" >&2; exit 4 ;; esac
if ! printf '%s\n' "$deployment_id" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'; then
    echo "deployment ID must be a lowercase UUID" >&2
    exit 4
fi
service_name="pixels-private-$component@$deployment_id.service"
systemctl disable --now "$service_name" >/dev/null 2>&1 || true
if systemctl is-active --quiet "$service_name"; then
    echo "component service did not stop" >&2
    exit 5
fi
systemctl reset-failed "$service_name" >/dev/null 2>&1 || true
echo "unregistered $service_name; releases, private environment and data were preserved"
