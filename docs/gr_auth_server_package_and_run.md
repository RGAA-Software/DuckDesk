# gr_auth_server 打包与运行指南

本指南用于快速把 `gr_auth_server`（授权服务）打包成一份独立可运行的前后端，启动 exe 后即可通过浏览器访问管理后台。

## 1. 前置条件

确保以下工具已安装并加入 `PATH`：

- **Rust / cargo**
- **Node.js / npm**
- **OpenSSL**（项目已自带在 `tools/openssl/`，无需单独安装）
- **MongoDB**（运行时需要，默认连接 `mongodb://localhost:27017/`）

检查命令：

```bash
cargo --version
npm --version
```

> `tools/openssl/` 已包含 portable OpenSSL，脚本会直接使用它生成证书。如果你不小心删掉了这个目录，脚本会自动从网络重新下载。

## 2. 一键打包

在项目根目录执行：

```bash
scripts/package_gr_auth_server.bat
```

脚本会依次完成：

1. 检查 `cargo`、`npm`、`openssl` 是否可用。
2. 在 `output/gr_auth_server/certs/` 生成 **100 年有效期** 的自签名 HTTPS 证书（仅首次）。
3. 编译前端 `web/gr_auth`。
4. 编译后端 `rust_server/gr_auth_server`。
5. 把所有产物整理到 `output/gr_auth_server/`：

```text
output/gr_auth_server/
├── gr_auth_server.exe              # 后端可执行文件
├── gr_auth_server_settings.toml    # 配置文件
├── certs/
│   ├── cert.pem                    # 自签名证书
│   └── key.pem                     # 私钥
└── web_auth/                       # 前端静态资源
    ├── index.html
    └── assets/
```

> 重复执行时，如果证书已存在则跳过生成；前端/后端会重新编译。

## 3. 配置

打包完成后，必须编辑：

```text
output/gr_auth_server/gr_auth_server_settings.toml
```

至少修改以下两项：

```toml
[bootstrap]
# 必填：至少 32 位随机字符串，用于签发登录 token
jwt_secret = "YOUR_RANDOM_SECRET_WITH_AT_LEAST_32_CHARS"

# 必填：首次启动时创建管理员账号的密码
admin_password = "YOUR_ADMIN_PASSWORD"

# 可选：访客账号，密码留空则不创建
visitor_password = ""
```

其他配置按需修改：

```toml
server_port = 30400                         # HTTPS 监听端口
db_path = "mongodb://localhost:27017/"      # MongoDB 连接串
```

## 4. 启动服务

### 4.1 启动 MongoDB

确保 MongoDB 已启动并监听默认端口 `27017`。

### 4.2 启动授权服务

在 `output/gr_auth_server/` 目录下运行：

```bash
gr_auth_server.exe
```

成功启动后命令行会显示：

```text
https.listening on 30400
```

## 5. 访问管理后台

浏览器打开：

```text
https://localhost:30400
```

由于使用的是自签名证书，浏览器会提示“您的连接不是私密连接”或类似安全警告，点击 **高级 -> 继续前往 localhost（不安全）** 即可。

登录：

- 账号：`Admin`（与 `admin_name` 一致，默认 `Admin`）
- 密码：你在 `gr_auth_server_settings.toml` 中设置的 `admin_password`

登录后即可在网页上创建、查询、管理授权。

## 6. 常见问题

### 6.1 证书过期或想换正式证书

替换 `output/gr_auth_server/certs/` 下的 `cert.pem` 和 `key.pem` 为你的正式证书文件即可，保持文件名一致。

### 6.2 修改 jwt_secret 后之前的 token 失效

`jwt_secret` 变更会导致所有已登录用户的 token 失效，需要重新登录。

### 6.3 只想重新编译前端或后端

- 重新编译前端：

```bash
cd web/gr_auth
npm run build
```

- 重新编译后端：

```bash
cd rust_server
cargo build -p gr_auth_server --release
```

然后手动复制到 `output/gr_auth_server/` 对应位置，或直接重新运行 `scripts/package_gr_auth_server.bat`。

### 6.4 端口被占用

修改 `gr_auth_server_settings.toml` 中的 `server_port`，重启服务即可。

## 7. 生产环境建议

- 不要使用自签名证书，替换为可信机构签发的 TLS 证书。
- `jwt_secret` 使用密码生成器生成足够强度的随机字符串。
- 为 MongoDB 启用认证，并修改 `db_path` 使用带用户名密码的连接串。
