#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: uninstall_linux_service.sh <deployment-uuid>" >&2
    exit 2
fi
if [ "$(id -u)" -ne 0 ]; then
    echo "uninstall_linux_service.sh must run as root" >&2
    exit 3
fi

deployment_id=$1
if ! printf '%s\n' "$deployment_id" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'; then
    echo "deployment ID must be a lowercase UUID" >&2
    exit 4
fi

service_name="pixels-console@$deployment_id.service"
systemctl disable --now "$service_name" >/dev/null 2>&1 || true
systemctl reset-failed "$service_name" >/dev/null 2>&1 || true
echo "unregistered $service_name; private environment and runtime data were preserved"
