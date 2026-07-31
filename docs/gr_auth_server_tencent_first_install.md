# gr_auth_server 腾讯云全新服务器首装指南

本文面向一台**全新的腾讯云 Ubuntu 服务器**，从零安装 gr_auth_server 的运行环境并完成首次部署。
日常增量部署（已装好的机器）直接用 `rust_server/scripts/deploy_auth_server.sh` 即可，不需要本文。

服务器侧既定布局（与 `deploy_auth_server.sh` 对应，不要随意改）：

```text
/opt/gr_auth_server/
├── gr_auth_server                  # musl 静态二进制
├── gr_auth_server_settings.toml    # 配置文件
├── certs/                          # TLS 证书 + 授权签名密钥
│   ├── cert.pem
│   ├── key.pem
│   ├── auth_license_private.key    # 授权签名私钥（保密，备份好）
│   └── auth_license_public.key
├── web_auth/                       # 前端静态资源（由部署脚本上传）
└── logs/                           # 运行日志（自动创建）
```

## 1. 安全组

在腾讯云控制台安全组放行：

- `22/tcp`：SSH（建议限制来源 IP）
- `443/tcp`：对外 HTTPS 入口（nginx 反代时）
- `30400/tcp`：后端直连端口。走 nginx 反代时**不要**对公网放行，只留本机
- `27017/tcp`：**不要放行**，MongoDB 只监听本机

## 2. 安装 MongoDB

以 Ubuntu 22.04 为例：

```bash
# 导入官方源（jammy）
curl -fsSL https://www.mongodb.org/static/pgp/server-7.0.asc | \
  sudo gpg --dearmor -o /usr/share/keyrings/mongodb-server-7.0.gpg
echo "deb [signed-by=/usr/share/keyrings/mongodb-server-7.0.gpg] \
  https://repo.mongodb.org/apt/ubuntu jammy/mongodb-org/7.0 multiverse" | \
  sudo tee /etc/apt/sources.list.d/mongodb-org-7.0.list
sudo apt update && sudo apt install -y mongodb-org
sudo systemctl enable --now mongod
```

默认配置 `bindIp: 127.0.0.1`，保持不动即可（服务与 MongoDB 同机部署）。
如果后期要开认证，在 `gr_auth_server_settings.toml` 的 `db_path` 连接串里带上账号密码，
参考 `docs/gr_auth_server_package_and_run.md`。

## 3. 安装 supervisor

```bash
sudo apt install -y supervisor
```

新建 `/etc/supervisor/conf.d/gr_auth_server.conf`：

```ini
[program:gr_auth_server]
command=/opt/gr_auth_server/gr_auth_server
directory=/opt/gr_auth_server
user=ubuntu
autostart=true
autorestart=true
stdout_logfile=/opt/gr_auth_server/logs/supervisor_stdout.log
stderr_logfile=/opt/gr_auth_server/logs/supervisor_stderr.log
environment=GR_AUTH_LICENSE_PRIVATE_KEY=""
```

说明：

- `directory=/opt/gr_auth_server` 很关键：配置文件、证书、前端、日志都按工作目录相对路径查找。
- 授权签名私钥建议通过 `environment=GR_AUTH_LICENSE_PRIVATE_KEY="<base64>"` 注入，
  而不是落盘 `certs/auth_license_private.key`；二选一即可（环境变量优先）。
- `sudo` 需免密（部署脚本里直接调 `sudo supervisorctl`）。腾讯云 Ubuntu 镜像默认
  `ubuntu` 用户已有 NOPASSWD sudo，一般无需处理。

```bash
sudo mkdir -p /opt/gr_auth_server/logs
sudo chown -R ubuntu:ubuntu /opt/gr_auth_server
sudo supervisorctl reread && sudo supervisorctl update
```

## 4. 准备配置与证书

```bash
sudo mkdir -p /opt/gr_auth_server/certs
```

- **配置文件**：把 `rust_server/gr_auth_server/src/gr_auth_server_settings.toml`
  拷到 `/opt/gr_auth_server/gr_auth_server_settings.toml`，然后修改：
  - `server_port`（默认 30400）
  - `db_path`（默认 `mongodb://localhost:27017/`，同机部署不用改）
  - `verify_server`：**会烧进每条授权的 deploy_str 给客户端回连**，填对外正式地址，
    确定后不要轻易改，改了影响存量客户端
  - `[bootstrap]` 的 `admin_password` / `visitor_password`（模板里的是示例值，必须改）
- **TLS 证书**：`certs/cert.pem` + `certs/key.pem`。自签名证书 SAN 只有 localhost，
  生产环境应换成域名正式证书（保持文件名不变）。
- **授权签名密钥对**：`certs/auth_license_private.key` / `auth_license_public.key`。
  私钥丢失或轮换会导致**所有已签发授权验签失败**，部署后务必备份；
  公钥需同步给 CMS（`GR_AUTH_LICENSE_PUBLIC_KEY`）。

## 5. nginx 反代（可选但推荐）

让外部走 443 + 正式证书，30400 只留本机：

```nginx
server {
    listen 443 ssl;
    server_name auth.example.com;

    ssl_certificate     /etc/nginx/ssl/auth.example.com.pem;
    ssl_certificate_key /etc/nginx/ssl/auth.example.com.key;

    location / {
        proxy_pass https://127.0.0.1:30400;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    }
}
```

注意后端只讲 HTTPS（rustls），所以 `proxy_pass` 是 `https://`；
nginx 默认不校验上游证书，自签名也能代理。

## 6. 首次部署

**部署前必须先把前端编译好**。musl 构建不会现场编译 `web/gr_auth`，
它用的是仓库里预编译的 `rust_server/gr_auth_server/assets/`，
所以每次部署前（无论首装还是日常更新）都要先做这一步，否则会发布旧页面：

```bash
cd web/gr_auth && npm run build && cd ../..
rm -rf rust_server/gr_auth_server/assets/*
cp -r web/gr_auth/dist/* rust_server/gr_auth_server/assets/
```

然后在本地（Windows + Git Bash）执行部署：

```bash
bash rust_server/scripts/deploy_auth_server.sh
```

脚本会交叉编译 musl 静态二进制、上传二进制与 `web_auth/`、`supervisorctl` 重启并验证。
它从 `rust_server/gr_auth_server/tencent_server.txt` 读取 SSH 凭据（该文件不入库），
目标 IP 写在脚本里，换服务器时改脚本顶部的 `SERVER=`。

## 7. 部署后检查

```bash
sudo supervisorctl status gr_auth_server
curl -sk -o /dev/null -w '%{http_code}\n' https://127.0.0.1:30400/
tail -f /opt/gr_auth_server/logs/gr_auth_server/*.log
```

首次启动会用 `[bootstrap]` 里的账号建管理员/访客（库里已有 admin 时忽略），
之后请立即登录后台改掉初始密码。

## 8. 运维注意事项

- **重启即全量登出**：`jwt_secret` 每次启动自动重新生成并写回配置文件，属预期行为。
- **前端不同步风险**：musl 部署用的前端来自仓库里预编译的 `rust_server/gr_auth_server/assets/`，
  不是现场构建 `web/gr_auth`。改了前端后，先把 `web/gr_auth/dist` 内容同步进 `assets/` 再部署。
- **MongoDB 不要暴露公网**：保持 `bindIp: 127.0.0.1`，安全组不放行 27017。
- **授权私钥备份**：`certs/auth_license_private.key`（或注入用的环境变量值）丢了，
  已签发的授权全部作废。
