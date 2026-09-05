# 桌面远控逻辑会话、角色与直连访问产品定义

> 状态：目标产品契约（2026-09-01）。本文以 `app_mode=desktop` 远程桌面为主，并定义 game-hook/WebView 可选观看的产品边界；不改变它们既有的调度、实例身份和输入终点。现有运行时尚未完全满足，改造项见第 12 节。

## 1. 产品收敛

GammaRay 的一个 **desktop Render** 在任一时刻只允许 **一个控制者（Controller）**，并可同时服务多个 **观察者（Observer）**。这项规则不随媒体传输方式、是否经过 Console、或客户端类型变化。

- Controller：拥有完整远程会话能力：观看/收听、输入、剪贴板和文件传输。
- Observer：只观看/收听；不能输入，也不能使用剪贴板或文件传输。

“多控一”不是当前产品能力。第二个控制请求必须明确拒绝，或由已授权的显式接管（takeover）替换现任 Controller；绝不同时把两个来源的输入注入被控机。

观察者上限首期沿用 RTC Local 的 16 人能力。首期多观察只保证 RTC Local；WS 不以“多观察”承诺对外。UDP 永远不参与逻辑会话注册，只能消费可靠控制面已分配好的单一媒体端点。

### 游戏与 WebView 的观看扩展

`game-hook` 和 `webview` 也是 Console 调度的独立应用实例，它们可以选择支持“一人操作、多人观看”，但不与 desktop 共用控制目标或直接 IP 入口。

- **game-hook 游戏实例**：Console 为每次启动创建独立 `instance_id` 和独立 Render 进程。`allow_observer` 默认开启，提供一名 Player 和最多 16 名 Spectator；Spectator 仅接收游戏音视频。`allow_takeover` 开启时，Spectator 可接管成为 Player，旧 Player 降为 Spectator。输入始终只发往当前 Player 对应的 game-hook IPC。账号/存档隔离由游戏应用配置决定；游戏实例只走 Console 调度与授权，不开放直接 IP。
- **WebView 云应用实例**：每个实例拥有独立 CEF profile 与网页状态。`allow_observer` 默认开启，提供一名 Operator 和最多 16 名 Viewer；Viewer 只看页面音视频，不能向 CEF 发送鼠标、键盘或 IME。`allow_takeover` 开启时，Viewer 可成为 Operator，旧 Operator 降为 Viewer。Operator 的完整控制权只包括该 WebView 页面输入，绝不扩大为 Windows 桌面、系统剪贴板或本地文件权限。含有敏感登录态的应用可以显式关闭 `allow_observer`；WebView 不开放直接 IP。
- 多观看仍只首期承诺 RTC Local；UDP 只能一个媒体接收者，WS 完成逐会话隔离前不开放多观看。UDP 仅接收已由可靠控制面关联的音视频，不承载令牌、角色、接管或统计状态；各模式必须保留自己的实例身份和输入终点。

## 2. 统一逻辑会话

所有入口均创建服务端签发的 `LogicalSession`，而不是把客户端传来的 `stream_id` 当作权限。一个逻辑会话可以绑定多个可靠数据连接，例如 WS 控制、WS 媒体回退、RTC 媒体及文件传输；UDP 只是由这些可靠连接预先关联的媒体端点。

```text
LogicalSession
  session_id                 不可猜测、由签发方生成的会话标识
  subject                    Console 用户/设备，或 direct 本地访问主体
  render_instance_id         被访问的唯一 Render 实例
  role                       Controller 或 Observer；角色决定全部能力
  expiry + nonce             时效与防重放边界
  controller_lease           仅 Controller 有；含 lease_generation
  transport_bindings[]       WS / RTC / FT 的连接身份及状态；UDP 仅保存其控制面关联的媒体端点
```

`stream_id` 是会话的内部路由别名，必须一对一绑定 `session_id`，由 Console 或本机直连签发器生成。每一个新的 WS、RTC、文件传输连接在入场时都必须校验令牌的实例、角色、能力、有效期、服务端签发的 `stream_id` 及连接绑定；令牌有效期只限制新连接入场，已经建立的绑定由连接关闭、接管或服务端策略结束，不能因为短期令牌到期而在会话中途静默失去输入权或统计状态。旧客户端可能携带的 query `stream_id` 只是兼容输入，不能选择、覆盖或否决 ticket 已绑定的路由。UDP 不携带也不处理 grant：可靠控制面先创建并固定媒体端点/短期媒体密钥，UDP 仅把音视频发往该端点；未知端点或密钥的报文直接丢弃，绝不触发会话建立、接管、断开或重连。

媒体运输切换只改变 `transport_bindings`，不改变逻辑会话。UDP 失败而回退 WS、RTC 重协商、或文件通道单独关闭，都不能被解释为“用户离线”，更不能释放另一个仍在线 Controller 的按键或鼠标状态。

## 3. desktop 的两种入口

| 入口 | 身份与签发者 | 使用边界 |
|---|---|---|
| Console 入口 | Console 签发短时、签名的 session grant，绑定用户、Render 实例和角色 | Console ACL、审计、撤销与设备“允许接管”策略为权威来源 |
| 直接 IP（无 Console） | 不携带设备 ID，Panel 启动客户端前以设备安全密码摘要或本机随机密码完成认证，并让 Render 预留短期连接流 | 首期只支持 Windows 客户端的 Direct RTC，且只适用于受控局域网/VPN；设备 ID 不作为 IP 直连的认证条件，Render 生成的 `stream_id` 只用于连接路由和占用管理 |

无 Console 不等于无授权。没有设备 ID 的请求明确视为 IP 直连：Panel 在创建 `px_client` 前访问密码验证端点并提交设备安全密码摘要或本机随机密码；Render 认证通过后直接预留一个绑定来源地址与高熵启动 nonce、有效期 5 分钟的临时 `stream_id`。Panel 启动子进程时只传本来就需要的 `stream_id`、nonce、IP 和端口，不传远端密码，也不增加授权环境变量；`px_client` 只连接已经准备好的流，不做第二次密码校验。Render 不从本机 Console 配置补设备 ID，不要求 Console ticket 或携带 ID 路径的 direct-session grant，也不做设备身份匹配。认证通过后仍执行单 Controller、占用和接管等运行期安全规则，这些不是额外身份校验。当前实现使用 Direct RTC 的媒体和数据通道；Panel 中无 Console 的“自动”“直连”和“UDP 直连”选择均收敛到 Direct RTC，不开放无凭证的 WS/UDP/独立 FT。Console 入口的 WS 与 UDP 控制面必须在每条 WS 路由上携带 Console ticket；UDP 仍只承载音视频。密码和密码摘要不得写入响应日志或持久化存储。

### Direct RTC 本机 grant（已实现）

`POST /alloc/local/rtc` 只有在调用方明确携带设备 ID 的兼容直连请求中才使用仅驻留 Render 内存的、**5 分钟** `direct_session_grant`。没有设备 ID 的标准 IP 直连不签发或校验该 grant；Panel 在进程启动前完成密码认证，Render 同时预留正常的临时 `stream_id`，子进程随后只连接该流。旧版或独立诊断客户端没有预留流时暂时保留 SDP 端点直接验密码的兼容分支：

1. 首次请求带安全密码摘要和高熵 `client_nonce`。Render 从设备、来源和 nonce 派生本机签发的稳定 `stream_id`，并在成功响应返回它和不透明 grant；客户端传来的 `stream_id` 不参与直连身份或授权。密码不会写入响应、日志或后续请求。
2. 重连仅在 JSON body 提交 grant 和同一个 `client_nonce`，并由服务端校验设备、本机签发的 `stream_id`、客户端 nonce 和远端地址。校验成功时旧 grant 立即消费，响应签发新的 grant；因此任何旧值不能重放。
3. 地址/设备/路由/nonce 不匹配、已消费或到期均返回 `706 Direct session grant rejected`。若网络在轮换响应前中断，客户端安全地回退为重新进行初始认证；服务重启同样使内存 grant 失效。

本机 `net_ws` 插件的持久日志记录 `Direct RTC audit` 的 `initial_auth_rejected`、`grant_rejected`、`admission_rejected` 和 `admitted` 结果，以及设备 ID、不可逆 subject 摘要和接管标志。日志绝不记录密码、安全密码摘要、SDP、grant、nonce 或完整远端地址。该最小本机审计不替代 Console 的永久会话审计。

直连的 `subject` 使用 `direct:` 前缀及本机派生的不可逆摘要，绑定远端地址和客户端 nonce；它不能伪装成 Console 用户。无 Console 时，设备以 `--direct_allow_takeover` 控制是否允许**显式**接管，默认开启；它只影响 Direct RTC，不覆盖 Console ticket 的策略。直连没有中心 ACL、远程撤销和完整审计，因此产品默认关闭，且不宣称可安全暴露到公网。公网直连必须另行完成 TLS、强认证、限速和数据面令牌校验后才可开放。

## 4. 角色、接管和并发矩阵

| 场景 | Controller | Observer | 备注 |
|---|---:|---:|---|
| Console + RTC Local | 1 | 最多 16 | 首期完整的“一控多观”路径 |
| Console + WS | 1 | 暂不承诺 | 完成逐会话隔离前只支持单逻辑会话 |
| Console + UDP Direct | 1 | 不支持 | UDP 当前只可绑定一个媒体接收端 |
| 直接 IP + Windows 客户端（Direct RTC） | 1 | 首期不支持 | 仅允许一个活跃远控会话；第二个客户端必须显式请求接管 |

角色没有细粒度 capability 配置：Controller 天然拥有完整能力，Observer 天然仅观看/收听。设备只有一个与席位竞争相关的开关：**允许接管**。关闭时，当前 Controller 在其会话结束前不可替换；开启时，任意在线 Observer 可选择接管。直接 IP 首期虽默认启用观看策略，但 Direct RTC 尚不提供并发 Observer；它只允许新认证客户端在 `allow_takeover` 开启且调用方明确发起接管时替换现任 Controller。同一 `client_nonce` 的短暂重连仍视为同一逻辑会话，不要求重复确认。

## 5. 控制租约与输入安全

Controller 持有 Render 实例范围内唯一的控制租约。所有输入事件必须携带 `session_id` 和 `lease_generation`，Render 在解析后、注入前验证它们仍等于当前租约。无租约、过期、来自 Observer 或旧 generation 的输入一律丢弃并记录安全事件。

第二个控制请求的行为：

1. “允许接管”关闭：返回“控制席位已占用”。
2. “允许接管”开启：Observer 点击接管后，原子地使旧 Controller 的租约失效；新会话成为 Controller 并立即获得完整能力，旧会话降为 Observer。
3. 控制连接断开时，立即释放该会话按下的按键和鼠标按钮；会话保留 5 秒重连窗口。窗口内同一会话恢复后签发新 generation；窗口结束则 Controller 席位空闲。
4. Controller 结束后不自动晋升 Observer；任一 Observer 必须主动选择接管才成为新的 Controller。

按键、鼠标、组合键和输入统计必须按 `session_id` 维护，不能使用进程级全局 pressed-set。这样文件通道断开、媒体回退和观察者离开都不会造成误释放或卡键。

## 6. 各数据能力的隔离规则

- **视频/音频**：编码帧可以共享，发送队列、拥塞控制、丢包和带宽统计必须按 transport binding 隔离。RTC Local 可利用每 Peer 适配；UDP 只有一个由可靠控制面分配的媒体目标，不能接受第二观察者。
- **UDP 边界**：UDP 只承载可丢失、可乱序的音视频帧及其 FEC/关键帧请求等媒体反馈；不得承载认证、角色变更、接管、输入、文件、会话生命周期、统计快照或任何需要可靠抵达的命令。UDP 超时仅上报媒体不可用，由 WS/RTC 控制面决定回退与会话状态。

### WS + UDP 的媒体端点关联

当客户端选择 `udp_media=1` 时，WS 仍是唯一控制面。客户端在 WS 握手中提交高熵的关联码；WS 在完成 grant 校验和逻辑会话准入后，把该码记录为该 WS binding 的短期首次登记 **UDP media association**，其中只包含媒体关联码、目标 `stream_id`、到期时间和待登记状态；它不是角色令牌，不能建立、修改或关闭 LogicalSession。

关联码、ticket、nonce 和实例 ID 作为 WS 查询参数发送时必须逐值做 URI 百分号编码，禁止直接拼接。关联码当前来自 Base64 字符集，未编码的 `+` 会被服务端查询解析器解释为空格，形成随机的 WS/UDP 关联失败。UDP Hello 在首个媒体包到达前允许周期性重发，以容忍 UDP 丢包和端点登记时序；它仍只是已授权媒体面的端点发现，不承担认证、接管或逻辑会话状态变更。

1. 客户端使用关联码发送 UDP Hello，Render 只登记该报文来源的 `IP:port` 为媒体端点。
2. UDP Hello、心跳丢失、NAT 换端口和旧端点替换只能改变该媒体端点的 `pending/ready/unavailable` 状态，不能发送 `ClientConnected` / `ClientDisconnected`，不能接管，也不能影响 Controller lease。首个有效 Hello 之后，由心跳维持端点存活、由 WS binding 显式撤销；短时到期只保护首次登记，已关联端点可携带同一关联码完成 NAT 换端口。
3. 当前 UDP Direct 实现会在初始 WS binding 上暂停媒体下发，并由客户端等待首个 UDP 媒体帧；探测或 watchdog 失败时，客户端重新建立一个可靠 WS 媒体 binding。这个回退只改变 transport binding，绝不能由 UDP 自行宣布逻辑离线。
4. UDP 的 FEC、FrameStatus、IDR、RFI 等反馈只接受已关联的当前端点；它们是媒体反馈，不得转换为应用输入或逻辑会话事件。
5. 后续若要优化首帧黑屏，可增加可靠的“UDP ready”确认后再暂停 WS 媒体；该确认必须走 WS，不能由 UDP 报文本身改变会话状态。
- **资源优先级**：在机器 100 Mbps 网络等受限环境中，Controller/Player/Operator 的低延迟媒体优先；慢观看者独立降码率/降帧。存在互动会话时，文件传输默认限速至 20 Mbps，且不得挤占互动媒体发送队列。
- **剪贴板**：系统剪贴板是全局资源，仅 Controller 双向同步；不能向全部 `stream_id` 广播。Observer 不接收剪贴板。
- **文件传输**：路由键必须包含 `session_id` 和连接实例身份（例如 `connection_instance_id`），不能只使用 `stream_id`。文件通道关闭只能关闭自己的 route，不能触发整个逻辑会话的 disconnect。
- **生命周期事件**：传输事件与逻辑会话事件分开。只有最后一个必要 binding 离开、显式撤销或服务端策略终止时，才发 `LogicalSessionClosed`；短期 grant 到期只拒绝新的 binding，不能关闭已建立会话，媒体切换只发 `TransportChanged`。

## 7. 统计、展示与审计

在线人数按逻辑会话计数，不能简单累加 WS、UDP、RTC 插件连接数：同一用户的 WS 控制和 UDP 媒体仍是一人。UI 同时展示 Controller/Observer 数量与每个会话的当前媒体传输。

每个逻辑会话记录角色、主体、远端地址（直连）、媒体传输、输入租约状态、媒体质量、队列/流量、文件任务和回退次数。聚合统计另行计算。直连会话明确标注为 direct，不能归入 Console 用户或 Console 审计指标。

## 8. 逻辑状态机

```text
Issued -> Binding -> Active -> TransportSwitching -> Active
                  \-> Draining -> Closed
```

文件传输 binding 可以独立进入/离开；它不驱动 `Active` 以外的逻辑会话状态变化。显式断开、服务端撤销、控制租约被接管、或所有必需 binding 结束才进入 `Draining/Closed`。短期 grant 到期仅阻止新 binding 入场，已建立 binding 的生命周期仍由连接和服务端策略管理。

## 9. 对用户可见的产品文案

- “控制”：当前设备已有控制者。设备允许接管时，你可以选择接管；否则仅可观看。
- “观看”：观看不会影响现有控制；观看者不能操作被控设备。
- “直接连接”：此设备正在使用本地直连授权，适合受信任网络；其权限与 Console 帐户分开管理。
- “传输切换”：正在切换媒体通道，控制会话仍保持，不应显示为被控端离线。

Windows 客户端和 Panel 的用户界面统一把 Direct RTC 显示为“IP 直连”，不得暴露“RTC Local”等内部实现名称。WebSocket 完成 HTTP 升级不等于业务连接成功；只有 Render 完成 ticket 校验和逻辑会话准入后才可显示“已连接”。RTC 信令同样必须先解析 JSON 业务码，不能把所有 HTTP 403 都映射成密码错误：`700` 才是密码错误，`704` 是控制席位占用，`705/706/707` 是授权凭据拒绝，其余策略错误按会话策略拒绝显示。上述终止错误均停止 transport 自动重连并只展示一次对应提示。网络中断仍沿用 transport 重连策略，不能与业务拒绝混淆。

## 10. Console 改造范围

Console 需要从“按网络插件连接计数”改为“按逻辑会话管理和展示”，但不进入实时输入或媒体转发路径。

1. **设备与应用策略**：desktop 设备及每个游戏/WebView 应用实例都配置 `allow_takeover`；它是唯一的控制席位竞争配置。关闭时只允许当前操作员继续控制，开启时任一在线观看者可以接管。三类产品的 `allow_observer` 默认开启，WebView/游戏可按应用显式关闭。Console 同时保存 desktop 是否启用直接访问，但直接访问的密码/一次性码只在设备本地校验；游戏/WebView 不提供直接访问。
2. **会话授权**：观看入口签发短时 grant，包含 `session_id`、Render 实例、到期时间、nonce 与初始角色（desktop 为 Controller/Observer，游戏为 Player/Spectator，WebView 为 Operator/Viewer）。权限默认继承设备观看权限，WebView 可以叠加应用用户组限制。Render 回报会话结果；Console 不为输入、剪贴板和文件传输分别签发权限。
3. **会话事件**：接收并保存 `SessionOpened`、`RoleChanged`、`Takeover`、`TransportChanged`、`SessionClosed`。接管事件至少记录新旧会话、时间、原因和结果。
   会话事件为永久审计记录，不建立 TTL 删除策略；当前状态可覆盖更新，但绝不覆盖既有事件。
4. **Report 模型**：用 `session_id` 去重，展示 Controller 数、Observer 数、每会话当前传输和质量；WS、UDP、RTC 的连接数仅作为底层诊断，不能作为在线用户数。每会话质量独立保存，Render 总量另列为聚合指标。Console 接收快照时会合并同一 `session_id` 的传输集合；若同一 `session_id` 同时携带冲突的主体、路由、角色或接管来源，整份快照拒绝持久化并保留上一份可信状态。
   Render 每秒通过 Render→Service→Console 的可靠 heartbeat 上报 `logical_sessions_json` 快照；UDP 只可改变媒体端点状态，绝不可自行产生会话打开、关闭、接管或统计事件。
5. **Web UI**：应用/机器详情展示当前操作员和观看者列表、媒体通道、质量及接管记录；只有 `allow_observer` 开启的游戏/WebView 才展示观看入口，“接管”按钮只在 `allow_takeover` 开启时出现。接管由用户点击后立即执行，不增加审批、排队或细粒度授权界面。

直接 IP 且设备没有连接 Console 时，以上 Console 功能不构成可用性依赖：本机仍执行同一角色与接管规则，并保存最小本地审计。设备日后重新接入 Console 时，可以尽力上报历史摘要，但不能伪造为实时在线状态。

## 11. 测试策略

测试以“逻辑会话语义”而不是单一协议是否连通为主，并分为以下层次。

1. **单元测试**：使用可控时钟测试 SessionRegistry、grant 到期/重放、唯一 Controller、允许/禁止接管、旧 lease 输入拒绝、主控断开按会话释放输入、传输 binding 变化不关闭会话，以及统计去重。Controller-only 数据必须以服务端创建的 transport binding 查询 lease；测试须证明消息内伪造的 `stream_id` 不能让旧 Controller 或 Observer 冒充当前 Controller。
2. **插件集成测试**：启动测试网络栈，验证 Observer 的输入/剪贴板/文件请求被拒绝；两个会话并发文件传输不串流；文件通道关闭不释放主控输入；UDP 只接受 WS 预关联的一个媒体端点；丢失/重放 Hello、NAT 换端口、UDP 超时和 UDP→WS 回退都不产生逻辑离线，且 UDP 乱序/丢失绝不改变逻辑会话状态。
3. **Report 契约测试**：验证 Console 收到的 Controller/Observer 数、会话质量、传输切换和接管事件与 Registry 一致；同一 WS+UDP 会话只计一人，多 RTC Peer 不漏计，最后上报者不覆盖其他会话数据；同一 `session_id` 的重复传输条目必须合并，身份冲突的重复条目必须拒绝。
4. **双客户端验收**：在本机或局域网/90 号机，以一主控一观察者验证禁止接管、允许接管、5 秒重连窗口、UDP 回退、文件传输隔离和 Console 展示；再以直接 IP 的 Windows 客户端验证无 ID 密码预认证、预留流连接、单活跃会话、接管替换和重连路径；携带 ID 的兼容路径另行验证 grant 过期与重放。游戏与 WebView 还应分别验证一操作员一观看者、接管后的输入终点，以及 WebView 观看者不获得桌面/本地文件能力。该阶段不依赖公网。`scripts/run_rtc_multi_session_lan_case.ps1` 固化了 desktop RTC Local 的三项 LAN gate：Controller 与 Observer 并发健康、第二 Controller 在拒绝接管后得到业务层占用提示、以及显式确认后新 Controller 连接成功并使旧 Controller 降为 Observer；它为每个浏览器建立独立的短期 Console 身份和证据目录。`scripts/run_direct_rtc_negative_auth_case.ps1` 不需要任何密码，固定验证携带 ID 的兼容路径拒绝伪造 direct grant（403/706）、拒绝错误密码（403/700）、无 ID 兼容请求仍只按密码失败（403/700），以及无 ID 请求拒绝未预留的 `stream_id`（403/707）。
5. **生命周期回归**：重复 start/stop、连接注销时正在分发的回调、回调中 shutdown、以及队列销毁后回调，确认无遗留 lease、无卡键和无资源泄漏。

IP 直连的产品验收必须从 Panel 的设备卡片或直接调用 Panel 的
`RunningStreamManager::StartStream` 启动，证据必须同时包含 Panel 实际生成的
`--stream_id`、`--connection_nonce`，Render 对同一绑定的兑换结果，以及客户端首帧。
仅调用预验证接口后手工拼接参数启动 `px_client` 只能作为接口/组件测试，不能证明
Panel 启动链正确，也不得记为产品链路通过。生产参数构造与测试必须共用
`BuildStreamLaunchCredentialArguments`，其中 nonce 不依赖 Console ticket 是否存在。

## 12. 落地顺序与验收

1. 建立 `SessionRegistry` 与 Console/direct 两类 grant 签发和校验，禁止裸 `stream_id` 入站；grant 只携带 Controller/Observer 角色。
2. 将 WS、RTC、FT connection 绑定到 Registry；由可靠控制面为 UDP 预关联唯一媒体端点，UDP 显式拒绝其他端点且不改动 Registry，WS 在完成多会话隔离前限制为单会话。
3. 引入 Controller lease 和按会话输入状态；把传输断开与逻辑离线拆开。
4. 收紧剪贴板/文件的角色路由，修复仅按 `stream_id` 的目标发送。
5. 统计改为按逻辑会话，并在 Console/Panel 展示角色、当前通道和直连标识。

最低回归集：Controller + Observer 并发、第二 Controller 拒绝/接管、Observer 伪造输入、WS+UDP 回退不触发逻辑离线、FT 关闭不释放输入、两会话并行 FT 不串流、无 ID IP 直连只验证密码、携带 ID 的兼容 direct grant 过期/重放/越权，以及重复 start/stop 与队列回调析构。

## 13. 当前自动化验收基线（2026-09-03）

- `test_logical_session_registry`：12/12，通过唯一 Controller、Observer 隔离、接管、重连窗口、FT 独立关闭、transport 集合去重，以及“短期 grant 只限制入场、已建立 binding 不会中途失去输入租约”的语义。
- `test_file_transfer_route_registry`：6/6；WS 文件传输端到端以 1 MiB 文件完成上传、下载 SHA-256 校验和远端删除。
- `test_direct_session_grant_store`：6/6，通过 grant 轮换、URL 安全的预留流 ID、过期、对端绑定校验及并发重放仅一个成功者。
- `test_udp_media_fallback_state`：3/3，通过一次性 UDP→可靠 WS 媒体回退和停止后的迟到回调拒绝。
- `test_render_execution_context_lifecycle`：覆盖回调内 shutdown、注销时排队事件、回调内注销和重复 create/post/stop。
- `px_console_server` Rust 单测：152 通过；本机 MongoDB L1 原子 redeem/renew/binding 门禁已显式执行并通过（20 路并发 redeem/renew 各恰好一个成功，错误绑定不消耗 ticket，同一 Direct takeover redemption 可安全重试）。该门禁仍保持显式 ignored，CI 或新环境必须在具备 MongoDB 时单独执行。
- 90 号机部署后 `run_native_auth_case.ps1 -Mode account -NetworkType webrtc` 通过（RTC、视频、音频和文件通道均可用）；`run_rtc_multi_session_lan_case.ps1` 通过主控与观察者并发、第二主控业务级占用拒绝、显式接管和旧主控持续作为 Observer 观看。验收脚本要求接管后的最后采样仍存在 RTC Peer、候选对和视频统计，不能仅凭接管前的历史样本判通过。真实异网/NAT relay、端口耗尽和更大规模观察者验收仍按第 11 节执行，不能由本机自动化替代。
- 2026-09-03 在 90 号机补充原生 Windows 客户端回归：无 Console 的 IP 直连显式省略 `remote_device_id` 时，仅验证设备密码且不回填本机 Console 设备号，Direct RTC、首帧、音频和文件通道全部通过；一条 Controller 保持在线时，Console ticket 的直连 WS 收到一次业务级 `occupied` 拒绝，3 秒观察内自动重试为 0，且准入前未上报 `MsgNetworkConnected`；Controller 退出并越过 5 秒保护期后，同一路径正常连接、首帧和文件通道通过。`run_native_auth_case.ps1` 以 `-OmitRemoteDeviceId` 和 `-ExpectOccupied` 固化这两个回归入口。
- 2026-09-03 根据 Panel、客户端和 90 号机日志修复 IP 直连交替误报：失败样本中 Panel 的 `/verify/security/password` 已返回 200，随后子进程连接请求的 HTTP 403 实际业务码为 `704 occupied`，旧客户端却把所有 403 都显示为密码错误。修复后 Panel 在启动子进程前完成密码验证并让 Render 预留临时 `stream_id`，子进程只按正常参数连接，不再接收远端密码或额外授权环境变量。90 号机实测预留流可完成 IP 直连和视频首帧，子进程密码未配置、额外授权环境变量不存在；双客户端实测第二 Controller 得到 `403/704`、没有 `700`、自动重试 0 次；负向脚本的 `700/706/707` 四项门禁通过。
- 2026-09-03 复核发现上一条“预留流可连接”的手工子进程脚本绕过了 Panel 参数生成，不能作为 Panel 产品验收。真实 Panel 日志证明旧实现有 `stream_id` 却遗漏 nonce，Render 因而返回 `403/707`。修复后从真实 Panel 的 90 设备卡片启动，日志同时出现同一启动的 `ip-direct` stream ID 与 nonce，随后记录 `Rtc local, connected`、远端 transport connected、首个关键帧和首个 UI 解码帧。参数回归 3/3、终态/回退回归 7/7；授权或占用终态现在由子进程显式通知 Panel 关闭加载层，连接成功也会移除超时任务的 UI 状态。预留 stream ID 改为 32 位十六进制 URL 安全值，90 号机部署后的预验证接口已确认返回 `ip-direct:[0-9a-f]{32}`。
- 2026-09-03 完成 desktop、游戏和 WebView 的一控多看收敛。游戏 Hook 在无 RTC Peer 时保留最近捕获帧，并在新 Peer 建立时请求 IDR、重放缓存帧，解决游戏失焦停帧导致后加入者只有音频的问题；冷启动无客户端保护期调整为 45 秒。游戏 `app-14-08e37e67` 与 WebView `app-21-2f9cafde` 均在 90 号机通过 Controller+Observer、第二 Controller 业务拒绝、显式接管且原 Controller 降级继续观看三阶段门禁。
- 2026-09-03 修复 FT data channel 独立关闭误触发整条 RTC 客户端断开的生命周期错误。FT 关闭现在仅按逻辑会话和连接实例移除自己的文件路由，不释放输入租约；路由单测 6/6、插件上下文 10/10、FT DLL 生命周期 1/1、RTC DLL 重复启停卸载 2/2 均通过。
- 2026-09-03 修复 Service 被不同 Render 心跳互相覆盖 `logical_sessions_json` 的统计错误，改为按 `render_{port}` 聚合并在对应 Render 断开时独立移除。`px_service` 单测 63/63；游戏和 WebView 实际验收各得到 3 个逻辑会话、0 最终活跃、3 次 `SessionOpened`、1 次 `RoleChanged`、1 次带关联会话的 `Takeover`、3 次 `SessionClosed`，三条会话均保留 RTC Local 历史传输证据。Console 重启后运行中的应用实例恢复为 running，并可继续签发 Observer ticket；会话与事件记录永久持久化，不配置 TTL。
- 2026-09-03 发布版本 3.3.65：客户端完整 C++/Rust/Web dist、Console/Auth/Desk 服务端和 NSIS 安装包均完成；安装包 SHA-256 为 `B53D885F0006F5D07A8922A0F76FACE4493F62EF9D193F6420078399A1DC4F27`。90 号机 SYSTEM 静默安装返回 0，安装版本 3.3.65，服务为 Automatic/Running，20369/20371/20375 均监听，Parsec VDD 已签名且状态正常，安装目录与发布 dist 的 410/410 个文件 SHA-256 全部一致。真实公网/NAT 验收按用户要求暂缓，不计入本轮完成条件。
