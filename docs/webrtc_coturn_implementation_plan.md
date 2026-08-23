# WebRTC + Coturn 开发与验收文档

> 状态：核心链路、功能回归和单局域网双机故障注入验收已完成；仅剩不同公网/NAT 的物理网络门禁和容量门禁
> 更新日期：2026-08-24
> 范围：Pixels Console、Console Web、px_panel、px_client、px_client_rtc、px_render/net_rtc
> 验收报告：[webrtc_rtc_acceptance_report_20260824.md](webrtc_rtc_acceptance_report_20260824.md)

## 0. 当前实现结论

截至 2026-08-24，代码阶段和 10.0.0.90 实机回归已经完成：

- Console Web 的“WebRTC / TURN”设置页、内置 Coturn 状态、配置校验、事务应用和失败回滚。
- Console 内置 Coturn、最多 8 个附加 STUN/TURN Server、revision 持久化和短期 TURN REST 凭据。
- 保存后向在线 Panel/Service 推送 revision；节点启动、重连、收到推送及周期任务拉取配置。
- 连接 ticket 携带本会话的权威 ICE 快照、短期凭据、应用 Relay 地址和端口。
- Panel 在 `direct_probe_enabled=false` 时固定选择 `webrtc`；开启后直达成功选择 `webrtc_direct`，否则选择 `webrtc`。Direct 探测成功但实际建连失败时会作废旧 ticket，并自动重开标准 RTC。
- 同一份 `px_client.exe` 分离 `kWebRtcDirect` 和 `kWebRtc`，标准模式通过应用 Relay 完成 ticket 认证、SDP 和 Trickle ICE。
- `net_rtc` 和 Windows/Web Client 均接受多个动态 ICE Server，由 libwebrtc 自动选择 host、srflx 或 relay；活跃连接支持 `SetConfiguration` 和 ICE restart。
- 标准 RTC 的画面协议消息、控制、剪贴板、音频协议消息和文件传输通过各自 DataChannel 传输；Render 按 ticket 权限拒绝越权通道和消息。
- 浏览器标准 RTC 不接触全局 appkey，使用 ticket 绑定且不提前消费；ticket 最终仅由 Render 原子兑换。
- `px_turn.exe`、默认配置和许可证随 Console 构建/打包；Console 启动时自动生成运行配置并启动相邻的 `px_turn.exe`。
- WebClient 与原生客户端统计均展示 RTC 模式、配置 revision、选中候选对、TURN 节点/协议和 RTT。
- 10.0.0.90 已通过强制 TURN/UDP、仅 TURN/TCP、自动 Direct、Direct 故障回退、ICE restart、全功能、多屏和连续重连；Service 重启后 Panel、Render、Console 控制链在 3.3 秒内恢复。

当前未宣称完成的内容只剩无法由同一局域网故障注入等价替代的发布门禁：两台分别位于不同公网/NAT 的物理机器真实 relay 建连、对称 NAT/srflx 行为，以及并发 allocation、端口耗尽和长周期容量测试。执行步骤见第 18 节。

本期保持现有产品媒体协议：编码帧和音频协议消息复用现有 protobuf 管线，经 WebRTC DataChannel 传输；不在本期把它们迁移成独立 H.264/Opus RTP Track。这样不修改现有编解码和多显示器业务，也符合“不替换现有音视频业务协议”的范围约束。

## 1. 目标

项目保留两个独立的 Render WebRTC 插件，但二者不是两个同等级的手动传输选项：

- `net_rtc_local` 是局域网或明确可直达场景的特化版本。
- `net_rtc` 是配置 STUN/TURN、支持完整 ICE 探测的标准 WebRTC 实现。
- Panel 在连接前先探测 Render 是否能够直达；能够直达时优先使用 `net_rtc_local`，不能直达时使用 `net_rtc`。
- `net_rtc` 进入 ICE 流程后，由 libwebrtc 自动选择 host、srflx 或 relay 候选，不由业务代码手动指定连接路径。
- Pixels Console 所在机器默认运行 `px_turn.exe`，内置 Coturn 是默认 ICE Server；管理员可以在此基础上继续追加多个 STUN/TURN Server。
- Console 是 ICE Server 配置的唯一权威来源。配置保存后主动通知所有在线节点，节点同时定时拉取作为兜底。
- `px_client.exe` 保持单一可执行文件，但必须完整实现 Direct RTC 和标准 RTC 两条独立启动链路。

## 2. 已确认的架构原则

### 2.1 两个 RTC 插件的定位

| 连接模式 | 启动值 | Render 插件 | ICE Server | 连接能力 |
|---|---|---|---|---|
| RTC Direct | `webrtc_direct` | `net_rtc_local` | 不配置 | 只使用现有直达链路 |
| RTC Standard | `webrtc` | `net_rtc` | Console 下发的多个 STUN/TURN | host、srflx、relay 自动选择 |

`net_rtc_local` 是经过特化和验证的快速路径。本项目开发标准 RTC 时不得向其中加入 STUN、TURN、配置热更新、ICE Server 获取或自动回退逻辑。

`net_rtc` 使用标准策略：

```cpp
configuration.type =
    webrtc::PeerConnectionInterface::IceTransportsType::kAll;
```

典型选择结果：

```text
同一网络直接可达       -> host
跨 NAT 且打洞成功      -> srflx
直接连接和打洞均失败   -> relay（Coturn）
```

配置了 TURN 不代表强制经过 TURN。强制 relay 只作为测试诊断能力，不作为生产默认策略。

### 2.2 应用 Relay 与 TURN Relay 的区别

项目现有 Relay 和 Coturn 的职责必须分开：

- 应用 Relay：承载设备发现、房间准备以及 SDP/ICE 等业务信令，必要时也可承载项目自定义协议数据。
- TURN Relay：WebRTC ICE 的标准媒体和 DataChannel 中继节点。
- `net_rtc` 第一版复用现有应用 Relay 交换 SDP 和 Trickle ICE，但视频、音频、控制和文件传输在 PeerConnection 建立后走 WebRTC。
- 日志和界面必须分别显示“Signaling Relay”和“TURN candidate”，不能都简称为 Relay。

## 3. 开发前代码基线（历史记录）

开发前以以下事实作为基线；这些缺口已由本期核心实现处理：

1. `src/px_render/plugins/net_rtc_local` 已实现可用的局域网 WebRTC 路径，使用 `/alloc/local/rtc` 进行 HTTP 非 Trickle 信令。
2. `src/px_render/plugins/net_rtc` 已存在，但仍包含硬编码公网 STUN 和被关闭的示例 TURN 配置。
3. `src/px_deps/px_webrtc_client/rtc_connection.cpp` 在 local 模式下不配置 STUN，在标准模式下仍使用硬编码 STUN，示例 TURN 被禁用。
4. `webrtc_direct` 和 `webrtc` 在 `src/px_client/ct_main_ws.cpp` 中被折叠成同一个 `ClientNetworkType::kWebRtc`。
5. `NetClient` 当前收到 `kWebRtc` 后创建的是 `WebRtcLocalConnection`，标准 `WebRtcConnection` 只在 `Relay + enable_p2p` 条件下创建。
6. `src/px_web_client` 已定义 Direct RTC 和标准 RTC 类型，但标准 RTC 启动分支尚未完整实现。
7. Console 已能托管 `px_turn.exe` 进程并显示进程状态，但 Coturn 的正式认证、动态 ICE 配置和客户端下发链路尚未打通。
8. 当前 Coturn 配置启用了长期凭据机制，但没有形成可供所有 RTC 会话安全使用的完整凭据签发流程。

## 4. 总体连接流程

### 4.1 Panel 自动选择

```text
用户点击 Connect
        |
        v
Panel 向 Console 申请连接票据
        |
        v
Panel 探测目标 Render 是否可直达
        |
   +----+----+
   |         |
 true      false
   |         |
   v         v
webrtc_direct  webrtc
   |         |
   v         v
net_rtc_local net_rtc
               |
               v
        ICE 自动选择 host/srflx/relay
```

直达探测仅决定是否走 Local 特化插件。探测结果为 false 并不代表标准 RTC 必须使用 TURN；标准 RTC 仍可能通过自己的 ICE 检查选择 host 或 srflx。

### 4.2 Direct RTC

```text
px_client / Web Client
        |
        | HTTP offer/answer
        v
Render /alloc/local/rtc
        |
        v
net_rtc_local
```

此链路保持现状，不读取 Console ICE Server 配置。

### 4.3 标准 RTC

```text
px_client / Web Client
        |
        | 获取 ICE 配置和短期凭据
        v
Pixels Console
        |
        | SDP / Trickle ICE 信令
        v
现有应用 Relay / Console 信令
        |
        v
px_render / net_rtc
        |
        | WebRTC ICE connectivity checks
        v
host / srflx / Console Coturn / 附加 TURN
```

## 5. Console ICE Server 配置

### 5.1 默认内置 Coturn

当管理员没有手动修改时，Console 自动生成默认节点：

```text
stun:{console_public_host}:20128
turn:{console_public_host}:20128?transport=udp
turn:{console_public_host}:20128?transport=tcp
```

默认地址选择顺序：

1. 管理员设置的 Coturn 公网地址。
2. Console 的 `server_w3c_ip`。

默认端口：

```text
STUN/TURN listener：20128 TCP/UDP
TURN relay range：20200-20500 UDP
```

监听地址和对外公布地址必须分开：

- `listen_ip` 用于本机绑定，允许使用 `0.0.0.0` 或指定内网网卡。
- `public_ip` 用于 ICE candidate 对外公布。
- Console 位于 NAT 后时，通过 Coturn `external-ip` 映射公网地址和内网地址。

### 5.2 附加多个 STUN/TURN Server

内置 Coturn 默认作为基础节点保留。管理员可以继续添加多个外部节点，而不是保存一个地址后直接覆盖基础节点。

管理员可以显式停用内置 Coturn，以支持完全外部化的 TURN 集群，但不能删除内置节点定义。

建议的服务端配置模型：

```json
{
  "revision": 15,
  "managedConsoleServer": {
    "enabled": true,
    "publicHost": "console.example.com",
    "stunUrls": [
      "stun:console.example.com:20128"
    ],
    "turnUrls": [
      "turn:console.example.com:20128?transport=udp",
      "turn:console.example.com:20128?transport=tcp"
    ],
    "credentialMode": "console_ephemeral"
  },
  "additionalServers": [
    {
      "id": "turn-beijing",
      "name": "Beijing TURN",
      "enabled": true,
      "urls": [
        "stun:bj.example.com:3478",
        "turn:bj.example.com:3478?transport=udp",
        "turn:bj.example.com:3478?transport=tcp"
      ],
      "credentialMode": "static"
    }
  ]
}
```

约束：

- 只接受 `stun:`、`stuns:`、`turn:`、`turns:`。
- URL 规范化后去重。
- 不同用户名或凭据的 URL 不能合并到同一个 `RTCIceServer`。
- 第一版最多允许 8 个 ICE Server，每个 Server 最多 4 个 URL。
- 管理端显示顺序用于配置和诊断，不承诺 libwebrtc 严格按数组顺序选择节点。
- ICE 仍根据候选优先级、网络协议、可达性和 RTT 选择候选对。

### 5.3 Console Web 设置页

新增“WebRTC / TURN”管理页面，至少包含：

#### 内置 Coturn

- 启用/停用。
- 自动使用 Console 公网地址。
- 自定义公网地址和绑定地址。
- STUN/TURN 监听端口。
- Relay 端口范围。
- Realm。
- UDP/TCP 开关。
- 短期凭据有效期。
- 服务状态、最近错误和健康检查时间。
- 测试配置。
- 保存并应用。
- 恢复默认值。

#### 附加节点

- 新增、编辑、删除、启用和停用。
- 多 URL 编辑。
- 凭据模式选择。
- 用户名和密码设置。
- UDP/TCP/TLS 连通性测试。
- 最近健康检查、延迟和错误。

### 5.4 保存事务

配置保存必须是事务式操作：

1. 校验字段和 URL。
2. 检查端口冲突和 Relay 范围。
3. 内置 Coturn 发生变化时生成新的运行时配置。
4. 重启或平滑重载 `px_turn.exe`。
5. 执行 STUN/TURN 健康检查。
6. 健康检查成功后原子保存配置。
7. `revision` 加一。
8. 广播配置失效事件。
9. 失败时保留上一份可用配置并显示明确错误。

## 6. TURN 认证与凭据

### 6.1 Console 内置 Coturn

采用 Coturn REST 风格短期凭据：

```text
username   = {expires_at}:{session_or_subject}
credential = Base64(HMAC-SHA1(shared_secret, username))
```

要求：

- Console 首次启动时生成安装级共享密钥。
- 共享密钥保存在 `storage` 下受保护的文件中。
- 共享密钥不提交仓库、不返回 Web、不显示在 Panel、不写普通日志。
- 客户端和 Render 只拿到当前会话的短期 username/credential。
- 默认凭据有效期 300 秒，可配置但必须限制上下界。
- 已建立的 TURN allocation 不因签发凭据到期被主动拆除；新的分配或 ICE restart 必须获取新凭据。

### 6.2 外部 TURN

第一版支持：

- 静态用户名和密码。
- 外部服务自带的固定凭据。

后续可扩展：

- 外部 TURN REST secret。
- 第三方临时凭据 API。
- 区域和权重策略。

外部固定密码只保存在 Console 服务端。配置变更广播中不得包含密码。

### 6.3 配置与凭据分离

节点定时拉取的长期配置包含：

- revision。
- STUN/TURN URL。
- enabled。
- credential mode。
- 健康状态摘要。

每次创建标准 RTC 会话时另外获取：

- username。
- credential。
- expiresAt。
- sessionId。
- revision。

## 7. 配置推送与定时拉取

### 7.1 主动推送

保存成功后，Console 向所有在线节点发送失效通知：

```json
{
  "event": "rtc_ice_config_changed",
  "revision": 15,
  "changedAt": 1787472000
}
```

通知对象：

- 所有在线 px_panel。
- 所有在线 Service。
- 所有长期运行的 Render。
- Console Web 管理端。
- 正在运行且保持 Console 通道的 Web Client。

推送只发送 revision，不发送 TURN 密码或共享密钥。

### 7.2 主动拉取

节点在以下时机拉取：

1. 进程启动。
2. 登录或设备认证成功。
3. Console WebSocket 重连。
4. 收到 `rtc_ice_config_changed`。
5. 创建 `net_rtc` 会话之前。
6. ICE restart 之前。
7. 定时检查。

建议定时周期：

```text
基础周期：60 秒
随机抖动：±15 秒
```

请求携带现有 revision，并支持 ETag 或等价机制：

```http
GET /api/v1/rtc/ice-config?current_revision=14
```

未变化时返回 `304 Not Modified` 或等价的轻量响应，避免传输完整配置。

### 7.3 防止惊群

- 推送后各节点增加 0-3 秒随机延迟再拉取。
- 周期拉取带 ±15 秒抖动。
- Console 对相同 revision 使用内存快照。
- 节点只接受 revision 大于本地版本的配置。

### 7.4 活跃会话

配置变化时：

- `net_rtc_local` 忽略事件。
- 空闲 Panel、Client 和 Render 更新缓存。
- 新建 `net_rtc` 会话始终使用 ticket 中的最新权威配置快照。
- 已建立会话保持创建时的 ICE 配置，不因管理端保存而被强制打断。
- 配置推送用于刷新节点缓存和后续 ticket；活动会话 ICE restart/热迁移留作后续增强。

## 8. Panel 直达探测与连接编排

### 8.1 探测职责

Panel 在启动 px_client 之前执行轻量探测，不修改 `net_rtc_local`：

- Render HTTP 地址可达。
- 配置或专用 probe 接口返回正确 nonce。
- 设备 ID、实例 ID 与连接票据一致。
- 响应时间未超时。
- 目标应用实例仍处于 running。

建议参数：

```text
单次超时：800 ms
重试：1 次
总时限：约 1.6 秒
```

现有 `RenderApi::GetRenderConfiguration` 可作为首期探测基础；如果其结果不能可靠区分实例或票据，再在 `net_ws`/Render HTTP 层增加专用 probe API，不进入 `net_rtc_local` 目录修改。

### 8.2 测试阶段固定走标准 RTC

开发测试阶段使用明确配置：

```toml
[rtc]
direct_probe_enabled = false
```

此时探测固定返回 false，所有 RTC 连接进入 `net_rtc`。禁止在业务代码中散落临时 `return false`。

标准 RTC 稳定并完成验收后恢复：

```toml
direct_probe_enabled = true
```

### 8.3 Local 失败后的会话级回退（后续增强）

正式阶段如果探测为 true，但 Local 会话未在规定时间进入 Connected：

1. Panel 关闭本次 px_client/Local 会话。
2. 释放旧连接状态。
3. 向 Console 申请新的 ticket 和 nonce。
4. 使用 `--network_type=webrtc` 重新启动标准 RTC。

回退发生在 Panel 连接编排层，不进入 `net_rtc_local` 实现。

## 9. px_client 完整开发计划

### 9.1 单一可执行文件

继续使用：

```text
px_client.exe
px_client_rtc.dll
```

不新增 RTC Client EXE。Panel 通过 `--network_type` 决定同一个 Client 进程使用哪条连接链路。

### 9.2 分开网络类型

protobuf 采用追加值保持兼容，不能重排既有编号：

```protobuf
enum ClientNetworkType {
  kWebsocket = 0;
  kUdpKcp = 1;
  kWebRtc = 2;
  kRelay = 3;
  kUdpDirect = 4;
  kWebRtcDirect = 5;
}
```

启动映射：

```text
--network_type=webrtc_direct
  -> ClientNetworkType::kWebRtcDirect
  -> WebRtcLocalConnection

--network_type=webrtc
  -> ClientNetworkType::kWebRtc
  -> WebRtcConnection
```

必须同步检查：

- `ct_main_ws.cpp` 参数解析。
- `NetClient::Start()`。
- `ThunderSdk` 连接进度。
- `Settings::IsDirectMode()`。
- 统计面板网络类型。
- 仅文件传输允许的传输类型。
- 断线和自动重连分支。
- 日志中的模式名称。

### 9.3 Client 标准 RTC 启动顺序

```text
解析 webrtc 模式
  -> 建立 Console/信令认证
  -> 获取最新 ICE revision
  -> 获取本次会话短期凭据
  -> 初始化 px_client_rtc.dll
  -> 注入 RTCConfiguration
  -> 创建 PeerConnection
  -> 通过信令 Relay 交换 SDP/Trickle ICE
  -> 等待媒体轨和必要 DataChannel 就绪
  -> 通知 UI Connected
```

TURN 密码不得出现在 px_client 命令行。Client 使用已有 Console 地址、登录身份和连接 ticket 获取短期凭据。

### 9.4 RTC DLL 接口

本期已经扩展：

```cpp
SetIceServersJson(...)
```

transport policy 使用 libwebrtc 的 `kAll`；revision 与凭据有效期随 JSON 快照传入。候选对回调和不重建 PeerConnection 的 `RestartIce()` 留作后续诊断与热迁移增强。

Local 链路继续执行现有的：

```cpp
SetLocalRtcMode(true);
```

且不调用新增的 ICE Server 配置接口。

标准 RTC 执行：

```cpp
SetLocalRtcMode(false);
SetIceServers(console_ice_servers);
SetIceTransportPolicy(kAll);
```

### 9.5 标准 RTC 信令

重构现有依赖关系：

```text
旧：ClientNetworkType::kRelay + enable_p2p 才创建 WebRtcConnection
新：ClientNetworkType::kWebRtc 直接创建信令连接和 WebRtcConnection
```

信令连接只负责：

- 会话认证。
- 房间准备。
- Offer/Answer。
- Trickle ICE。
- ICE restart。
- 配置 revision。
- 会话关闭。

标准 RTC 建立后，媒体和文件不得继续重复经过应用 Relay。

### 9.6 音视频能力

本期标准 `WebRtcConnection` 复用已经验证的业务消息和编解码链，实现：

- 编码帧回调到现有 FFmpeg/Vulkan 解码链。
- 多显示器消息与窗口映射。
- 现有音频协议消息接收与播放。
- 现有控制协议中的关键帧恢复和显示器切换。
- 显示器切换。
- 虚拟显示器拓扑变化。

本期不迁移到 H.264/Opus RTP Track。独立 RTP Track、麦克风原生 Track 和 PLI/RTCP 属于媒体协议升级，不是 Coturn/ICE 接入的阻塞项。

### 9.7 DataChannel

标准 RTC 至少建立：

| Channel | 用途 | 可靠性 |
|---|---|---|
| `media_data_channel` | 普通控制和协议消息 | reliable + ordered |
| `input_data_channel` | 键鼠等低延迟输入 | 按现有输入语义配置 |
| `ft_data_channel` | 文件传输 | reliable + ordered |
| WebRTC stats/ICE state | RTT 和连通性诊断 | PeerConnection 原生状态 |

仅文件传输模式：

```text
--mode=file-transfer --network_type=webrtc
```

要求：

- 不创建视频、音频和输入能力。
- 只协商文件传输所需的 DataChannel。
- 控制会话内点击 File Transfer 时复用当前 `ft_data_channel`，不创建新进程。
- 只有没有现有控制会话时，才由现有规则启动独立文件传输进程。

### 9.8 Client 配置更新

- 空闲：更新内存缓存。
- 正在协商和已连接：保持会话创建时的 ticket 快照，避免管理保存打断业务。
- 新连接或完整重连：申请新 ticket，使用最新 revision 和新短期凭据。
- Direct RTC：忽略 ICE Server 事件。

### 9.9 Client 断线与重连

状态必须分开处理：

```text
SignalingDisconnected
IceDisconnected
IceFailed
DataChannelClosed
RemoteSessionClosed
```

要求：

- 短暂 `IceDisconnected` 不立即弹出整个会话断开。
- `IceFailed` 才进入刷新配置、刷新凭据和 ICE restart。
- 重建 PeerConnection 时彻底释放旧 tracks、sinks 和 channels。
- 不重复创建工作区或文件传输窗口。
- 一次性 ticket 不重复消费。
- 完整会话重建时申请新 ticket/nonce。
- 用户主动退出时不触发自动重连。

### 9.10 Client 统计

统计面板新增：

```text
Mode: RTC Direct / RTC Standard
ICE state
Signaling state
Bytes sent/received
```

当前 UI 已区分 RTC Direct / RTC Standard。候选类型、TURN 节点、RTT、可用码率和丢包的完整 `getStats()` 展示列入后续诊断增强；日志与 UI 始终不得显示凭据。

不得显示 username、credential 或共享密钥。

## 10. px_render / net_rtc 开发计划

### 10.1 冻结检查

开发开始前记录：

```text
src/px_render/plugins/net_rtc_local
```

的 Git 状态和文件哈希。每个里程碑以及最终验收都检查该目录没有代码差异。

### 10.2 net_rtc 动态 ICE 配置

删除：

- 硬编码 `stun:39.91.109.105:60498`。
- 示例 TURN 域名、用户名和密码。
- 永久关闭 TURN 的 `if (0)` 分支。

新增每会话配置对象：

```cpp
struct RtcIceConfiguration {
    std::vector<RtcIceServer> servers;
    uint64_t revision = 0;
    int64_t credential_expires_at = 0;
    bool enable_ice_restart = true;
};
```

每个 `RtcServer` 在创建 PeerConnection 之前取得配置快照。配置更新通过线程安全的插件上下文传递。

### 10.3 Render 获取配置

Render 在以下时机重新获取或接收最新配置：

1. Render 启动。
2. Console/Panel 通知 revision 变化。
3. 收到新的标准 RTC 会话。
4. ICE restart 前。
5. Console/Service 连接恢复后。

Render 使用设备身份、本次连接 ticket 或 Panel 的受认证本地代理获取配置。不得依赖启动命令行中的固定 Coturn 密码。

### 10.4 net_rtc 功能补齐

标准 RTC 要对齐当前产品能力：

- 复用主编码管线。
- 复用现有编码帧和音频协议消息的数据通道。
- 多显示器。
- 虚拟显示器。
- 控制 DataChannel。
- 文件传输 DataChannel。
- 连接 ticket 和权限检查。
- Trickle ICE。
- 新 ticket 完整重连。
- 会话退出和资源回收。
- 拥塞控制和发送队列保护。
- ICE/连接状态日志。

允许抽取 `net_rtc_local` 目录之外的项目内部公共组件，但不能为了复用而修改 Local 插件文件。两个插件继续生成独立 DLL。

## 11. Web Client 计划

现有 `rtc_direct` 行为保持不变。

补齐标准模式：

```text
connType=rtc_direct -> 现有 Direct RTC
connType=rtc        -> 标准 RTC + Console ICE 配置
```

标准模式执行：

```javascript
new RTCPeerConnection({
  iceServers,
  iceTransportPolicy: "all"
})
```

要求：

- 连接前获取最新 revision 和短期凭据。
- 支持多个 `RTCIceServer`。
- 支持 Trickle ICE。
- 每个新 ticket 使用权威 ICE 快照，不复用长期凭据缓存。
- 活跃会话保持现状；完整重连时取得新配置和凭据。
- 候选详情 `getStats()` 展示作为后续诊断增强。
- 浏览器页面刷新或主动退出时发送正常关闭事件，不触发错误重连弹窗。

## 12. API 与事件建议

具体路径可在实现时按现有 Router 规范调整，语义保持如下：

```text
GET  /api/v1/rtc/ice-config
PUT  /api/v1/admin/rtc/ice-config
POST /api/v1/admin/rtc/ice-config/test
POST /api/v1/rtc/session-credentials
GET  /api/v1/admin/rtc/turn-status
```

事件：

```text
rtc_ice_config_changed
rtc_session_offer
rtc_session_answer
rtc_session_ice
rtc_session_restart
rtc_session_closed
```

所有管理接口要求 Console 管理员权限。节点配置接口要求有效的 Panel/设备身份，短期凭据接口还必须校验连接 ticket、session ID、角色和有效期。

## 13. 开发阶段

### M0：文档和冻结基线（已完成）

- 完成本文件。
- 更新所有“WebRTC 只支持直连”的过期说明。
- 记录 `net_rtc_local` 冻结基线。
- 明确测试阶段 `direct_probe_enabled=false`。

### M1：Console 配置和 Coturn（已完成）

- 增加配置模型和数据持久化。
- 实现默认内置 Coturn 和附加节点。
- 实现 Console Web 管理页。
- 实现运行时 Coturn 配置、密钥生命周期和健康检查。
- 扩展 Console 面板状态。

### M2：版本推送和节点拉取（核心完成）

- 实现 revision。
- 实现保存后广播。
- 实现 Panel、Service 的启动拉取、事件拉取和周期拉取；Render/Client 从每次 ticket 获取权威快照。
- Service 周期拉取和推送拉取带抖动；Panel 结合心跳兜底。

### M3：Panel 自动选择（核心完成）

- 实现直达探测器。
- 测试配置下固定返回 false。
- 实现 Direct/Standard 启动参数选择。
- Local 已探测选路；Local 建连失败会旋转一次性 ticket 并自动重开标准 RTC，90号机故障注入回归通过。

### M4：Client 类型和信令分流（已完成）

- 增加 `kWebRtcDirect`。
- 修复 CLI、SDK、统计和重连分支。
- 将 `WebRtcConnection` 从 `Relay + enable_p2p` 中独立出来。
- 打通标准 RTC Offer/Answer/Trickle ICE。

### M5：net_rtc 与 Coturn（核心完成）

- 删除硬编码 ICE Server。
- 两端注入多个动态 ICE Server。
- 实现短期凭据。
- 本机和90号机验证 host、强制 relay、TURN UDP 与 TURN TCP；不同公网/NAT 的真实 srflx/relay 仍为外部环境门禁。
- 配置更新用于新 ticket；活动会话 `SetConfiguration`、ICE restart 和短期凭据刷新已实现并通过75秒在途更新回归。

### M6：功能对齐（DataChannel 产品链路已完成）

- 视频、音频、多显示器和虚拟显示器。
- 控制、输入、剪贴板和文件传输 DataChannel。
- Client 解码、音频播放和完整统计。
- Web 标准 RTC。

### M7：稳定性和发布（单LAN双机已验证，公网/容量门禁待执行）

- 本机与90号机双机全功能、断线、重连、退出和服务重启回归已通过。
- 强制 TURN UDP、阻断 UDP 后 TURN TCP 回退已通过。
- `direct_probe_enabled=true` 自动选路和 Direct 建连失败后标准 RTC 回退已通过。
- 外网/复杂 NAT、并发 allocation、带宽和端口耗尽仍待发布环境执行。
- 验收证据见 2026-08-24 报告。

## 14. 测试矩阵

### 14.1 配置

- 默认自动生成 Console STUN/TURN。
- 增加一个和多个外部节点。
- URL 去重和非法 scheme 拒绝。
- 内置 Coturn 停用和恢复。
- 配置失败时回滚。
- 保存后在线节点收到 revision。
- 丢失推送后周期拉取恢复。
- 离线节点重连后取得最新配置。
- 日志、命令行和状态接口无密钥泄漏。

### 14.2 Direct RTC

- `net_rtc_local` 目录无代码变化。
- 关闭 `px_turn.exe` 后 Direct RTC 正常。
- 候选保持现有直达行为。
- 多显示器、音频、控制、文件传输和虚拟显示器无回归。
- 用户主动退出不弹重连。

### 14.3 标准 RTC 候选

- 同一 LAN：选中 host。
- 不同 NAT 且可打洞：选中 srflx。
- 阻断直达和打洞：选中 relay。
- 强制 relay 诊断：必须经过 Coturn。
- TURN UDP 被阻断：尝试 TURN TCP。
- 一个 TURN 节点失败：其他节点仍可建立。
- 所有 TURN 节点失败且无法直连：明确失败。

### 14.4 标准 RTC 功能

- 单显示器和多显示器视频。
- 系统音频和麦克风上行。
- 鼠标、键盘和组合键。
- 剪贴板文本和文件。
- 控制会话内文件传输。
- 独立仅文件传输。
- 虚拟显示器创建、删除和切换。
- 录屏、截图和统计入口。

### 14.5 生命周期

- Console、Coturn、Panel、Render 和 Client 分别重启。
- ICE 配置在协商中发生变化。
- 活跃连接增加备用节点。
- 活跃连接当前 TURN 节点被删除。
- 短期凭据过期后 ICE restart。
- 网络切换和短暂断网。
- 主动退出、被控端退出、设备重启和会话接管。
- 连续连接/退出 100 次无残留进程、线程或会话。

### 14.6 性能与容量

- 1080p60 和目标最高规格持续运行。
- 大文件双向传输。
- 视频与文件传输并行。
- 多个 TURN 并发 allocation。
- Relay 端口范围接近耗尽时有明确告警。
- Console 配置推送大量节点时无请求惊群。

## 15. 验收标准

开发完成必须同时满足：

1. `src/px_render/plugins/net_rtc_local` 没有代码修改。
2. 测试阶段所有连接可通过配置固定进入 `net_rtc`。
3. 正式恢复探测后，可直达优先进入 `net_rtc_local`。
4. 不可直达进入 `net_rtc`。
5. `net_rtc` 能自动选中 host、srflx 和 relay。
6. Console 内置 Coturn 默认开箱可用。
7. 管理员可以追加多个 STUN/TURN Server。
8. 配置保存后在线节点主动刷新，推送丢失时定时拉取恢复。
9. Panel、Render、Client、Web 使用相同 revision 的配置。
10. Client 标准 RTC 的现有视频/音频业务消息、控制和文件传输完整可用。
11. 配置和短期凭据不会通过日志或命令行泄漏。
12. Coturn 进程、监听地址、Relay 端口和错误能在 Console 面板查看。
13. 发布前在真实外网确认选中 relay candidate，并验证 TURN UDP/TCP。
14. 发布前恢复 `direct_probe_enabled=true` 并完成自动选择回归。

## 16. 非目标与限制

- 不修改 `third_party`。
- 不修改 `net_rtc_local`。
- 不新增 px_client 可执行文件。
- 不替换现有音视频、控制和文件传输业务协议。
- 第一版不要求 `turns:` 和 443 端口，但配置模型预留 TLS URL。
- 第一版不实现多地域智能调度，只依赖 ICE 的标准候选选择。
- 不把长期 TURN 共享密钥提交仓库或下发到节点。
- 不把 Panel 的直达探测结果当作标准 RTC 最终候选类型。

## 17. 回滚策略

- Console 保留最近一份健康的 ICE 配置和 revision。
- 新配置健康检查失败时不发布 revision。
- 标准 RTC 出现严重问题时，可以关闭标准 RTC 入口，保留现有 WS、应用 Relay和 Direct RTC。
- Coturn 停止不影响 `net_rtc_local`。
- 恢复旧配置后广播新的 revision，不能复用已经发布过的旧 revision 数字。
- 配置迁移采用带默认值的新增字段，旧 `px_console.toml` 仍能启动并自动使用 Console 内置 Coturn 默认值。

## 18. 构建、启动与验收操作

### 18.1 构建

Coturn 二进制已经随仓库保存，普通构建不需要重新编译它。只有更新 Coturn 源码或依赖时才执行：

```bat
scripts\build_px_turn.bat
```

构建 Console 服务、Web、媒体和 Coturn 发布目录：

```bat
build_px_console_server.bat
```

构建同一份 Windows Client、RTC DLL 和文件插件：

```bat
scripts\build_px_client.bat build_official 8
```

构建 Panel、Render 和标准 RTC 插件：

```bat
cmake --build build_official --config RelWithDebInfo --parallel 8 --target net_rtc px_render px_panel
```

Web 前端单独验证：

```bat
cd web\px_console
npm run build

cd ..\..\src\px_web_client
npm run build
```

### 18.2 启动位置

1. 从正式 Console 发布目录启动 `px_console.exe`。
2. Console 从自身相邻目录查找并启动 `px_turn.exe`；不会搜索 PATH，也不会启动仓库中其他副本。
3. 首次启动在发布目录的 `storage` 下生成：
   - `rtc_ice_config.json`：带 revision 的管理配置；
   - `turn_rest_secret`：安装级随机密钥；
   - `turnserver.generated.conf`：当前运行配置；
   - `px_turn.log`：Coturn 日志。
4. 管理员登录 Pixels Console Web，进入“WebRTC / TURN”。确认状态为“运行中”，监听端口、对外地址和 Relay 端口范围正确。
5. 测试期保持“启用 Panel 直达探测”关闭。Panel 从设备卡片的 Connect 或 File Transfer 入口启动同一份 `px_client.exe`。

### 18.3 防火墙和 NAT

至少放行并映射：

```text
TCP/UDP 20128       STUN/TURN listener
UDP 20200-20500     TURN allocations
Console 应用 Relay 端口  SDP/Trickle ICE WebSocket
```

如果 Console 在 NAT 后，“监听 IP”填写本机网卡或 `0.0.0.0`，“对外公布地址”填写客户端可访问的公网 IP/域名。外部 TURN 可以在内置 Coturn 的基础上追加；首版外部 TURN 支持无凭据 STUN或固定用户名/密码 TURN，不支持把 Console 的临时密钥误用于第三方 TURN。

### 18.4 自动化结果（2026-08-24）

```text
px_console_server Rust tests       140 passed
px_service Rust tests               52 passed
px_client 专用构建                  passed
net_rtc + px_render + px_panel      passed
test_voice_call                     31 passed, 2 conditional skipped
WebClient voice state               19 assertions passed
Console Web unit tests              15 passed
Pixels Console Web production build passed
px_web_client production build      passed
build_px_console_server.bat release/package passed
bundled px_turn.exe --version        4.17.2
本机真实 STUN Binding 请求           passed（40-byte success response）
git diff --check                     passed
```

`net_rtc_local` 后续因 WebClient 双向语音、动态 RTC 配置、统计和恢复能力纳入了本期修改；早期“目录冻结”约束已经由后续明确需求替代，不再以 Git diff 为空作为验收条件。

### 18.5 双机/公网最终验收

1. 在 Console Web 保存配置，revision 必须增加；在线 Panel/Service 日志应收到相同 revision，断开推送后周期拉取也能恢复。
2. `direct_probe_enabled=false` 时连接，Client 统计必须显示 `RTC Standard`，Render 必须加载 `net_rtc`，不得加载 `net_rtc_local` 会话。
3. 同 LAN 验证画面、键鼠、音频、剪贴板、文件传输、多显示器和主动退出；主动退出不得弹错误重连。（本机 + 90 已通过）
4. 在不同 NAT 下连接，使用浏览器 `chrome://webrtc-internals` 或 libwebrtc 日志确认候选可为 srflx。
5. 阻断端到端 UDP/直达路径但保留 Console TURN，确认 selected candidate pair 的 candidate type 为 `relay`；传输文件并保持画面运行。（受控故障注入已通过，仍需不同公网复验）
6. 仅阻断 TURN UDP，保留 TCP 20128，确认可通过 TURN TCP 建连。（受控故障注入已通过，仍需不同公网复验）
7. 配置一个不可达的附加 TURN 和一个可达 TURN，确认 ICE 会选择可达节点。
8. 恢复 `direct_probe_enabled=true`：LAN 可达目标应启动 `webrtc_direct`，不可达目标应启动 `webrtc`。（本机 + 90 已通过）
9. 连续连接/退出并检查无残留 Client、RTC Session 或 Coturn allocation。（本机 + 90 已通过）

真实 relay candidate 和复杂 NAT 测试必须在至少两台处于不同网络的机器上执行；单机 STUN 响应成功只能证明 Coturn listener 与协议栈可运行，不能替代公网 relay 验收。

本次每个实测项的环境、注入方式、判定和证据摘要见 [2026-08-24 RTC 验收报告](webrtc_rtc_acceptance_report_20260824.md)。
