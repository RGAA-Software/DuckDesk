#!/usr/bin/env bash
# One-click deploy: cross-build gr_desk_server (musl) + web/gr_desk frontend,
# deploy to the Tencent server (existing nohup deployment at /root/off_site).
# Usage: scripts/deploy_desk_server.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"              # rust_server
PROJ_ROOT="$(cd "$WS_ROOT/.." && pwd)"              # GammaRayPremium
SERVER="ubuntu@43.134.55.209"
CRED_FILE="$WS_ROOT/gr_desk_server/tencent_server.txt"
BIN="$WS_ROOT/target/x86_64-unknown-linux-musl/release/gr_desk_server"
WEB_DIST="$PROJ_ROOT/web/gr_desk/dist"
REMOTE_DIR="/root/off_site"
WEB_ROOT="/var/godesk.uk"   # nginx 公网静态根（443 server 块 root，/api 反代到 5000）

# --- SSH password via askpass (no interactive prompt) ---
TMPD="$(mktemp -d)"
trap 'rm -rf "$TMPD"' EXIT
grep '^password:' "$CRED_FILE" | sed 's/^password: //' > "$TMPD/.pw"
PW="$(cat "$TMPD/.pw")"
cat > "$TMPD/askpass.sh" <<EOF
#!/bin/bash
cat "$TMPD/.pw"
EOF
chmod +x "$TMPD/askpass.sh"
export SSH_ASKPASS="$TMPD/askpass.sh" SSH_ASKPASS_REQUIRE=force DISPLAY=:0
SSH_OPTS="-o StrictHostKeyChecking=accept-new -o ConnectTimeout=30"

echo "== [1/4] cross build (musl) =="
"$SCRIPT_DIR/build_desk_server_musl.sh"

echo "== [2/4] build frontend =="
cd "$WEB_DIST/.."
npm run build

echo "== [3/4] upload =="
scp $SSH_OPTS "$BIN" "$SERVER:/tmp/gr_desk_server.new"
tar czf "${TMPD}/static.tar.gz" -C "$WEB_DIST" .
scp $SSH_OPTS "${TMPD}/static.tar.gz" "$SERVER:/tmp/"

echo "== [4/4] install & restart =="
# sudo -S 从 stdin 读第一行作为密码，bash -s 继续读剩余脚本
ssh $SSH_OPTS "$SERVER" "sudo -S bash -s" <<EOF
$PW
set -e
TS=\$(date +%Y%m%d%H%M%S)
cd $REMOTE_DIR
# 备份旧二进制
[ -f gr_off_site ] && cp -a gr_off_site gr_off_site.bak.\$TS
[ -f gr_desk_server ] && cp -a gr_desk_server gr_desk_server.bak.\$TS
# 停掉旧进程
pkill -x gr_off_site || true
pkill -x gr_desk_server || true
sleep 1
# 安装新二进制
mv /tmp/gr_desk_server.new $REMOTE_DIR/gr_desk_server
chmod +x $REMOTE_DIR/gr_desk_server
# 安装前端静态资源（保留 document/ 子目录；/var/godesk.uk 为 nginx 公网静态根）
rm -rf $REMOTE_DIR/static/assets
tar xzf /tmp/static.tar.gz -C $REMOTE_DIR/static
[ ! -d $REMOTE_DIR/static/document ] && [ -d $WEB_ROOT/document ] && cp -a $WEB_ROOT/document $REMOTE_DIR/static/document
# 更新 nginx 公网静态根（备份后替换，保留 document/）
cp -a $WEB_ROOT $WEB_ROOT.bak.\$TS
rm -rf $WEB_ROOT/assets
tar xzf /tmp/static.tar.gz -C $WEB_ROOT
rm /tmp/static.tar.gz
# 启动
cd $REMOTE_DIR
setsid nohup ./gr_desk_server > nohup.out 2>&1 < /dev/null &
sleep 3
ss -tlnp | grep -E ':5000|:5001' || { echo 'ERROR: ports not listening'; tail -20 nohup.out; exit 1; }
curl -sk -o /dev/null -w "local https://127.0.0.1:5001/ -> %{http_code}\n" https://127.0.0.1:5001/
EOF

echo "deploy done."
