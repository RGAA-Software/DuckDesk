#!/usr/bin/env bash
# One-click deploy: cross-build gr_auth_server (musl) and deploy to the Tencent server.
# Usage: scripts/deploy_auth_server.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SERVER="ubuntu@49.232.190.218"
CRED_FILE="$WS_ROOT/gr_auth_server/tencent_server.txt"
BIN="$WS_ROOT/target/x86_64-unknown-linux-musl/release/gr_auth_server"
WEB_DIR="$WS_ROOT/target/x86_64-unknown-linux-musl/release/web_auth"

# --- SSH password via askpass (no interactive prompt) ---
TMPD="$(mktemp -d)"
trap 'rm -rf "$TMPD"' EXIT
grep '^password:' "$CRED_FILE" | sed 's/^password: //' > "$TMPD/.pw"
cat > "$TMPD/askpass.sh" <<EOF
#!/bin/bash
cat "$TMPD/.pw"
EOF
chmod +x "$TMPD/askpass.sh"
export SSH_ASKPASS="$TMPD/askpass.sh" SSH_ASKPASS_REQUIRE=force DISPLAY=:0

echo "== [1/3] build =="
"$SCRIPT_DIR/build_auth_server_musl.sh"

echo "== [2/3] upload =="
scp "$BIN" "$SERVER:/tmp/gr_auth_server.new"
# web_auth 静态资源（前端页面）
tar czf "${TMPD}/web_auth.tar.gz" -C "$WEB_DIR" .
scp "${TMPD}/web_auth.tar.gz" "$SERVER:/tmp/"

echo "== [3/3] restart & verify =="
ssh "$SERVER" '
  set -e
  sudo supervisorctl stop gr_auth_server
  sudo cp /tmp/gr_auth_server.new /opt/gr_auth_server/gr_auth_server
  sudo chown ubuntu:ubuntu /opt/gr_auth_server/gr_auth_server
  rm /tmp/gr_auth_server.new
  sudo rm -rf /opt/gr_auth_server/web_auth
  sudo mkdir -p /opt/gr_auth_server/web_auth
  sudo tar xzf /tmp/web_auth.tar.gz -C /opt/gr_auth_server/web_auth
  sudo chown -R ubuntu:ubuntu /opt/gr_auth_server/web_auth
  rm /tmp/web_auth.tar.gz
  sudo supervisorctl start gr_auth_server
  sleep 3
  sudo supervisorctl status gr_auth_server
  curl -sk -o /dev/null -w "backend https://127.0.0.1:30400 -> %{http_code}\n" https://127.0.0.1:30400/
'
curl -s -o /dev/null -w "https://auth.rgaa.vip/ -> %{http_code}\n" --connect-timeout 10 https://auth.rgaa.vip/
echo "deploy done."
