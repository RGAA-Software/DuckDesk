# Console PostgreSQL 产品配置与发布

> 2026-09-22。本文只描述当前 `px_console.exe`。没有 Mongo、Redis、旧 TOML、appkey、设备密码、旧 API 或旧端口 fallback。

## 运行配置

Console 只从环境读取配置；发行包不携带真实配置、证书、私钥或数据库口令。

| 环境变量 | 含义 |
|---|---|
| `PIXELS_CONSOLE_DATABASE_URL` | Console 专用 `pixels_console_runtime` PostgreSQL DSN；生产使用 verify-full 和受信 CA |
| `PIXELS_DEPLOYMENT_ID` | 与三库初始化及恢复安全状态一致的非 nil 部署 UUID |
| `PIXELS_CONSOLE_LISTEN` | 显式监听 `IP:port`；无默认端口，禁止 20371 |
| `PIXELS_CONSOLE_STATIC_DIRECTORY` | 发行包的 `static` 绝对路径，必须包含普通文件 `index.html` |
| `PIXELS_CONSOLE_TLS_CERT` / `PIXELS_CONSOLE_TLS_KEY` | 正式环境必须同时提供的证书链和私钥路径 |
| `PIXELS_CONSOLE_PUBLIC_ORIGIN` | 浏览器唯一允许的规范 HTTPS Origin，不从 Host 或转发头推导 |
| `PIXELS_CONSOLE_REGISTRATION` | `0` 或 `1`，是否开放用户注册 |
| `PIXELS_CONSOLE_GUESTS` | `0` 或 `1`，是否开放访客签发 |
| `PIXELS_CONSOLE_SESSION_LIFETIME_SECONDS` | 登录会话期限，必须在实现规定的有界范围内 |
| `PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS` | 访客期限，60–86400 秒 |
| `PIXELS_CONSOLE_GUEST_SOURCE_KEY` | 32 字节私有来源 HMAC 密钥文件 |
| `PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY` | 当前工作区加密密钥 UUID |
| `PIXELS_CONSOLE_WORKSPACE_KEYS` | 最多 32 个 `{id,path}` 的严格 JSON 数组；包含活动密钥及轮换期旧密钥 |
| `PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY` | 已由管理工具初始化、绑定当前 deployment 的私有录像缓存目录 |
| `PIXELS_CONSOLE_RECORDING_CACHE_BYTES` | 缓存字节上限，1 MiB–1 TiB；占用和预留容量均纳入限制 |
| `PIXELS_CONSOLE_RECORDING_CACHE_DOWNLOADS` | 同时下载上限，1–32；超过上限直接拒绝而非无界排队 |
| `PIXELS_CONSOLE_RECORDING_CACHE_TTL_SECONDS` | 非保留缓存有效期，60–604800 秒 |
| `PIXELS_CONSOLE_DISTRIBUTION` | 必填 `official`、`customer` 或 `oem`；不按缺失字段推断发行类型 |
| `PIXELS_CONSOLE_RELEASE_NAMESPACE` | 必填精确发行命名空间：`pixels.official`、`pixels.customer` 或 `oem.<oem_id>` |
| `PIXELS_CONSOLE_OEM_ID` | OEM 必填规范 ID；Official/Customer 必须不配置 |
| `PIXELS_CONSOLE_LICENSE_TRUST_STORE` | 权限收紧、规范编码的 Auth 公钥信任根文件；不信任许可证或下载响应携带的 key |
| `PIXELS_CONSOLE_LICENSE_FILE` | 唯一接受的 `PXLIC2` 许可证文件；不解析旧开发格式或 deploy 字符串 |
| `PIXELS_CONSOLE_MINIMUM_CLIENT_BUILD` | 平台允许接入的最低客户端 build，必须大于零 |
| `PIXELS_CONSOLE_LOCAL_DEVELOPMENT=1` | 仅显式本机开发：监听和 PG 都必须为 loopback，才允许无 TLS |

配置缺失、未知格式、私有文件权限过宽、静态目录无效、数据库身份/schema/deployment 不匹配，都会在监听前失败。
数据库 authority 丢失后当前进程终止；监督器可以启动新进程，但同一进程不会重新取得权威继续服务。
现有独立 Console Linux 服务入口使用 `deploy/systemd/pixels-console@.service`：非零退出由 systemd 在 5 秒后重启，正常 SIGTERM 则有界关闭监听、任务和连接池，
不会被当成崩溃重启。实例 `%i` 是 deployment UUID；私有环境文件固定在 `/etc/pixels/%i/console.env`，可写运行状态固定在
`/var/lib/pixels/%i/console`。不要再套一层进程守护器，也不要把密钥值写入 unit 文件。

该独立入口的脚本位于 `scripts/server_console/install_linux_service.sh` 与 `uninstall_linux_service.sh`。安装器只接受小写 deployment UUID、绝对普通可执行文件和
权限不宽于 `0600` 的绝对环境文件；环境文件必须含与参数完全相同的 `PIXELS_DEPLOYMENT_ID`，未知的非赋值行、符号链接、宽权限文件和同机另一活动
Console 实例均在覆盖前拒绝。安装器先确认当前实例已停止，再原子替换 `/opt/pixels/current/bin/px_console`，把私有环境以
`pixels-console:pixels-console 0400` 安装，最后 enable/start 并检查 active。注销只停止并禁用指定实例，保留私有环境和运行数据，供升级回退、审计或
显式授权的后续清理使用。

私有 Customer Linux Server 套件另带 `pixels-private-console@.service` 及按组件安装/停用脚本；它把 Console、Relay、Desk 的当前 release
指向分别管理，Console 静态路径固定为 `/opt/pixels/private/<deployment-uuid>/current-console/static/console`，环境文件为
`/etc/pixels/<deployment-uuid>/private-console.env`。正式套件 `1.0.2` 的操作顺序见[私有部署操作入口](private_server_install_guide.md)，
已验收范围见[部署与升级计划](server_deployment_and_upgrade_plan.md)；不要将独立 Console 开发入口的 `/opt/pixels/current` 路径与私有部署路径混用。

## 全新部署

1. 用独立 owner DSN 执行 `px_db migrate console`；再用 runtime DSN 执行 `px_db check console`。
2. 在仅服务身份、SYSTEM、Administrators 可访问的目录中准备两个尚不存在的文件路径，设置
   `PIXELS_CONSOLE_GUEST_SOURCE_KEY`、`PIXELS_CONSOLE_WORKSPACE_KEY` 和新的 `PIXELS_CONSOLE_WORKSPACE_KEY_ID`，执行
   `px_console_admin generate-secrets`。工具使用 create-new，失败不覆盖已有密钥。
3. 把输出的 UUID 配为 `PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY`，并把对应 `{id,path}` 写入
   `PIXELS_CONSOLE_WORKSPACE_KEYS`。撤下 `PIXELS_CONSOLE_WORKSPACE_KEY` 这个仅生成工具使用的变量。
4. 使用 owner DSN、`PIXELS_CONSOLE_INITIAL_USERNAME` 和私有 `PIXELS_CONSOLE_INITIAL_PASSWORD_FILE` 执行
   `px_console_admin bootstrap`。只允许全新空库成功一次，并发初始化只有一个胜者。
5. 配置绑定当前 deployment 的 `PXLIC2` 许可证及签发 Auth 的规范公钥信任根。Console 本地验签并检查 deployment、到期时间、服务集合和
   stream 上限；不连接 Auth 在线验证，不创建许可证水位或机器绑定。许可证替换由运维原子覆盖受控文件并重启 Console。
6. 创建仅服务身份、SYSTEM、Administrators 可访问的空缓存目录，设置 `PIXELS_DEPLOYMENT_ID` 和
   `PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY`，执行 `px_console_admin initialize-recording-cache`。工具只初始化空目录、写入
   deployment 身份且从不覆盖；复制其他部署的目录或手工创建标记都会被拒绝。
7. 撤下 owner 和初始化口令，设置 runtime DSN、TLS、Origin、缓存限额及上表其余变量，启动 `px_console.exe`。

Console 在打开数据库监听前完成本地许可证准入，并在业务请求和 readiness 中继续检查到期时间。Official、Customer 与 OEM 使用同一签名
契约；许可证只绑定 Console deployment，不绑定发行类型、机器或 OEM。撤销阻止 Auth 后续续期，但已交付副本在签名有效期内保持可用。

管理员可用 `GET /api/console/managed/license` 查看 license ID、revision、到期时间、`max_streams` 和 services；普通用户、访客和节点身份无权读取。
该接口只反映当前本地已验签许可证，不触发 Auth 请求，也不返回发行、模式、机器或在线新鲜度字段。

额度不是 UI 提示：未关闭资源会话达到 `max_streams` 后，新会话事务以 advisory lock 串行化并拒绝最后名额竞争者；设备登记数量不受许可证限制。
`cloud_applications` 允许 game-hook/webview，`desktop` 允许桌面目标，`rdp` 允许 RDP 应用；
实例预约、资源会话创建和连接授权签发都会检查对应 service。幂等重试可以返回已经提交的原结果，但不会创建新资源或签发新 grant。
Service 不读取 `PXLIC2`，只执行通过 Console 数据库事务与当前 control epoch 下发的命令，避免形成第二个额度权威。

## 客户端端点与 TLS

客户端只使用正常 HTTPS/TLS 验证服务器身份，不再使用自定义部署证书、签名 descriptor、challenge/proof 或客户端持久化身份水位。
Official 构建固定连接 Pixels 官方 Console，设置页不能改写；Customer 构建由管理员填写自己的私有部署地址，并必须拒绝已知官方地址。
OEM 使用自身发行配置提供的私有地址策略。私有部署的 CA 和 HTTPS 证书可由客户自行生成或自签，不要求公有 CA；
客户负责使其终端信任所用证书。用户名、密码、Cookie 和 token 仍沿用当前 HTTPS 连接路径，本阶段不做证书签发来源的专项验证。

客户端通过版本化 API 获取业务能力和当前 Render 连接描述；这些响应不承担第二套服务器 PKI。Windows Client、Web Client、Android、Panel、
Service、Render 与 Console 必须共同遵守同一端点来源和 API 版本窗口，不能恢复已退役的 `PXDC2`、`PXDD2` 或 `PXDP1` 路径。

生产服务账号不能获得 owner、DDL、跨库或私钥目录外权限。密钥不写数据库、不随发行包分发、不因缺失自动生成。

## 录像缓存与下载

本人入口 `POST /api/console/recordings/{recording_id}/cache` 和管理员入口
`POST /api/console/managed/recordings/{recording_id}/cache` 请求缓存；响应只返回 `fetching` 或 `ready` 及稳定元数据，不返回磁盘路径、
内部 lease 或节点凭据。对应 `GET .../download` 只接受当前 bearer 与终端身份，下载前再次检查原登录、录像 owner/ACL、文件版本、
内容 SHA-256 和当前缓存运行代际。

下载支持完整文件和单一 byte range；多 range、非法或越界 range 返回 416。服务端以 64 KiB 有界分块和小容量队列传输，读租约在慢速
接收期间持续短周期续期；退出、改密、撤权、取消或文件证明变化都会停止后续读取。文件名固定为
`recording-<uuid>.mp4`，不会采用数据库或请求提供的路径。缓存清理任务每 30 秒有界处理废弃 attempt；retain/evict 和物理文件锁仍由
数据库协调器裁决。

管理员通过 `GET /api/console/managed/recording-cache` 查看缓存状态，以 `PATCH .../recordings/{id}/cache` 携带精确 revision 设置或取消
保留，以 `DELETE .../recordings/{id}/cache?revision=<revision>` 驱逐 Console 副本。驱逐不会删除节点原录像；保留副本、旧 revision、
活跃读取租约或无法取得物理独占锁都会 fail-closed。删除顺序固定为数据库进入不可逆 deleting、精确文件删除、删除证明回写 deleted，
中间失败由既有对账/清理流程继续收敛，不返回假成功。

节点通过已认证的控制长连接轮询待取录像；Console 返回最长 30 秒、一次使用、绑定当前 node generation/cache attempt/source
identity 的上传能力。大文件不进入控制 WebSocket，而是 PUT 到同一 Console Origin 下的固定
`/api/console/node-recording-cache/<attempt-id>`，请求必须使用 `application/octet-stream`、精确 Content-Length 且不得携带浏览器、Origin、
代理或查询参数身份。Console 在读取正文前重新验证当前节点权威，边收边校验大小和 SHA-256，成功 sync/rename 后才提交 ready。

Render 只在 MP4 trailer、flush、close 和 `.recording` 标记移除全部成功后，通过带临时 bearer 的本机类型化 IPC 发送完成段事件。
事件携带文件 basename、codec 和可证明的 resource-session UUID；同一录像段出现未知或不同会话时，该段永久降级为
`session_id=null`，绝不使用设备、账号或当前连接猜测 owner。Service 只登记此事件明确完成的文件，不把目录中裸 MP4 自动纳入清单。

Service 从 `C:\Users\Public\Pixels\px_render_records` 重新校验已登记的直接子文件；存在 `.recording` 标记、重解析点、非普通文件、
非 MP4、空文件或超过 1 TiB 的对象不会上报/上传。Service 在自己的 `px_data` 中原子持久化 source UUID、session UUID、codec、hash、
sequence 和 present 状态，重连时重新向当前 generation 报告，文件消失或被替换时显式报告撤下。Render 对未确认事件在当前进程内
有界保留并在 IPC 重连后重发，Service 对相同完成元数据幂等确认；冲突元数据 fail-closed。具有唯一 Cloud Application session 的录像
可以进入本人授权链，无法唯一归属的录像只允许管理员链使用。

## 构建与发行

- 日常后端/前端聚焦构建：`scripts_build\build_px_console_server.bat`，输出到 `output\px_console\dev`，不提升版本。
- 仅更新 Console Web：`scripts_build\build_console_web.bat`，同步到 `output\px_console\dev\static` 并逐文件校验哈希。
- Windows/WSL2 进程与 Linux SIGTERM 聚焦短测：
  `scripts\server_validation\postgres.ps1 TestSuite -Suite console-process -Linux`；它使用一次性数据库，不替代正式目标发行版 systemd 验收。
- WSL2 systemd 安装生命周期短测：`scripts\server_validation\linux_console_systemd.ps1`；它验证错误 deployment/权限拒绝、专用身份、覆盖升级、
  restart、SIGTERM 和保留数据注销。该脚本使用可观察信号的验证进程测试安装层；真实 Console 进程行为由上一条测试负责，两者都不替代目标发行版 VM。
- 正式发行：`scripts\package_px_console_server.bat`。它独立提升 Console 版本，运行前端合同测试和生产构建，编译 PostgreSQL
  `px_console.exe`、`px_console_admin.exe`、`px_db.exe`，输出新的 `output\px_console\releases\<run-id>`。

发行脚本永不删除既有 release，不覆盖部署目录，不生成或复制密钥/证书/口令。`release.json` 固定声明 PostgreSQL 和环境配置，
`sha256.json` 覆盖包内全部先前文件；包内容门禁拒绝旧 `px_console.toml`、ZLMediaKit、Coturn/TURN sidecar。

## 升级与回退

先停止准入并等待请求收敛，停止 Console 以释放共享 schema 锁，完成三库协调备份，再由 owner 运行新包的 `px_db migrate console`。
新 release 与旧 release 并列放置，监督器只切换可执行文件和 `static` 路径；私有配置及密钥路径保持在包外。Linux unit 只在非零
退出时重启，因此数据库 lease、许可证到期或其他运行权威失效会触发新进程重新完成全部启动门禁；正常维护停止不会自启。
启动后检查 ready、管理员登录、节点重连及关键目录。若迁移已经执行，不能只换回旧二进制；必须按恢复计划恢复匹配的三库、密钥与
外部见证后再启动旧 release。开发中的产品不提供旧 schema、旧 API 或旧配置兼容层。

功能与数据库验收以[数据库实施状态](server_database_execution_status.md)为准；打包成功不等于公网 Windows/Web/Android 或最终长测通过。
