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

当前缺口：Console 仍从一组三元环境变量取得单个 Relay，Relay 没有进入受认证服务库存，也没有由 Console 执行多节点容量选择。
机器池、CPU/内存/网络预算和多 Relay 实机验收仍未完成。Broker 首版先作为 Console 内部的窄连接编排模块，不为拆进程而复制业务状态。

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

P3-1 尚未整体完成。下一批 P3-1B 只实现 Relay 主动连接 Console 的受认证 WebSocket 状态生产者，把 P3-0 的实时计数写入本库存，
并让 Console 下发期望 draining；不在这一批提前实现选择、绑定或管理页面。
