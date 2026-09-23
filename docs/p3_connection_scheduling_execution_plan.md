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

当前缺口：Relay 已进入受认证服务库存并由 Console 按实时容量选择；尚缺运维管理 API/页面及多 Relay 实机验收。
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

下一批 P3-3 只补 Relay 管理 API/页面和部署凭据闭环：管理员创建节点时只返回一次明文 token，持久层只保存摘要；页面展示权威状态并控制
disabled/draining。部署脚本通过受权 API 获取凭据，不直接写 PostgreSQL。该批不增加自动扩缩容、热迁移或额外票据体系。
