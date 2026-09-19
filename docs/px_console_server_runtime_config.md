# Console PostgreSQL 产品配置与发布

> 2026-09-20。本文只描述当前 `px_console.exe`。没有 Mongo、Redis、旧 TOML、appkey、设备密码、旧 API 或旧端口 fallback。

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
| `PIXELS_CONSOLE_DISTRIBUTION` | 必填 `official` 或 `customer`；不按缺失字段推断发行类型 |
| `PIXELS_CONSOLE_MACHINE_SHA256` | 当前 Console 机器身份的 lowercase hex64，必须与许可证绑定一致 |
| `PIXELS_CONSOLE_LICENSE_AUTHORITY_DEPLOYMENT_ID` | 预置的许可证签发 Auth deployment UUID，必须与信任根一致 |
| `PIXELS_CONSOLE_LICENSE_TRUST_STORE` | 权限收紧、规范编码的 Auth 公钥信任根文件；不信任许可证或下载响应携带的 key |
| `PIXELS_CONSOLE_LICENSE_FILE` | 唯一接受的 `PXLIC1` 许可证文件；不解析旧 deploy 字符串 |
| `PIXELS_CONSOLE_LICENSE_STATE_DIRECTORY` | 数据库/备份之外的私有水位目录，保存 license revision、可信时间及 Auth recovery generation |
| `PIXELS_CONSOLE_AUTH_VERIFY_URL` | 仅 Official 必填，固定为 HTTPS `/api/auth/licenses/verify`；Customer 必须完全不配置；本机开发可用 loopback HTTP |
| `PIXELS_CONSOLE_AUTH_VERIFY_CA` | Official 可选的 Auth 私有 CA PEM；存在时只加入该 HTTPS 客户端的信任根，仍执行主机名与证书链校验；Customer 禁止配置 |
| `PIXELS_CONSOLE_DEPLOYMENT_CERTIFICATE` | Pixels 离线根签发的 `PXDC1` 部署证书；绑定 deployment UUID、official/private 类别、部署公钥、版本和有效期 |
| `PIXELS_CONSOLE_DEPLOYMENT_SIGNING_KEY` | 与部署证书公钥匹配的 Ed25519 PKCS#8 私钥；仅用于平台描述和在线 nonce 证明，不用于许可证或用户 token |
| `PIXELS_CONSOLE_DEPLOYMENT_TRUST_STORE` | 规范编码的 Pixels 部署根公钥集合；不接受发现响应自行携带的新根 |
| `PIXELS_CONSOLE_DEPLOYMENT_CERTIFICATE_VERSION` | 本部署允许的最低证书版本，必须大于零；用于撤下旧证书 |
| `PIXELS_CONSOLE_DESCRIPTOR_REVISION` | 平台描述单调 revision，必须大于零；端点/策略变化时提升，不允许回退 |
| `PIXELS_CONSOLE_DEPLOYMENT_TRUST_EPOCH` | 部署信任材料水位，必须与信任根文件一致且大于零 |
| `PIXELS_CONSOLE_MINIMUM_CLIENT_BUILD` | 平台允许接入的最低客户端 build，必须大于零 |
| `PIXELS_CONSOLE_LOCAL_DEVELOPMENT=1` | 仅显式本机开发：监听和 PG 都必须为 loopback，才允许无 TLS |

配置缺失、未知格式、私有文件权限过宽、静态目录无效、数据库身份/schema/deployment 不匹配，都会在监听前失败。
数据库 authority 丢失后当前进程终止；监督器可以启动新进程，但同一进程不会重新取得权威继续服务。
Linux 发行使用包内 `deploy/systemd/pixels-console@.service`：非零退出由 systemd 在 5 秒后重启，正常 SIGTERM 则有界关闭监听、任务和连接池，
不会被当成崩溃重启。实例 `%i` 是 deployment UUID；私有环境文件固定在 `/etc/pixels/%i/console.env`，可写运行状态固定在
`/var/lib/pixels/%i/console`。不要再套一层进程守护器，也不要把密钥值写入 unit 文件。

## 全新部署

1. 用独立 owner DSN 执行 `px_db migrate console`；再用 runtime DSN 执行 `px_db check console`。
2. 在仅服务身份、SYSTEM、Administrators 可访问的目录中准备两个尚不存在的文件路径，设置
   `PIXELS_CONSOLE_GUEST_SOURCE_KEY`、`PIXELS_CONSOLE_WORKSPACE_KEY` 和新的 `PIXELS_CONSOLE_WORKSPACE_KEY_ID`，执行
   `px_console_admin generate-secrets`。工具使用 create-new，失败不覆盖已有密钥。
3. 把输出的 UUID 配为 `PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY`，并把对应 `{id,path}` 写入
   `PIXELS_CONSOLE_WORKSPACE_KEYS`。撤下 `PIXELS_CONSOLE_WORKSPACE_KEY` 这个仅生成工具使用的变量。
4. 使用 owner DSN、`PIXELS_CONSOLE_INITIAL_USERNAME` 和私有 `PIXELS_CONSOLE_INITIAL_PASSWORD_FILE` 执行
   `px_console_admin bootstrap`。只允许全新空库成功一次，并发初始化只有一个胜者。
5. 配置部署绑定的 `PXLIC1` 许可证、签发 Auth 的规范信任根及机器 hex64。创建仅服务身份、SYSTEM、Administrators 可访问的
   独立空水位目录并设置 `PIXELS_CONSOLE_LICENSE_STATE_DIRECTORY`。Official 必须配置自己的 Auth HTTPS verify URL；Customer
   必须不配置任何 Auth URL，也不能把官方路径作为可填服务器。首次有效验证会 create-new 水位，后续只能提高 revision/可信时间；
   Auth recovery generation 改变时必须走恢复准入/轮换流程，进程不会自行重置水位。
6. 先用 `px_console_admin generate-deployment-key` 在目标部署 create-new 部署私钥，再由不进入任何产品安装包的离线
   `px_deployment_authority` 为同一 `PIXELS_DEPLOYMENT_ID` 签发部署证书；配置证书、部署私钥、部署根信任文件和四个单调版本变量。
   `official` 许可证必须匹配 `official` 部署证书，`customer` 必须匹配 `private`；数据库 UUID、证书 UUID、私钥公钥或 trust epoch
   任一不一致都在监听前失败。部署证书与商业许可证是两套独立 wire，不得共用密钥或把许可证当平台身份证明。
   完整的根初始化、轮换和验收见[部署身份离线签发与安装](deployment_identity_provisioning.md)。
7. 创建仅服务身份、SYSTEM、Administrators 可访问的空缓存目录，设置 `PIXELS_DEPLOYMENT_ID` 和
   `PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY`，执行 `px_console_admin initialize-recording-cache`。工具只初始化空目录、写入
   deployment 身份且从不覆盖；复制其他部署的目录或手工创建标记都会被拒绝。
8. 撤下 owner 和初始化口令，设置 runtime DSN、TLS、Origin、缓存限额及上表其余变量，启动 `px_console.exe`。

Console 在打开数据库监听前完成许可证准入。Official 必须先由 Auth 数据库时钟在线确认当前 revision 且未撤销，再以本地受控信任根
复核同一 wire；Customer 完全离线验签，因此只能以导入的许可证 revision/有效期和库外水位为界，不能宣称获知尚未导入的官方撤销。
所有 API 请求和 readiness 在返回前后都会重查可信时间与到期时间，时钟回拨或到期立即 fail-closed。水位目录出现未知文件、
不完整原子替换或 deployment/发行/机器/Auth generation 不一致时拒绝启动，不猜测修复。

Official 启动后每 30 秒重新调用同一精确 `/verify`；成功响应才推进内存和库外可信时间。最后一次成功后 40 秒仍不能重新确认、收到撤销、
revision 已被续期替换、响应绑定不符或本机时钟回拨时，所有 API/readiness 立即拒绝，监督任务取消 Console 并让进程非零退出。
Customer 不创建在线任务。管理员可用 `GET /api/console/managed/license` 查看 license ID/revision、发行、模式、到期、额度、features、
最后权威时间和 Official 在线新鲜度截止时间；普通用户、访客和节点身份无权读取。

额度不是 UI 提示：活动设备目录达到 `max_devices` 后创建事务拒绝；未关闭资源会话达到 `max_sessions` 后新会话事务拒绝，两个计数都以
事务 advisory lock 串行化最后名额。`cloud_applications` 允许 game-hook/webview，`desktop` 允许桌面目标，`rdp` 允许 RDP 应用；
实例预约、资源会话创建和 descriptor 签发都会检查对应 feature。幂等重试可以返回已经提交的原结果，但不会创建新资源或签发新 grant。
Service 不读取 `PXLIC1`，只执行通过 Console 数据库事务与当前 control epoch 下发的命令，避免形成第二个额度权威。

## 平台发现与持有证明

`GET /.well-known/pixels` 返回 `certificate_wire` 与最长 300 秒的 `descriptor_wire`。证书由 Pixels 离线部署根签名；描述由当前部署
私钥签名，固定声明 deployment UUID、official/private 类别、descriptor revision、trust epoch、最低客户端 build、协议范围、认证方式、
注册策略及相对 API 路径。客户端必须先完成系统/企业 CA 的 TLS 主机名和证书链校验，再使用发行内置或管理员导入的部署根验证两个 wire；
Customer 只接受 `private`，Official 只接受 `official`。不能只检查 JSON 中的类别字符串、域名、IP、User-Agent 或静态 secret。

随后客户端向 `POST /.well-known/pixels/challenge` 发送 32 字节随机数的规范 base64url nonce 和刚验证的 descriptor revision；Console 返回
最长 30 秒的 `proof_wire`。客户端验证同一 deployment 公钥、nonce、revision 和有效期后，才可发送用户名、密码、Cookie、节点 token 或
其他凭据。旧 revision、旧 trust epoch、过期证书/描述/证明、未知字段、非规范编码、篡改签名和错误发行全部 fail-closed。Android 已消费
该协议并在账号/guest/所有 bearer 请求前验证，持久化 deployment/certificate/descriptor/trust 单调水位；Official 固定端点、Customer
私有端点及两种独立 applicationId/输出沙箱也已落到构建入口。Windows/Web/Service 的同等消费、正式发行材料和跨端验收仍按 DB5/P0
继续实施，不能把服务端发现接口或 Android 聚焦测试单独记为发行隔离完成。

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
新 release 与旧 release 并列放置，监督器只切换可执行文件和 `static` 路径；私有配置及密钥路径保持在包外。Linux unit 只在非零
退出时重启，因此数据库 lease、许可证在线 currentness 或其他运行权威失效会触发新进程重新完成全部启动门禁；正常维护停止不会自启。
启动后检查 ready、管理员登录、节点重连及关键目录。若迁移已经执行，不能只换回旧二进制；必须按恢复计划恢复匹配的三库、密钥与
外部见证后再启动旧 release。开发中的产品不提供旧 schema、旧 API 或旧配置兼容层。

功能与数据库验收以[数据库实施状态](server_database_execution_status.md)为准；打包成功不等于公网 Windows/Web/Android 或最终长测通过。
