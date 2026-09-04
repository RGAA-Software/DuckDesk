# Render 内置模块与单向数据管线升级计划

## 1. 决策、目标和范围

Render 当前的大多数功能都是产品固定能力，不需要独立发现、替换或卸载。此次升级保留
数据源、数据处理器和数据观察者等插件能力，但将“插件能力”和“DLL 交付形态”分开：

- 除 WebRTC 外，当前有效的 Render 功能改为静态模块并链接进 `px_render.exe`。
- `net_rtc.dll` 和 `net_rtc_local.dll` 仍为动态库；它们与 WS、UDP、Relay 同属网络传输层，
  只是链接方式不同，不属于新的数据插件体系。
- `mock_video_stream` 和 `obj_detector` 退出生产构建与发布。
- 媒体数据按 Source -> Processor -> Encoder -> Sink/Observer 单向流动。
- 请求、响应、超时、重试和关闭等控制流程使用现有 `PxAwaitable`、`PxAsyncScope` 和
  `PxAsyncOneShot`。
- 高频帧不建立逐帧协程调用链，而是使用直接调用或有界队列；异步 Sink 使用一个
  长生命周期协程消费队列。
- 新增和本次触及的 GammaRay C++ 代码必须零裸指针并符合
  `docs/cpp_smart_pointer_standard.md`。
- 日志、错误码、性能聚合和隐私规则是架构交付的一部分，不作为事后补充。

此次升级不改变产品功能集合、协议语义或 WebRTC 内部对象模型。迁移按模块分批完成，
任何一个批次都必须保持可构建、可运行、可回退。

## 2. 非目标

- 不在此次升级中重写 libwebrtc 或 `src/px_deps/px_webrtc_client` 的借用 ABI。
- 不把 coroutine frame、STL 类型或 `asio::awaitable` 导出到 WebRTC DLL 边界。
- 不为 WebRTC 再建立 `IWebRtcTransport` 或通用 `ITransport` 虚接口。
- 架构稳定前不以“大文件移动”代替迁移；稳定后必须按职责移动实现并删除
  `src/px_render/plugins`，避免交付形态与源码归属继续矛盾。
- 不使用 `build_official.bat` 做日常开发或聚焦验证。

## 3. 目标架构

```text
RdApplication
      |
      v
RenderCompositionRoot
      |
      +-- RenderRuntime
      |   +-- PxAsyncRuntime
      |   +-- Control lane
      |   +-- State lane
      |   `-- Worker lane
      |
      +-- Domain Services
      |   +-- SessionService
      |   +-- InputService
      |   +-- DisplayService
      |   +-- ClipboardService
      |   +-- FileTransferService
      |   `-- VoiceCallService
      |
      +-- MediaPipeline
      |   +-- Source
      |   +-- Processor
      |   +-- Encoder
      |   `-- Observer / Sink
      |
      `-- NetworkTransportHub
          +-- WsTransport                 built-in
          +-- UdpTransport                built-in
          +-- RelayTransport              built-in
          +-- WebRtcLibrary/net_rtc       DLL
          `-- WebRtcLibrary/net_rtc_local DLL
```

### 3.1 数据面

视频数据流：

```text
DDA / GDI / GameHook
        |
        v
CaptureIngress
        |
        v
FrameProcessorChain
        |
        v
EncoderCoordinator
        |
        v
EncodedVideoFanout
        +-- NetworkOutput
        +-- MediaRecorder
        +-- LivePusher
        +-- FrameDebugger
        `-- Statistics
```

音频数据流：

```text
WAS / Process Loopback
        |
        v
AudioIngress -> AudioProcessor -> OpusEncoder -> EncodedAudioFanout
                                                +-- NetworkOutput
                                                +-- MediaRecorder
                                                +-- LivePusher
                                                `-- Statistics
```

### 3.2 控制面

```text
WS / UDP / Relay / WebRTC callback
        |
        v
NetworkIngress -> Parse/Validate -> Resolve LogicalSession -> Typed Command
                                                         +-- SessionService
                                                         +-- InputService
                                                         +-- DisplayService
                                                         +-- ClipboardService
                                                         `-- FileTransferService
```

Transport 只负责连接、收发、队列和协议边界。控制权限、takeover、输入 lease、显示器、
虚拟显示、剪贴板和文件传输业务全部归属领域 Service。

## 4. 现有组件的目标归属

| 当前目标 | 新角色 | 最终交付方式 |
|---|---|---|
| `cap_dda`、`cap_gdi`、`cap_was_audio` | Source | 静态模块 |
| GameHook/IPC capture | Source | exe 内部实现 |
| `frame_resizer`、`frame_carrier` | Processor | 静态模块 |
| `enc_nvenc`、`enc_amf`、`enc_ffmpeg` | Video Encoder | 静态策略模块 |
| `enc_opus` | Audio Encoder | 静态策略模块 |
| `frame_debugger` | Observer | 静态模块 |
| `media_recorder`、`live_pusher` | Encoded Sink | 静态模块 |
| `clipboard`、`ft`、`joystick`、`event_replayer`、`voice_call` | Domain Service/Sink | 静态模块 |
| `net_ws`、`net_udp`、`net_relay` | Network Transport | 静态模块 |
| `net_rtc`、`net_rtc_local` | Network Transport | 现有 DLL |
| `mock_video_stream`、`obj_detector` | 无产品用途 | 退出生产构建和发布 |

两个 WebRTC DLL 保持现有二进制身份、`GetInstance` 契约和卸载时机，但该契约只允许
存在于动态库实现与 `webrtc_library_host.cpp` 的最小兼容边界。Registry、消息路由、统计、
生命周期和测试均只能使用具体 `WebRtcLibrary`；不得把 WebRTC 放回通用插件集合。

## 5. 建议目录

```text
src/px_render/
|-- composition/
|   |-- render_composition_root.h
|   `-- render_composition_root.cpp
|-- runtime/
|   |-- render_runtime.h
|   `-- render_runtime.cpp
|-- pipeline/
|   |-- media_types.h
|   |-- video_pipeline.h
|   |-- audio_pipeline.h
|   |-- observer_dispatcher.h
|   `-- bounded_media_queue.h
|-- services/
|   |-- session_service.*
|   |-- input_service.*
|   |-- display_service.*
|   |-- clipboard_service.*
|   |-- file_transfer_service.*
|   `-- voice_call_service.*
|-- network/
|   |-- network_transport_hub.*
|   |-- network_ingress.*
|   |-- outbound_media_dispatcher.*
|   |-- ws_transport.*
|   |-- udp_transport.*
|   |-- relay_transport.*
|   `-- webrtc_library.*
|-- diagnostics/
|   |-- render_log_context.*
|   |-- render_error.*
|   |-- rate_limited_log.*
|   `-- performance_window.*
`-- modules/
    `-- module_ids.h
```

## 6. 现代 C++ 所有权与 RAII 约束

### 6.1 零裸指针硬门禁

本次新增和修改的项目代码不得声明、存储、传递、返回或捕获裸指针，包括局部变量、
成员、容器元素、函数参数/结果和 callback 参数。还必须满足：

- 不捕获 `[this]`；异步 callback 捕获 `weak_ptr` 并在使用点 `lock()`。
- 不使用手工 `new`/`delete`，使用 `make_unique`、`make_shared` 或 typed RAII handle。
- 不通过 `.release()` 把智能指针所有权交给 Qt parent。
- 不使用 `void*`、`std::any` 或整数转换隐藏对象所有权。
- 不跨 `co_await` 保存容器元素引用、锁保护引用或其他借用对象。
- C、Win32、Qt 和第三方 ABI 的裸值只允许在最小边界瞬时出现；现有兼容边界使用
  `NOLINT(gammaray-raw-pointer-boundary)` 并说明 ABI 和生命周期原因。
- 上述 marker 不允许用于新项目 API、普通局部变量、对象状态或异步捕获。

现有 `GetInstance` 和 libwebrtc 借用指针是仓库规定的兼容例外。本计划不新增同类表示，
也不把这些值传出兼容 host。

### 6.2 所有权表

| 对象 | 所有权和观察方式 | 关闭责任 |
|---|---|---|
| `RenderCompositionRoot` | `RdApplication` 持有 `shared_ptr` | 按逆依赖顺序 Stop/Drain |
| Service/异步 Module | owner 持有 `shared_ptr`，callback 持有 `weak_ptr` | 自有 `PxAsyncScope` |
| 独占实现资源 | `unique_ptr` | owner 析构或显式 Stop 后析构 |
| Thread | `std::jthread` 值成员或专用 RAII executor | `request_stop` + join |
| Win32 HANDLE | `wil::unique_handle` 或项目 typed unique handle | deleter 关闭句柄 |
| COM/D3D | `Microsoft::WRL::ComPtr` | 引用计数自动释放 |
| 动态库 | `DynamicLibrary` RAII owner | 遵守现有 WebRTC 卸载时机 |
| Subscription | move-only `ScopedSubscription` | 析构自动 unregister |
| Frame payload | `shared_ptr<const Frame/Data>` | 最后一个消费者释放 |
| 异步 Operation State | `shared_ptr<State>` | operation/scope 完成释放 |
| Observer registry | 存 `weak_ptr` | dispatch 时 lock，失效即跳过 |

### 6.3 生命周期标注

每个新异步类的头文件必须包含 `Lifetime` 与 `Threading` 注释：

```cpp
// Lifetime:
// - Owned by RenderCompositionRoot.
// - Asynchronous work is owned by async_scope_.
// - Callbacks capture weak_ptr only.
// - StopAsync cancels and drains all outstanding work.
//
// Threading:
// - Mutable session state is confined to PxAsyncLane::kState.
// - No mutex is held across a suspension point.
```

关键成员标注其角色：

```cpp
// Shared owner: keeps the service alive for the Render lifetime.
std::shared_ptr<SessionService> session_service_;

// Weak observer: does not extend the pipeline lifetime.
std::weak_ptr<VideoPipeline> video_pipeline_;

// Owner: unregisters the callback in its destructor.
ScopedSubscription session_subscription_;
```

### 6.4 Coroutine 规则

- 所有 coroutine 通过 `PxAsyncScope::Spawn()` 启动并具有稳定任务名。
- 优先使用 free/static coroutine，参数传 owned value、`shared_ptr<State>` 或 `weak_ptr`。
- 每个挂起点后重新判断 owner、scope cancellation 和 session generation。
- 每个外部等待必须支持 deadline 或 cancellation。
- State lane 串行修改 session、route 和 topology 状态。
- Worker lane 执行阻塞调用；Control/State lane 不执行 CPU 或同步 I/O 阻塞。
- 高频媒体帧不逐帧 `co_spawn`；Sink 使用长期运行的消费 coroutine。

## 7. 数据对象和插件能力

新增 owned/value 数据类型：

- `CapturedVideoFrame`
- `CapturedAudioFrame`
- `ProcessedVideoFrame`
- `EncodedVideoFrame`
- `EncodedAudioFrame`
- `CursorFrame`
- `NetworkEnvelope`
- `TransportRoute`
- `SessionCommand`

规则：

- payload 自持有且发布后不可修改。
- D3D/COM 资源使用 `ComPtr`，CPU buffer 使用共享不可变存储。
- route、codec、monitor identity 和 connection identity 使用值类型。
- frame 不保存 Source、Encoder、Observer 或 Transport 实例。
- 新数据路径不使用 `std::any`。

插件能力按职责拆分为 Source、Processor 和 Observer/Sink。它们可以是静态链接模块；
“插件”不再隐含“DLL”。Composition root 显式创建 factory，避免静态全局注册和运行期
service locator。Processor 不查找上下游，Observer 不改变主数据。

具体契约统一放在 `architecture/extensions/flow_node_plugin.h`：

- `FlowNodePlugin` 只保留描述、启停、enable 和异步生命周期，不继承旧
  `PxPluginInterface`；
- `VideoSourcePlugin` / `AudioSourcePlugin` 只获得 `MediaSourcePort` 输出能力；
- `VideoProcessorPlugin` / `AudioProcessorPlugin` 同步返回强类型处理结果，不持有或查找
  下游；
- `VideoEncoderPlugin` / `AudioEncoderPlugin` 把 captured frame 转为 owned encoded frame；
- `ObserverPlugin` 只读观察，不能修改、替换或阻断帧；
- `SinkPlugin::Submit*` 只允许快速校验和入有界队列，阻塞 I/O 由一个长生命周期消费
  coroutine 完成；
- factory 返回 `shared_ptr<FlowNodePlugin>`，不存在裸实例、`GetInstance` 或全局自动注册。

生命周期 callback 使用 `PxAwaitable`，因此 start、stop、drain、超时和取消可以线性
`co_await`，不再层层嵌套 completion callback。逐帧 Source/Processor/Encoder 调用保持同步，
不会为每帧 `co_spawn`。第三方 callback API 只在实现内部通过 `PxAsyncOneShot` 转成
awaitable，并在每个挂起点后重新 `weak_ptr::lock()`；WebRTC DLL ABI 不导出 coroutine。

## 8. 队列、背压和投递语义

每个异步 Observer/Sink 必须声明：

- 队列容量；
- 丢最旧或丢最新；
- 是否保护关键帧；
- 最大允许排队时间；
- shutdown 时 drain 或 cancel；
- backlog、drop、high-watermark 和处理耗时指标。

主链只执行快速 `Submit()`，不等待 Observer 完成。队列满不是通过日志刷屏处理，而是执行
明确策略并累计指标。关键业务 Sink 如果不能丢数据，应使用独立持久化/流控协议，不允许
无界增长内存。

## 9. 网络层

### 9.1 显式具体对象

`NetworkTransportHub` 显式持有 WS、UDP、Relay 和 WebRTC 具体对象，不使用
`vector<ITransport*>`，也不使用 `VisitNetPlugins()`。

出站调用分为：

- `BroadcastMedia(frame)`
- `SendControl(route, message)`
- `SendFileTransfer(route, message)`

`TransportRoute` 在发送前已解析完成，包含 TransportKind、LogicalSessionId、
ConnectionInstanceId、StreamId 和 ChannelKind。不再遍历所有 transport 逐个拒绝。

### 9.2 WebRTC 动态边界

`WebRtcLibrary` 是网络层具体组件，不定义新的通用虚接口。`WebRtcLibraryHost` 在其
`.cpp` 内部封闭现有 ABI：

```text
NetworkTransportHub
        |
        v
WebRtcLibrary --shared ownership--> WebRtcLibraryHost private state
                                      |
                                      `-- existing GetInstance ABI
```

旧裸实例只能留在动态库导出函数和 host `.cpp` 的兼容实现中，不进入 Registry、
Composition root、coroutine、callback capture、
route、service 或 frame。DLL callback 到达项目代码后立即复制为 owned/value 数据。
需要等待结果时，exe 使用 `PxAsyncOneShot` 把现有 callback 转为 awaitable；不把 awaitable
跨 DLL 导出。

## 10. 日志与可观测性规范

### 10.1 目标

任意一条错误日志必须能回答：

1. 哪个稳定事件失败；
2. 失败发生在哪个 component/stage；
3. 稳定错误码是什么；
4. 属于哪个脱敏 session/request/connection；
5. 是否可恢复，系统采取了什么动作；
6. 同一时间窗口内发生了多少次；
7. 相关队列、延迟和 transport 状态是什么。

新日志采用适合现有 spdlog 的单行 `key=value` 结构。首批不强制引入 JSON logger，避免
同时替换日志基础设施；字段名和事件名必须稳定，便于文本检索和后续机器解析。

统一示例：

```text
[ERROR] event=encoder.start component=nvenc code=ENCODER_NVENC_INIT_FAILED \
operation=create_encoder outcome=fallback recoverable=true monitor=8a4c70b1 \
native_code=8 attempt=1 fallback=ffmpeg
```

### 10.2 日志级别

| 级别 | 使用条件 | 示例 |
|---|---|---|
| ERROR | 当前操作最终失败、功能不可用、数据确定丢失或不变量被破坏 | 所有编码器失败、WebRTC DLL 加载失败、scope drain 超时 |
| WARN | 降级、重试、拒绝、异常输入或暂时失败，但系统仍能继续 | DDA 降级 GDI、过载丢帧、RTC 重连 |
| INFO | 低频生命周期和业务状态转换 | 模块启动、session 建立、transport 切换、配置摘要 |
| DEBUG | 开发诊断和低频步骤细节 | capability probe、路由选择原因 |
| TRACE | 默认关闭的深度诊断 | 单帧/单包信息，仅受控短时启用 |

普通断开、用户取消、权限拒绝和 shutdown cancellation 不应默认记为 ERROR。只有它们违反
当前操作预期或导致产品能力不可用时才升级级别。

### 10.3 稳定事件名

事件名采用小写分层命名：

```text
render.start
render.stop
module.start
module.stop
capture.start
capture.fallback
capture.frame_summary
processor.window
encoder.start
encoder.fallback
encoder.window
observer.queue_summary
transport.connect
transport.disconnect
transport.window
session.admit
session.close
workflow.complete
async.scope_stop
async.scope_drain
```

事件名表示“发生了什么”，错误原因放入 `code`，避免把动态信息拼进事件名。

### 10.4 稳定错误码

错误码使用大写、领域前缀、稳定原因：

```text
CAPTURE_DDA_INIT_FAILED
CAPTURE_SOURCE_STOP_TIMEOUT
ENCODER_NVENC_INIT_FAILED
ENCODER_ALL_BACKENDS_FAILED
PIPELINE_INVALID_FRAME
OBSERVER_QUEUE_OVERFLOW
TRANSPORT_RTC_LIBRARY_LOAD_FAILED
TRANSPORT_SEND_QUEUE_FULL
SESSION_ADMISSION_DENIED
WORKFLOW_DEADLINE_EXCEEDED
ASYNC_SCOPE_DRAIN_TIMEOUT
MODULE_DEPENDENCY_UNAVAILABLE
```

要求：

- 错误码一旦发布不得改变含义或复用。
- `PxResult`/`PxAsyncError` 在调用链中携带 code、stage、recoverable、native_code 和
  简洁原因；不要靠解析日志文本判断错误。
- 底层返回 typed error；由“决定重试、降级、返回客户端或终止操作”的所有权层记录一次
  主错误，避免同一失败在五层重复记录。
- 下层如需记录，只能用 DEBUG 或增加新的、不同语义的边界事件。
- 捕获异常必须转成稳定错误码；禁止只打印 `exception.what()` 而没有 code/stage。

### 10.5 必填字段

所有 WARN/ERROR：

```text
event
component
code
operation
outcome
recoverable
```

按上下文增加：

```text
trace
request
session
connection
stream
transport
monitor
codec
attempt
deadline_ms
elapsed_ms
queue_depth
queue_capacity
dropped
native_code
fallback
```

字段缺失时省略，不输出空字符串占位。数值单位必须进入字段名，例如 `elapsed_ms`、
`latency_us`、`bytes`、`bitrate_bps`。

### 10.6 关联上下文

建立值类型 `RenderLogContext`，随 command/workflow 复制：

```text
trace_token
request_token
logical_session_token
connection_token
stream_token
transport_kind
```

上下文不得保存 transport、session 或 logger 实例。跨 coroutine 时按值复制，不能保存
借用引用。内部原始 ID 通过 `PrivacyLogId()` 生成短关联 token 后再输出；日志 token 仅供
诊断，不能用于认证或业务查找。

### 10.7 隐私和敏感数据

禁止记录：

- appkey、ticket、token、nonce、密码和 cookie；
- 完整设备 ID、stream ID、session ID、call ID；
- SDP、ICE credential、Authorization header；
- 剪贴板正文、文件内容和语音 PCM；
- 完整本地文件路径；
- 未脱敏的公网 IP、用户名、窗口标题和显示器名称。

允许记录：

- `PrivacyLogId()` 生成的稳定短 token；
- 文件大小、媒体尺寸、codec、统计计数；
- 经过分类的网络类型和错误码；
- 必要时经过掩码的地址族、端口或 IP 前缀。

任何新 string 字段在评审中默认按敏感字段处理，必须证明可以输出，而不是反过来等待
发现泄漏。

### 10.8 去重、限频和日志风暴控制

- 禁止逐帧、逐音频包、逐鼠标移动或逐网络包输出 INFO/WARN/ERROR。
- 高频错误首次立即输出，之后按 `(component, code, route token)` 聚合。
- 默认聚合窗口为 5 秒；持续异常每 30 秒输出一次 summary。
- summary 包含 `count`、`first_ts`、`last_ts`、`queue_high_watermark` 和最近一次
  native code。
- 恢复时输出一条 INFO/WARN recovery，包含异常持续时间和累计次数。
- rate limiter 必须有容量上限和淘汰策略，不能让动态 key 造成内存增长。
- 每个限频器使用 RAII owner；timer callback 捕获 `weak_ptr`。

例子：

```text
[WARN] event=observer.queue_summary component=media_recorder \
code=OBSERVER_QUEUE_OVERFLOW outcome=drop_oldest count=284 window_ms=5000 \
queue_depth=120 queue_capacity=120 queue_high_watermark=120
```

### 10.9 性能统计原则

- 所有区间耗时使用 `steady_clock`；墙上时间只由日志 sink 添加。
- 统计窗口使用固定容量 bucket、直方图或在线算法，禁止无界保存逐帧样本。
- 媒体线程只更新低开销计数器/聚合器，不直接执行同步文件日志。
- summary 由 State/Worker lane 周期输出。
- 活跃媒体路径默认 5 秒一个窗口；空闲状态 30 秒或状态变化时输出。
- 输出 count、rate、avg、p50、p95、p99、max；不能只输出平均值。
- 生产环境不输出单帧 trace。受控诊断开启后必须有自动过期时间。

### 10.10 关键性能点

| 区域 | 必须统计的指标 |
|---|---|
| Capture | input/output FPS、frame gap p50/p95/p99/max、Acquire 超时、重复帧、capture error、fallback 次数 |
| Frame ingress | 接收数、拒绝数、非法尺寸/格式、topology generation、首帧等待时间 |
| Processor | 每 stage latency p50/p95/p99/max、失败/跳过数、CPU/GPU 路径、输出格式 |
| Encoder | wait、encode latency、FPS、bitrate、frame bytes、keyframe、IDR/RFI、queue depth、backend fallback |
| Audio | callback gap、buffer frames、capture-to-encode latency、underrun/overrun、restart 次数 |
| Fanout | 每 Sink submit 数、accepted、dropped、queue high watermark、最长排队时间 |
| WS | send queue、bytes/s、messages/s、send latency、disconnect/reconnect、slow consumer |
| UDP | bytes/s、packet rate、short write、loss、FEC recovered、pacing delay、endpoint association |
| Relay | send queue、bytes/s、RTT、reconnect、room/session count |
| WebRTC | active peers/tracks、RTT、jitter、loss、NACK/PLI/FIR、available bitrate、ICE/DTLS state、send queue |
| Session | active controller/observer、admission latency、takeover、stale disconnect、route switch |
| Workflow | count、success/failure/timeout/cancel、latency p50/p95/p99、retry count |
| Async scope | spawned/completed/failed/rejected/outstanding、cancel count、drain duration、drain timeout |
| Process | working set、private bytes、thread count、handle count、module count、uptime |

示例窗口日志：

```text
[INFO] event=encoder.window component=nvenc monitor=8a4c70b1 codec=h264 \
window_ms=5000 frames=301 fps=60.2 latency_avg_us=1840 latency_p95_us=2410 \
latency_p99_us=3260 latency_max_us=5090 queue_max=1 bitrate_bps=18400231 \
keyframes=1 dropped=0
```

```text
[INFO] event=transport.window component=webrtc transport=rtc session=7b21fa10 \
window_ms=5000 video_frames=300 bytes=11400231 rtt_ms=18 jitter_ms=3 \
loss_ppm=1200 nack=7 pli=0 queue_max=2
```

### 10.11 状态变化日志

以下状态必须记录一次转换日志，避免靠周期日志猜测：

- Render starting -> ready -> stopping -> stopped；
- module created -> started -> degraded -> stopping -> stopped；
- capture source 选择和 fallback；
- encoder backend 选择和 fallback；
- transport connecting/connected/disconnected/reconnecting；
- session admitted/role changed/closed；
- queue normal/overloaded/recovered；
- async scope accepting/stopping/drained/timed out。

日志记录 `from`、`to`、`reason/code` 和脱敏关联 token。同一状态重复赋值不重复输出。

### 10.12 启动和关闭日志

启动结束输出一条配置摘要：

- build/version；
- app mode；
- capture backend；
- encoder backend；
- 启用的静态模块；
- WebRTC DLL 版本/加载结果；
- lane 和 worker 数；
- 不含任何 credential。

关闭按依赖逆序记录，每个模块输出：

- `outstanding_before`；
- `cancelled`；
- `drain_ms`；
- `outstanding_after`；
- `outcome`。

`ASYNC_SCOPE_DRAIN_TIMEOUT` 必须为 ERROR，并列出 bounded 数量的 outstanding task names；
不得因此输出任务 payload 或敏感标识。

### 10.13 日志文件与轮转

内置化后所有静态模块默认写入 Render 主日志，通过 `component` 区分，不再为每个原插件
初始化独立全局 logger。首批沿用当前 rotating sink 基线（50 MiB x 5），在性能验收中测量
日志吞吐后决定是否增加独立 performance sink。

性能日志用稳定前缀 `[PERF]` 或 `event=*.window` 识别。即使未来分文件，字段、错误码和
事件名保持不变。ERROR/FATAL 初始化失败需立即 flush；媒体窗口日志不要求每条 flush。

### 10.14 日志代码自身的 RAII

- Logger/sink 使用智能所有权，不暴露 `spdlog::logger*` 给新业务代码。
- `RenderLogContext`、`RenderError` 和统计 snapshot 都是值类型。
- 周期 flush 任务属于 diagnostics module 的 `PxAsyncScope`。
- timer、subscription 和 sink 由 RAII 对象停止、注销和释放。
- 日志 callback 捕获 `weak_ptr`，不得捕获业务模块 `this`。
- diagnostics 停止后拒绝新周期任务，但 shutdown ERROR 仍由主 logger 输出。

## 11. 分阶段迁移

### 阶段 0：ADR、基线和保护网

产出架构决策、模块清单、现有消息图、shutdown 顺序、性能基线、错误码初始表和功能矩阵。
加入按 capability 选择 legacy/new 的迁移开关；同一能力只能有一个 active owner，禁止
旧模块和新模块同时执行真实副作用。

基线至少记录启动时间、线程/handle 数、working set、24 个 DLL 的磁盘大小、capture/
encode/transport 窗口指标、停止耗时和 outstanding callback 数。

### 阶段 1：移除两个无用生产模块

从 CMake、`px_build_premium_all`、plugin IDs/getters、发布脚本、dist 收集、设置和测试
引用中移除 `mock_video_stream` 和 `obj_detector`。先停止构建与发布，源码删除使用独立
清理提交。确认任何采集或编码 fallback 都不依赖 mock。

### 阶段 2：新骨架和 diagnostics

实现 Composition root、owned media types、bounded queue、scoped subscription、module
async scope、typed error、log context、rate limiter 和 performance window。旧功能暂时通过
compatibility bridge 工作。

### 阶段 3：Observer/Sink

按 frame debugger、statistics、media recorder、live pusher 顺序迁移。验证慢 Observer
不阻塞编码，队列策略确定，停止时能够 drain/cancel，销毁后不再回调。

### 阶段 4：Processor 和 Encoder

按 frame resizer、frame carrier、Opus、FFmpeg、NVENC、AMF 顺序迁移。建立
`EncoderCoordinator`，负责多屏映射、IDR、RFI、重配置、D3D state 和 fallback。删除对
PluginManager 编码器 getter 的运行期依赖。

### 阶段 5：Source

按 WAS audio、GDI、DDA、GameHook/IPC ingress 顺序迁移。建立 `CaptureCoordinator`，
负责 backend 选择、DDA -> GDI fallback、多屏 topology、首帧、start/stop 和音频 PID。

### 阶段 6：领域 Service

迁移 clipboard、file transfer、input、display、voice call 和 event replay。将
`OnMessageRaw(std::any)` 替换为 typed command/result；请求响应、超时和 retry 使用
awaitable workflow。

### 阶段 7：WS、UDP、Relay 内置化

按 Relay、UDP、WS 顺序迁移。WS 最后迁移，因为它还承载 HTTP、GameHook IPC、
UserProxy、UDP admission 和 direct session。先剥离业务职责，再改变链接形态。

### 阶段 8：WebRTC 网络层收口

将两个 WebRTC DLL 从通用插件广播中移出，只由 NetworkTransportHub 使用；callback
统一进入 NetworkIngress，service 请求使用 awaitable。保留既有 DLL ABI、实例身份和
进程生命周期契约。

### 阶段 9：删除旧插件框架

当最后一个内部 DLL 迁移完成后，删除目录扫描、`AttachPlugin`、`AttachNetPlugin`、
`GetPluginById`、具名 getter、UUID 业务路由、三个 Plugin Router、内部模块
`GetInstance`、每插件 `PxPluginContext` 和非 WebRTC 发布规则。最终动态加载只处理固定的
WebRTC DLL，不执行目录中的任意 DLL。

## 12. 详细测试方案

### 12.1 测试目标和原则

测试不仅验证“功能还能用”，还必须证明新架构的关键约束真实成立：

- 数据只按声明的 Source -> Processor -> Encoder -> Sink 路径流动。
- 一个慢 Observer、失败 Transport 或迟到 callback 不会拖垮其他分支。
- typed route 不会把控制、媒体或文件消息投递到错误连接。
- coroutine 在 timeout、cancel、owner 销毁和 shutdown 竞争下只完成一次。
- RAII 能够在部分构造失败、正常停止、异常停止和 callback 内停止时释放所有资源。
- 新增和修改的生产代码、测试代码均不引入裸指针。
- 错误日志可定位、可关联、受限频保护且不泄漏敏感信息。
- 性能、线程、handle、内存和包体相对迁移基线没有不可接受的回退。

测试必须优先使用确定性同步原语和可控故障注入。除真实 E2E 和 soak 外，单元测试不得用
任意 `Sleep` 猜测异步任务是否完成；使用 `promise/future`、`latch`、`barrier`、可控 clock、
`PxAsyncOneShot` 或 scope drain 等待确定事件。

### 12.2 测试分层

| 层级 | 范围 | 外部依赖 | 运行频率 | 失败是否阻断 |
|---|---|---|---|---|
| L0 静态门禁 | ownership、async lifetime、格式和依赖规则 | 无 | 每次 C++ 变更 | 是 |
| L1 单元测试 | value type、queue、route、error、统计器、纯状态机 | 无真实 GPU/网络 | 每次提交 | 是 |
| L2 组件测试 | 单个 Source/Processor/Encoder/Sink/Service | fake 或 loopback | 每次相关提交 | 是 |
| L3 进程内集成 | 完整 pipeline、services、transport hub | loopback/软件编码 | 每次迁移批次 | 是 |
| L4 本机进程测试 | `px_render.exe` + 本机 Client/Web | 实际进程、端口、文件 | 每个里程碑 | 是 |
| L5 LAN/硬件 E2E | 多机、GPU、WebRTC、Relay、GameHook | 真实设备和服务 | 网络/媒体里程碑 | 是 |
| L6 性能与 soak | 长时间媒体、重连、故障恢复 | 固定测试机 | 里程碑/最终验收 | 是 |

L1-L3 必须能够在没有真实显示器变更、外部 Relay 或公网 Coturn 的开发机上执行。依赖真实
硬件或网络条件的用例放入明确标签，不得让“环境缺失”表现成测试通过。

### 12.3 测试注册和执行基础设施

当前部分 Render 测试只有 `add_executable`，现有多个 `build_cpp_*_tests.bat` 也只构建目标。
升级首先完成以下测试基础设施：

1. 所有新测试和继续保留的 Render 测试使用 `add_test()` 注册到 CTest。
2. 使用 CTest label 分类：
   - `render-unit`
   - `render-lifecycle`
   - `render-integration`
   - `render-hardware`
   - `render-e2e`
   - `render-performance`
3. 新增聚焦入口 `build_cpp_render_arch_tests.bat`，职责为：
   - 调用 `scripts/build_cpp_target.bat` 构建精确测试目标；
   - 执行 L0 门禁；
   - 使用 `ctest --test-dir build_official --output-on-failure` 执行 L1-L3；
   - 保留退出码，任何测试失败都使脚本失败；
   - 将 JUnit/控制台日志写入本次 run 的证据目录。
4. 原有 `build_cpp_gdi_capture_tests.bat`、`build_cpp_was_audio_tests.bat`、
   `build_cpp_opus_encoder_tests.bat`、`build_cpp_media_recorder_tests.bat`、
   `build_cpp_live_pusher_tests.bat` 和 `build_cpp_voice_call_tests.bat` 在模块迁移时调整为
   构建并运行对应的新 module/runtime 测试，而不是只编译旧 DLL lifecycle 测试。
5. 硬件测试若不满足前置条件，结果必须明确为 `SKIP: reason`；发布验收环境中必需硬件的
   SKIP 视为未完成，而不是 PASS。

计划中的 runner 模式：

```text
build_cpp_render_arch_tests.bat quick
    L0 + L1，供每次本地修改使用

build_cpp_render_arch_tests.bat lifecycle
    L0 + 全部生命周期/并发测试

build_cpp_render_arch_tests.bat integration
    L0 + L1 + L2 + L3

build_cpp_render_arch_tests.bat hardware
    构建并执行当前机器满足条件的 DDA/GDI/NVENC/AMF/WAS 测试

build_cpp_render_arch_tests.bat all
    L0 + L1 + L2 + L3；不隐式启动外部 LAN E2E

build_cpp_render_arch_tests.bat performance
    执行固定配置的基线/对比测试并生成 performance comparison
```

runner 只调用 `build_cpp_*`/精确 CMake target，不调用 `build_official.bat`。LAN E2E 需要
明确的目标设备和凭据注入，继续由对应 `scripts/run_*.ps1` 显式启动，不能由普通单元测试
偷偷访问外部环境。

建议的测试目录：

```text
src/px_render/tests/
|-- unit/
|   |-- test_media_types.cpp
|   |-- test_bounded_media_queue.cpp
|   |-- test_scoped_subscription.cpp
|   |-- test_transport_route.cpp
|   |-- test_render_error.cpp
|   |-- test_rate_limited_log.cpp
|   `-- test_performance_window.cpp
|-- lifecycle/
|   |-- test_module_scope_lifecycle.cpp
|   |-- test_pipeline_subscription_lifecycle.cpp
|   |-- test_network_transport_lifecycle.cpp
|   `-- test_webrtc_library_lifecycle.cpp
|-- integration/
|   |-- test_video_pipeline.cpp
|   |-- test_audio_pipeline.cpp
|   |-- test_encoder_coordinator.cpp
|   |-- test_capture_coordinator.cpp
|   |-- test_network_transport_hub.cpp
|   `-- test_render_shutdown.cpp
|-- fault/
|   |-- test_capture_faults.cpp
|   |-- test_encoder_faults.cpp
|   |-- test_sink_faults.cpp
|   `-- test_transport_faults.cpp
`-- support/
    |-- manual_clock.h
    |-- fault_plan.h
    |-- bounded_test_probe.h
    `-- test_log_sink.h
```

测试 support 代码也受零裸指针门禁约束。测试使用 `gtest_main`，不新增带 `char** argv`
的自定义 main；fake、probe、clock 和 completion state 全部使用值类型或智能指针。

### 12.4 可测试性设施

#### 12.4.1 ManualClock

timeout、retry、rate limit 和 performance window 使用可注入的单调 clock。测试推进虚拟
时间，不等待真实秒数。生产实现包装 `steady_clock`，测试实现由 `shared_ptr<ManualClock>`
拥有状态。任何 clock callback 只能捕获 `weak_ptr`。

#### 12.4.2 FaultPlan

使用值类型 `FaultPlan` 在明确边界注入：

- 第 N 次 start 失败；
- 第 N 帧处理失败；
- send queue 在指定水位返回 full；
- callback 延迟到 owner 销毁后；
- request 永不完成或重复完成；
- transport disconnect/reconnect 次序；
- recorder write、RTMP connect 或 D3D operation 返回指定错误。

故障注入不能依赖全局变量，不允许改变生产默认行为。每个 fault point 都必须有稳定名称，
并能从测试输出确认已经命中，避免测试因没有触发故障而假通过。

#### 12.4.3 BoundedTestProbe

Probe 只保存有界数量的 owned/value snapshot，记录调用次数、顺序、线程/lane、关联 token
和结果。Probe 不保存被测对象实例，不保留借用引用，不允许测试自身造成无界内存增长。

#### 12.4.4 TestLogSink

测试日志 sink 使用 RAII 安装和恢复默认 sink，捕获结构化单行记录。析构时自动注销，
测试结束后不得影响其他用例。日志断言按字段解析，不依赖完整自然语言文本。

### 12.5 L0 静态与构建门禁

每个 C++ 变更运行：

```text
cmake --build build_official --target check_cpp_ownership
scripts/check_async_lifetime.ps1
git diff --check
```

并增加迁移期扫描：

- 新增代码不存在 raw object pointer、`[this]`、手工 `new/delete`。
- 新内部模块不存在 `PX_PLUGIN_EXPORT`、`GetInstance`、`GetPluginById`、`AttachPlugin`。
- 新数据路径不存在 `std::any`。
- 新 Observer 队列不存在无容量容器。
- 非 compatibility/WebRTC 文件不得新增 `NOLINT(gammaray-raw-pointer-boundary)`。
- 静态模块不能反向链接 PluginManager 或旧 Plugin Router。
- WebRTC 静态依赖不能传播到 `px_render.exe` 链接接口。

门禁失败必须先修复，不允许通过扩展 ignore 路径、通配 NOLINT 或降低检查范围绕过。

### 12.6 L1 单元测试矩阵

#### 12.6.1 Owned media types

验证：

- copy/move 后 payload 生命周期正确；
- 发布后的 frame 不可修改；
- monitor、codec、frame index、timestamp 和 topology generation 完整保留；
- 最后一个消费者释放后 buffer/ComPtr 测试替身析构一次；
- 空 payload、非法尺寸、未知格式返回 typed error；
- frame 不携带 Source、Encoder、Sink 或 Transport 对象。

#### 12.6.2 Bounded media queue

对每种策略验证：

- `drop_oldest` 保留最新数据；
- `drop_newest` 不破坏已排队顺序；
- keyframe protection 不错误淘汰唯一关键帧；
- shutdown-drain 处理已有项并拒绝新项；
- shutdown-cancel 立即释放所有 payload；
- 多 producer/单 consumer 下 count、drop 和 high watermark 准确；
- 队列永远不超过声明容量；
- blocked consumer 销毁后 producer 不访问已失效对象。

#### 12.6.3 Scoped subscription

覆盖：

- token 析构自动 unregister；
- move 后只有新 token 拥有注销责任；
- double reset 幂等；
- dispatch 中注销自己；
- A callback 注销 B callback；
- unregister 与并发 dispatch；
- callback 排队后 observer 销毁；
- registry 只存 weak ownership，不延长 observer 生命周期。

#### 12.6.4 TransportRoute

验证 route equality、hash、expiry、connection generation 和 channel kind。重点覆盖同一
stream 在 WS、RTC、RTC Local、Relay 间切换时，旧 disconnect 不能删除新 route；FT-only
route 不依赖媒体 client count；controller route 和 observer route 不能互相冒用。

#### 12.6.5 Typed error 和 async operation

覆盖：

- success/failure 只完成一次；
- 重复 response 被拒绝；
- response 与 timeout 同时到达；
- cancel 与 response 同时到达；
- late response 不修改新 generation；
- scope stop 取消 suspended coroutine；
- scope stop 后拒绝 spawn；
- error 的 code、stage、recoverable、native_code 在层间不丢失；
- exception 转换为稳定错误码。

#### 12.6.6 Diagnostics

验证 percentile、窗口滚动、窗口重置、空窗口、极值、counter overflow 防护、rate limiter
容量淘汰、first/summary/recovery 顺序和字段完整性。使用 ManualClock，不等待真实窗口。

### 12.7 L2 模块组件测试

#### 12.7.1 Source

每个 Source 使用受控 Sink 验证：

- Start/Stop 幂等；
- Start 失败没有半初始化资源；
- Stop 后不再投递；
- queued callback 在 Stop 后被拒绝；
- format/topology 变化按 generation 生效；
- 多次设备切换不累积线程、handle 或 callback；
- Source 销毁释放线程、timer、COM 和 D3D 资源。

真实 DDA/GDI/WAS 的硬件部分另放 L5；L2 测 coordinator 与边界状态机。

#### 12.7.2 Processor

验证 processor chain 顺序、格式约束、旁路、失败策略和多显示器隔离。某个 processor
失败时必须按配置 drop/fallback，不能把部分修改的数据继续发送。

#### 12.7.3 Encoder

验证 capability probe 和选择顺序：NVENC -> AMF -> FFmpeg。覆盖：

- 指定 backend 不可用；
- 初始化中途失败；
- D3D device generation 变化；
- IDR 和 monitor 定向；
- RFI 成功与不支持时的 IDR fallback；
- bitrate/FPS 重配置；
- 多屏共享或独立 encoder 的去重调用；
- Stop 与正在编码竞争；
- 编码失败后的 fallback 不重复发送 frame。

硬件不可用时软件编码组件测试仍必须执行。

#### 12.7.4 Observer/Sink

对 frame debugger、statistics、recorder、live pusher 分别验证：

- 只收到声明的数据类型；
- 不修改共享 frame；
- 慢消费触发自己的 queue policy；
- 一个 Sink 失败不停止其他 Sink；
- recorder 文件关闭、segment rollover 和磁盘错误；
- live pusher connect/reconnect、mux 错误和 shutdown；
- statistics 窗口有界且 reset 正确；
- Stop 后 drain/cancel 符合模块声明。

#### 12.7.5 Domain Service

对 Session、Input、Display、Clipboard、FT、Voice 分别验证权限、状态机、重复请求、迟到
事件和 shutdown。输入测试必须证明 observer 无控制权、旧 controller generation 不能继续
注入、takeover 会释放前一个输入状态。

#### 12.7.6 Transport

WS、UDP、Relay 使用 loopback/fake socket 边界验证：

- start/listen/connect/stop；
- 精确 route；
- media/control/FT channel 不混用；
- queue full 和 slow peer；
- disconnect/reconnect；
- stale callback；
- unauthorized endpoint；
- send completion 晚于 Stop；
- 单个 transport 故障不影响其他 transport。

WebRTC 不提供 fake interface。其纯业务逻辑在 NetworkIngress、SessionService 和
OutboundMediaDispatcher 测试；DLL 本身通过具体 `WebRtcLibrary` 组件测试和现有 DLL
lifecycle 测试验证。

### 12.8 L3 进程内集成测试

#### 12.8.1 Video pipeline

用确定性 Source 产生包含 frame index 和 monitor identity 的 owned frame，连接真实
processor chain、软件 encoder 和多个 test Sink。验证：

- 端到端顺序和数据一致；
- 60 FPS 输入窗口统计正确；
- 多屏不会交叉；
- Sink 分支相互隔离；
- processor/encoder fallback 后每个 frame 最多发布一次；
- topology generation 更新后旧帧不进入新 pipeline；
- Stop 后所有队列为空、scope outstanding 为零。

#### 12.8.2 Audio pipeline

输入确定性 PCM pattern，经过处理和 Opus runtime，验证格式、frame duration、左右声道、
编码顺序、录制/推流/网络 fanout，以及音频设备重启时不重复创建 worker。

#### 12.8.3 Network transport hub

同时启动 loopback WS、UDP、Relay 和禁用/真实本机 WebRTC 配置：

- broadcast media 到所有订阅 transport；
- target control 只到目标 route；
- FT 只走已选择且仍存活的 FT route；
- transport 切换更新 route generation；
- 旧 disconnect 和旧 send completion 被忽略；
- 一个 transport queue full 不阻塞其他 transport；
- NetworkIngress 产生相同 typed command，不携带 transport 实例。

#### 12.8.4 Render shutdown

构造完整 composition root 后，在以下时机调用 Stop：

- 空闲；
- capture callback 中；
- processor 正在执行；
- encoder callback 已排队；
- observer queue 满；
- service coroutine suspended；
- transport reconnect timer 中；
- WebRTC callback 到达前；
- diagnostics 正准备输出窗口。

每种场景至少重复 10 轮，断言停止顺序、outstanding task、线程、handle、subscription、
日志 recovery/drain 字段和析构次数。

### 12.9 故障注入矩阵

| 故障 | 注入位置 | 预期行为 | 必须出现的主错误/状态 |
|---|---|---|---|
| DDA 初始化失败 | Capture factory | fallback GDI，主流程继续 | `CAPTURE_DDA_INIT_FAILED`, outcome=fallback |
| D3D device lost | Processor/Encoder boundary | generation 更新并重建，旧帧丢弃 | device-lost + rebuild 状态 |
| NVENC 不可用 | Encoder factory | 尝试 AMF/FFmpeg | `ENCODER_NVENC_INIT_FAILED` |
| 所有编码器失败 | EncoderCoordinator | 当前媒体 session 失败，不启动半成品 pipeline | `ENCODER_ALL_BACKENDS_FAILED` |
| Observer 队列满 | Observer dispatcher | 执行声明的 drop policy，其他 Sink 正常 | `OBSERVER_QUEUE_OVERFLOW` summary |
| Recorder 磁盘写失败 | Recorder runtime | 关闭当前文件并报告失败，网络继续 | 稳定 recorder code |
| RTMP 断开 | Live pusher runtime | 有界重连，不影响录制/网络 | reconnect state + rate-limited error |
| WAS 设备变化 | Audio Source | 取消旧 backend 后重建一次 | restart state，线程数恢复基线 |
| WS send queue full | WsTransport | 仅该 route backpressure/drop | `TRANSPORT_SEND_QUEUE_FULL` |
| UDP short write/loss | UdpTransport | 统计并按现有策略恢复/FEC | transport window counters |
| Relay disconnect | RelayTransport | 有界重连，route generation 更新 | disconnected/reconnecting/recovered |
| WebRTC DLL 缺失 | WebRtcLibrary startup | RTC 标记不可用；其他 transport 可启动 | `TRANSPORT_RTC_LIBRARY_LOAD_FAILED` |
| WebRTC 迟到 callback | compatibility boundary | owner/generation 失效后丢弃 | debug/summary，不访问已释放对象 |
| Service 永不响应 | Awaitable workflow | deadline 失败并清理 registry | `WORKFLOW_DEADLINE_EXCEEDED` |
| callback 重复完成 | Async operation | 第一次生效，第二次拒绝 | duplicate completion counter |
| shutdown drain 超时 | Module scope | 输出 bounded outstanding names 并进入安全终止 | `ASYNC_SCOPE_DRAIN_TIMEOUT` |

每个故障测试必须同时验证行为、资源释放、稳定错误码、日志次数和未受影响的旁路能力。

### 12.10 L4 本机进程测试

从 `build_official/dist` 启动真实 `px_render.exe`，验证用户实际运行目录，而不是只运行
build tree 产物。每个用例使用独立端口、独立日志 token 和明确超时，结束后检查残留进程。

场景：

1. Desktop + GDI + FFmpeg 最小软件路径。
2. Desktop + DDA + 首选硬件 encoder。
3. GameHook capture 和 IPC 音视频。
4. WebView smoke。
5. WS 连接、输入、剪贴板和文件传输。
6. UDP association、媒体切换和 fallback。
7. RTC Local 本机连接。
8. 录制开始/停止、文件可读、进程退出后文件句柄释放。
9. Live pusher 使用本机受控 RTMP endpoint。
10. Render 运行中停止、客户端断开和立即重启。

复用现有 `scripts/run_game_hook_render.bat`、`scripts/test_webview_smoke.ps1`、
`scripts/test_webrtc_local.bat` 和本地诊断脚本；迁移后更新其中仍引用原插件 DLL 的路径和
检查逻辑。

### 12.11 L5 LAN、硬件和产品 E2E

在固定测试主机上执行以下矩阵：

| 维度 | 覆盖值 |
|---|---|
| Capture | DDA、GDI、GameHook、WebView、多屏 |
| Encoder | NVENC、AMF、FFmpeg，H.264/H.265 |
| Transport | WS、UDP、Relay、RTC、RTC Local |
| Role | Controller、Observer、Wall observer |
| Capability | view、audio、input、clipboard、file、voice |
| Recovery | reconnect、takeover、Console restart、topology change、encoder fallback |

复用并纳入验收：

- `scripts/run_native_auth_case.ps1`
- `scripts/run_direct_rtc_negative_auth_case.ps1`
- `scripts/run_rtc_lan_case.ps1`
- `scripts/run_rtc_lan_stability.ps1`
- `scripts/run_rtc_multi_session_lan_case.ps1`
- `scripts/run_rtc_app_multi_session_lan_case.ps1`
- `scripts/run_ft_transport_e2e.ps1`
- `scripts/run_native_ft_ui_e2e.ps1`
- `scripts/test_console_restart_recovery.ps1`
- `scripts/cdp_voice_call_e2e.mjs`
- `scripts/cdp_webview_input_stability_test.mjs`

E2E 判定不能只看进程未崩溃，必须确认连接建立、首帧到达、音频、输入、角色权限、目标
route、断开清理和日志中没有未预期 ERROR。涉及网络损失的脚本同时保存 packet/loss/FEC
窗口摘要。

### 12.12 日志测试

#### 12.12.1 字段和级别

为每个 error path 建立表驱动测试，断言稳定的 `event/component/code/operation/outcome/
recoverable`。业务拒绝、用户取消、正常 disconnect 和 shutdown cancellation 必须保持
INFO/WARN 语义，不能误报 ERROR。

#### 12.12.2 去重和限频

使用 ManualClock 注入 1,000 次相同高频错误，断言：

- 第一次立即输出；
- 窗口内不逐条输出；
- summary 的 count、first/last 和 high watermark 正确；
- recovery 只输出一次；
- 不同 component/code/route token 独立统计；
- 超过 key 容量时按规定淘汰，内存保持有界。

#### 12.12.3 性能窗口

输入已知分布，精确验证 avg、p50、p95、p99、max、rate、drop 和窗口 reset。空窗口不输出
误导性的 0 latency；窗口跨越 shutdown 时不产生迟到日志。

#### 12.12.4 隐私

向测试输入注入唯一 canary：ticket、token、password、nonce、SDP、ICE credential、完整
路径、剪贴板正文和公网 IP。扫描全部测试日志，任意 canary 原文出现即失败；相同业务 ID
必须产生稳定 `PrivacyLogId` token。

#### 12.12.5 根因只记录一次

模拟底层发送失败，验证底层返回 typed error，负责 retry/fallback 的所有权层记录唯一主
WARN/ERROR。调用链不得为同一个 code 输出多条相同主错误。

#### 12.12.6 日志生命周期

覆盖周期 flush 已排队时 diagnostics owner 销毁、日志 callback 内 Stop、并发输出和 sink
替换。断言无死锁、无 use-after-free、默认 sink 被 RAII 恢复，shutdown ERROR 仍能输出。

### 12.13 性能基线和回归门槛

阶段 0 在固定硬件、固定分辨率、固定 FPS/bitrate、固定 transport 和固定测试时长下保存
基线。每次比较必须使用同一配置和至少三轮样本，报告 median 与最差一轮。

建议门槛：

- 稳态 capture/encode FPS 达到配置值的 99% 以上。
- 无故障本机路径的主 pipeline 不允许架构性 drop。
- capture-to-encoded p95 不高于基线 `max(+5%, +0.5 ms)`。
- capture-to-encoded p99 不高于基线 `max(+10%, +1 ms)`。
- 输入/控制 workflow p95 不高于基线 10%，timeout 数不得增加。
- 普通日志级别相对关闭 diagnostics 的 CPU 开销低于 1%，媒体 p95 增量低于 2%。
- 工作集和 handle 数不得无解释增长超过 5%；停止后应回到预定稳态范围。
- 线程数不得高于旧架构基线；目标是消除每插件 context 后明显下降。
- 所有队列 high watermark 低于容量；达到容量必须有对应 drop/backpressure 证据。
- 最终不存在 `rd_plugins`；仅两个 WebRTC 动态网络库位于 `deps/network`，运行目录总
  DLL 大小按实际结果登记。

如固定硬件的自然波动超过上述门槛，先通过阶段 0 数据校准阈值并记录理由，不能在看到
迁移结果后反向放宽门槛。

### 12.14 稳定性和资源测试

#### 12.14.1 30 分钟压力测试

同时启用采集、编码、两种以上 transport、录制、推流和统计；周期制造 client connect/
disconnect、observer queue pressure 和 IDR。每 5 秒采集窗口指标，断言：

- FPS 和延迟无持续恶化；
- working set、handle、thread、queue 无单调增长；
- 无未预期 ERROR；
- 所有 recovery 都有成对状态日志。

#### 12.14.2 8 小时 soak

最终里程碑执行 8 小时稳定性测试，至少包含 WebRTC、一个非 WebRTC transport、音视频、
录制的周期启停和定期 topology/session 变化。验收：

- 无崩溃、死锁或 watchdog 重启；
- 内存、handle 和线程数没有趋势性增长；
- coroutine outstanding 回落到稳态；
- route 数与活动 session 数一致；
- 日志量符合轮转预算且没有错误风暴。

#### 12.14.3 重复生命周期

模块级至少 100 轮快速 start/stop，进程级至少 20 轮启动/连接/停止。涉及真实硬件、驱动
或 WebRTC 的较慢用例可以降低轮数，但不得低于 10 轮，并在报告中标明。

### 12.15 各迁移阶段的必跑测试

| 阶段 | 必跑范围 | 阶段特有断言 |
|---|---|---|
| 0 基线 | L0、现有 focused tests、关键 E2E | 保存可比较基线和现有已知失败 |
| 1 移除无用模块 | Render build、启动 smoke、dist audit | 无 mock/obj DLL、ID/getter/配置引用 |
| 2 新骨架 | L0、全部新 L1、scope lifecycle | 零裸指针、queue/subscription/log 可控 |
| 3 Observer | Observer L2、video L3、30 分钟 pressure | 慢/失败 Sink 不影响主链 |
| 4 Processor/Encoder | processor/encoder L2-L3、GPU L5 | backend fallback、IDR/RFI、多屏 |
| 5 Source | Source L2、capture L4-L5 | Stop 后无 callback、DDA/GDI fallback |
| 6 Service | service L2-L3、auth/FT/voice E2E | typed command、权限、timeout/late response |
| 7 WS/UDP/Relay | transport L2-L5 | 精确 route、并存、拥塞和重连隔离 |
| 8 WebRTC 收口 | WebRTC lifecycle、RTC E2E、soak | DLL ABI 保持、迟到 callback 安全 |
| 9 删除旧框架 | 全量 L0-L6、dist audit | 无旧 Router/扫描/内部插件 DLL |

`frame_debugger` 首批 Observer 的 L1 自动化用例还必须覆盖：默认禁用时零排队、启用控制
幂等、原始帧日志计数、编码器事件先于编码帧、队列 drain 后文件字节一致、文件句柄关闭、
回调内请求整体停机、启动失败逆序回滚和连续重复执行。生产接线验收额外检查 Panel 中只
出现一个原 UUID 条目，且 dist 与加载日志中均无 `frame_debugger.dll`。

### 12.16 测试证据和报告

每次里程碑测试产生独立目录，例如：

```text
test-results/render-architecture/<run-id>/
|-- environment.txt
|-- git-revision.txt
|-- build-targets.txt
|-- ctest.xml
|-- unit.log
|-- integration.log
|-- e2e/
|-- performance/
|   |-- baseline.json
|   `-- comparison.json
|-- process-metrics.csv
|-- log-privacy-scan.txt
|-- artifact-hashes.txt
`-- summary.md
```

`environment.txt` 记录 OS、CPU、GPU、驱动、显示器、网络路径、分辨率、FPS、bitrate 和启用
模块，不记录 credential。`summary.md` 必须列出 PASS、FAIL、SKIP、已知问题、性能对比、
未预期 ERROR 数和是否允许进入下一阶段。

测试产生的大文件不默认提交 Git；提交测试脚本、基线定义、结果摘要和必要的小型 fixture。

### 12.17 Go/No-Go 和回退

满足以下条件才允许进入下一迁移批次：

- L0-L3 全部 PASS；
- 本批相关 L4/L5 全部 PASS，发布必需硬件没有 SKIP；
- 没有新增未解释 ERROR；
- ownership、async lifetime、privacy scan 全部 PASS；
- 性能未越过门槛；
- build tree 与 dist 哈希一致；
- legacy/new 开关仅激活一个 owner。

出现崩溃、数据错误路由、输入越权、use-after-free、shutdown 卡死、敏感日志泄漏或无界
内存增长时立即 No-Go，不允许以“后续批次修复”继续叠加迁移。回退到上一已验收 capability
开关，保留失败日志、dump、配置和测试 seed 后再修复。

## 13. 构建和交付规则

日常开发使用 `build_cpp_render.bat`、对应 `build_cpp_*_tests.bat` 或精确 CMake target。
不为此升级运行 release-only 的 `build_official.bat`。

每个迁移批次交付前：

1. 构建受影响目标；
2. 运行 ownership 和 async lifetime gate；
3. 运行相关单元、生命周期和集成测试；
4. 同步 `px_render.exe`、仍保留的 WebRTC DLL、资源和语言文件；
5. 清理已经退休的 Render DLL；
6. 比较 build tree 与 `build_official/dist` 对应文件的 SHA-256；
7. 哈希一致后才能报告可供 Windows Client 验证。

## 14. 完成标准

- 除 WebRTC 外，现有有效功能全部内置进 `px_render.exe`。
- WS、UDP、Relay 和 WebRTC 位于同一网络传输层。
- WebRTC 只是动态链接的具体网络组件，不进入 Source/Processor/Observer 插件体系。
- `mock_video_stream`、`obj_detector` 不再生产构建和发布。
- Source、Processor、Observer/Sink 是明确扩展点，插件不再等同于 DLL。
- 不存在全量插件互联、UUID service lookup 和 `OnMessageRaw(std::any)`。
- 高频媒体数据不经过通用 PluginEventRouter。
- 请求响应、超时、重试和关闭工作流使用 typed awaitable。
- 内部模块可取消、可 drain，shutdown 无悬空 callback。
- 新增和触及的项目代码零裸指针；现有 ABI 例外仅存在于最小兼容边界。
- 日志具有稳定事件名、错误码、脱敏关联、限频和性能窗口。
- 所有功能、生命周期、日志、性能和交付验收通过。

## 15. 第一批实施范围

第一批只包含：

1. 阶段 0 的 ADR、基线采集和测试保护网；
2. 移除 `mock_video_stream` 与 `obj_detector` 的生产构建/发布；
3. 建立 Composition root、owned media types 和 Observer 管线骨架；
4. 建立 typed error、结构化日志上下文、限频器和性能窗口；
5. 迁移 `frame_debugger` 作为第一个 Observer；
6. 验证 RAII、queued callback、unregister、shutdown、日志限频和性能聚合；
7. 聚焦构建、同步到 `build_official/dist` 并核对 SHA-256。

第一批不改变 capture、encoder、transport 或 WebRTC 生产行为，用于先证明新骨架和交付
流程可靠，再进入高风险媒体与网络迁移。
