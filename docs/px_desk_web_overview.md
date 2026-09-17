# Desk 站点：开发与部署

> 2026-09-17。新 PostgreSQL 实现；旧 Mongo、密码文件、固定双端口及旧 API 不再适用。
> 当前功能证据以[数据库执行状态](server_database_execution_status.md)为准，不以本页替代 DB0–DB5 验收。

## 职责与结构

Desk 提供咨询/问题反馈、管理列表和版本元数据；不承担 Console 调度或许可证签发。
数据库字段、权限及新 API 见[Desk 契约](postgresql_desk_contract.md)。

- Rust：`rust_server/px_desk_server`。入口 main 只装配配置、PG、HTTP/TLS 和退出；auth、model、store、handlers 分工。
- Vue：`web/px_desk`。门户及产品页保留；`ContactUs.vue` 提交咨询，`MainPage.vue` 提交问题；
  `/admin` 换取短期会话，`/admin/panel` 分页查看及 CAS 标记、服务端撤销退出。
- 匿名提交只返回 UUID receipt；相同请求 ID/正文重试幂等，管理会话方可读取联系方式。
- 浏览器只保存短期会话，不保存原始管理凭据。所有 API 同源；未知 `/api` 路由 404，不转发旧 API。
- 版本是元数据，不代表未来自动升级签名、回滚和安装已实现。

## 启动配置

全部必需项显式提供，无自动生成默认管理员、监听端口或数据库地址：

| 环境变量 | 含义 |
|---|---|
| PIXELS_DATABASE_URL | pixels_desk_runtime 的 PG URL；不要打印、写入仓库或放在进程参数 |
| PIXELS_DEPLOYMENT_ID | 本部署 UUID，必须与数据库初始化身份一致 |
| PIXELS_DESK_ADMIN_TOKEN_SHA256 | 独立随机 32 字节生成的 64 字符小写 hex 凭据的 **UTF-8 文本** SHA-256，小写 hex；不是把 hex 解码后再 hash |
| PIXELS_DESK_LISTEN | 显式 IP:端口；测试可使用 127.0.0.1:0 分配端口 |
| PIXELS_DESK_STATIC_DIRECTORY | 已构建的网页目录，必须存在 index.html |
| PIXELS_DESK_TLS_CERT / PIXELS_DESK_TLS_KEY | TLS 证书链、私钥文件；正式部署必填 |
| PIXELS_DESK_LOCAL_DEVELOPMENT | 仅明确值 1 启用本机开发：HTTP 只允许 loopback，PG 也只允许 loopback；不是正式部署的 TLS 绕过开关 |

管理凭据通过安全通道交给管理员；服务只接收其 hash。配置轮换并重启会使之前的会话指纹失效。
正式部署由运维注入环境/密钥；不要把实际 URL、凭据、私钥写入公开示例。

先按[PG 环境说明](../deploy/development/postgres/README.md)初始化三库角色/身份。
使用独立 owner 的 URL 执行 `px_db migrate desk`，随后换成 runtime 的 URL 启动 `px_desk`。
服务进程只校验 schema，不自动 DDL；缺配置、身份不符、checksum 不符均非零退出。
`/health/live` 为进程探针，`/health/ready` 校验 PG/部署/schema；断库请求返回 503。

## 开发编译与测试

在仓库根目录执行（不涨版本，不跑 Windows 全量发行构建）：

```powershell
cargo build --locked --manifest-path rust_server/Cargo.toml -p px_desk_server -p px_pg --target-dir .cache/pg-cargo
npm --prefix web/px_desk run build
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 Test -Linux
```

测试运行器建立独立空 PG，验证 API/原生进程，构建并用真实浏览器提交、登录、标记、退出，
再验证真实 Desk 进程重启与 PG 停机/恢复；只清理本次随机测试容器与卷，不碰其他库。
日志和 hash 在 `test-results/server_validation/<run_id>`；失败、缺环境、未运行不算通过。

前端单独调试：将 `PIXELS_DESK_DEV_TARGET` 设为自己启动的 Desk 地址，然后 `npm --prefix web/px_desk run dev`。
不配置则不代理；不硬编码服务器，不关闭 TLS 证书校验。
正式发布使用 `scripts/package_px_desk_server.bat`，它会完整构建 Desk 并提升其版本，输出新的 `output/px_desk/releases/<批次>`；
含 px_desk、px_db、static 和 SHA-256 清单，不携带真实密钥，不删除既有发布目录。日常调试不运行此入口。

## 部署边界

正式环境使用专属低权限服务账号、受控配置及 HTTPS；不再参考旧主机上的 nohup、目录、Mongo 或端口布局。
既有公网站点没有因本地测试被自动覆盖。新的服务器部署、Windows SCM/定时备份与 DB5 整体验收分别留证。
