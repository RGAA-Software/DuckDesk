# Auth：PostgreSQL 新服务配置与开发

> 2026-09-22。仅描述当前新实现。没有 Mongo、旧 TOML/JWT/HMAC、旧签名或旧 API 回退。

## 运行配置

| 环境变量 | 含义 |
|---|---|
| PIXELS_DATABASE_URL | Auth 专用 runtime PostgreSQL DSN；生产 verify-full，CA 用 sslrootcert |
| PIXELS_DEPLOYMENT_ID | 本 Auth 服务部署 UUID，与数据库身份一致；不是许可证客户的 deployment |
| PIXELS_AUTH_LISTEN | 显式监听地址；无隐式默认端口 |
| PIXELS_AUTH_STATIC_DIRECTORY | 本次构建的 web/px_auth/dist 或发行包 static，必须有 index.html |
| PIXELS_AUTH_SIGNING_KEY | ACL 保护的 PKCS#8 v2 二进制 Ed25519 私钥文件 |
| PIXELS_AUTH_TRUST_STORE | ACL 保护、schema 2 规范 JSON 的许可证公钥 keyring；声明唯一活动 key 与最多 16 个受信公钥 |
| PIXELS_AUTH_TLS_CERT / PIXELS_AUTH_TLS_KEY | 正式环境两者必填；受信证书/私钥，不接受跳过证书验证的客户端方案 |
| PIXELS_AUTH_TRUSTED_PROXY_IP | 可选的单一可信反向代理 IP；只允许该 socket peer 提供恰好一个、无逗号的 `X-Forwarded-For` 客户端 IP |
| PIXELS_AUTH_LOCAL_DEVELOPMENT=1 | 仅显式本机开发：监听及 PG 连接均限 loopback，才允许无 TLS |

私钥不存数据库、不随包分发、不从旧 Base64 文件导入，不因缺失自动生成。
Windows 文件及所有权只允许当前服务身份、SYSTEM、Administrators，其他主体的允许 ACE 拒绝；
Unix 私钥拒绝 group/other 权限。文件类型/权限与读取在同一打开的句柄上检查，拒绝链接/重解析点。
密钥或 keyring 缺失、活动私钥不匹配、PG/schema/deployment 不匹配均在监听前失败并退出非零。keyring 不绑定数据库恢复代际；
数据库恢复安全仍由三库恢复封印独立保证。运行时故障 ready=503，live=204。
业务入口和 ready 只接受最小权限 pixels_auth_runtime，拒绝 owner/超级用户或被授予建表等额外权限的 runtime。
首次管理员初始化是独立 owner 工具，不改变业务启动的账号边界。

首次配置可显式执行 px_auth_admin generate-key：先创建仅服务身份/SYSTEM/Administrators 可访问的目录
（Unix 0700），设置 PIXELS_AUTH_SIGNING_KEY 为其中尚不存在的文件路径。工具检查目录权限、以 create-new
创建并验证 PKCS#8 v2 文件，只输出公钥 hex 与 key_id；已有文件绝不覆盖，写入失败保留半成品供管理员检查。
首次部署和每次轮换把 `PIXELS_AUTH_TRUST_STORE` 设置为一个尚不存在的版本化文件，随后执行
`px_auth_admin create-trust-store`。
该命令从活动私钥派生 `active_key_id`；轮换宽限期可通过 `PIXELS_AUTH_ADDITIONAL_PUBLIC_KEYS`
传入逗号分隔的旧公钥 hex，使已有未过期许可证继续验签。确认消费者已取得新根后，再生成一个不含旧公钥的新文件并切换配置，
旧 key 不再用于新签发；命令始终 create-new，不覆盖当前 keyring。私钥和 keyring 独立备份并纳入恢复审批，全部 key ID 同步记录在备份
外部见证的 `available_key_ids`。数据库恢复代际不会隐式更换许可证签名 key；换 key 是显式运维动作。不要使用仓库的公开测试 seed。

## 初始管理员

1. 使用独立 owner 配置先运行 px_db migrate auth，运行账号没有 DDL 权限。
2. 配置 owner DSN、deployment、PIXELS_AUTH_INITIAL_USERNAME 和 PIXELS_AUTH_INITIAL_PASSWORD_FILE。
   密码文件使用同样的私有文件权限；内容为原样 UTF-8（不自动去换行），密码 12–256 字节。
3. 显式运行 px_auth_admin bootstrap。仅允许 pixels_auth_owner 且 authors 空表；
   并发进程只能一个成功。非空库拒绝，不 upsert、不重置现有账号。
4. 撤下初始化凭据，使用 runtime 启动 px_auth。新账号和重置由登录后的管理员操作。

管理密码只存 Argon2id v19 m=19456,t=2,p=1、随机 16 字节盐、32 字节 hash。
登录统一拒绝信息、dummy verify、4 个有界阻塞计算名额、每账号每分钟 10 次/每来源 30 次与有界桶数量；
未配置可信代理时来源始终是实际 socket peer，不信任任意 `X-Forwarded-For`。配置 `PIXELS_AUTH_TRUSTED_PROXY_IP` 后，仅当 socket peer
与该 IP 完全相同时才读取恰好一个、无逗号且可解析为 IP 的 `X-Forwarded-For`；缺失、重复、链式或非法值返回 400，非可信 peer 的伪造头
被忽略。边缘代理必须覆盖该头为直接客户端地址，不能追加客户端输入。CN 当前 Nginx 使用 `$remote_addr` 覆盖，并只信任 loopback。
会话为 32 字节随机 token，仅存 SHA-256；最长 8 小时，每次检查当前 role/revision/到期/撤销。
退出幂等撤销，改密事务递增授权 revision；在途旧密码校验不能签发新有效会话。

## API 与事务

仅 /api/auth/ 下的新接口；未知 /api 路径 404，不返回 SPA。
认证仅单个 Authorization: Bearer header，不接受 cookie、query token 或旧自定义头。

| 方法与路径 | 边界 |
|---|---|
| POST /sessions；DELETE /session；GET /me | 登录/撤销/当前身份；响应不含 hash |
| GET/POST /authors；PATCH /authors/{id}/password | admin 管理账号，重置需要 expected_revision；不能查看原密码 |
| GET/POST /customers | admin 创建，admin/visitor 去密查询；名称规范化唯一 |
| GET /licenses | admin/visitor UUID 游标分页，返回当前状态及最后签发 wire |
| POST /licenses/issue | admin；request_id + request(create/renew)，先事务提交再返回 |
| POST /licenses/{id}/revoke | admin；expected_revision CAS、撤销与审计同事务 |

列表必须传 limit=1..100，可传 after UUID，无无界全量查询。
签发 terms 只包含 customer、deployment、expires、max_streams 和 services；签发时间使用 Auth 数据库时间。
同作者同 request_id 正文不同返回 409；相同请求返回完全相同已提交 wire。
续期只能调整到期时间、最大 stream 数和授权服务，不能换客户或 deployment。撤销后不能通过旧请求或续期“复活”，但已经交付的
离线签名副本仍有效到自身到期时间。
签发中途失败回滚 license/request/audit；提交结果未知须按原 request_id 重试，不能制造新请求。
签名字节/固定向量见[许可证契约](postgresql_license_contract.md)。

管理网页提供中英、明暗主题、客户/账号/许可证管理，使用 same-origin；无公网 CDN 或硬编码服务器。
只读角色不显示写入入口，服务端仍逐请求鉴权。
Auth 不提供在线许可证验证或通知 outbox。Console 只用受控 keyring 在本地验证 `PXLIC2`；需要缩短撤销收敛时间时签发较短有效期并由
客户运维替换许可证，不能宣称离线副本会即时失效。

## 开发与发布

日常：设置 SQLX_OFFLINE=true，使用快速 Release 执行 cargo check/test，目标为 px_auth_server / px_auth_store / px_license；
target-dir=.cache/pg-cargo；网页 npm --prefix web/px_auth run build / run test:unit。
Vite 开发代理只在显式 PIXELS_AUTH_DEV_TARGET 配置时启用，HTTPS 证书验证不关闭。

隔离验收：pwsh -NoProfile -File scripts/server_validation/postgres.ps1 Test -Linux。
新增查询后显式运行 PrepareQueries 生成 SQLx 元数据，再运行 Test；生成元数据不等于验收通过。
覆盖固定向量、签发竞争/回滚、原生 API/进程、私钥 ACL、独立初始化、网页及数据库中断恢复。
报告状态以[实施与验收状态](server_database_execution_status.md)为准，不以文档描述代替证据。

日常 Linux 聚焦构建使用 `scripts_build/build_px_auth_linux.ps1`；它不升版本，交叉构建 `px_auth`、`px_auth_admin` 和 `px_db`，制品位于
`.cache/px-auth-linux-target/x86_64-unknown-linux-gnu/release/`。目标机使用
[`pixels-auth@.service`](../deploy/systemd/pixels-auth@.service) 以无登录 `pixels-auth` 身份运行，私有环境文件固定为
`/etc/pixels/<deployment-id>/auth/auth.env`，稳定执行入口为 `/opt/pixels/auth/current/bin/px_auth`。systemd unit 文件存在或静态校验通过
不等于部署成功；必须在目标 Linux 上验证 enable/start、重启、数据库断连 fail-closed、恢复及制品摘要。
当前官方 CN 实例的实际边界与短验收见 [CN Auth 部署记录](px_auth_cn_deployment.md)。

正式打包：scripts/package_px_auth_server.bat（release-only，独立 bump Auth 版本）。
输出 output/px_auth/releases/<run-id>/，包含 px_auth.exe、px_auth_admin.exe、px_db.exe、
static、运行/签名契约与 SHA-256 清单。打包不生成/复制 TLS 或签名私钥，不覆盖运行配置或部署目录。
安装部署/SCM/独立恢复与 Console 消费者尚须后续阶段验收；本轮日常测试没有触发正式发行或公网部署。
