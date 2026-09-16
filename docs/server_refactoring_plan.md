# Pixels 服务端改造计划

> 状态：设计草案，等待按阶段实施
>
> 日期：2026-09-16
>
> 范围：`px_console_server`、未来连接协调服务、Native P2P/Relay、云运行时调度及其客户端协议边界
>
> 核心决定：远程连接与云业务共用连接基础设施，但业务资源、业务会话和生命周期保持独立；不采用客户端短期一次性票据。

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

- `px_console_server` 当前同时承载身份、设备、应用目录、实例调度、连接描述、部分信令、Relay、RTC/TURN 管理等职责。
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

```text
                                 px_console
                 统一 API、身份、租户、策略、管理与审计
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
      ├─ 节点注册、心跳和资源上报
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

- 用户、租户、角色、ACL、配额和策略；
- DeviceId、NodeId、ApplicationId、InstanceId、WorkspaceId 管理；
- 设备注册、禁用、凭据轮换；
- 应用目录、可见性、版本和兼容性；
- 云节点、GPU/容量和应用放置；
- 云实例和云电脑 Workspace 生命周期；
- 远控授权、云资源授权；
- 创建、撤销和审计 SessionGrant；
- Web/Panel/Android/Client API；
- 管理后台和异步用量记录。

逐步移出 Console：

- 终端公网长连接；
- TCP/UDP observer；
- P2P 尝试状态；
- 路径提名和质量切换；
- Relay channel pairing；
- 媒体数据转发。

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

### 5.4 `px_connect_broker`

负责实时连接控制面：

- 用户端、设备端、节点端和实例端的认证连接；
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

第一阶段可以继续使用或兼容现有 `hbbr` 数据面，不必为了服务拆分同步重写所有转发协议。但 Broker 与 Relay 的控制协议必须逐步类型化，并支持真实负载和健康度上报。

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

`WorkloadProfile` 至少区分：

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
TenantId
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

SessionGrant 是 Broker 内的服务端授权状态：

```text
SessionGrant
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

新代际注册成功后，旧代际不再有资格发送控制消息、提交路径或改变 Session 状态，避免旧连接晚到消息破坏新连接。

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

可选验证方式：

1. Relay 保存 Broker 下发的 allocation 状态；
2. Relay 通过内部认证通道查询 Broker；
3. 使用会话生命周期内稳定、可轮换的证明材料。

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
| 用户、租户、ACL、配额 | Console | 持久数据库 |
| Device/Application/Workspace | Console | 持久数据库 |
| AppNode/Instance 调度状态 | Cloud Runtime Domain | 持久数据库 + 心跳对账 |
| Endpoint 当前连接 | Broker | 内存；集群路由索引可放 Redis |
| SessionGrant | Broker 为运行权威，Console 为业务来源 | Broker 内存/共享 TTL 状态 + Console 审计 |
| P2P attempt/path nomination | Broker | 内存，短期 TTL |
| Relay 节点目录和负载 | Broker | 内存 + 心跳/指标系统 |
| Relay channel pairing | Relay | 内存 |
| 连接用量和审计 | Console/分析系统 | 异步事件持久化 |
| 媒体数据 | Client/Render/Relay | 不进入数据库或 Redis |

Redis 只用于多 Broker 的 Endpoint 路由、短期 Session/Grant 协调、去重和节点目录；不得把媒体包或所有热路径消息通过 Redis 转发。

## 13. Broker 集群与路由

单实例阶段只需内存状态。多实例阶段需要解决：

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

当前连接描述中的 `password_hash` 属于已有兼容模型，不应继续扩展为未来 Broker/Relay 的跨服务凭据。

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

保留：

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

仍由一个显式状态机做最终路径提交，避免 TCP、UDP、Relay 三个子系统同时争抢活动连接。服务端第一阶段可以兼容现有协议，避免服务拆分与媒体传输重写同时进行。

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

- `px_console.exe`、`px_connect_broker.exe`、`px_relay.exe` 可以由同一个安装包安装；
- 它们即使默认部署在同一台机器，也必须通过明确接口协作，不能直接共享全局内存或数据库内部表；
- 服务安装、升级和卸载必须独立且幂等；
- 覆盖安装不得因 Broker/Relay 重启破坏 Console 持久数据；
- Windows 测试可将三者部署在一台机器上。

### 16.3 Linux Docker

- Console、Broker、Relay 使用独立容器；
- 配置数据库、Redis 和内部服务认证；
- Relay 使用 host network 或明确映射所需 TCP/UDP 端口；
- 节点、区域和动态端点由配置描述下发，不把部署端口写死在客户端；
- 生产环境可以按区域独立扩展 Broker/Relay，Console 保持中心控制面。

## 17. 分阶段迁移计划

### P0：冻结概念和协议语义

- 批准本文的服务边界和命名；
- 明确 RemoteDevice、CloudInstance、CloudWorkspace 三类 Target；
- 明确禁用短期一次性连接票据；
- 为所有请求定义 `request_id`、SessionId 和 generation 语义；
- 记录现有协议、部署和端到端行为基线；
- 不改变当前生产流量。

验收：领域模型、状态机、错误码和幂等规则通过设计评审，不存在同一字段在不同业务中表达不同身份的问题。

### P1：在 `px_console_server` 内建立逻辑边界

先不拆进程，抽取窄接口：

```text
IdentityDirectory
RemoteAccessAuthorizer
CloudRuntimeScheduler
EndpointRegistry
SessionGrantRegistry
RendezvousCoordinator
RelayDirectory / RelayAllocator
AuditEventSink
```

- 将业务授权与实时连接状态分开；
- 将连接描述改为类型化 Target/Endpoint；
- 给启动、停止、Attach、allocation 增加幂等行为测试；
- 避免新增全局单例和无类型 JSON bag；
- 保持现有外部 API 可工作，必要兼容只放在边界适配器中。

验收：单进程部署行为不回退；内部模块可通过假实现独立测试。

### P2：引入稳定 Session 与 SessionGrant

- 建立 ConnectionSession 状态机；
- Console 创建业务 Session，Broker 模块保存运行 Grant；
- 客户端通过认证连接 Attach(SessionId)；
- 支持 Detached/重连；
- 路径失败不关闭 Session；
- 支持多进程 participant 和 connection generation；
- 删除把短期票据当作 P2P/Relay 重连条件的路径。

验收：模拟延迟启动、重复请求、客户端崩溃、多进程 Attach、网络切换和 Console 短时不可用，Session 均按规则恢复。

### P3：拆出 `px_connect_broker`

- 从 Console 迁出终端长连接、presence、observer、rendezvous、nomination 和 relay allocation；
- 建立 Console ↔ Broker 内部认证接口；
- Console 创建/撤销 SessionGrant；
- Broker 发布异步连接审计事件；
- 保证 Console 重启不主动结束已有 Session；
- 单 Broker 先使用内存状态。

验收：远控和云应用都通过 Broker 建立连接；Console 重启期间已连接会话继续运行，恢复后可对账。

### P4：独立 Relay 数据面

- 将现有 Relay 或 hbbr 兼容实现置于独立部署；
- Broker 负责创建会话级 allocation；
- Relay 支持同一 allocation 多连接、多通道和重连；
- 引入健康、容量、带宽和 draining；
- 验证 Direct → Relay、Relay → Direct 和 Relay 节点切换。

验收：首次握手失败、控制/媒体并发建连、进程重启、NAT rebinding 都不会因一次性凭证失效而失败。

### P5：两类业务完整接入

- Remote Access Domain 接入 Device Target；
- CloudGame/CloudApplication 接入 Instance Target；
- Cloud Desktop 接入 Workspace Target 和 RDP 专用 carrier；
- 每类业务使用独立生命周期策略；
- 统一审计但不混淆资源 ID。

验收：三类 Target 不能互相伪装；停止连接不会误停设备或错误注销 Workspace。

### P6：集群和多地区

- Broker 粘性路由和跨 Broker 消息；
- Redis Endpoint/Session 索引；
- Session owner 接管代际；
- 地域 Probe 和 Relay；
- 基于地区、运营商、负载和质量的调度；
- 故障演练和容量门禁。

验收：Broker/Relay 单实例故障可恢复；同一 Endpoint 不出现双活控制；媒体不经过 Redis。

## 18. 测试与验收矩阵

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
- 不把标准 WebRTC TURN 与 Native `px_relay` 混为一个协议；
- 不恢复已退役端口或旧 Endpoint fallback；
- 不把当前 `px_desk_server` 改造成云电脑调度器。

## 20. 最终决策摘要

1. **产品有两个业务域**：Remote Access 与 Cloud Runtime。
2. **Cloud Runtime 内有三类工作负载**：云游戏、云应用、云电脑；前两者以 Instance 为核心，云电脑以持久 Workspace 为核心。
3. **业务域共用连接平台**：`px_connect_broker + px_relay`，但不共用业务生命周期。
4. **Console 保留身份、管理和业务编排**；实时在线、P2P、路径与 Relay allocation 逐步迁出。
5. **Relay 是纯数据面**，不理解用户、设备、应用和 Workspace。
6. **不使用客户端短期一次性票据**；使用认证长连接、服务端 SessionGrant、稳定 SessionId 和幂等 generation。
7. **重连是 Session 的正常状态迁移**，不是重新创建业务授权。
8. **先模块化、后拆进程、再做集群**，每一步都保持现有远控和云业务可验证。

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
| `rust_server/px_console_server/src/rtc/` | 标准 WebRTC/TURN 管理继续服务 Web 产品，不与 Native Relay 协议合并 |
| `rust_server/px_console_server/src/media_sidecar.rs` | 当前 Console 管理媒体/TURN sidecar；拆分时需明确其仍属于 Web RTC 部署边界 |
| `rust_server/px_desk_server/src/main.rs` | 当前只服务咨询、问题、版本等网站能力，不是 Cloud Desktop 服务 |
| `docs/android_cloud_apps_implementation_plan_20260914.md` | 已决定设备和云应用是独立资源域，客户端使用明确 CloudApplication target |
| `docs/console_app_schedule_plan.md` | 记录现有多机应用调度和 Service 启停链路 |
| `docs/console_app_nodes_plan.md` | 记录 Application → AppNode → AppInstance 模型和端口所有权 |
| `docs/rdp_application_mode_design.md` | 记录 Cloud Workspace/RDP 的单前端、拒绝第二客户端和 Windows Session 保留边界 |
| `docs/rdp_application_mode_implementation_plan.md` | RDP carrier、凭据、代理验证和实施顺序的专项计划 |

实施某一阶段前仍需重新读取对应文件和当时 Git 修订；本索引记录的是 2026-09-16 的调研基线，不能替代后续代码审查。
