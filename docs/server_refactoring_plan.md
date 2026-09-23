# Pixels 服务端改造计划

> 状态：架构规划；数据库前置工作已开始，连接服务拆分和完整产品验收尚未实施，见[数据库实施状态](server_database_execution_status.md)
>
> 日期：2026-09-16
>
> 范围：`px_console_server`、未来连接协调服务、Native P2P/Relay、云运行时调度及其客户端协议边界
>
> 核心决定：服务端共用一套实现，不引入多租户；Official 仅接入自营官方平台，Customer 仅接入客户私有平台。远控和云业务共用连接基础设施，业务生命周期独立；不采用客户端短期一次性连接票据。
>
> 产品、部署、发行、安全更新、热升级边界及新的实施阶段以 [独立部署与升级实施计划](server_deployment_and_upgrade_plan.md) 为准；本文保留领域与连接架构细节。所有阶段仍为待实施，文档不是验收报告。
>
> 2026-09-19媒体范围已收敛：Relay保持现名和既有非WebRTC数据转发；WebRTC只保留Direct Host；ZLMediaKit、Coturn/TURN及
> 经Relay中转的WebRTC信令退出活动产品。本文较早的P2P/TURN设想不再扩展当前范围，详见
> [Direct Host专项计划](direct_host_webrtc_scope_plan_20260919.md)。

## 0. 文档目的

Pixels 当前同时发展两类业务：

1. **远程连接**：用户连接一台已经存在、可能位于 NAT 后的设备。
2. **云运行时**：用户启动或恢复云游戏、云应用、云电脑，再连接到对应运行实例或工作区。

两类业务最终都需要身份认证、在线信令、路径建立、P2P、Relay 和断线恢复，但它们在连接建立之前的资源模型以及连接结束之后的生命周期完全不同。本计划的目标是：

- 明确业务控制面、连接控制面和数据面的责任边界；
- 在不重复实现传输栈的前提下支持两类业务；
- 从现有 `px_console_server` 单体逐步迁移，不要求一次拆成大量进程；
- 吸收现有 P2P/Relay 技术验证的可复用部分，避免直接复制其单体结构；
- 以稳定会话、幂等操作和服务端授权取代脆弱的短期票据；
- 为 Windows 一体化安装和 Linux Docker 部署保留一致的服务边界。
- 自营公网和客户私有部署共用服务端实现；各部署独立拥有用户、设备和数据，不引入 Tenant 模型。
- 客户端按产品与 Official/Customer 两个维度发行，平台发现、准入、更新来源和节点归属遵守同一部署边界。
- 将升级、数据迁移、回滚、连接排空和恢复作为架构输入，而非完成拆分后再补安装器。

本文是目标架构和迁移顺序，不代表所有能力已经实现或通过生产验证。

## 1. 调研基线

### 1.1 外部参考实现

本计划基于以下只读参考代码：

| 仓库 | 调研修订 | 用途 |
|---|---|---|
| `E:\source\signaling_server` | `319a2a25d845d2e00e93123e7dc64d950dc6a4e6` | Go 信令、ID、TCP/UDP rendezvous、路径协商、Relay 分配 |
| `D:\dolit\client_customer_dolit` | `675ced5c9a62578bc7bd65d16c3111fa26f43e18` | 产品外层与 Native 客户端集成 |
| `D:\dolit\client_customer_dolit\client` | `81f21985ceda1b0c41f2e37e84703f949957f1b2` | Native P2P、Relay、路径选择与恢复 |

调研时这些目录都存在本地修改。本次仅只读分析，未修改、清理或构建外部仓库。

### 1.2 Pixels 当前事实

- `px_console_server` 当前代码同时承载身份、设备、应用目录、实例调度、连接描述、Relay以及待归档的中央RTC/TURN与媒体sidecar；
  目标产品保留Relay数据能力，移除ZLM/Coturn和经Relay中转的WebRTC信令。
- 设备与云应用已经被确定为两个独立资源域；Android 也已使用独立的 `CloudApplication` 会话目标，见
  [Android 云应用实施计划](android_cloud_apps_implementation_plan_20260914.md)。
- 云应用调度已经具备 `Application → AppNode → AppInstance` 模型，见
  [Console 应用节点计划](console_app_nodes_plan.md)。
- 远控连接描述以 `device_id` 为目标；云应用连接描述以 `instance_id` 为目标。当前仍通过字符串形式生成不同信令目标，后续应改为类型化 Endpoint。
- `px_desk_server` 当前是咨询、问题反馈和版本接口服务，不是云电脑或云运行时调度服务；不能因为名称含 `desk` 就在其中加入 Cloud Desktop 生命周期。
- 桌面 Render 默认端口为 4601，应用 Render 从 4613–4998 动态分配；实际端口同时承载 TCP/WS 与 UDP。20371 已完全退役，不得重新成为默认值、探测目标或兼容回退。

## 2. 对参考 P2P/Relay 实现的结论

### 2.1 已经拆开的部分

参考服务实际部署为两个进程：

```text
signaling_server
  ├─ ID 与设备数据库
  ├─ 登录和在线连接
  ├─ 房间与会话
  ├─ TCP/UDP rendezvous
  ├─ 路径协商
  └─ Relay 节点选择与 allocation

hbbr
  └─ 控制和媒体字节转发
```

因此它并非完全没有拆分：实际 Relay 数据转发已经由独立 `hbbr` 进程承担。Go `relay` 包只选择节点、生成配对 UUID、维护短期 allocation 并返回 Relay 端点，本身不转发数据。

### 2.2 尚未拆开的部分

Go `SigServer` 仍同时拥有：

- SQLite 设备数据库和设备 ID 生成；
- WebSocket 长连接和在线状态；
- 房间管理；
- 会话授权；
- TCP/UDP 公网地址观察；
- P2P rendezvous；
- QUIC 身份交换；
- 双方路径提名；
- Relay 节点池和 allocation。

在线连接、房间、rendezvous 和 authorization grant 主要保存在单进程内存中；设备记录保存在本地 SQLite 中。当前实现适合技术验证，但不能直接作为多实例、高可用的生产控制面。

Relay 节点选择目前主要依赖静态优先级和 pending allocation 数量，不等同于真实连接数、带宽、丢包、RTT 或节点健康度。

### 2.3 值得复用的服务端边界

参考实现中的 TCP/UDP rendezvous 已经表达了正确方向：

- rendezvous 不拥有设备 ID；
- rendezvous 不决定业务授权；
- 只有上层完成房间/连接校验后才允许准备候选路径；
- 真正使用候选路径时再次确认双方仍是当前有效连接。

这些原则应保留，但需要把依赖从 `SigServer` 具体对象收敛为类型化接口。

### 2.4 客户端现状与复用点

参考客户端的底层组件已经有较好的独立边界：

- `SignalingRoomClient` / `RelaySignalingProtocol`：信令与 Relay 分配协议；
- `TcpPathAttempt`：有界 TCP 候选尝试；
- `UdpPathAttempt`：UDP/QUIC 候选尝试；
- `PathNomination`：双方路径提名的纯状态归约；
- `HbbrRelayPath` / Relay Path Factory：Relay 路径建立；
- `NativeSession`：活动控制/媒体会话以及路径替换。

主要问题是 `NativeRemoteRuntime` 同时编排信令、Relay 分配、TCP/UDP 尝试、质量测试、路径选择、回退、恢复和 `NativeSession` 生命周期，责任过大。

后续应复用底层 Path/Session 组件，拆分编排责任；不能在 Pixels 产品层重新复制一套 TCP、UDP 和 Relay 实现。

## 3. 总体设计原则

2026-09-16 新基线决策：现有环境只有开发数据，按全新系统实现，不迁移 Mongo 数据，不保留旧协议、配置、接口、标识、字段或客户端兼容逻辑。现有实现仅作领域与算法参考；新契约的生产端、消费端和测试一起调整。未来正式版本升级不包含对旧开发基线的支持。

1. **业务与连接分离**：业务服务决定“谁能连接什么资源”；连接服务决定“已获准的双方如何建立和恢复路径”。
2. **共用连接底座，不共用业务生命周期**：远控、云应用和云电脑最终都使用统一连接能力，但不共用含糊的目标类型或结束策略。
3. **Console 是控制面，不是媒体热路径**：Console 重启、升级或短时不可用不应中断已建立连接。
4. **Relay 是纯数据面**：Relay 不查询用户、设备、应用或工作区数据库，不承担业务调度。
5. **授权不等于短期票据**：准入以认证连接和服务端 SessionGrant 为依据，不要求客户端在数秒内消费一次性凭证。
6. **所有可重试操作幂等**：创建会话、启动实例、Attach、Relay allocation、路径换代都有稳定的幂等键和明确代际。
7. **类型化资源和 Endpoint**：Device、Application、Instance、Workspace、Session 和 Endpoint 不能以拼接字符串或可互换字符串表示。
8. **动态端点是唯一事实来源**：连接只使用 Console/节点返回的权威端点，不猜测固定端口，也不恢复旧端口回退。
9. **先划逻辑边界，再拆进程**：先在现有 Rust 单体内建立模块接口和状态机，通过测试后再迁移到独立服务。
10. **连接断开与业务资源销毁分离**：尤其是云电脑，传输停止不得隐式注销 Windows Session、删除账号或终止工作区应用。

## 4. 目标架构

可直接打开 [交互拓扑网页](server_topology.html)，按步骤查看组件职责、连接路径与升级维护场景。
各服务的管理页面、状态来源、运维操作和故障恢复边界见 [服务管理与运维后台计划](service_operations_console_plan.md)。
数据库已确定改用 PostgreSQL，先执行 [数据库前置改造、备份与升级方案](postgresql_database_migration_plan.md)，再推进服务拆分与新调度。

```text
                                 px_console
                 统一 API、身份、权限、策略、管理与审计
                                      │
                      ┌───────────────┴───────────────┐
                      │                               │
          Remote Access Domain              Cloud Runtime Domain
             远程连接业务                     云游戏/应用/电脑编排
                      │                               │
                      └───────────────┬───────────────┘
                                      │ 创建服务端 SessionGrant
                                      ▼
                            px_connect_broker
               在线连接、信令、P2P、路径协商、Relay 调度
                         │                    │
                  Direct Path             px_relay
                  TCP/UDP/QUIC             纯数据转发

节点侧：
  px_service / Node Agent
      ├─ 直连 Console：节点注册、管理长连接、心跳和资源上报
      ├─ 启停与监督 Render/RDP Proxy
      ├─ 动态分配实际 Render 端口
      └─ 上报实例/工作区运行状态
  px_render / RDP Proxy
      └─ 实际控制、媒体或 RDP Endpoint
```

后期多地区部署可增加独立 `px_probe`，承担 TCP/UDP 公网地址观察和区域网络质量测试；第一阶段保留为 Broker 内部模块，避免过早增加部署单元。

## 5. 服务职责

### 5.1 `px_console`

保留持久业务控制面：

- 本部署内的用户、角色、ACL、配额和策略；不增加企业多租户；
- DeviceId、NodeId、ApplicationId、InstanceId、WorkspaceId 管理；
- 设备注册、禁用、凭据轮换；
- 应用目录、可见性、版本和兼容性；
- 云节点、GPU/容量和应用放置；
- 节点 Service 的认证管理长连接、资源/实例上报、管理命令与结果对账；
- 云实例和云电脑 Workspace 生命周期；
- 远控授权、云资源授权；
- 创建、撤销和审计 SessionGrant；
- Web/Panel/Android/Client API；
- 管理后台和异步用量记录。

逐步移出 Console：

- 用于用户会话在线、Attach、路径协商的终端信令长连接；不包含 Service → Console 节点管理长连接；
- TCP/UDP observer；
- P2P 尝试状态；
- 路径提名和质量切换；
- Relay channel pairing；
- 媒体数据转发。

### 5.1.1 节点管理连接与会话信令连接分离

产品决策（2026-09-16）：云节点安装包中的 `px_service` 常驻，主动与所属部署的 Console 建立认证管理长连接。
这条连接不依赖 Panel 打开，也不依赖任何 Render 实例正在运行；节点空闲时仍上报机器和每张 GPU 的资源状态。
节点管理入口使用当前平台配置/认证发现描述，不猜测端口，不经 Broker 转发应用管理命令。

| 链路 | 用途 | 权威边界 |
|---|---|---|
| Service ↔ Console | 注册、心跳、资源/实例/工作区快照、预约、Start/Stop、排空、配置、任务回执 | Console 决定业务期望与预约；Service 确认本机实际执行和最终准入 |
| Render ↔ Service（受认证本机 IPC） | Ready、实际 GPU、运行指标、参与者/断线宽限、错误和退出协作 | Render 提供自身运行事实；Service 结合进程监督汇总，不能仅信任自报 PID |
| 客户端/运行 Endpoint ↔ Broker | 会话认证、Attach、连接在线、候选路径、Relay 分配和恢复 | Broker 管连接状态，不替代节点资源与实例状态权威 |

Render 不必另建一条向 Console 重复上报整机资源的管理连接；它按连接协议需要与 Broker 保持独立信令关系。
Broker 的 Endpoint 在线不等于 Service 管理在线，更不等于该机器可调度；反之，Service 在线也不证明 Render 健康。
Console 分别保存管理可达、资源新鲜度、实例状态和会话路径状态，不合并成一个“在线”字段。

长连接并不保证无信息差：关键事件立即推送，资源定期汇总；Console 先持久预约，节点执行前再次核验。
管理链路断开或关键数据过期时暂停该节点的新调度，保留未知占用，不能按零用户回收或升级。
重连必须重新认证、同步快照及未决任务、对账代际和预约，完成后才恢复符合其他门禁的准入。
既有业务沿用已批准授权和断线宽限，不因管理连接断开全部终止，更不能注销 RDP 工作区。
上报来源、可靠传递与恢复验收见 [运维后台计划第 6 节](service_operations_console_plan.md#6-状态指标和事件如何进入后台)。

### 5.2 Remote Access Domain

它可以先作为 `px_console` 内部领域模块存在，负责：

- 用户能否访问某个 Device；
- 是否需要被控端确认；
- Controller/Observer 角色；
- view-only、输入、剪贴板、文件、录制等权限；
- 主控接管策略；
- 远控业务会话和审计。

它不执行 NAT 打洞，也不选择 Relay。

### 5.3 Cloud Runtime Domain

它可以先作为 `px_console` 内部领域模块存在，负责：

- Application 目录和 ACL；
- AppNode/运行节点选择；
- GPU、编码器、端口和并发资源调度；
- Instance 启动、Ready、运行、停止、失败和恢复；
- Workspace 创建、保留和重新附着；
- guest/user 所有权；
- 空闲、计费、配额和回收策略；
- 生成连接所需的类型化 Instance/Workspace Endpoint。

云游戏、云应用和云电脑共用这个运行时平台，但使用不同 `WorkloadProfile` 和生命周期策略，不需要立即拆成三个后端服务。

应用目录/部署、机器与逐 GPU 调度的完整设计见 [云应用业务管理与多 GPU 调度](cloud_application_scheduling_plan.md)。
新请求按已部署版本构造 `机器 + GPU + ApplicationDeployment` 候选，先硬过滤再按请求后的资源压力排序，
通过持久原子预约和节点二次准入防止超售；不能只按用户数、整机 GPU 均值或最久未运行时间选择。
已有实例/工作区恢复优先，RDP 固定原 owner，运行中游戏不自动迁移。

### 5.4 `px_connect_broker`

负责实时连接控制面：

- 用户端、设备端及节点/实例 Endpoint 的认证会话信令连接；不承接 Service → Console 节点管理连接；
- Endpoint 在线状态和当前连接代际；
- 会话邀请、接受、拒绝和 Attach；
- TCP/UDP 公网地址观察；
- P2P candidate 交换；
- 双方路径提名；
- Direct/Relay 路径协调；
- Relay 节点目录、健康度和 allocation；
- 路径恢复与代际切换；
- 向 Console 异步发送连接审计事件。

第一阶段应把 presence、rendezvous、path nomination 和 relay allocation 保持在同一个 Broker 进程中。它们都依赖当前认证连接和实时 Endpoint 绑定，过早拆开会引入大量分布式竞态。

Broker 不负责：

- 用户 ACL 的最终决策；
- 云实例调度；
- Windows Workspace 生命周期；
- 应用启动和停止；
- 媒体编解码；
- 数据库存储的长期审计。

### 5.5 `px_relay`

只负责数据面：

- 接收 Broker 创建的 Relay allocation；
- 校验参与者及其会话级认证信息；
- 配对控制/媒体/文件子通道；
- 透明转发密文；
- 限制连接数、带宽和速率；
- 上报活动连接、流量、RTT、丢包、带宽和健康度；
- 支持 draining，不再接受新会话但保留已有会话。

升级按可用容量分流：有替代 Relay 容量时新旧实例排空切换；单 Relay 或无替代容量时提前公告维护窗口，停止新分配，
在约定时点中断中转路径、升级验证后恢复有效会话。客户端显示维护状态并有界重连；仅升级 Relay 不主动中断已有 Direct 会话。
维护预算必须核对 Render 的既有断线保留期限，不因中转维护注销 RDP 工作区；详见 [Relay 维护规则](server_deployment_and_upgrade_plan.md#63-relay-冗余容量与单实例维护窗口)。

现有 Relay 与参考 `hbbr` 只作为实现调研输入；交付使用唯一的类型化 Broker/Relay 协议，不保留旧客户端兼容入口。可复用经审查的内部转发组件；若第三方实现无法满足部署认证、可撤销 allocation 和 draining，则不能直接作为商业交付数据面。

### 5.6 `px_probe`（后期可选）

- 部署在不同地域；
- 提供 TCP/UDP observed endpoint；
- 辅助判断 NAT 和 UDP 可达性；
- 提供客户端到区域的 RTT/丢包测量；
- 不持有用户、业务会话或媒体状态。

## 6. 两类业务的独立流程

### 6.1 远程连接：Device → Session → Connection

```text
用户选择 DeviceId
  → Remote Access Domain 校验 ACL/角色/确认策略
  → 创建 RemoteAccessSession 与服务端 SessionGrant
  → Broker 找到该 Device 当前认证 Endpoint
  → 双方 Attach(SessionId)
  → 并行或按策略准备 Direct/Relay 路径
  → 选择路径并启动 NativeSession
  → 断线后重新 Attach 同一个 Session
```

特征：

- 设备先于连接长期存在；
- 设备可能位于家庭或办公 NAT 后；
- P2P 的价值很高，Relay 是保底路径；
- 远控结束不改变设备生命周期；
- 文件、剪贴板、输入和画面属于同一个逻辑会话的不同能力/通道；
- 停止一个 transport binding 不能误报整个用户会话离线。

### 6.2 云游戏/云应用：Application → Instance → Session → Connection

```text
用户选择 ApplicationId
  → Cloud Runtime 校验目录可见性、ACL、配额
  → 选择 AppNode/机器/GPU
  → Node Agent 启动 Render 和目标应用
  → 实例 Ready，并注册类型化 InstanceEndpoint
  → 创建 SessionGrant
  → 客户端 Attach(SessionId)
  → 根据策略选择云节点直连或 Relay
  → 断开后按应用保活/空闲策略决定是否停止 Instance
```

业务分类与运行模式分开：`business_kind=game/application`，`runtime_mode=game_hook/webview/rdp`；
资源与生命周期由版本化 `WorkloadProfile` 描述，只开放验证通过的组合。以下为规格示例，不是三个独立服务或同维度枚举：

| 类型 | 主要特性 |
|---|---|
| CloudGame | 低延迟、高帧率、手柄、通常独占实例、较短空闲期 |
| CloudApplication | 窗口级应用、键鼠、可能更长空闲期、可配置观察能力 |
| WebViewApplication | 浏览器隔离实例、页面状态和临时数据策略 |

云节点通常具有可控公网网络，不应无条件执行与家庭设备相同的对称 NAT 打洞流程。优先策略应由连接描述和节点能力决定。

### 6.3 云电脑：Workspace → Runtime Attachment → Session → Connection

云电脑不是普通临时应用实例，应具有持久 Workspace：

```text
Workspace
  workspace_id
  user/account mapping
  persistent profile/storage
  current Windows session identity
  runtime state
  active connection state
```

流程：

```text
用户选择 WorkspaceId
  → Cloud Runtime 校验所有权和 busy 状态
  → 查找并验证已有 Windows Session
  → 必要时启动 Render/RDP Proxy，但不盲目重建用户会话
  → 创建或恢复 WorkspaceConnectionSession
  → 客户端 Attach
```

必须遵守：

- 普通断开、空闲、票据/授权变化或网络故障不得注销 Windows Session；
- 不删除 Windows 账号、用户 Profile 或工作区应用；
- Render、代理进程和传输连接可以按现有宽限期退出；
- 下次授权访问重新附着已有 Windows Session；
- 第一版一个 Workspace 只允许一个活动前端；第二个客户端返回 busy；
- 不自动接管；显式接管属于后续产品能力；
- RDP 使用专用可靠 carrier，不与 Native 视频 P2P/Relay 语义混为一谈。

## 7. 统一但类型化的连接模型

### 7.1 标识类型

至少使用以下不可互换类型：

```text
UserId
DeploymentId
DeviceId
NodeId
ApplicationId
InstanceId
WorkspaceId
BusinessSessionId
EndpointId
ConnectionId
PathId
RelayAllocationId
```

禁止继续依赖以下拼接方式表达类型：

```text
server_{device_id}
server_{device_id}__instance__{instance_id}
```

迁移后应使用明确的 Endpoint 枚举：

```text
EndpointTarget
  RemoteDevice { device_id }
  CloudInstance { instance_id, node_id }
  CloudWorkspace { workspace_id, windows_session_id }
```

### 7.2 Session 是逻辑会话，不是某条 Socket

统一会话只表达连接参与者和能力：

```text
ConnectionSession
  session_id
  business_kind
  initiator
  target_endpoint
  participant_roles
  capabilities
  transport_policy
  lifecycle_state
```

`business_kind` 可包含：

- `remote_device`；
- `cloud_game_instance`；
- `cloud_application_instance`；
- `cloud_workspace`；
- `rdp_workspace`。

WS、TCP Direct、UDP/QUIC、Relay、文件通道都是 Session 下的 transport/path binding，不能各自创建一套业务在线状态。

### 7.3 传输策略

```text
TransportPolicy
  direct_tcp_allowed
  direct_udp_allowed
  p2p_rendezvous_allowed
  relay_allowed
  preferred_path
  reliable_carrier_required
  endpoint_constraints
```

建议默认策略：

| 业务 | 默认路径策略 |
|---|---|
| 普通远控 | P2P 优先，Relay 保底 |
| 云游戏 | 云节点 UDP/QUIC 直连优先，区域 Relay 保底 |
| 云应用 | 直连优先，根据网络和应用配置进入 Relay |
| 云电脑 Native 模式 | 由实际 Endpoint 能力决定 Direct/Relay |
| 云电脑 RDP 模式 | 专用可靠 RDP carrier，不伪装成 Native 媒体路径 |
| 文件传输 | 独立可靠子通道，可复用 Direct 或 Relay |

## 8. 明确否决客户端短期票据

### 8.1 否决原因

短期、一次性或按首次连接消费的票据在真实产品中容易失效：

- Render/实例启动时间超过票据期限；
- TCP、UDP、Relay 并行尝试导致重复消费；
- 网络重试被误判为重放；
- UI、Launcher、Client、Native Runtime、文件进程之间传递困难；
- 客户端崩溃或覆盖升级后丢失；
- 路径切换、Relay 重建、NAT rebinding 需要反复申请；
- 客户端与服务端时钟偏差造成提前失效。

核心问题不是“票据时间太短”，而是把业务授权、逻辑会话和一次网络尝试错误绑定。

因此，本改造计划禁止将短期票据作为 P2P、Relay、重连、路径切换或多进程 Attach 的必要条件。

### 8.2 替代方案：认证连接 + 服务端 SessionGrant

```text
客户端 ── 已认证长连接 ──▶ Broker
                               ▲
                               │ 服务间可信调用/事件
Console ── CreateSessionGrant ──┘
```

SessionGrant 是 Broker 执行的服务端授权状态；Console 持久化授权依据、授权版本和撤销记录，通过可重试事件同步。Broker 内存不是重启恢复的唯一来源：

```text
SessionGrant
  deployment_id
  session_id
  business_kind
  initiator_identity
  target_endpoint
  allowed_participants
  roles
  capabilities
  transport_policy
  lifecycle_state
  revocation_state
  authorization_revision
  authority_epoch
  issued_at
  not_after
  lease_revision
  offline_policy
  target_kind / app_id / instance_id / instance_generation
```

基本关系：

```text
SessionId       = 定位会话，不是秘密，也不是凭证
认证连接         = 证明当前连接“是谁”
SessionGrant    = 服务端判断这个身份“能否加入该会话”
```

只知道 SessionId 不能加入会话。Broker 必须同时验证：

- 当前连接已经完成用户、设备、节点或实例身份认证；
- 身份是 SessionGrant 的允许参与者；
- 请求角色与 capabilities 匹配；
- Session 未关闭、未撤销；
- 当前连接代际仍有效。

云应用会话使用独立 CloudApplication 目标；业务 owner、Android 身份及字段约束见
[调度计划第 2.1 节](cloud_application_scheduling_plan.md#21-android-云应用身份与会话目标)，不得用 device/account fallback。
上面的 app_id/instance_id/instance_generation 属于 cloud_application 类型化目标的必填字段，其他目标按各自类型校验，不填伪造应用 ID。
服务端授权租约不是客户端一次性连接票据：只有 Console 权威可续期，Broker、Render（含 Direct）及 Relay 均强制有效边界。
普通策略最多 300 秒、严格策略最多 30 秒，执行容差和失联收敛上限按
[部署计划第 5.3 节](server_deployment_and_upgrade_plan.md#53-持久授权与中断策略)；心跳/重连不延长租约。
首版始终只有一个活动 Console，不为升级建设双实例或 A/B 槽。P5 按“Server 先覆盖、节点和客户端后覆盖”验证相邻正式接口兼容；
PG 事务与命令 fencing 分别保护数据库和网络副作用，不能互相替代。

### 8.3 SessionGrant 生命周期

```text
Creating
  → Ready
  → Active
  → Detached
  → Active
  → Closing
  → Closed
```

- TCP 失败改试 UDP：仍是同一个 Session；
- P2P 失败进入 Relay：仍是同一个 Session；
- Client 进程重启：重新 Attach 同一个 Session；
- 短时网络断开：进入 Detached，并保留业务定义的重连宽限期；
- Console 暂不可用：已建立 Session 继续运行；
- 用户主动停止、管理员撤销或业务资源销毁：进入 Closing/Closed。

服务端可以设置清理期限、空闲期限和最长保留期，但这些期限只驱动服务端状态机和垃圾回收，不能要求客户端在几秒内完成一次性消费。

## 9. 幂等、重试、重连与多进程

### 9.1 幂等操作

以下请求必须携带稳定的 `request_id` 或代际键：

```text
CreateRemoteSession(request_id)
StartCloudInstance(request_id)
AttachSession(session_id, process_instance_id, connection_generation)
PrepareDirectPath(session_id, path_generation)
AllocateRelayPath(session_id, allocation_generation)
StopSession(request_id, session_id)
```

规则：

- 同一 `request_id` 重试返回原有结果，不重复创建资源；
- 相同参与者和连接代际重复 Attach 返回成功；
- 新 `connection_generation` 原子替换旧连接绑定；
- 同一 `path_generation` 重试返回同一结果或当前明确状态；
- 新路径成功提交后才释放旧路径；
- 已停止资源再次 Stop 是幂等成功，不降级成 Failed；
- 超时只代表调用方未等到结果，不等于服务端操作没有执行。

### 9.2 多进程模型

客户端可能包含 UI、Native Runtime、文件传输或辅助进程：

```text
SessionParticipant
  identity_id
  process_instance_id
  role
  connection_generation
```

- `ProcessInstanceId` 用于区分进程和诊断，不是安全凭证；
- 每个进程仍须通过已有用户/设备身份建立认证连接；
- Broker 根据 SessionGrant 决定该身份是否允许以指定角色 Attach；
- 进程重启可以生成新的 ProcessInstanceId，并重新 Attach 原 Session；
- 多进程操作不能导致 SessionGrant 或 Relay allocation 被一次性消费。

若 Windows Client 多进程协作持续复杂，可增加本机常驻 Connection Manager，由它独占 Broker 长连接，其他进程通过受控本地 IPC 操作 Session。该优化不是服务端第一阶段的前置条件。

### 9.3 重连代际

每个 Endpoint 的连接绑定使用单调递增代际：

```text
EndpointBinding
  endpoint_id
  connection_id
  generation
  connected_at
  last_heartbeat_at
```

新代际注册成功后，旧代际不再有资格发送控制消息、提交路径或改变 Session 状态，避免旧连接晚到消息破坏新连接。权威代际由服务端分配；进程重启、数据恢复和 owner 切换必须推进持久化 epoch，不能接受客户端任意递增值作为接管凭据。辅助进程绑定按参与者和角色区分，不得替换整个 Endpoint 的所有通道。

## 10. Relay allocation 不使用一次性票据

Broker 通过内部控制通道向 Relay 创建会话级 allocation：

```text
CreateRelayAllocation
  allocation_id
  session_id
  allocation_generation
  participant_a
  participant_b
  allowed_channels
  limits
```

客户端连接 Relay 时提交：

```text
session_id
allocation_id
allocation_generation
participant_role
connection_nonce
authentication_proof
```

这里的认证信息必须满足：

- 在该 Session/allocation 生命周期内允许控制、媒体和文件子连接多次建立；
- 首次握手失败不会消费 allocation；
- 网络切换和 NAT rebinding 可以重新连接；
- Relay 进程重启后可以从 Broker 恢复或重新创建同一代际 allocation；
- 只有 Session 关闭、授权撤销、allocation 明确换代或服务端回收时失效。

首版由 Relay 保存 Broker 经认证控制通道下发的 allocation 状态，并执行可重复的持有者证明。证明绑定 deployment、session、allocation generation、角色、通道用途和本次握手挑战；SessionId、静态内置 app secret 或可重放的旧响应均不能作为准入凭据。密钥可以轮换而不消费业务 Session，撤销和换代同步推进授权版本。具体握手与密钥协议在 P0 评审，不能以自定义未审查密码学直接实现。

即使采用密码学证明，也不能将其设计成几秒有效、首次连接即消费的客户端票据。

## 11. P2P 和路径选择

### 11.1 Broker 内部模块

```text
PresenceRegistry
SessionGrantRegistry
RendezvousCoordinator
  ├─ TcpObserver / TcpPathCoordinator
  └─ UdpObserver / UdpPathCoordinator
PathNominationCoordinator
RelayDirectory
RelayAllocator
AuditEventPublisher
```

第一阶段这些模块在同一进程中通过窄接口协作。不得由各模块分别维护一套参与者身份或业务授权。

### 11.2 路径建立原则

- 活动 Relay 路径存在时，可以后台尝试 Direct；失败不能打断现有会话；
- 新路径必须完成双方认证、探测和提名后才能原子提交；
- 单方认为可用不等于双方已切换；
- 路径质量选择应考虑 RTT、丢包、抖动、可用带宽和稳定时间；
- Direct 路径退化时允许回 Relay；Relay 节点故障时允许新 allocation generation；
- Path 的失败、关闭或换代不能隐式关闭业务 Session；
- 已有 `PathNomination` 的纯状态归约思路应保留。

### 11.3 Relay 调度指标

从当前静态优先级升级为：

- Region/运营商；
- 节点健康和最近心跳；
- 活动 Session/连接数；
- 当前上下行带宽；
- 丢包、RTT 和错误率；
- 最大容量和保留容量；
- draining 状态；
- 业务类型和带宽等级。

Relay 节点离线或 draining 时不得继续分配新会话，但已有连接应在能力允许时继续服务。

## 12. 数据和状态所有权

| 状态 | 权威所有者 | 建议存储 |
|---|---|---|
| 本部署用户、ACL、配额 | Console | 持久数据库 |
| Device/Application/Workspace | Console | 持久数据库 |
| AppNode/Instance 调度状态 | Cloud Runtime Domain | 持久数据库 + 心跳对账 |
| 机器/逐 GPU/实例实际状态 | Service 汇总本机监督与 Render IPC；Console 保存带新鲜度的投影 | 认证管理长连接快照/事件；关键回执与对账记录持久化 |
| 节点资源预约与期望状态 | Console / Cloud Runtime；Service 执行本机最终准入 | 持久预约/任务、代际及节点回执，不以监控图表代替台账 |
| Endpoint 当前连接 | Broker | 内存；集群路由索引可放 Redis |
| SessionGrant | Console 持久化授权来源，Broker 执行运行状态 | 持久化依据/撤销记录/outbox；Broker 缓存与可恢复快照 |
| owner epoch、幂等结果、升级任务 | 各领域唯一 owner | 持久存储；恢复后隔离旧 owner，禁止代际回退 |
| P2P attempt/path nomination | Broker | 内存，短期 TTL |
| Relay 节点目录和负载 | Broker | 内存 + 心跳/指标系统 |
| Relay channel pairing | Relay | 内存 |
| 连接用量和审计 | Console/分析系统 | 异步事件持久化 |
| 媒体数据 | Client/Render/Relay | 不进入数据库或 Redis |

上表中的主业务持久数据库统一为 PostgreSQL；先迁移当前 Console/Auth/Desk 和事务基线，再按各领域阶段扩展表。
媒体和高频全量遥测不进入业务事务热路径，Redis 不替代持久授权、预约或任务。迁移与备份规则以数据库前置方案为准。

Redis 只用于多 Broker 的 Endpoint 路由、短期 Session/Grant 协调、去重和节点目录；不得把媒体包或所有热路径消息通过 Redis 转发。

## 13. Broker 集群与路由

单实例阶段可在内存保存连接与尝试，但授权依据、撤销、幂等结果和 owner epoch 必须可恢复。多实例阶段还需要解决：

- 同一 Endpoint 只能有一个当前有效 generation；
- 两个参与者可能连接到不同 Broker；
- Broker 之间需要路由会话信令和路径候选；
- 同一幂等请求不能在两个 Broker 重复创建 Session/allocation；
- Broker 故障后客户端可连接新实例并恢复 SessionGrant。

推荐顺序：

1. 入口层采用稳定 Endpoint 粘性路由；
2. Redis 保存 `EndpointId → BrokerId + generation + TTL`；
3. Broker 间使用认证消息总线或内部 RPC 路由控制消息；
4. Session 的单一 owner Broker 通过一致性选择确定；
5. owner 失效时通过明确的恢复/接管代际迁移，而不是两个 owner 同时工作。

## 14. 安全边界

### 14.1 身份认证

- 设备使用可轮换、可撤销的安装身份凭据；
- 用户使用 Console 登录身份；
- Node Agent 使用节点身份；
- Render/实例不能伪装成普通 Device，应由 Node Agent 或受控实例注册流程证明；
- Android 必须使用 `client_type=android`，不得伪装成 Panel；
- 连接日志不记录密码、完整证明材料或可重用秘密。

当前连接描述中的 `password_hash` 属于待退役的旧认证边界，新 Broker/Relay 不接受它作为跨服务凭据；迁移到新基线时删除对应旧准入路径。

### 14.2 授权

- Console 决定业务授权，Broker 执行 SessionGrant；
- Broker 不相信客户端自行上报的角色、额度或业务类型；
- Relay 只相信 Broker 创建的 allocation 和当前参与者证明；
- SessionId、EndpointId、DeviceId 都不是秘密，也不能单独用于准入；
- 撤销必须由 Console 传播到 Broker，再由 Broker关闭新 Attach 和必要的活动连接。

### 14.3 数据安全

- Relay 只转发端到端加密数据；
- 不因使用 Relay 降低媒体或控制通道的加密等级；
- 控制、媒体和文件通道应进行用途隔离；
- connection nonce 用于区分具体连接和防止旧握手重放，但不能替代长期身份和 SessionGrant。

## 15. 客户端配套改造

参考实现中优先评估复用的组件（不能视为当前 Pixels 已具备；取得可复现参考代码后审查所有权、协议和依赖）：

- `NativeSession`；
- `TcpPathAttempt`；
- `UdpPathAttempt`；
- `PathNomination`；
- Relay Path Factory；
- 已有控制/媒体通道实现。

将大型 Runtime 编排拆成：

```text
NativeConnectionCoordinator
  ├─ SessionSignalingAdapter
  ├─ DirectPathCoordinator
  │    ├─ TcpPathAttempt
  │    └─ UdpPathAttempt
  ├─ RelayCandidateProvider
  ├─ PathSelectionCoordinator
  └─ PathRecoveryCoordinator
```

仍由一个显式状态机做最终路径提交，避免 TCP、UDP、Relay 三个子系统同时争抢活动连接。本次迁移直接切到新基线，不保留旧协议适配器；未来发布的有限协议共存窗口仅用于滚动升级，规则见独立部署与升级实施计划。

客户端必须区分：

- `RemoteSessionTarget.RemoteDevice`；
- `RemoteSessionTarget.CloudApplication`；
- `RemoteSessionTarget.CloudWorkspace`；
- RDP 专用目标。

不得通过 `fallbackRemoteDeviceId`、Account 或合成 DeviceId 把云实例伪装成远控设备。

## 16. 部署模型

### 16.1 近期部署单元

近期保持四类部署单元即可：

1. `px_console`
   - 身份、管理、Remote Access Domain、Cloud Runtime Domain；
2. `px_connect_broker`
   - 在线信令、P2P、路径协商、Relay allocation；
3. `px_relay`
   - 纯数据转发；
4. 节点侧 `px_service + px_render/RDP Proxy`
   - 资源执行和媒体 Endpoint。

不要在第一阶段立即拆出 Identity、Remote、Cloud Scheduler、Probe、Presence、Rendezvous 等十几个微服务。

### 16.2 Windows

- `px_console.exe`、`px_connect_broker.exe`、`px_relay.exe` 可以由独立服务端套件安装；该套件不混入 Cloud Node、Client、Remote 桌面产品安装包；
- 它们即使默认部署在同一台机器，也必须通过明确接口协作，不能直接共享全局内存或数据库内部表；
- 服务安装、升级和卸载必须独立且幂等；
- 覆盖安装不得因 Broker/Relay 重启破坏 Console 持久数据；
- Windows 测试可将三者部署在一台机器上。

### 16.3 Linux Docker

- Console、Broker、Relay 使用独立容器；
- 配置 PostgreSQL、数据库备份执行器和内部服务认证；单机版不强制引入 Redis，集群协调确有需要时再启用；
- Relay 使用 host network 或明确映射所需 TCP/UDP 端口；
- 节点、区域和动态端点由配置描述下发，不把部署端口写死在客户端；
- 首版支持同一部署内多个 Render 主机和 Relay；Broker 多实例按第 13 节另验路由/owner，不能只增加容器数。
- 区域标签可预留，跨地区控制面/自动就近调度属于后续专项；Console 首版保持中心控制面。

### 16.4 多机管理与监控边界

多 Render/多 Relay、资源池、服务发现、容量门禁、应用分发和分批维护属于 P7 商业首版，不要求 Kubernetes。
管理员首版准备主机，节点认证、能力发现、应用准备与验证通过后进入池；一个 Console 管理本部署多台机器，不为每机复制账号库。
Render 由 Cloud Runtime 选机/逐 GPU 预约，Relay 由 Broker 分配；增加机器只承接合规新业务，不迁移已有游戏或 RDP 工作区。
缩容先关闭新准入、排空和对账；有保留工作区/本机数据/未知占用时不能自动销毁主机。

Prometheus/Alertmanager 是监控告警，不是容器编排或业务权威；自营公网商业版独立部署，小型私有版可选装，Grafana 可补充图表。
管理长连接/PG 预约/节点准入负责业务事实，监控负责历史趋势，不能用监控采样替代事务和新鲜度门禁。
Linux 服务初期沿用成熟部署工具和受限执行器；Kubernetes 可在后续适配，Windows Render 保持完整 Windows + Service 模式。
云厂商 API 自动采购、Console 常态多活、跨地区调度和活动 Relay 路径迁移单独立项。
阶段与规模门槛见 [部署计划第 1 节](server_deployment_and_upgrade_plan.md#1-交付物与责任边界)，
独立监控与批量后台见 [运维计划](service_operations_console_plan.md#64-独立监控发现与容量边界)。

## 17. 分阶段迁移计划

阶段编号已按新的产品目标调整；详细交付物、依赖和验收见 [实施阶段](server_deployment_and_upgrade_plan.md#9-实施阶段与完成条件)。
优先完成必要的 P0 数据契约和 DB0–DB5，再推进 P1/P2/P3；不先建设 Mongo 上的新调度/HA。

| 阶段 | 工作与出口 |
|---|---|
| P0 | 冻结部署身份、发行隔离、发现/认证/升级协议和中断预算；取得参考源码或明确替代实现 |
| DB0–DB5 | 全新 PostgreSQL 基础、Console/Auth/Desk 数据层、事务/幂等、备份恢复、空库初始化与功能验收；不做 Mongo 数据迁移，是服务拆分的前置门槛 |
| DB-HA | PostgreSQL 主备与自动切换专项；在 DB 基线后推进，公网/私有 HA 商业发布前必过 |
| P1 | Official/Customer 全产品构建与平台配置；两套真实部署验证双向隔离 |
| P2 | Console 领域边界、独立身份/许可证、持久 SessionGrant、撤销、幂等和恢复 |
| P3 | Broker/Relay 拆分、多 Render/多 Relay 身份发现与容量准入、类型化多业务连接和监控指标 |
| P4 | 单机私有部署套件、安全更新、离线交付，将 DB 阶段备份恢复能力纳入停机升级闭环 |
| P5 | Server 先覆盖升级、节点重连、Broker 恢复和 Relay 排空/维护窗口；不建设双实例控制面，按组件实测中断 |
| P6 | 应用分发、完整包预准备、按资源池分批升级/安全退役、Windows/Android 更新、安装身份和数据保护 |
| P7 | 多 Render/多 Relay 公网/私有验收，独立监控告警、安全/容量/故障演练和经测规模上限 |
| P8 | 云厂商自动扩缩容、Console 多活、跨地区调度、Kubernetes 适配、活动 Relay 路径迁移等；不推迟首版多机能力 |

数据库基线完成后 P1 可先实现发行与配置框架，但实际认证和连接隔离验收依赖 P2/P3；不得把 UI 隐藏地址当作完整准入控制。
每一阶段交付仍须包含测试、完整产品构建、制品校验和部署证据，不能仅以代码合并标记完成。

## 18. 测试与验收矩阵

本节的场景按[逐步开发与测试门禁](server_incremental_validation_plan.md)落地为用例、入口和证据；先通过相关前置门禁，再集成依赖它的新能力。本文和网页模型均不作为已通过测试的证明。

### 18.1 授权和身份

- 只知道 SessionId/DeviceId 无法加入；
- 非参与者身份 Attach 被拒绝；
- 角色提升被拒绝；
- 设备、节点、实例和用户身份不能互相伪装；
- 撤销后禁止新 Attach，并按产品策略处理已连接 Session；
- Android 身份审计为 `android`，不是 `panel`。

### 18.2 幂等和恢复

- CreateSession 请求超时后重试不重复创建；
- StartInstance 回执丢失后重试不启动第二个实例；
- 相同 Attach 重复发送成功；
- 新 generation 替换旧连接，旧消息无效；
- Stop 已停止资源返回成功；
- Broker、Relay、Console 分别重启后的恢复符合所有权规则。

### 18.3 P2P/Relay

- TCP、UDP、Relay 并行尝试不争抢或提前关闭活动路径；
- P2P 失败稳定回 Relay；
- Relay 活动时后台 Direct 成功后无损切换；
- 新路径提交失败保留旧路径；
- allocation 可重复连接，不因首次失败失效；
- Relay draining 不接收新会话，已有会话继续；
- 对称 NAT、UDP 禁用、端口变化和移动网络切换均有明确结果。

### 18.4 多进程

- UI 发起、Native Runtime Attach、文件进程加入同一 Session；
- 任一辅助进程退出不结束整个 Session；
- Runtime 崩溃重启可重新 Attach；
- 多进程重复操作通过 request_id 去重；
- 无进程能仅凭 ProcessInstanceId 获得授权。

### 18.5 业务生命周期

- 远控断开不停止 Device；
- 云游戏/云应用按各自空闲策略保留或停止 Instance；
- 云实例 Start/Stop 和 Service 心跳对账一致；
- 云电脑断开不注销 Windows Session、不删除用户、不杀工作区应用；
- 云电脑第二客户端在 busy 时被拒绝，不自动接管；
- RDP carrier 断开不触发 Workspace 销毁。

### 18.6 安全和容量

- Relay 转发内容保持端到端加密；
- 日志无密码、完整认证证明或敏感连接描述；
- allocation 数量、连接数、带宽和速率限制有效；
- 重放旧 generation/nonce 被拒绝；
- Broker/Relay 达到容量上限时返回稳定、可诊断的错误；
- 不使用 20371 或任何退役端口作为默认、探测或回退。

## 19. 明确不做

- 不把所有业务继续堆入一个 `SigServer`/`px_console_server` 巨型对象；
- 不把远控设备、云实例和云工作区统一成无类型的“设备”；
- 不让 Broker 直接决定应用 ACL、云实例调度或 Workspace 销毁；
- 不让 Relay 查询 Console 业务数据库；
- 不采用客户端短期一次性票据解决重连授权；
- 不把 SessionId、DeviceId、EndpointId 当作秘密凭证；
- 不因拆服务而同步重写整个媒体协议；
- 不在第一阶段拆成大量微服务；
- 不恢复标准WebRTC STUN/TURN，也不把Direct Host WebRTC协商或媒体送入现有Relay；
- 不恢复已退役端口或旧 Endpoint fallback；
- 不把当前 `px_desk_server` 改造成云电脑调度器。
- 不引入多企业租户体系；不允许 Customer 回退或接入官方业务平台。
- 不把发行字符串、静态内置秘密或 IP 黑名单视为客户端真实性证明。
- 不承诺单进程替换、活动 Render 替换、驱动升级或 Android APK 更新零中断。

## 20. 最终决策摘要

1. **产品有两个业务域**：Remote Access 与 Cloud Runtime。
2. **Cloud Runtime 内有三类工作负载**：云游戏、云应用、云电脑；前两者以 Instance 为核心，云电脑以持久 Workspace 为核心。
3. **业务域共用连接平台**：`px_connect_broker + px_relay`，但不共用业务生命周期。
4. **Console 保留身份、管理、业务编排和 Service 管理长连接**；会话 Endpoint 在线、P2P、路径与 Relay allocation 逐步迁出。
5. **Relay 是纯数据面**，不理解用户、设备、应用和 Workspace。
6. **不使用客户端短期一次性票据**；使用认证长连接、服务端 SessionGrant、稳定 SessionId 和幂等 generation。
7. **重连是 Session 的正常状态迁移**，不是重新创建业务授权。
8. **先模块化、后拆进程、再做集群**，每一步都保持现有远控和云业务可验证。
9. **一套服务端、两种客户端发行**：Official 固定官方入口，Customer 自填私有入口，独立部署无多租户。
10. **升级分级交付**：先有安全更新和恢复，再按 Server、节点、客户端顺序逐个覆盖并执行 Relay 排空；相邻正式接口兼容、schema 迁移和人工恢复边界先定义再实施。
11. **Render 首版采用整节点空闲升级**：云游戏节点按通常约 2–4 个并发用户规划，先准备完整包，停止新调度，等无用户且既有重连/任务保留结束后统一升级；不要求活动实例迁移或同节点新旧 Render 并行。
12. **数据库先改为 PostgreSQL**：唯一业务存储基线，先完成 DB0–DB5 的现有功能、事务与备份恢复，再拆服务；不保留 Mongo 双写或运行 fallback。
13. **媒体链路收敛**：Relay保持现名和既有数据转发；Direct Host WebRTC直接连接Render；ZLMediaKit、Coturn/TURN及中央RTC signaling归档，不提供兼容或fallback。

## 21. 调研证据索引

以下文件是本计划形成时的主要代码证据。外部参考路径只用于只读对照，不属于本仓库交付物。

### 21.1 `E:\source\signaling_server`

| 文件 | 观察结论 |
|---|---|
| `main.go` | 单一 `SigServer` 组合根并启动全部信令能力 |
| `server/sig_server.go` | 同时持有数据库上下文、ClientMgr、RoomMgr、ID 生成、Relay allocator、TCP/UDP 服务和 authorization broker |
| `app/context.go` | 日志、认证、SQLite DeviceManager 和设置仍在同一应用上下文初始化 |
| `server/client_id_generator.go` | 9 位 ID 由本地计数器和 Feistel PRP 生成；多机分段仍只是未来设想 |
| `db/device.go` | Device、install/fingerprint/MAC 和 legacy random password 等持久记录位于本地设备库；其中 MD5 密码模型不得成为未来跨服务凭据 |
| `native_tcp/rendezvous.go` | rendezvous 明确不拥有 ID 或授权，是可保留的正确边界 |
| `native_udp/rendezvous.go` | UDP 尝试与 TCP 类似，适合收敛到统一 RendezvousCoordinator |
| `server/native_tcp.go` | observer、attempt、在线连接校验和候选下发仍耦合在服务实现内 |
| `server/native_udp.go` | UDP observer、rendezvous 和 WSS 候选交换尚未形成独立服务边界 |
| `server/native_path_signal.go` | 双方路径 nomination 当前仍通过主信令服务路由 |
| `relay/allocator.go` | 只创建配对 UUID/端点并维护短期 allocation，不转发数据 |
| `relay/native_node_pool.go` | 节点选择基于静态优先级和 pending 数量，缺少真实带宽、连接和质量指标 |
| `server/NATIVE_SESSIONS.md` | 原型复用现有 device/password/quota 检查，明确不能视为最终生产身份与策略模型 |
| `deploy/hangzhou/swiftlink-native-signal.service` | Go signaling server 是独立部署进程 |
| `deploy/hangzhou/swiftlink-native-relay.service` | 数据面实际运行外部 `hbbr`，证明 Relay 数据面已经可以独立部署 |

### 21.2 `D:\dolit\client_customer_dolit\client`

| 文件 | 观察结论 |
|---|---|
| `native_transport/native_remote_runtime.h` | Runtime 可复用外部已经认证的信令连接，无需强制创建第二条 WSS |
| `native_transport/native_remote_runtime.cc` | 集中编排信令、Relay、TCP/UDP、质量、nomination、回退、恢复和 NativeSession，后续应按协调责任拆分 |
| `native_transport/signaling_room_client.cc` | presence/room、心跳、Relay request/response 和重连仍由一个客户端对象承担 |
| `native_transport/tcp_path_attempt.h` | 有界候选尝试，不主动销毁活动路径；最终选择由 owner 决定 |
| `native_transport/udp_path_attempt.h` | UDP/QUIC 尝试边界可直接复用 |
| `native_transport/path_nomination.h` | 纯状态归约，不自行开关 Socket 或重启媒体，是推荐保留的设计 |
| `native_transport/native_session.h` | 不依赖 UI/SCM/信令 JSON，可作为活动控制/媒体会话和路径替换的稳定核心 |
| `native_transport/README.md` | 多项能力仍具有实验性质，技术通过不能替代正式产品身份、安全、容量和故障验收 |

### 21.3 当前 Pixels 仓库

| 文件 | 与本计划的关系 |
|---|---|
| `rust_server/px_console_server/src/main.rs` | 当前 Console 组合根同时启动多类控制面和 Relay/媒体相关能力，是逻辑拆分的主要入口 |
| `rust_server/px_console_server/src/app_schedule/manager.rs` | 已有 Application/AppNode/Instance 调度、幂等、心跳对账和重启恢复逻辑，应归入 Cloud Runtime Domain |
| `rust_server/px_console_server/src/native_connection.rs` | 当前分别构造设备和应用实例连接描述，也暴露了字符串 signal target 与 password hash 边界问题 |
| `rust_server/px_console_server/src/console_relay/` | 当前 Console 内嵌 Relay 连接、房间和流量记录能力，后续迁往 Broker/Relay 边界 |
| `rust_server/px_console_server/src/rtc/` | 当前含待归档的中央RTC/TURN管理；只保留Direct Host消费者确实需要且不经Relay的类型化边界 |
| `rust_server/px_console_server/src/media_sidecar.rs` | ZLMediaKit与Coturn sidecar生命周期退出活动产品；移除前按归档规则保存完整当前实现 |
| `rust_server/px_desk_server/src/main.rs` | 当前只服务咨询、问题、版本等网站能力，不是 Cloud Desktop 服务 |
| `docs/android_cloud_apps_implementation_plan_20260914.md` | 已决定设备和云应用是独立资源域，客户端使用明确 CloudApplication target |
| `docs/console_app_schedule_plan.md` | 记录现有多机应用调度和 Service 启停链路 |
| `docs/console_app_nodes_plan.md` | 记录 Application → AppNode → AppInstance 模型和端口所有权 |
| `docs/rdp_application_mode_design.md` | 记录 Cloud Workspace/RDP 的单前端、拒绝第二客户端和 Windows Session 保留边界 |
| `docs/rdp_application_mode_implementation_plan.md` | RDP carrier、凭据、代理验证和实施顺序的专项计划 |

实施某一阶段前仍需重新读取对应文件和当时 Git 修订；本索引记录的是 2026-09-16 的调研基线，不能替代后续代码审查。
