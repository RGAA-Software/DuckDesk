# Console PostgreSQL 产品接入契约

> 2026-09-17 建立，2026-09-19 完成正式产品入口切换。当前 `px_console.exe` 由 PostgreSQL 组合根构建；本页仍包含后续
> DB2–DB5 产品链验收约束，不能把二进制切换本身当作全部功能已经验收。

## 1. 切换边界

只提供一套新的 `/api/console` 契约。删除旧 `/api/v1/*`、`/cms/*`、appkey/设备密码式身份、
默认字段补齐和配置 fallback；不搭建同时运行的旧/新 Console。产品依赖树移除 Mongo/BSON 和引入它们的旧基础包。
Desk/Auth 已有新接口不再次套兼容层。Windows、Android、网页和节点生产者同步改用新 UUID、显式 owner/target 和连接描述符。

按领域分模块接入，组合根只负责：严格配置与私有文件读取 → schema/账号/部署检查 → 活跃控制者 → 密钥/许可/恢复准入 →
ConsoleDatabase 单池 → 业务域与有界执行器 → HTTP/WS → 后台任务。失败逐层逆序关闭，不带半初始化对象开始监听。
后台任务持有取消令牌和受监督的 JoinHandle；关闭入口、停止新任务、结束连接后关闭池与私有文件锁。

单活动 Console 必须是运行时约束，不是文档约定：数据库专用连接持有进程活动锁，不把持锁连接归还池；
租约连接失效时取消派发、停止准入并退出。新进程取得锁后才调用 begin_runtime，旧 epoch/generation 不再被接受。
这不引入 Console 多活，也不依赖进程内全局 singleton 作为数据库身份权威。

运行锁基础实现位于 `px_pg::ServiceLease`：本地状态最长 5 秒、单次 probe 最长 1 秒，调用方至少每秒续租；
掉线、过期、权限扩大均使当前句柄永久失效，Drop 同样失效。取得数据库锁后等待完整 5 秒静默窗口并重新验证锁，
再返回可用状态，保证上一进程残存的本地状态先失效。新进程此后仍须取得业务事务 gate 并推进控制 epoch。
2026-09-17 的 Windows 专项六组已通过（含独立 OS 进程、终止 PG backend、二十方争锁）；当时尚未接入 Console 产品组合根。
因六组包含真实接管/到期等待，测试运行器的 Linux 整轮时限由 540 秒调整为 660 秒、外层 720 秒；
没有改变单用例截止或减少断言。跨平台完整回归另行留证。

当前产品实现位于 `rust_server/px_console_server/runtime`，作为最终 Console 产品组合根，不是第二套后端或旧 API 适配器。
它已经连接单池、初始化检查、活动锁续租与取消、控制 epoch，以及身份/用户组、访客、目录和资源入口新路由；
2026-09-19 已正式产出 `px_console.exe`，旧 Mongo 组合根不再参与正式开发构建和发行。
Auth 和新 Console 共用 `px_credentials` 的密码策略及账号/来源限流，原 Auth 内部重复实现已移除。
新增真实路由生命周期测试需要多次完整 5 秒启动静默窗口，Linux 整轮上限随测试扩展为 780 秒，外层 840 秒。
该批已在 `pg-20260917-093329-47b2eafe` 完成两平台回归。加入目录 API 三组与构建后，
后续整轮 Linux 上限调整为 960 秒、外层 1020 秒；原批实际约十二分半，保留构建与新增用例余量，不修改单用例断言。

schema 升级新增独立互斥门禁（正在专项验收）：`DatabaseConfig::connect_runtime` 在每条物理连接上持有共享 schema 锁 `22091701`，
包括空闲池连接；取得锁后再检查最小角色、部署身份、完整版本/checksum。断线后新连接必须重新取锁并重新检查，
不会因独立活动锁后端已断开而让仍存活的业务连接穿过 DDL。普通 `connect` 只用于离线管理和隔离测试基础设施。
迁移工具先取得 SQLx 串行锁，再尝试独占 schema 锁；只要任一业务连接仍在，返回冲突，不执行迁移（包括 no-op）。
因此升级步骤必须停止准入、结束在途请求、关闭所有实例的业务池，再运行迁移和启动新版本。
Auth/Desk 的多个池可同时持有共享锁，不套用 Console 的单活动限制。测试覆盖全部三库、多连接/多池、真实迁移进程、
旧进程在 DDL 中/后重连、进程被杀、启动取消和锁释放；此基础门禁不替代 DB4 的备份屏障/升级编排与恢复准入。

## 2. 身份和入口

每个入口以严格 DTO 拒绝未知/缺失字段；错误采用稳定 code 和标准 HTTP 状态，不返回 SQL、DSN、口令或内部密文。
客户端明确声明 `client_type=panel/android/user_web/admin_web`，服务器检查它与已签发会话一致，不由请求头提升管理权限。
新浏览器接口统一 bearer，不接受 cookie、查询字符串 token 或旧自定义头作为第二条认证路径；
管理网页不保存原始密码，避免在 URL、日志、事件中携带会话令牌。原生客户端令牌进入平台受保护存储。
浏览器请求校验配置的公开 Origin；不从未经信任的 Host/X-Forwarded-* 推导可信来源。代理来源解析必须有明确代理白名单。

第一段新入口只接受直接 TLS 连接（显式开发模式仅 loopback HTTP），来源取 socket peer，拒绝 Forwarded/X-Forwarded-*。
尚未实现可信代理配置，不能部署成“信任所有转发头”的反向代理模式。客户端通过唯一的 `X-Pixels-Client-Type` 明确四种终端之一，
与 bearer 对应的入库类型必须相同；浏览器终端必须携带与配置完全相同的 Origin，原生终端携带 Origin 时也须匹配。

访客来源使用 composition root 必须加载的 32 字节稳定部署私钥，按域标签、deployment UUID、规范化 IP 计算 HMAC-SHA256；
不存原始 IP，不从请求 DTO、Host 或转发头取来源。密钥不能在重启时自动生成，否则已有来源封禁会失去同一身份。
共享资源入口还要求且只允许一个 `X-Pixels-Subject-Kind: user|guest`；服务器只检查该种凭据，不尝试另一张表。
访客签发本身拒绝 Authorization，admin_web 不能成为访客或资源主体。

| 入口领域 | 接入到的权威 | 必须验证 |
|---|---|---|
| 注册/登录/退出/改密/本人资料 | IdentityStore + 管理权限/撤销事务 | 公共注册仅 user；Argon2id 在有界阻塞池；账号与真实来源限流、dummy verify；登录和改密竞争不签旧版本 |
| 管理用户/组/授权 | ControlStore、GroupStore | admin 写、viewer 读、普通会话不能伪装 admin_web；最后管理员保护、CAS、同事务审计/outbox |
| 访客身份与公开应用 | GuestStore、ApplicationStore | 来源 HMAC 从受信连接地址产生；阻止后不可换 guest ID 绕过；无 user/device/account 身份兜底 |
| 设备/节点登记与配置 | DeviceStore、NodeStore | 公开编号不是口令；随机登记凭据只返回一次；禁止其他节点代上报；管理页面不显示摘要/密钥 |
| 节点遥测 latest/历史 | NodeStore | 当前 generation/sequence 原子落 latest 与历史；未知为 NULL；历史复合游标；7 天有界清理；runtime 不可 UPDATE 历史 |
| 节点遥测告警 | TelemetryAlertStore | 每节点显式阈值/连续样本/回滞策略；同报告事务去重开立、升级和恢复；admin 确认并审计；稳定游标筛选；恢复后 180 天有界清理 |
| 应用/部署/启动/停止 | ApplicationStore、DeploymentStore、InstanceStore | 三模式严格字段；owner 来自认证；幂等请求及审计；未准备、陈旧、无容量拒绝 |
| 资源会话与描述符 | ResourceSessionStore | Desktop 与 CloudApplication 明确分型；最长 30 秒、实际 endpoint；描述符秘密不混入管理 DTO |
| 传输/录像/连接/访问历史 | FileTransferStore、RecordingStore、ActivityStore | 原生产者/原登录/当前授权复查；去密、稳定游标；历史记录不当作当前在线事实 |
| 缓存/Range 播放 | RecordingCacheStore + 私有文件证明 | 有界阻塞 IO、配额/读租约、真实 hash/物理锁、撤权停止；缺失/损坏文件不得继续显示可播放 |
| 具名连接设置 | SavedConnectionStore | 本人+终端隔离、明确资源目标；设置不能保存授权或覆盖服务器 endpoint |
| 更新目录 | UpdateStore + px_release_catalog | 管理登记/审批/撤回；平台身份；内容不可变；目录存在不代表已验签/可安装 |

管理员初始化是独立本机工具，仅允许全新空库初始化一次，使用明确 owner 身份；业务账号不建表、不自升管理员。
业务监听前验证初始化完成；不能让公开注册抢占首个管理身份。初始化竞争和进程中断必须有真实 PG 测试。
管理重置与用户自助改密分别校验授权，但使用同一撤销事务，不能以“重置成功”掩盖 outbox 写入失败。

存储基础增量（2026-09-17）：空库 owner 初始化、启动时管理员存在检查、精确登录绑定的本人资料/口令读取、
改密提交时再次验证原 token/client、管理分组分页已经实现。运行角色禁止改写登录归属/有效期或物理删除身份历史。
Windows `accounts` 七组专项通过，包括二十方初始化竞争、审计失败整体回滚、一百次退出/改密竞争；
这不代表初始化 CLI、HTTP 或产品组合根已经交付。

## 3. 节点控制与副作用

节点保持受认证的长连接；服务器端保存不可由 JSON 构造的 NodeConnection 上下文。
重连生成新 generation，客户端不能指定或恢复旧 generation；报告与命令处理必须使用该连接上下文。
首帧认证有截止、消息大小/待发队列/连接数有上限；错误、断线、超时与关闭由同一个生命周期对象收敛。

当前 Console 增量使用 `/api/console/node-control` WebSocket：不接受 query token、Authorization、Origin、客户端类型或
Forwarded/X-Forwarded-*；节点凭据只允许在首个严格 JSON 消息出现。认证截止 5 秒，消息/帧上限 64 KiB，
全进程最多 128 个节点连接，并按直连 socket 来源限制建连频率。每条后续消息使用严格递增的非零 `request_id`；
同一连接串行处理和回包，不设无界待发队列。节点每 35 秒至少产生协议流量，单次数据库/写入分别最长 5 秒。
Runtime 取消会终止接收；每个升级连接持有一个生命周期 permit，shutdown 在关闭监听后等待全部 permit 归还，
各会话先尝试关闭数据库 generation，再允许共享池关闭。

wire 契约位于独立的 `px_node_protocol` crate，不依赖 Console store、HTTP 或 Service 实现；Console 通过显式转换适配存储模型，
返回节点所需的最小确认字段而不是完整管理 profile。动作目前为 report、report_deployment、list_deployments、
begin_reconciliation、reconcile、poll_command、acknowledge_command、list/admit/retire frontend，以及 open/report channel、
begin/report file transfer、report recording；所有消息共用严格 request_id 和当前认证连接上下文。
命令由节点显式 poll，Console 不把“写入 socket”当作执行；ACK 仍由 repository 校验命令、lease、instance、launch、
revision、generation 和 epoch。Windows 隔离 PG/真实 TCP WebSocket 专项已覆盖空清单对账和 Start/Running、Stop/Absent 闭环；
Console 协议总门禁中的全流程客户端仍是合成节点；但 Windows Service 已实际实现前端准入的节点操作桥，Render 已通过本机 IPC 调用它。
这项真实桥接仍不是 Windows Job、首帧媒体、文件/录像生产或 RDP 会话验收。

先提交命令，再发送。租约/命令 ID/instance ID/launch ID/epoch/generation/revision 全部进入节点执行边界；
发送成功不等于执行成功，收到 ACK 也不能跳过严格状态/身份复查。节点断线不把 Unknown 自动变 Stopped。
排队任务和 late ACK 的拒绝不得伤害 PID/端口复用后的新资源。

Service 执行新授权/命令前校验自身当前代际。原生前端描述符只能向当前受信节点兑换；
续租期限从请求发出时的单调时钟计算，超时拒绝。不能把客户端传来的 expires_at 当作延长权限的证据。
撤销 outbox 有界重试并以端点已执行/授权已失效作为完成依据；仅写到 Socket 不立即标记撤销完成。

Game Hook 严守本次 private Job AND 规范化完整路径；WebView 不加 Windows 用户；
RDP 只关闭运行时/承载，保留账号/Profile/Session/应用，同一 application/node 工作区拒绝第二个前端。
实际 Windows 执行尚未验收之前，模拟节点测试不得被记作这三项产品行为通过。

Windows Service 接入增量已经删除旧 `/console/service`、`/cms/service`、query token、appkey/设备授权仓库和 Panel 覆盖节点地址的运行入口，
改用 `px_node_protocol` JSON WebSocket。节点配置只接受精确 `/api/console/node-control`：生产必须 WSS，开发明文 WS 仅允许 loopback；
64 字符小写十六进制 node token、endpoint 和必填公网 host 进入 SYSTEM/Administrators ACL 目录，并使用 machine-scope DPAPI 加密。
管理员通过标准输入调用 `px_service.exe --configure-node-control`，token 不进入命令行、TOML 或日志。
Service 已实现认证、节点/端口/能力报告、challenge 清单对账、命令轮询、generation/epoch/revision/deadline 校验、精确 launch Stop 和 ACK；
Service 同时接受 Render 的类型化前端准入 IPC，只把 session/revision/一次性 token 发给当前节点控制任务，等待 Console
`FrontendAdmitted` 后返回绑定 target/instance/role 的剩余租约；排队、IPC 和网络耗时从租约扣除，超时、断线、迟到响应及错误 request_id
均不产生授权。非桌面 Render 在 WebSocket 分配前强制走此路径，并验证目标为 CloudApplication、instance 为本进程实例；没有设备密码
或旧 Console 路径 fallback。通道、文件传输和录像 wire 已在 Console 协议中定义，但实际 Service/Render 生产者尚未接入。
直接模式不再用空 Relay 字段启动“启用 Relay”的 Render。新 RDP workspace envelope 和 GPU 绑定尚未进入 wire，因此 Service 明确报告
`rdp=false`，带 GPU 或 RDP 的意外 Start 不执行；这不能记作 RDP/GPU 或实际 PG→Windows Render 端到端通过。

## 4. 现有能力核对与接入顺序

切换前产品曾包含 RTC/TURN 配置、直播/视频墙/媒体 sidecar、设备自命名和策略、用户资料/头像、日志/遥测及管理审计。
中央 RTC/TURN、直播与媒体 sidecar 已按 2026-09-19 产品决策归档退役；其余能力仍须逐项说明新实现归属，不能随着删除 Mongo 模块一起无声丢失。
其中遥测实际采集/逐 GPU 硬过滤评分属于 P3，但 DB2 的持久字段/历史查询及状态新鲜度不能拿零值冒充未知。
外部媒体访问仍须经过当前身份/目标授权；不可重新公开静态 uploads、任意文件路径下载或任意 shell 管理接口。

接入顺序：

1. 组合根/单活动锁、严格配置、私有密钥、本机初始化工具；进程启动/退出/错误配置测试。
2. 身份与管理 HTTP、真实网页登录注册和拒绝用例；补齐本人资料/管理接口的存储覆盖。
3. 资源目录/启动/描述符、节点长连接/持久命令/对账；同步 Windows 与 Android 新身份/目标消费者。
4. 记录、缓存媒体、具名设置、更新与现有实时能力；真实输入/音频/传输/恢复测试。
5. 共享许可证验证与库外水位、自动备份/恢复；按 DB4/DB5 原门禁验收。

每个增量先走独立真实 PG/API/进程测试，然后跑受影响前序回归；达到阶段出口时才做完整产物与真实部署验收。
不能因功能尚未接通而删除必测项、引入 Mock 生产成功路径或临时关闭授权。
