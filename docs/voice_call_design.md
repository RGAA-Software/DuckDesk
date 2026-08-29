# 远控语音通话设计（非 WebRTC 传输兼容）

> 状态：Windows 原生客户端、正式 `px_panel` 授权闭环、M2 核心音频链和 WebClient 双向实现已完成；核心路径、真实 WASAPI 双工/2小时闭环以及 Chrome WebClient 双向 RTP 已在 10.0.0.90 验证。2026-08-24 又完成浏览器上行 RTP → Render 解码 PCM → 通话端点接收的量化闭环。双物理机主观音质、设备/弱网矩阵、生产 HTTPS 入口和完整发布矩阵仍待验收。
> 目标：Windows 主控端与 Windows Render 之间的一对一、全双工语音通话；音频媒体可走 WebRTC、UDP、KCP、WebSocket 或 Relay。
>
> 配套执行文档：[工程实施计划](voice_call_implementation_plan.md)；[测试与验收计划](voice_call_test_acceptance_plan.md)。

## 结论

RustDesk 的语音通话不是独立 SIP/RTP 栈。它在已有远控会话中发送 `VoiceCallRequest`、`VoiceCallResponse`、`AudioFormat` 和 Opus `AudioFrame`；桌面端通过 CPAL/PulseAudio 采集、`magnum-opus` 的 `LowDelay` 编解码并复用既有传输流。当前核查的源码未实现软件 AEC：没有 WebRTC Audio Processing、SpeexDSP 或 RNNoise 的调用。Android 仅把输入源切到 `VOICE_COMMUNICATION`，可能借助系统/驱动的语音处理，但不保证效果。

GammaRay 不应照搬 RustDesk 的“将全局音频输入切换为麦克风”方式。系统声音和通话麦克风必须是独立流，以免通话使桌面声音消失，或让多个会话争用一个全局输入设备。

AEC 与传输协议无关。非 WebRTC 媒体链路应复用 vendored libwebrtc 的 **Audio Processing Module（APM）**，但不需要创建 `PeerConnection`：APM 在本机完成回声消除、噪声抑制和自动增益，编码后的 Opus 仍可走 UDP/KCP/WS/Relay。

## 已实现范围（2026-08-24）

- `px_message.proto` 已加入 590–593 四类独立语音消息，并同步 Web proto 镜像；没有复用桌面声音 `AudioFrame`。
- `px_voice_call` 已实现一对一状态机、30 秒请求超时、严格 `call_id/request_id` 匹配、媒体序号去重、SDL2 全双工端点及独立 Opus VOIP 编解码。
- 固定媒体格式为 48 kHz、单声道、16-bit PCM、20 ms、32 kbps Opus，开启 FEC；音频帧走现有已认证的会话媒体通道。
- Render `voice_call.dll` 已实现认证 stream 绑定、单通话独占、明确同意/拒绝、超时、断线/挂断清理和收发统计。来电通过带完整关联身份与绝对截止时间的 Render/Panel IPC 交给当前交互会话中的 `px_panel`；生产代码已移除 WTS/MessageBox 路径，Panel 不可用时 fail closed。
- Panel 对外保留 `/panel` 客户端通道；仅供本机进程使用的 `/panel/renderer`、`/sys/info` 强制回环来源，避免局域网节点伪造内部来电或读取内部同步信息。
- 原生客户端悬浮控制条已加入语音呼叫按钮；呼叫中再次点击即挂断。通话建立后显示独立的麦克风与扬声器静音按钮，并带可访问名称和非纯颜色状态图标。为可访问性和自动化保留 `F10` / `Ctrl+Alt+V` 等价入口。
- 主控请求超时会显式发送 `connect=false`，被控端按原 `call_id/request_id` 关闭待处理呼叫；迟到响应作为 stale/replay 丢弃。
- M2 候选已接入独立 `px_voice_apm.dll`（AEC/NS/AGC）、10 ms APM、WASAPI 默认通信设备和显式端点选择、60 ms 抖动预填充、200 ms 上限、Opus PLC、设备重路由清理，以及原生客户端麦克风/扬声器静音。
- WebClient 已实现悬浮语音入口、capability/CMS 权限门控、浏览器用户手势授权、严格请求关联、两条独立 audio m-line、双向静音与全路径清理。Render 将浏览器上行 PCM 在 Panel 同意后送入同一个 WASAPI/APM 端点播放，Render 麦克风处理后的 PCM 则进入独立 WebRTC 下行语音轨；桌面系统声音保持独立。
- 非 WebRTC 语音增加独立最多5帧的传输队列，发生阻塞时丢旧保新；日志只记录稳定短哈希，不输出完整通话 ID。双物理机外放测试通过前仍保留耳机提示，不宣称已完成主观 AEC 验收。

## 当前代码基础

| 能力 | 现有位置 | 结论 |
| --- | --- | --- |
| 下行系统声音协议 | `src/px_deps/px_message_new/px_message.proto` 的 `kAudioFrame` / `AudioFrame` | 已有；仅表达已有的主机到客户端音频，不应承载反向通话语音。 |
| 系统声音采集 | `src/px_render/plugins/was_audio_capture/` | 已有 WASAPI/process loopback。 |
| Opus 编码 | `src/px_render/plugins/opus_encoder/opus_encoder_plugin.cpp` | 已有编码帧扇出；可复用 codec 库，但通话需要独立实例和队列。 |
| Windows/Android 下行播放 | `src/px_client/ct_audio_player.*`、`src/px_android/app/src/main/cpp/audio_player.*` | 已有基础播放器；通话需低延迟、可重建的独立播放路径。 |
| WebRTC local 下行音轨 | `src/px_render/plugins/net_rtc_local/audio_source_impl.*` | 已有 Render 到浏览器的 10ms 音频喂入。 |
| WebRTC local 上行麦克风 | `src/px_render/plugins/net_rtc_local/remote_audio_sink.*` | 已按授权 `call_id` 接收浏览器解码 PCM，并转入独立通话端点作 WASAPI 播放和 AEC reverse reference。 |

## 范围与产品规则

v1 范围：一对一、全双工、Windows 主控端与 Windows Render、显式请求/接受/挂断；暂不包含群语音、录音、语音留言或自动接听。

1. 发起者点击“语音通话”后才允许创建请求；被控端必须明确接受。
2. 已有远控权限不等于麦克风权限。来电应显示“对方将听到此设备麦克风”。
3. 同一 `stream_id` 同时只允许一个 `call_id`；设备采集、APM 和播放资源按会话独占。
4. 远控被接管、鉴权失效、Render 重启、用户关闭语音权限或连接断开时，立即停止采集并通知对端。
5. 系统声音、我的麦克风、对方语音各有独立开关和音量；通话时可选择压低系统声音（ducking）。
6. 默认采样格式为 48 kHz、单声道、16-bit PCM、20 ms Opus 帧；APM 内部以 10 ms 处理。

当前候选的 AEC reverse reference 已覆盖通话对端语音的实际 WASAPI 播放，但尚未把被控机所有本地应用的系统 loopback 可靠地对时混入同一个参考流。因此原生端和 WebClient 在每次发起前都明确提示：先暂停远控声音和被控机应用音频，再开始说话，并优先佩戴耳机；被控端 Panel 的来电授权框也要求接受前暂停本机应用音频。双物理机外放门禁通过、且系统声音参考链得到客观验证前，不得移除这些提示或宣称能消除被控机系统声音串入麦克风。

## 协议设计

不要复用既有 `AudioFrame`。在 `px_message.proto` 中新增未占用的消息类型和消息，并同步生成 C++/Rust/TypeScript 代码与 Web proto 镜像。

```proto
message VoiceCallRequest {
  string call_id = 1;       // 128-bit 随机值
  uint64 request_id = 2;    // 请求/响应匹配、防重放
  bool connect = 3;         // true=请求，false=挂断
}

message VoiceCallResponse {
  string call_id = 1;
  uint64 request_id = 2;
  bool accepted = 3;
  string reason = 4;        // rejected/busy/no_mic/unsupported/timeout
}

message VoiceAudioConfig {
  string call_id = 1;
  uint32 sample_rate = 2;   // v1: 48000
  uint32 channels = 3;      // v1: 1
  uint32 frame_ms = 4;      // v1: 20
  uint32 bitrate_bps = 5;
  bool fec = 6;
  bool dtx = 7;
}

message VoiceAudioFrame {
  string call_id = 1;
  uint32 sequence = 2;
  uint64 capture_time_ms = 3; // 单调时钟，仅用于时序和诊断
  bytes opus = 4;
}
```

协议约束：

- 所有信令与媒体均校验 `device_id`、`stream_id`、认证状态和 `call_id`。
- `VoiceCallResponse` 必须匹配未过期 `request_id`；不匹配即丢弃并记安全日志。
- 仅 `Connected` 状态接受媒体帧；挂断后所有旧帧均丢弃。
- 音频不可靠重传。序号重复、乱序过久或到达时已超过 500–1000 ms 的帧直接丢弃。
- 旧客户端未声明 voice capability 时，UI 置灰，不发送未知消息试探。

## 状态机

```text
Idle
 ├─ local request ──> OutgoingPending ── accepted ──> Connected
 └─ remote request ─> IncomingPending ── accepted ──> Connected

OutgoingPending / IncomingPending
 ├─ reject / timeout / busy / device error ──> Ended ──> Idle
 └─ disconnect / takeover / policy revoked ──> Ended ──> Idle

Connected
 └─ local hangup / remote hangup / disconnect / device fatal ──> Ended ──> Idle
```

请求超时为 30 秒；所有清理路径都必须幂等。UI 只消费状态机事件，不直接操作声卡或网络对象。

## 本地音频管线（目标形态）

### 发送端

```text
WASAPI microphone callback (10 ms PCM)
  → bounded PCM queue (最多 100 ms)
  → APM.ProcessStream()：AEC + NS + AGC
  → 聚合 20 ms PCM
  → Opus encoder
  → bounded transport queue (最多 3–5 帧)
  → UDP / KCP / WS / Relay
```

### 接收端

```text
network receive
  → session/call_id/sequence validation
  → jitter buffer（目标 60–100 ms，最大 200 ms）
  → Opus decoder + PLC
  → Voice playout queue（最大 200 ms）
  → WASAPI communication-output callback
```

音频 callback 中禁止内存分配、网络 I/O、磁盘 I/O、长时间锁和高频日志。队列满时丢最旧的语音帧，而不是让延迟持续增长。

## AEC：非 WebRTC 传输的实现

使用已有 vendored libwebrtc 中的 `webrtc::AudioProcessing`。它只作为本地 DSP 库使用，不创建 `PeerConnection`、不使用 RTP 或 SRTP。

```text
远端语音解码 PCM ─┐
本地系统声音输出 ─┼→ 按真实扬声器播放内容混音
                  └→ APM.ProcessReverseStream()（扬声器参考）

本地麦克风 PCM
  → APM.ProcessStream()（AEC + NS + AGC）
  → Opus → 网络
```

实现要求：

1. 每个通话端点一个 APM 实例，反向参考与麦克风必须由同一 10 ms 调度器处理。
2. 参考信号必须是**实际送给本机扬声器**的 PCM，而非网络收到的压缩包；包含远端语音，若本地系统声音也会从扬声器播放则同样应混入参考。
3. 输入/输出设备发生变化时暂停发送、清空队列、重建 WASAPI 与 APM，再恢复；不要把旧设备的参考喂给新设备。
4. AEC 无法消除耳机以外所有环境回声；第一版完成前，产品应提示扬声器模式需要 AEC，耳机效果最佳。
5. 若 v1 未接入 APM，产品文案必须明确“建议佩戴耳机”，不能宣称免回声通话。

## 传输适配

### WebRTC

WebRTC 路径使用原生 audio track，不把 Opus 再封装进 protobuf data channel。浏览器端使用 `getUserMedia` 并请求 `echoCancellation`、`noiseSuppression`、`autoGainControl`；Render 端把已存在的远端 audio track 从统计 sink 接到独立 WASAPI 播放器。WebRTC 自行负责 RTP、SRTP、NetEq、抖动缓冲、带宽反馈和 Opus。

### UDP / KCP / WebSocket / Relay

使用 `VoiceAudioFrame`。控制输入优先级高于语音，语音优先级高于视频 delta 帧；语音不得进入文件传输队列。UDP 允许丢包，不重传，使用 Opus FEC/PLC；可靠流发生队头阻塞时，仅保留最新 3–5 个音频帧。

## 平台实施顺序

1. **Windows 原生 MVP（已完成）**：协议、状态机、SDL2 麦克风采集/播放、独立 Opus、非 WebRTC 会话媒体通道、原生悬浮入口和本地确认。
2. **Windows 音质阶段（开发中）**：APM/AEC、WASAPI 通信设备后端、设备枚举和显式选择、默认设备重路由、抖动缓冲、PLC 和原生静音已实现；蓝牙/热插拔矩阵、码率自适应、双物理机音质和长稳仍待验收。
3. **WebRTC local / Web 客户端（已实现、核心实机路径已验证）**：浏览器悬浮入口、权限、严格关联、双向原生 audio track、独立静音，以及 Render `RemoteAudioSink` 授权后实际播放均已落地；90号机 Chrome 已通过接受/拒绝/超时、双向 RTP、独立静音和挂断清理。当前 `http://IP` 入口不属于浏览器安全上下文，生产环境必须提供 HTTPS（或 localhost）后才能调用麦克风；Edge、真实浏览器麦克风和异常矩阵仍待执行。
4. **Android（待实施）**：`RECORD_AUDIO`、`VOICE_COMMUNICATION`、AudioFocus、蓝牙路由、前台服务。

以上仅定义平台顺序。具体工作编号、依赖、里程碑退出条件、兼容策略、风险与交付清单以
[工程实施计划](voice_call_implementation_plan.md) 为准。当前状态不得笼统标记为“语音已完成”：
Windows 原生 MVP、正式 Panel 来电 UI 的核心 Console 路径和 WebClient Chrome 双向核心路径已完成；Panel 的完整 Windows 会话矩阵、AEC/设备可靠性、生产 HTTPS/Edge 和发布矩阵仍需分别过门禁。

## 测试与验收

详尽的 P0/P1/P2 用例、环境矩阵、量化指标、90号机执行步骤、证据格式和发布门禁见
[测试与验收计划](voice_call_test_acceptance_plan.md)。本节只保留已经执行的历史基线，不能替代候选版本的正式验收。

### 已执行结果（2026-08-23）

| 项目 | 结果 |
| --- | --- |
| `test_voice_call` | 33 项：本地 31 通过、2 项条件测试按设计跳过（2小时长稳、真实WASAPI）；覆盖状态机、日志脱敏、64包重放窗口、抖动缓冲、APM、WebRTC PCM播放参考、拥塞保新队列、静音恢复、并发Stop、设备枚举/切换事件、SDL dummy闭环、Panel IPC和授权缓存。真实WASAPI条件项已在90号机用显式麦克风端点单独通过。 |
| `test_client_voice_call_protocol` | 5/5：请求关联、挂断身份、固定格式、独立消息类型、非零唯一请求 ID。 |
| `test_client_virtual_display` 回归 | 11/11，通过；悬浮控制条改动未破坏虚拟显示器状态/协议。 |
| 90 号机连接与画面 | 原生客户端经 WebSocket 连接成功，收到 `DISPLAY1`、`DISPLAY44` 和首帧，语音 capability 正常。 |
| 请求超时闭环 | 30 秒后主控发送同一 `call_id/request_id` 的 `connect=false`；90 收到取消，迟到响应在主控端按 stale/replay 丢弃。 |
| 正式 Panel 授权 | 实际 `px_panel` 窗口展示访问者、开麦告知、倒计时、拒绝/接受按钮；UI Automation 保存控件树并真实点击接受/拒绝。接受后才创建音频端点；拒绝、等待中取消和 Panel 不可用 fail-closed 均通过。 |
| 双向媒体与挂断 | 正式 Panel 点击接受后持续约 128 秒并主动挂断。90：`tx=5623, rx=6082`；主控：`tx=6082, rx=5618`，证明双向均有实际 Opus 包。 |
| 加固候选回归 | 新 Panel 再次通过接受/拒绝/超时/等待中取消；56 秒媒体阶段主控 `tx=2655, rx=2459`、90 `tx=2463, rx=2655`。外部 `/panel` 可用，内部 `/panel/renderer`、`/sys/info` 均拒绝非回环连接。 |
| 自动化入口 | 三个测试目标已注册到 CTest；`ctest --test-dir build_official -C RelWithDebInfo --output-on-failure` 为 3/3。远端 UI 探针位于 `src/px_deps/px_voice_call/tests/integration/`，脚本不含凭据。 |
| M2真实声卡 | 90号机交互 Console 下 WASAPI 48 kHz mono 双工采集/播放回调通过；系统麦克风总开关为Deny时可复现并诊断为capture permission denied，临时Allow后通过，测试后恢复原值。详见 [M2测试报告](voice_call_m2_test_report_20260823.md)。 |
| Web实现与90号机E2E | `npm run test:voice` 19项关联/格式/每次通话前提示/安全上下文断言通过；`vue-tsc --noEmit && vite build` 生产构建通过。90号机 Chrome 实际连接正式 Render/Panel，接受、拒绝、30秒超时、双向 RTP、麦克风静音、通话扬声器独立静音、挂断清理和再次提示均通过。为隔离服务器 HTTP 限制，媒体正向 E2E 使用 Chrome 的仅测试安全源开关；正常 `http://IP` 另测为明确禁用并提示 HTTPS/localhost，不能把测试开关视作生产 HTTPS 验收。 |
| 2026-08-24 Web上行PCM闭环 | 浏览器麦克风 outbound bytes `95→7506`；90端收到48kHz/mono/16-bit/480-frame首个PCM，RTC统计153包/7506字节/0丢包，通话端点结束统计 `rx_pcm_samples=207840`。挂断后同一远控连接继续120秒并以1998帧/connected结束。详见 [RTC验收报告](webrtc_rtc_acceptance_report_20260824.md)。 |
| 2026-08-30 生命周期加固 | Render插件改为薄 ABI 适配器，`VoiceCallRuntime` 以共享所有权持有状态、端点和传输；设备丢失、PCM、Opus及传输发送回调不再捕获插件实例。音频端点及 SDL/WASAPI backend 的回调桥改用共享状态，PCM 参数改为 `span`，支持回调内 Stop、重复启停以及销毁后迟到回调安全丢弃。新增回调内注销、回调内Shutdown、外部Shutdown等待在途投递、销毁后迟到消息、10轮Runtime生命周期、传输回调内Stop及10轮DLL加载/卸载测试。`test_voice_call` 共39项；常规矩阵启用真实WASAPI后38项通过、仅2小时门禁跳过，2秒真实WASAPI→APM→Opus端点用例另行启用并通过；Runtime 6/6、传输3/3、客户端协议5/5、DLL 10/10、真实WASAPI smoke 10/10通过。focused构建发布`px_client.exe`、`voice_call.dll`、`px_voice_apm.dll`时强制校验`build_official\dist` SHA-256一致。 |

尚未完成的验收项是双物理机 AEC/扬声器主观回声、设备热插拔、Relay/UDP/KCP专项弱网、生产 HTTPS/Edge/真实浏览器麦克风异常矩阵以及Android；这些不能由当前自动化结果替代。

- 单元：状态机、请求重放、错误 `call_id`、超时、序号去重、乱序、抖动缓冲、PCM/Opus 编解码。
- 组件：确定性 PCM 向量经 APM/Opus/模拟网络后，校验帧数、时长、PLC 和队列水位。
- 集成：Windows 直连、Relay、UDP/KCP；边文件传输和高码率视频边通话。
- 可靠性：双端通话 2 小时，播放队列不单向增长、无持续内存增长、无声卡回调 underrun。
- 设备：USB 拔插、默认通信设备切换、蓝牙路由、睡眠恢复、权限拒绝、被接管、断网重连。
- 安全：未授权请求、伪造 `stream_id`/`call_id`、挂断后继续发帧、旧 response 重放都不得播放或重新启用麦克风。
- 主观：双方扬声器外放通话 10 分钟无明显回声；正常网络下单向端到端延迟目标不高于 150 ms。

## 观测与审计

记录脱敏的 `call_id`、`stream_id`、状态转换原因、通话时长、采集格式、Opus 码率、队列深度、丢弃/乱序/PLC 数、播放 underrun 和设备重建次数。不得记录 PCM、Opus payload、密码或完整 SDP。管理端应可禁用语音通话，并审计请求、接受、拒绝和结束。
