# P3 连接与多节点调度执行计划

> 起始日期：2026-09-23。前置 P1/P2 已完成。
>
> 本计划只补当前实现的真实缺口，不重做 DB0–DB5 已完成的节点、实例、逐 GPU 调度和 Relay 数据转发。

## 1. 当前基线

已经完成并直接复用：

- 多个 Windows Service 节点登记、管理长连接、代际 fencing、应用部署 Ready 和断线对账；
- PostgreSQL 对多个 ApplicationDeployment 和逐 GPU 候选执行硬过滤、稳定评分及原子实例预约；
- GPU stable key、库存代际、预算和 requested/actual Render 落点验证；
- Direct Host 与现有 Relay 数据模式的 Windows、Web、Android 短链路；
- Relay 房间认证、反压、载荷统计、空闲回收和资源会话 admission ticket。

当前缺口：Relay 库存、管理页、实时容量选择及两台物理 Relay 的跨机短验收已完成；尚缺第二台物理 Render/Service 的跨机短验收。
机器池、CPU/内存/网络预算和多 Render 实机验收仍未完成。Broker 首版先作为 Console 内部的窄连接编排模块，不为拆进程而复制业务状态。

## 2. 约束

- Relay 保持现名和现有非 WebRTC 数据协议；不恢复中央 WebRTC signaling、Coturn/TURN 或 ZLMediaKit。
- 不增加客户端短期 ticket 体系；继续使用当前资源会话、稳定 SessionId、frontend grant 和 Relay admission ticket。
- Console 选择 Relay，但不把应用 ACL、实例生命周期或许可证解析下放给 Relay。
- 新 Relay 只接新连接；不迁移现有 Socket、房间、游戏实例或 RDP 工作区。
- 单 Relay 时通过 draining 拒绝新连接并等待现有连接自然结束；无备用容量时由运维安排维护窗口。
- 不引入 Kubernetes、Console 多活、云厂商自动采购或跨地区热迁移。

## 3. 实施顺序

| 批次 | 实现 | 短验收 |
|---|---|---|
| P3-0 Relay 排空基线 | 独立控制密钥、动态 draining、容量上限和当前连接/房间状态；排空只拒绝新连接 | 错误控制密钥拒绝；draining 后新连接 503、已有连接继续；恢复后可新建连接 |
| P3-1 Relay 服务库存 | PostgreSQL Relay 节点、稳定身份、公开 endpoint、容量、地域/线路标签、disabled/draining、连接代际和有时效的状态快照 | 重复/旧心跳不覆盖新代际；过期、禁用、排空、满载和未知状态均不可分配 |
| P3-2 Relay 选择与绑定 | 在连接编排事务中选择 Relay 并把 relay_id/代际绑定到会话或实例；节点 Start 与客户端 descriptor 使用同一权威 Relay | 两个 Relay 只选符合容量者；并发最后一槽不超售；重试返回同一绑定；RDP 不分配 Relay |
| P3-3 管理页面 | Relay 列表/详情显示来源时间、容量、连接、房间、字节、draining 和拒绝原因；操作只调用受权 Console API | 只读角色不能排空；Unknown 不显示健康；页面状态不能绕过后端门禁 |
| P3-4 多机短测 | 至少两个 Render 节点和两个 Relay 节点，执行新节点接入、应用 Ready、容量选择、单节点失联和排空 | 新请求进入有效新容量；已有会话不热迁移；实际节点、GPU、Relay 与持久绑定一致 |

P3-1 先只上报调度必需的连接数、房间数、累计字节和容量。实时带宽、RTT、丢包、抖动、CPU/内存和 Prometheus 历史属于随后监控切片，
不能用尚未实现的观测值冒充容量权威。

## 4. P3-0 已完成

`px_relay` 现在要求独立的 `PIXELS_RELAY_CONTROL_KEY`，长度 32–512 字节。受认证的
`POST /control/draining` 原子切换排空状态；`/healthz` 返回 `status`、`accepting_new_connections`、当前/最大连接数、当前/最大房间数和既有
载荷统计。排空时 `/relay` 的新升级请求返回 503，已建立连接和房间不被主动关闭；取消排空后恢复新准入。

公网部署脚本在首次部署时生成 32 字节随机控制密钥，后续覆盖升级保留原密钥，且继续使用受限 launcher ACL。Release 单元及真实 WebSocket
回归 8/8 PASS，严格 Release Clippy、rustfmt、PowerShell 语法和 `git diff --check` 通过。

P3-0 不是多 Relay 完成声明。

## 5. P3-1A 已完成：Relay PostgreSQL 权威库存

Console fresh schema 0030 已增加 `relay_nodes` 和仅保存摘要凭据的管理库存。当前已落地：

- Relay 稳定 ID、唯一名称和唯一公网 host/port；
- 安全默认值：新 Relay 初始为 offline 且期望 draining；
- 管理员创建、分页查看和带 revision 的 disabled/draining 配置及审计；
- 复用 Console runtime epoch，连接重建增加 generation，旧连接无法继续上报或关闭替代连接；
- 上报 sequence 必须严格递增，连接数/房间数必须在 Relay 自报上限内，累计字节使用有界整数；
- `fresh` 只代表 30 秒内受认证连接仍属于当前 Console epoch，不冒充 Ready 或可调度结论；
- Console 新进程启动时同时使 Render/Service 节点和 Relay 旧连接失效，避免持久快照被当成实时状态。

全新 PostgreSQL schema 的 SQLx 元数据 287/287 生成通过；Relay 库存短验收 3/3 PASS，既有节点代际回归 11/11 PASS，Release
Clippy 和 rustfmt 通过。报告分别为 `pg-20260923-214413-9e59a565` 和 `pg-20260923-214608-f0a9f0b0`。

## 6. P3-1B 已完成：主动状态生产者

Relay 现在通过独立的严格 JSON 协议主动连接 Console `/api/console/relay-control`，令牌只放在首帧而不进入 URL、header 或日志。Console 认证成功后
绑定 runtime epoch 和连接 generation；Relay 每 5 秒上报连接数、房间数、累计上传/转发字节、容量、版本和实际 draining。请求 ID 与 report sequence
分别严格递增，旧连接、乱序报告和失效 Console epoch 均不能更新库存。

Console 的 `desired_draining` 是运行权威。Relay 启动和控制连接失效时默认排空；收到状态变化后立即补发一帧实际状态确认，正常静默时最长 15 秒
fail-closed，已有数据连接不被强制迁移或中断。该协议只传服务状态，不新增客户端短期 ticket，也不承载 WebRTC signaling。

真实 Console WebSocket + Relay 控制客户端 + PostgreSQL + Relay HTTP 健康状态短闭环 1/1 PASS，报告
`pg-20260923-221147-3e5ea134`；共享协议 1/1、既有 Relay 数据路径回归 8/8 和严格 Release Clippy/rustfmt 均通过。

P3-1 已完成。下一批进入 P3-2：只增加可分配 Relay 查询和资源会话的原子稳定绑定；不在该批实现运维页面、Socket 热迁移或机器池扩展。

## 7. P3-2A 已完成：资源会话稳定绑定

Fresh schema 0031 增加独立 `resource_session_relays` 关系，不污染既有会话身份对象。非 RDP 会话创建事务现在按以下硬条件选择 Relay：当前 Console
epoch、30 秒内受认证状态、Ready、未禁用、期望和实际均未排空、连接/房间容量已知且仍有余量。排序只使用连接与房间两项实际压力并以稳定 ID
打破平局；未知、满载、排空、过期或离线节点不会被选择。

绑定和资源会话在同一事务提交；同一 request 重试复用原 session/relay，已有绑定不会因 Relay 后续排空或重连而迁移。容量判断同时观察 Relay 实报
和当前未关闭绑定，资源会话创建已有的 PostgreSQL advisory transaction lock 保证并发判断不超售。RDP 不创建 Relay 绑定。Descriptor 只从持久绑定读取
host/port；单独配置共享签名 key 不再伪装成可用 Relay。

SQLx fresh-schema 元数据 290/290；资源会话 14/14、Console node-control 2/2 和严格 Release Clippy/rustfmt 通过，报告为
`pg-20260923-222240-541432a9`、`pg-20260923-222522-d8897efc`。

## 8. P3-2B 已完成：实例、节点命令和会话使用同一 Relay

Fresh schema 0032 增加 `instance_relays`。非 RDP application instance 在预约节点/GPU 的同一事务中选择并持久绑定 Relay；选择和实例容量
预约共用 PostgreSQL advisory transaction lock，避免并发最后一槽超售。Node Start 从该实例绑定取得 host/port，后续 CloudApplication
资源会话原子继承相同 relay ID、generation 和 endpoint，不执行第二次选择。实例已绑定后 Relay 排空或重连不会把运行中连接热迁移到另一节点。

Desktop 会话仍在自己的资源会话事务中独立选择 Relay；RDP 不创建 Relay 绑定，也不获得 Relay Start 参数。Console 运行配置已删除静态
`PIXELS_RELAY_PUBLIC_HOST/PORT`，仅保留部署共享 `PIXELS_RELAY_APP_KEY` 用于签发准入票据；没有数据库持久绑定时，单独配置密钥不会产生 Relay
endpoint 或伪造可用性。

SQLx fresh-schema 元数据 292/292；实例预约 17/17、资源会话 14/14、Console node-control 2/2、Console runtime 12/12 及严格 Release
Clippy/rustfmt 通过，报告分别为 `pg-20260923-223407-8c23d74f`、`pg-20260923-223550-0164fd11`、
`pg-20260923-223702-6b2b12be`。P3-2 至此完成。

## 9. P3-3 已完成：Relay 管理与部署凭据闭环

Console 已提供受权 `managed/relays` 创建、分页列表和带 revision 的配置 API。创建响应只返回一次 64 位节点 token，PostgreSQL 只保存摘要；后续列表
和实时管理事件均不包含明文 token。Admin 可以修改 disabled/draining，Viewer 可以读取库存但写操作返回 403。

Console 运维页现显示公开 endpoint、最后受认证上报时间、版本、当前/最大连接数与房间数、累计接收/转发字节、期望/实际 draining，以及按后端同一
硬条件计算的明确不可调度原因；Unknown 不显示为健康。页面通过独立 `relays` 管理事件刷新，不轮询或接触 Relay 控制密钥。

公网覆盖脚本不再接受或写入静态 Relay host/port，也不直接写 PostgreSQL。运维先从管理页面/API 登记 Relay 并安全保存一次性 token，再以
`Read-Host -AsSecureString` 取得该 token，连同权威 Console `wss://.../api/console/relay-control` 地址传给脚本；受限 launcher 保存节点 token、
Relay 数据面 app key 和独立 control key。Console launcher 只保留 app key。

真实 PostgreSQL + Console + Relay 控制客户端 + Relay HTTP 状态短闭环 1/1 PASS，覆盖创建、列表不泄密、Viewer 写拒绝、Admin 取消排空、实际
状态收敛及 Console 失联 fail-closed，报告 `pg-20260923-230053-a893a7b2`。前端 API/本地化聚焦测试 4/4、TypeScript 类型检查、Vite 正式构建、
严格 Release Clippy/rustfmt 和 PowerShell 语法均通过。

P3-3 至此完成。下一批 P3-4 只做两个 Relay 与至少两个 Render/Service 节点的短功能验收，确认容量选择、排空、失联和持久绑定；不做长时间压力
测试、自动扩缩容或连接热迁移。

## 10. P3-4A 已完成：双实例门禁与公网单节点闭环

全新 PostgreSQL 环境下已用两个独立 Service/Render 节点连接和两个 Relay 库存节点完成调度短测：第一个实例选择较空闲 Relay，原 Relay 进入
draining 后新实例选择备用 Relay，两个节点分别取得 Start 命令，已有实例绑定不迁移。17/17 PASS，最新报告为
`pg-20260924-003635-1e88c8c2`。

真实进程门禁启动两个独立 Relay HTTP/数据服务和两个控制客户端，验证两者独立上报、Viewer 写拒绝、Admin 只排空其中一个、另一个继续准入，
以及 Console 停止后 Relay fail-closed；1/1 PASS，最新报告为 `pg-20260924-002809-1d26fb06`。这证明双 Relay 行为，不冒充两台物理机器。

公网 Windows 节点已正式应用 schema 0030–0032，并部署当前 Console、管理页和 Relay。由于私有部署 Console 使用自己的 CA，Relay 增加可选
`PIXELS_RELAY_CONSOLE_CA_FILE`：只为 `wss` 加载明确 CA，仍执行完整证书与主机名校验，不提供跳过校验。公网 Relay 通过
`wss://39.71.45.66:4600/api/console/relay-control` 实际上报，Console 状态为 `ready/fresh`，期望和实际上报均为非 draining，公网 4605
健康端点确认准入；Relay 安装 SHA-256 为 `53FE39FA95A9B3179A80040D9DC83738D3B95D36383B96AED34DB091614A4935`，与开发输出一致。
Console 静态页面 4 件文件也已逐件与开发输出 SHA-256 对齐。

第二台物理 Relay 已部署到 SG Ubuntu 24.04 主机。`px_relay` 由 systemd 以受限动态用户运行，通过私有 CA 校验后的 WSS 连接当前
Console；云安全组未开放 4605，因此只由既有 Nginx 在公网 80 精确代理 `/relay` 和 `/healthz` 到本机 4605，其余路径返回 404。Linux
安装文件 SHA-256 为 `17F2E30B64129F83C8A9FE7FE38F08DD85CD8328920DDD43A45C7035FBA7370B`，与 WSL2 Release 输出一致。

两台物理 Relay 的排空和故障短测已完成：Windows 主 Relay 排空时保留 2 条既有连接，新 WebSocket 准入返回 503；SG Relay 同一请求到达
应用认证并返回 401，证明备用公网数据入口仍接单。恢复主 Relay 后实际 draining 收敛为 false。随后停止 SG systemd 服务，Console 将其标记为
offline/not fresh，Windows Relay 保持 ready/accepting；重启后 SG 恢复 ready/fresh。一次强制指定 SG Relay 的真实 Windows
CloudApplication 连接已建立房间、收到 TCP 媒体并解码关键帧，Client 记录输入发送成功；SG 健康计数记录双向载荷 651 / 12,448 字节。

首轮 CloudApplication 自动验收未通过：验收器未从 `Process.MainWindowHandle` 识别已渲染窗口；同时公网 Console 因
`Console runtime authority was lost` 安全退出，导致清理请求超时。Console 计划任务恢复后，残留实例为 stopped，两台 Relay 均恢复
`ok/accepting`。随后验收器改为按 Client PID 枚举最大可见顶层窗口，同一 SG Relay 链路重跑完整 PASS：工作区窗口、解码首帧、输入、房间和
双向载荷均通过，脚本退出码 0；Client build/dist SHA-256 均为
`38707DF4FA2BABBC7D3DAF2B8BB4345ACADA34E03BEABBBF84CDDCC6A44C568C`。Console 租约续签失败处增加脱敏错误类别日志，
本次复测未再次失去权威；首次事件的根因仍待进一步定位，不能据单次恢复宣称已根治。

2026-09-24 针对此偶发退出再核对当前运行代码：Windows 主进程的 `authority was lost` 来源是 PostgreSQL 专用会话租约取消；
租约每秒续签、五秒到期，数据库探测失败后保持终止态，不在原进程内重新取得锁。90 的 PostgreSQL 服务和 Console 当前均正常，
但旧 Console stderr 被计划任务后续启动覆盖，现场 PostgreSQL 日志亦未保留，故无法从现有证据区分瞬时数据库故障、探测超时或锁丢失，
不得声称已找到首次事件根因。租约续签现在额外记录脱敏的超时、查询错误类别、锁丢失和本地截止时间类别；90 的启动脚本在覆盖
stdout/stderr 前保留非空旧日志，归档失败只告警、不阻断启动。隔离 PostgreSQL 的六项租约专项（包括后端终止、进程退出、过期不可复活）
全部通过，报告 `test-results/server_validation/pg-20260924-223255-21e274c3`。新版快速 Release Console 已覆盖到 90，
程序 SHA-256 为 `67530AFF2A2EF0491B4C304AF6C6868C8CE4E635076FA583CFCE6C0575A99A5B`，任务运行且 readiness 为 204；
部署前受权接口确认无活动资源会话、两台 Relay 均为零房间/零连接。本轮只补可追溯性和安全回归，不人为中断公网数据库或声称偶发故障已根治。

严格 Release Clippy（含 PostgreSQL integration features）、rustfmt、PowerShell 语法及差异检查通过。P3-4B 现在只剩第二台物理
Render/Service 的跨机短测；当前没有第二台物理 Render 主机，该门禁等硬件具备后执行，不阻塞 P4 开发。开发阶段不运行长时间压力测试，
统一长测仍在商业发布前门禁。
