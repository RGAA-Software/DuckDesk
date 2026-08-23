# 远控语音通话设计（非 WebRTC 传输兼容）

> 状态：Windows 原生客户端 MVP 已实施并完成 10.0.0.90 实机验收（2026-08-22）；Web 与 AEC 待实施。
> 目标：Windows 主控端与 Windows Render 之间的一对一、全双工语音通话；音频媒体可走 WebRTC、UDP、KCP、WebSocket 或 Relay。
>
> 配套执行文档：[工程实施计划](voice_call_implementation_plan.md)；[测试与验收计划](voice_call_test_acceptance_plan.md)。

## 结论

RustDesk 的语音通话不是独立 SIP/RTP 栈。它在已有远控会话中发送 `VoiceCallRequest`、`VoiceCallResponse`、`AudioFormat` 和 Opus `AudioFrame`；桌面端通过 CPAL/PulseAudio 采集、`magnum-opus` 的 `LowDelay` 编解码并复用既有传输流。当前核查的源码未实现软件 AEC：没有 WebRTC Audio Processing、SpeexDSP 或 RNNoise 的调用。Android 仅把输入源切到 `VOICE_COMMUNICATION`，可能借助系统/驱动的语音处理，但不保证效果。

GammaRay 不应照搬 RustDesk 的“将全局音频输入切换为麦克风”方式。系统声音和通话麦克风必须是独立流，以免通话使桌面声音消失，或让多个会话争用一个全局输入设备。

AEC 与传输协议无关。非 WebRTC 媒体链路应复用 vendored libwebrtc 的 **Audio Processing Module（APM）**，但不需要创建 `PeerConnection`：APM 在本机完成回声消除、噪声抑制和自动增益，编码后的 Opus 仍可走 UDP/KCP/WS/Relay。

## 已实现范围（2026-08-22）

- `px_message.proto` 已加入 590–593 四类独立语音消息，并同步 Web proto 镜像；没有复用桌面声音 `AudioFrame`。
- `px_voice_call` 已实现一对一状态机、30 秒请求超时、严格 `call_id/request_id` 匹配、媒体序号去重、SDL2 全双工端点及独立 Opus VOIP 编解码。
- 固定媒体格式为 48 kHz、单声道、16-bit PCM、20 ms、32 kbps Opus，开启 FEC；音频帧走现有已认证的会话媒体通道。
- Render `voice_call.dll` 已实现认证 stream 绑定、单通话独占、明确同意/拒绝、超时、断线/挂断清理和收发统计。Windows 服务场景通过 `WTSSendMessage` 把确认框投递到 Render 所在用户会话，而不是依赖服务桌面的普通消息框。
- 原生客户端悬浮控制条已加入麦克风按钮；呼叫中再次点击即挂断。为可访问性和自动化保留 `F10` / `Ctrl+Alt+V` 等价入口。
- 主控请求超时会显式发送 `connect=false`，被控端按原 `call_id/request_id` 关闭待处理呼叫；迟到响应作为 stale/replay 丢弃。
- 当前 MVP 没有接入 APM/AEC，客户端会明确提示佩戴耳机；Web 客户端尚无语音 UI 和采集/播放实现。

## 当前代码基础

| 能力 | 现有位置 | 结论 |
| --- | --- | --- |
| 下行系统声音协议 | `src/px_deps/px_message_new/px_message.proto` 的 `kAudioFrame` / `AudioFrame` | 已有；仅表达已有的主机到客户端音频，不应承载反向通话语音。 |
| 系统声音采集 | `src/px_render/plugins/was_audio_capture/` | 已有 WASAPI/process loopback。 |
| Opus 编码 | `src/px_render/plugins/opus_encoder/opus_encoder_plugin.cpp` | 已有编码帧扇出；可复用 codec 库，但通话需要独立实例和队列。 |
| Windows/Android 下行播放 | `src/px_client/ct_audio_player.*`、`src/px_android/app/src/main/cpp/audio_player.*` | 已有基础播放器；通话需低延迟、可重建的独立播放路径。 |
| WebRTC local 下行音轨 | `src/px_render/plugins/net_rtc_local/audio_source_impl.*` | 已有 Render 到浏览器的 10ms 音频喂入。 |
| WebRTC local 上行麦克风 | `src/px_render/plugins/net_rtc_local/remote_audio_sink.*` | 已可接收浏览器 Opus 解码后的 PCM，但当前仅统计，不实际外放。 |

## 范围与产品规则

v1 范围：一对一、全双工、Windows 主控端与 Windows Render、显式请求/接受/挂断；暂不包含群语音、录音、语音留言或自动接听。

1. 发起者点击“语音通话”后才允许创建请求；被控端必须明确接受。
2. 已有远控权限不等于麦克风权限。来电应显示“对方将听到此设备麦克风”。
3. 同一 `stream_id` 同时只允许一个 `call_id`；设备采集、APM 和播放资源按会话独占。
4. 远控被接管、鉴权失效、Render 重启、用户关闭语音权限或连接断开时，立即停止采集并通知对端。
5. 系统声音、我的麦克风、对方语音各有独立开关和音量；通话时可选择压低系统声音（ducking）。
6. 默认采样格式为 48 kHz、单声道、16-bit PCM、20 ms Opus 帧；APM 内部以 10 ms 处理。

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
2. **Windows 音质阶段（待实施）**：APM/AEC、设备选择与热插拔、蓝牙/默认通信设备重建、抖动缓冲和码率自适应。
3. **WebRTC local / Web 客户端（待实施）**：浏览器来电 UI、权限、原生 audio track，以及 Render `RemoteAudioSink` 接入实际播放。
4. **Android（待实施）**：`RECORD_AUDIO`、`VOICE_COMMUNICATION`、AudioFocus、蓝牙路由、前台服务。

以上仅定义平台顺序。具体工作编号、依赖、里程碑退出条件、兼容策略、风险与交付清单以
[工程实施计划](voice_call_implementation_plan.md) 为准。当前状态不得笼统标记为“语音已完成”：
Windows 原生 MVP 已完成，正式 Panel 来电 UI、AEC/设备可靠性、WebClient 和发布矩阵仍需分别过门禁。

## 测试与验收

详尽的 P0/P1/P2 用例、环境矩阵、量化指标、90号机执行步骤、证据格式和发布门禁见
[测试与验收计划](voice_call_test_acceptance_plan.md)。本节只保留已经执行的历史基线，不能替代候选版本的正式验收。

### 已执行结果（2026-08-22）

| 项目 | 结果 |
| --- | --- |
| `test_voice_call` | 11/11：状态机、超时、重放/错呼叫、序号回绕、幂等清理，以及 SDL dummy 设备的采集→Opus→解码→播放闭环。 |
| `test_client_voice_call_protocol` | 5/5：请求关联、挂断身份、固定格式、独立消息类型、非零唯一请求 ID。 |
| `test_client_virtual_display` 回归 | 11/11，通过；悬浮控制条改动未破坏虚拟显示器状态/协议。 |
| 90 号机连接与画面 | 原生客户端经 WebSocket 连接成功，收到 `DISPLAY1`、`DISPLAY44` 和首帧，语音 capability 正常。 |
| 请求超时闭环 | 30 秒后主控发送同一 `call_id/request_id` 的 `connect=false`；90 收到取消，迟到响应在主控端按 stale/replay 丢弃。 |
| 双向媒体与挂断 | 自动化媒体验收使用一次性自动同意测试构建；持续约 46 秒后主动挂断。90：`tx=2140, rx=2156`；主控：`tx=2156, rx=2135`，证明双向均有实际 Opus 包。测试后已重新构建并部署必须本地确认的最终 DLL，远端与本地 SHA-256 一致。 |
| 90 最终状态 | `px_service` 运行，Render 位于 Administrator console 会话 1；临时计划任务、进程、凭据和测试文件已清理。 |

尚未完成的验收项是 AEC/扬声器主观回声、2 小时耐久、设备热插拔、Relay/UDP/KCP 专项弱网以及 Web/Android；这些不能由当前 MVP 的通过结果替代。

- 单元：状态机、请求重放、错误 `call_id`、超时、序号去重、乱序、抖动缓冲、PCM/Opus 编解码。
- 组件：确定性 PCM 向量经 APM/Opus/模拟网络后，校验帧数、时长、PLC 和队列水位。
- 集成：Windows 直连、Relay、UDP/KCP；边文件传输和高码率视频边通话。
- 可靠性：双端通话 2 小时，播放队列不单向增长、无持续内存增长、无声卡回调 underrun。
- 设备：USB 拔插、默认通信设备切换、蓝牙路由、睡眠恢复、权限拒绝、被接管、断网重连。
- 安全：未授权请求、伪造 `stream_id`/`call_id`、挂断后继续发帧、旧 response 重放都不得播放或重新启用麦克风。
- 主观：双方扬声器外放通话 10 分钟无明显回声；正常网络下单向端到端延迟目标不高于 150 ms。

## 观测与审计

记录脱敏的 `call_id`、`stream_id`、状态转换原因、通话时长、采集格式、Opus 码率、队列深度、丢弃/乱序/PLC 数、播放 underrun 和设备重建次数。不得记录 PCM、Opus payload、密码或完整 SDP。管理端应可禁用语音通话，并审计请求、接受、拒绝和结束。
