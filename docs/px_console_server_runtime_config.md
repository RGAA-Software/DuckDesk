# Console PostgreSQL 产品配置与发布

> 2026-09-19。本文只描述当前 `px_console.exe`。没有 Mongo、Redis、旧 TOML、appkey、设备密码、旧 API 或旧端口 fallback。

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
| `PIXELS_CONSOLE_LOCAL_DEVELOPMENT=1` | 仅显式本机开发：监听和 PG 都必须为 loopback，才允许无 TLS |

配置缺失、未知格式、私有文件权限过宽、静态目录无效、数据库身份/schema/deployment 不匹配，都会在监听前失败。
数据库 authority 丢失后当前进程终止；监督器可以启动新进程，但同一进程不会重新取得权威继续服务。

## 全新部署

1. 用独立 owner DSN 执行 `px_db migrate console`；再用 runtime DSN 执行 `px_db check console`。
2. 在仅服务身份、SYSTEM、Administrators 可访问的目录中准备两个尚不存在的文件路径，设置
   `PIXELS_CONSOLE_GUEST_SOURCE_KEY`、`PIXELS_CONSOLE_WORKSPACE_KEY` 和新的 `PIXELS_CONSOLE_WORKSPACE_KEY_ID`，执行
   `px_console_admin generate-secrets`。工具使用 create-new，失败不覆盖已有密钥。
3. 把输出的 UUID 配为 `PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY`，并把对应 `{id,path}` 写入
   `PIXELS_CONSOLE_WORKSPACE_KEYS`。撤下 `PIXELS_CONSOLE_WORKSPACE_KEY` 这个仅生成工具使用的变量。
4. 使用 owner DSN、`PIXELS_CONSOLE_INITIAL_USERNAME` 和私有 `PIXELS_CONSOLE_INITIAL_PASSWORD_FILE` 执行
   `px_console_admin bootstrap`。只允许全新空库成功一次，并发初始化只有一个胜者。
5. 创建仅服务身份、SYSTEM、Administrators 可访问的空缓存目录，设置 `PIXELS_DEPLOYMENT_ID` 和
   `PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY`，执行 `px_console_admin initialize-recording-cache`。工具只初始化空目录、写入
   deployment 身份且从不覆盖；复制其他部署的目录或手工创建标记都会被拒绝。
6. 撤下 owner 和初始化口令，设置 runtime DSN、TLS、Origin、缓存限额及上表其余变量，启动 `px_console.exe`。

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
- 正式发行：`scripts\package_px_console_server.bat`。它独立提升 Console 版本，运行前端合同测试和生产构建，编译 PostgreSQL
  `px_console.exe`、`px_console_admin.exe`、`px_db.exe`，输出新的 `output\px_console\releases\<run-id>`。

发行脚本永不删除既有 release，不覆盖部署目录，不生成或复制密钥/证书/口令。`release.json` 固定声明 PostgreSQL 和环境配置，
`sha256.json` 覆盖包内全部先前文件；包内容门禁拒绝旧 `px_console.toml`、ZLMediaKit、Coturn/TURN sidecar。

## 升级与回退

先停止准入并等待请求收敛，停止 Console 以释放共享 schema 锁，完成三库协调备份，再由 owner 运行新包的 `px_db migrate console`。
新 release 与旧 release 并列放置，监督器只切换可执行文件和 `static` 路径；私有配置及密钥路径保持在包外。
启动后检查 ready、管理员登录、节点重连及关键目录。若迁移已经执行，不能只换回旧二进制；必须按恢复计划恢复匹配的三库、密钥与
外部见证后再启动旧 release。开发中的产品不提供旧 schema、旧 API 或旧配置兼容层。

功能与数据库验收以[数据库实施状态](server_database_execution_status.md)为准；打包成功不等于公网 Windows/Web/Android 或最终长测通过。
