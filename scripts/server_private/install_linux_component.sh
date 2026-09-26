#!/bin/sh
set -eu

if [ "$#" -ne 4 ]; then
    echo "usage: install_linux_component.sh <console|relay|backup> <deployment-uuid> <absolute-candidate-directory> <absolute-private-config-file>" >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "private Server installation requires Python 3.8 or newer" >&2
    exit 3
fi
if [ "$(id -u)" -ne 0 ] || [ "$(ps -p 1 -o comm=)" != systemd ]; then
    echo "installation requires root and a running systemd" >&2
    exit 3
fi

component=$1
deployment_id=$2
candidate_directory=$3
source_environment=$4
case "$component" in console|relay|backup) ;; *) echo "unknown component" >&2; exit 4 ;; esac
if ! printf '%s\n' "$deployment_id" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'; then
    echo "deployment ID must be a lowercase UUID" >&2
    exit 4
fi
case "$candidate_directory:$source_environment" in /*:/*) ;; *) echo "candidate and environment paths must be absolute" >&2; exit 4 ;; esac
if [ -L "$candidate_directory" ] || [ ! -d "$candidate_directory" ] || [ -L "$source_environment" ] || [ ! -f "$source_environment" ]; then
    echo "candidate and private environment must be regular paths" >&2
    exit 4
fi
if find "$source_environment" -prune -perm /077 -print -quit | grep -q .; then
    echo "private environment must not grant group or other permissions" >&2
    exit 4
fi
if [ "$component" = backup ]; then
    if ! python3 - "$source_environment" "$deployment_id" "$candidate_directory" <<'PY'
import hashlib
import json
import os
import stat
import sys
from pathlib import Path

with open(sys.argv[1], encoding="utf-8") as configuration_file:
    configuration = json.load(configuration_file)
if not isinstance(configuration, dict) or configuration.get("schema_version") != 2 or configuration.get("deployment_id") != sys.argv[2]:
    raise SystemExit("private backup configuration does not match deployment")

for tool_name in ("pg_dump", "pg_restore"):
    tool_path = Path(configuration.get(f"{tool_name}_path", ""))
    pinned_hash = configuration.get(f"{tool_name}_sha256", "")
    if (not tool_path.is_absolute() or tool_path.name != tool_name
            or not isinstance(pinned_hash, str) or len(pinned_hash) != 64
            or any(character not in "0123456789abcdef" for character in pinned_hash)
            or tool_path.resolve(strict=True) != tool_path
            or not tool_path.is_file() or not os.access(tool_path, os.X_OK)):
        raise SystemExit(f"private backup {tool_name} tool is unavailable or unpinned")
    tool_digest = hashlib.sha256()
    with tool_path.open("rb") as tool_file:
        for payload_bytes in iter(lambda: tool_file.read(1024 * 1024), b""):
            tool_digest.update(payload_bytes)
    if tool_digest.hexdigest() != pinned_hash:
        raise SystemExit(f"private backup {tool_name} hash does not match")

packaged_toolchain_manifest = Path(sys.argv[3]) / "postgresql" / "18" / "sha256.json"
if packaged_toolchain_manifest.is_file():
    installed_toolchain_root = Path("/opt/pixels/postgresql/18")
    installed_manifest = installed_toolchain_root / "sha256.json"
    if not installed_manifest.is_file() or installed_manifest.read_bytes() != packaged_toolchain_manifest.read_bytes():
        raise SystemExit("install the matching packaged PostgreSQL client toolchain before Backup")
    with packaged_toolchain_manifest.open(encoding="utf-8") as manifest_file:
        toolchain_manifest = json.load(manifest_file)
    for tool_name in ("pg_dump", "pg_restore"):
        if (Path(configuration[f"{tool_name}_path"]) != installed_toolchain_root / "bin" / tool_name
                or configuration[f"{tool_name}_sha256"] != toolchain_manifest["artifacts"][f"bin/{tool_name}"]):
            raise SystemExit(f"private backup {tool_name} is not the packaged PostgreSQL client")

backup_secret_root = Path("/etc/pixels") / sys.argv[2] / "backup"
tls_root = backup_secret_root / "postgres-ca.crt"
if (not tls_root.is_file() or tls_root.is_symlink()
        or b"-----BEGIN CERTIFICATE-----" not in tls_root.read_bytes()):
    raise SystemExit("private backup PostgreSQL TLS root is unavailable")
required_targets = [target for target in configuration.get("plan", {}).get("targets", [])
                    if target.get("state") == "required"]
if not required_targets:
    raise SystemExit("private backup has no required database")
for target in required_targets:
    credential_path = Path(target.get("database", {}).get("password_file", ""))
    if (credential_path.parent != backup_secret_root or not credential_path.is_file()
            or credential_path.is_symlink() or stat.S_IMODE(credential_path.stat().st_mode) & 0o077):
        raise SystemExit("private backup database credential is unavailable or permissive")
PY
    then
        exit 4
    fi
else
    if ! awk '/^[[:space:]]*($|#)/ { next } /^[A-Z][A-Z0-9_]*=/ { next } { exit 1 }' "$source_environment"; then
        echo "private environment contains an invalid assignment" >&2
        exit 4
    fi
    if ! grep -Fxq "PIXELS_DEPLOYMENT_ID=$deployment_id" "$source_environment"; then
        echo "private environment is not bound to the requested deployment" >&2
        exit 4
    fi
fi
if [ "$component" = console ]; then
    grep -Fxq 'PIXELS_CONSOLE_DISTRIBUTION=customer' "$source_environment" || { echo "Customer Console distribution is required" >&2; exit 4; }
    grep -Fxq 'PIXELS_CONSOLE_RELEASE_NAMESPACE=pixels.customer' "$source_environment" || { echo "Customer release namespace is required" >&2; exit 4; }
    grep -Fxq "PIXELS_CONSOLE_STATIC_DIRECTORY=/opt/pixels/private/$deployment_id/current-console/static/console" "$source_environment" || {
        echo "Console static path must point to its independent current release" >&2; exit 4;
    }
fi

candidate_directory=$(realpath -e -- "$candidate_directory")
unit_source="$candidate_directory/systemd/pixels-private-$component@.service"
if [ ! -f "$unit_source" ] || [ -L "$unit_source" ]; then
    echo "component unit is missing" >&2
    exit 4
fi
python3 "$candidate_directory/tools/verify_candidate.py" "$candidate_directory"
/bin/sh "$candidate_directory/tools/preflight_linux_host.sh"
if [ ! -x "$candidate_directory/bin/px_$component" ]; then
    echo "component executable is missing" >&2
    exit 4
fi

manifest_digest=$(sha256sum "$candidate_directory/sha256.json" | cut -d ' ' -f 1)
release_root="/opt/pixels/private/$deployment_id"
release_directory="$release_root/releases/$manifest_digest"
current_link="$release_root/current-$component"
service_user="pixels-$component"
service_name="pixels-private-$component@$deployment_id.service"
unit_destination="/etc/systemd/system/pixels-private-$component@.service"
configuration_root="/etc/pixels/$deployment_id"
environment_destination="$configuration_root/private-$component.env"
if [ "$component" = backup ]; then
    environment_destination="$configuration_root/backup/config.json"
fi
for protected_path in /opt/pixels/private "$release_root" "$release_root/releases" /etc/pixels "$configuration_root" \
    /var/lib/pixels "/var/lib/pixels/$deployment_id" "/var/lib/pixels/$deployment_id/$component"; do
    if [ -L "$protected_path" ]; then
        echo "installation path must not be a symbolic link: $protected_path" >&2
        exit 4
    fi
done
if [ "$component" = backup ] && [ -L "$configuration_root/backup" ]; then
    echo "backup configuration directory must not be a symbolic link" >&2
    exit 4
fi
if [ -L "$environment_destination" ] || [ -L "$environment_destination.next" ]; then
    echo "private configuration target must not be a symbolic link" >&2
    exit 4
fi
if [ -L "$unit_destination" ]; then
    echo "systemd unit target must not be a symbolic link" >&2
    exit 4
fi
if [ -e "$current_link" ] && [ ! -L "$current_link" ]; then
    echo "component current path must be a symbolic link" >&2
    exit 4
fi
previous_release=$(readlink "$current_link" || true)
if [ -n "$previous_release" ]; then
    if ! printf '%s\n' "$previous_release" | grep -Eq '^releases/[a-f0-9]{64}$'; then
        echo "existing component release link is invalid" >&2
        exit 4
    fi
    python3 "$candidate_directory/tools/check_upgrade.py" "$release_root/$previous_release" "$candidate_directory"
fi
if ! getent group "$service_user" >/dev/null; then groupadd --system "$service_user"; fi
if ! getent passwd "$service_user" >/dev/null; then
    useradd --system --gid "$service_user" --home-dir /nonexistent --shell /usr/sbin/nologin "$service_user"
fi
if [ "$component" = backup ]; then
    python3 - "$source_environment" "$service_user" <<'PY'
import json
import subprocess
import sys

with open(sys.argv[1], encoding="utf-8") as configuration_file:
    configuration = json.load(configuration_file)
for tool_name in ("pg_dump", "pg_restore"):
    subprocess.run(["runuser", "-u", sys.argv[2], "--", "test", "-x", configuration[f"{tool_name}_path"]], check=True)
PY
fi
install -d -o root -g root -m 0711 /etc/pixels "$configuration_root"
install -d -o root -g root -m 0755 /var/lib/pixels "/var/lib/pixels/$deployment_id"
install -d -o "$service_user" -g "$service_user" -m 0700 "/var/lib/pixels/$deployment_id/$component"
if [ "$component" = backup ]; then
    install -d -o root -g "$service_user" -m 0750 "$configuration_root/backup"
    python3 - "$source_environment" "$service_user" <<'PY'
import json
import os
import pwd
import stat
import subprocess
import sys
from pathlib import Path

with open(sys.argv[1], encoding="utf-8") as configuration_file:
    configuration = json.load(configuration_file)
service_identity = pwd.getpwnam(sys.argv[2])
backup_secret_root = Path("/etc/pixels") / configuration["deployment_id"] / "backup"
tls_root = backup_secret_root / "postgres-ca.crt"
subprocess.run(["runuser", "-u", sys.argv[2], "--", "test", "-r", str(tls_root)], check=True)
for target in configuration["plan"]["targets"]:
    if target["state"] != "required":
        continue
    credential_path = Path(target["database"]["password_file"])
    if credential_path.parent != backup_secret_root:
        raise SystemExit("private backup credential left its deployment directory")
    credential_descriptor = os.open(credential_path, os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        if not stat.S_ISREG(os.fstat(credential_descriptor).st_mode):
            raise SystemExit("private backup credential is not a regular file")
        os.fchown(credential_descriptor, service_identity.pw_uid, service_identity.pw_gid)
        os.fchmod(credential_descriptor, stat.S_IRUSR)
    finally:
        os.close(credential_descriptor)
    subprocess.run(["runuser", "-u", sys.argv[2], "--", "test", "-r", str(credential_path)], check=True)
PY
    for directory_name in repository scheduler status offsite; do
        install -d -o "$service_user" -g "$service_user" -m 0700 "/var/lib/pixels/$deployment_id/backup/$directory_name"
    done
fi
if [ -e "$release_directory" ]; then
    python3 "$release_directory/tools/verify_candidate.py" "$release_directory"
else
    install -d -o root -g root -m 0755 "$release_root/releases"
    staged_release="$release_root/releases/.stage-$manifest_digest-$$"
    install -d -o root -g root -m 0755 "$staged_release"
    cp -a "$candidate_directory/." "$staged_release/"
    python3 "$staged_release/tools/verify_candidate.py" "$staged_release"
    chown -R root:root "$staged_release"
    chmod -R a-w "$staged_release"
    mv -- "$staged_release" "$release_directory"
fi

previous_environment="$configuration_root/.private-$component.env.previous-$$"
previous_unit="$configuration_root/.pixels-private-$component.unit.previous-$$"
if [ -f "$environment_destination" ]; then cp -p "$environment_destination" "$previous_environment"; fi
if [ -f "$unit_destination" ]; then cp -p "$unit_destination" "$previous_unit"; fi
was_enabled=$(systemctl is-enabled "$service_name" 2>/dev/null || true)
was_active=$(systemctl is-active "$service_name" 2>/dev/null || true)

rollback() {
    systemctl stop "$service_name" 2>/dev/null || true
    if [ -n "$previous_release" ]; then
        ln -s "$previous_release" "$release_root/.current-$component.rollback-$$"
        mv -Tf "$release_root/.current-$component.rollback-$$" "$current_link"
    else
        rm -f -- "$current_link"
    fi
    if [ -f "$previous_environment" ]; then mv -f "$previous_environment" "$environment_destination"; else rm -f -- "$environment_destination"; fi
    if [ -f "$previous_unit" ]; then mv -f "$previous_unit" "$unit_destination"; else rm -f -- "$unit_destination"; fi
    systemctl daemon-reload
    if [ "$was_enabled" = enabled ]; then
        systemctl enable "$service_name" >/dev/null 2>&1 || true
    else
        systemctl disable "$service_name" >/dev/null 2>&1 || true
    fi
    if [ "$was_active" = active ] && [ -n "$previous_release" ]; then
        systemctl start "$service_name" || true
    else
        systemctl stop "$service_name" >/dev/null 2>&1 || true
    fi
}
trap rollback EXIT
if systemctl is-active --quiet "$service_name"; then
    systemctl stop "$service_name"
    if systemctl is-active --quiet "$service_name"; then
        echo "existing component service did not stop" >&2
        exit 5
    fi
fi

install -o "$service_user" -g "$service_user" -m 0400 "$source_environment" "$environment_destination.next"
mv -f "$environment_destination.next" "$environment_destination"
ln -s "releases/$manifest_digest" "$release_root/.current-$component.next-$$"
mv -Tf "$release_root/.current-$component.next-$$" "$current_link"
install -o root -g root -m 0644 "$unit_source" "$unit_destination"
systemctl daemon-reload
systemctl enable --now "$service_name"
systemctl is-active --quiet "$service_name"
sleep 2
systemctl is-active --quiet "$service_name"
trap - EXIT
rm -f -- "$previous_environment" "$previous_unit"
echo "installed $service_name from release $manifest_digest"
