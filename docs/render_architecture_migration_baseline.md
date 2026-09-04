# Render 架构升级基线

记录日期：2026-09-03
代码基线：`1330961c`

本文记录升级开始前可重复核对的构建与结构基线。总体设计、迁移阶段、日志规范和
测试方案见 `render_builtin_modules_architecture_upgrade_plan.md`。

## 1. 发布物基线

升级开始时，`build_official/dist/deps/rd_plugins` 中共有 24 个 Render 插件 DLL，
合计 530,719,232 字节（506.13 MiB）。其中两个已确认无产品用途的模块为：

| 文件 | 大小 | SHA-256 |
|---|---:|---|
| `mock_video_stream.dll` | 15,618,560 B | `6D0FE4A49A45720001C71BD91927D67C698A948D6AFECF3F4153F4D065A37B08` |
| `obj_detector.dll` | 10,029,056 B | `46554BC345FB574F6EBAFF3696CDFCA27F4315A664F2FC254BB038B316464C7E` |

二者合计 25,647,616 字节（24.46 MiB）。第一阶段完成后，生产发布目录的 Render
插件数量应降为 22，并且任何增量构建残留都不得被重新收集或发布。

升级开始时 `build_official/dist/px_render.exe`：

- 大小：26,095,104 字节；
- SHA-256：`17C5C1BE77FB537DC50EDB0083CEB5C3F3FFA39DF8A356C702AAF2D4F22C0444`。

## 2. 结构基线

- `src/px_render/plugins/CMakeLists.txt` 直接构建 24 个动态插件目标；
- `PluginManager` 扫描 `deps/rd_plugins` 下的 DLL，解析 `GetInstance`，再建立插件间
  广播关系；
- `plugin_event_router.cpp` 为 557 行；
- `PxPluginEventType` 有 33 个既有事件值；
- 正常退出路径无法安全执行完整插件释放，说明异步回调与 DLL 生命周期仍有耦合。

## 3. 第一阶段验收口径

第一阶段只改变两个已退役模块的生产构建和发布，不改变其他 Render 功能行为：

1. CMake 生产图和 `px_build_premium_all` 不再包含两个目标；
2. 聚焦发布不再接受两个目标；
3. 聚焦发布会删除 `dist` 中的旧 DLL；
4. 全量收集会忽略增量构建目录中可能残留的旧 DLL；
5. `render_retired_modules_guard` 持续检查以上约束；
6. `px_render` 聚焦构建、所有权门禁和发布哈希校验通过。

保留两个模块的源码目录，暂不删除，避免把“停止生产交付”和“删除测试素材”混成
一个不可回退动作。待新单向管线测试替身落地后，再决定迁移到测试目录或删除。

## 4. 2026-09-03 执行记录

### 阶段 1：生产退役已完成

- CMake 插件图和 `px_build_premium_all` 已移除 `mock_video_stream`、`obj_detector`；
- 聚焦发布映射已移除两个目标，发布动作会精确清理两个旧 DLL；
- 完整 dist 收集显式忽略增量构建目录中残留的两个 DLL；
- Render 和 Panel 已删除 `--mock_video`、设置字段、mock provider、编码器特判以及
  `PluginManager::GetMockVideoStreamPlugin`，生产运行时不再存在 mock 分支；
- `render_retired_modules_guard` 同时检查构建图、发布映射、运行时引用和 dist；
- 源码目录暂存且不参与任何生产目标，插件 UUID 仅供这两个暂存目录自身引用。

执行后 `build_official/dist/deps/rd_plugins` 为 22 个 DLL，共 505,071,616 字节
（481.67 MiB）；两个退役文件均不存在。

### 阶段 2：第一批基础设施已完成

新增静态库 `render_architecture_core` 并链接进 Render 构建图；基础设施完成后，已由首个
内置 Observer 接管对应生产流量。
已实现：

- `CapturedVideoFrame`、音视频 owned payload 和稳定 frame identity；
- `RenderError` 稳定错误码及值类型 `RenderLogContext`；
- 有容量上限、明确 drop/close 语义和统计快照的 `BoundedMediaQueue`；
- 有容量上限的 `PerformanceWindow` 和 `RateLimitedLogGate`；
- registry 弱持有 callback、令牌析构自动注销的 `ScopedSubscription`；
- 基于 `PxAsyncOneShot` 的 `AwaitOwnedCallback`，将一次性 callback 转成带 deadline、
  cancellation 和迟到回调保护的 `co_await` 工作流；
- `render_architecture_boundary_guard`，禁止新静态核心依赖 `PluginManager`、旧插件导出、
  `std::any` 或新增 ABI 裸指针豁免。
- `BuiltinModuleCatalog`，提供稳定 ID、能力分类、依赖拓扑、启用状态、运行状态和错误快照；
- `RenderCompositionRoot`，在 state lane 上以 coroutine 执行依赖顺序启动、失败逆序回滚、
  逆序停止、重复启停和回调内停机。

所有新异步/并发类均带 `Lifetime`、`Threading` 标注，新 C++ 和测试没有声明、保存、
传递、返回或捕获裸指针。所有媒体 payload 通过 `shared_ptr<const T>` 发布。

### 阶段 3：首个 Observer 已完成代码迁移

`frame_debugger` 已从通用 DLL 插件迁移为 exe 内的 `FrameDebuggerObserver`：

- 原 UUID、名称、Panel 列表展示及启用/禁用命令保持兼容；
- 编码器创建、原始 RGBA 帧、编码帧和新客户端连接改为显式单向 Observer 投递；
- 原始帧日志按 monitor 限频，输出被抑制次数，避免逐帧日志风暴；
- 编码帧保存采用容量 120、drop-oldest 的有界队列和单个长期 worker coroutine，主编码
  路径不等待文件 I/O；
- 停机采用 drain + deadline，记录 accepted、dropped、high-watermark、写入字节和失败数；
- 旧实现中的 `save_encoded_video` 从未由 PluginManager 注入，生产等效值一直为 false；
  新模块保持该默认值，同时保留已单测覆盖的可配置落盘实现；
- `frame_debugger.dll` 已从 CMake、聚合目标、聚焦发布映射和 dist 收集移除；加载器还会
  忽略增量构建树残留 DLL，避免新旧实现产生双重副作用。

当前 `build_official/dist/deps/rd_plugins` 为 21 个 DLL，共 494,846,976 字节
（471.92 MiB）；`frame_debugger.dll` 不存在。阶段 3 的代码和 L1 测试已完成，真实视频
L3、30 分钟 pressure 和客户端人工验证仍是进入下一个 Observer 前的验收项。

### 验证结果

| 验证 | 结果 |
|---|---|
| `build_cpp_render_arch_tests.bat 8` | 通过 |
| 架构核心单元测试 | 8 个 suite、24 个 test 全部通过 |
| 架构核心重复运行 | 连续 20 次全部通过 |
| `render_retired_modules_guard` | 通过 |
| `render_architecture_boundary_guard` | 通过 |
| `check_cpp_ownership` | 通过，且现已覆盖 `src/`/`tests/` 中未跟踪的新 C++ 文件 |
| `px_render` 聚焦构建与发布 | 通过 |
| `px_panel` 聚焦构建与发布 | 通过 |
| `panel_stream_launch_child_arguments` | 通过 |
| `git diff --check` | 通过 |

Panel 聚焦链接同时修复了 Client 插件内置化后遗留的无效
`px_client_plugin.lib` 链接项；代码中没有该旧库的符号使用。

当前交付哈希：

- `px_render.exe`：`C9E6AEA9BBE2C6483B5CA255716787205C426B1385A00A0793FB50F227F21F81`；
- `px_panel.exe`：`E45042D95DDB40A85CB8EB0E4A80A74125BF01803A72D4D02124567FC4324CD5`。

以上均由聚焦发布脚本验证 build-tree 与 `build_official/dist` 的 SHA-256 相同；未运行
`build_official.bat`。

## 5. 2026-09-04 后续迁移执行记录

在第一批骨架之后，以下生产模块已继续完成静态内置；它们保留原稳定 UUID 和 Panel
模块身份，但不再产生或加载各自的 Render DLL：

| 原 DLL | 新角色 | 生产接线 |
|---|---|---|
| `media_recorder.dll` | Sink | 订阅 typed encoded media bus，独立有界队列写盘 |
| `live_pusher.dll` | Sink | 订阅 typed encoded media bus，FFmpeg runtime 静态链接 |
| `frame_resizer.dll` | Processor | `RdContext` 显式持有并由 EncoderThread 调用 |
| `frame_carrier.dll` | Processor | 按 monitor 持有共享纹理 carrier，异步任务只捕获 weak owner |
| `enc_opus.dll` | Processor | 订阅 `CapturedAudioFrame`，发布 `EncodedAudioFrame` |
| `event_replayer.dll` | Service | 网络入口在 controller lease 校验后显式调用 |
| `cap_was_audio.dll` | Source | MiniAudio/process-loopback runtime 静态链接并直接输出 `CaptureAudioFrame` |
| `joystick.dll` | Service | 网络入口显式路由 hello/gamepad/disconnect，ViGEm 句柄由 RAII 持有 |
| `ft.dll` | Service | controller lease 校验后直达 typed service，按精确 transport/connection route 回包 |

`clipboard.dll` 的旧 Render utility 边界也已移除。当前产品剪贴板和剪贴板文件数据由
UserProxy/网络入口直接完成权限检查和转发，旧 DLL 已不在有效数据路径，移除它不会删除
剪贴板能力。

WAS 音频迁移特别移除了 `PxPluginContext -> PxPluginBaseEvent -> PluginEventRouter` 的二次
回调链。采集 runtime 只产生 owned `CaptureAudioFrame`；Source 在组合根中管理启停，应用
边界负责把值对象投递到主队列。fatal restart、stop 取消、callback 内 stop、销毁后迟到
callback 和重复 start/stop 均由 runtime/Source 测试覆盖。

Joystick 迁移把原来的“网络消息广播给所有 DLL”改成 `JoystickService::HandleMessage` 和
`HandleClientDisconnected`。`kGamepadState` 同时加入 controller-only lease 门禁，避免旧
会话在控制权移交后继续注入手柄状态。ViGEm client/target 使用带 deleter 的 typed
`unique_ptr`，不再由业务类保存或手工释放裸句柄。

文件传输迁移保留既有 RustDesk 兼容引擎，但删除 `FtPlugin`、`GetInstance`、`std::any`
入口和 `ft.dll`。`FileTransferService` 由组合根启动/停止，每个逻辑会话使用独立 state
lane；异步回调只通过 `weak_ptr<AsyncBridge>` 回到 owner。网络入口在 controller lease
验证后传入 `FileTransferInbound`，回包通过精确的 transport/stream/connection route
发送，不再向所有插件广播。路由代际替换、旧连接迟到断开、禁用权限回复、队列 drain、
重复启停和 owner 销毁均有服务级测试。

本批已验证：

- `test_was_audio_capture_source` 连续 20 轮通过；
- `test_was_audio_capture_runtime` 连续 20 轮通过；
- `test_joystick_service` 连续 20 轮通过；
- `test_file_transfer_service` 的 4 个生命周期/路由用例连续 20 轮通过；
- `render_retired_modules_guard` 与 `render_architecture_boundary_guard` 连续 20 轮通过；
- `check_cpp_ownership` 通过；
- `px_render` 聚焦链接通过。

### 阶段 6：固定生产模块静态化与网络边界收口

- DDA/GDI、NVENC/AMF/FFmpeg、WS/UDP/Relay 已改为静态库并链接进 `px_render.exe`；
- 上述模块不再导出 `GetInstance`，发布器和完整收集器会清理/忽略其旧 DLL；
- `PluginManager` 不再做任意 DLL 发现，只允许加载 `net_rtc.dll` 与
  `net_rtc_local.dll` 两个明确的 WebRTC 动态组件；
- `NetworkTransportHub` 已成为 control、file-transfer、voice 的 typed 出站边界；
- `voice_call.dll` 已移除，`VoiceCallService` 在组合根中启动和停止，网络消息、面板决策、
  RTC 授权和 owned PCM 均为直接 typed 路由；voice runtime 已删除对 `px_plugin`、
  `PxPluginBaseEvent` 和 `PxPluginContext` 的依赖；
- WebRTC 输入 PCM 在 DLL 内将 libwebrtc 借用视图立即复制为 owned vector，再进入 host
  service；两个 WebRTC DLL 的既有 libwebrtc 内部对象模型和卸载契约未改动。

### 阶段 7：显式模块组合与高频数据入口收口

本阶段继续删除“所有东西都是可发现插件”的运行期模型：

- 原 `PluginManager` 类型已替换为 `RenderModuleRegistry`。注册表不再保存按字符串 ID
  查询的 map，而是显式拥有 DDA/GDI、FFmpeg/AMF/NVENC、WS/UDP/Relay 和两个固定 RTC
  槽位；对外只暴露按职责分类的模块与网络操作。
- 原三个 Router 类型分别收口为 `RenderEventIngress`、`NetworkEventIngress` 和
  `EncodedVideoFanout`。DDA/GDI 的视频帧与光标 callback、三种编码器的编码完成 callback
  均在注册时绑定到专用入口，不再进入通用 event type switch。
- 编码器从 `CaptureVideoFrame` 输入到 `PxPluginEncodedVideoFrameEvent` 输出的元数据已全部
  强类型化；FFmpeg、AMF、NVENC 和 encoded fanout 中不再使用 `std::any/any_cast`。
- AMF 的异步输出元数据按 frame index 保存并受 mutex 保护，提交失败和 shutdown 都会
  清理，避免旧单一 `extra_` 值在多帧并发时串帧或形成无界保留。
- 静态 WS 的 UserProxy、UDP association 和 Direct RTC 关系由组合阶段使用
  `shared_ptr/weak_ptr` 显式注入；WS 源码不再调用 `GetPluginById`。
- 旧 all-to-all `AttachPlugin/AttachNetPlugin`、其裸指针容器和
  `GetPluginById` service locator 已从 Render 源码移除。RTC Local 的显示拓扑改由
  `CaptureMonitorInfoMessage` 明确推送，选择捕获屏幕则通过具名控制事件返回宿主。
- 网络入口不再把每条解析消息广播给所有模块。文件传输、语音、手柄、输入和 WebRTC
  signaling 进入各自的 service/具体 RTC 槽位；连接通知只发给确实消费它的 capture，
  hello/heartbeat 只进入网络 transport 类别。
- `WebRtcLibraryHost` 只按确定名称加载 `net_rtc.dll` 和 `net_rtc_local.dll`，不扫描目录；
  `DynamicLibrary` 由 RAII owner 管理，模块别名不能逃逸 DLL 生命周期。

架构守卫同步增加了以下不可回退断言：显式注册表不得出现 `PluginManager`、
`GetPluginById` 或 `std::map`；通用控制入口不得重新接收捕获帧、光标或编码帧；视频编码
主链不得重新出现 `std::any`；WS 不得重新使用服务定位；WebRTC host 不得目录扫描且必须
明确列出两个允许的库名。

新增验证覆盖静态模块身份链接、voice callback 内 stop、delivery 自注销、外部 shutdown
等待 in-flight callback、重复启停、面板不可用拒绝和停止后无迟到媒体。最终发布仍必须执行
聚焦同步与 SHA-256 一致性检查。

### 阶段 8/9：WebRTC 固定动态边界与旧框架退场

- WebRTC host 的公共结果已从通用 `PxPluginInterface` 收窄为网络组件，并只加载
  `net_rtc.dll`、`net_rtc_local.dll`；固定库 owner 与借用 singleton 使用 aliasing
  `shared_ptr` 绑定，引用释放后才允许卸载。
- RTC、RTC Local、WS、UDP 的 `OnMessageRaw(std::any)` 已删除。SDP、ICE、逻辑会话
  capability 和 UDP association 分别使用明确类型的网络命令；架构守卫会扫描全部 Render
  C++，禁止重新引入 untyped control entry。
- `PxPluginInterface` 中的全量互联 map、attach API、按 UUID 查询和旧 dispatch helper
  已删除；DDA 的网络积压判断改成组合根注入的 weak backlog probe。
- 旧 `plugin_manager.*` 和三个 `plugin_*router.*` 生产文件名已退出构建图。当前生产
  边界分别为 `modules/render_module_registry.*`、
  `ingress/render_event_ingress.*`、`ingress/network_event_ingress.*` 和
  `pipeline/encoded_video_fanout.*`，其类型名与实际职责一致。
- 原 `stream plugin thread` 接口已更名为 media dispatch lane；代码不再通过旧命名暗示
  媒体必须进入插件广播。
- 移动后纳入“新增文件”所有权门禁时暴露的两个 settings singleton 裸指针成员，已改为
  构造期绑定的非空进程生命周期引用；无新增所有权豁免。

架构守卫现在还断言全 Render C++ 中不存在 `GetPluginById`、`AttachPlugin`、
`AttachNetPlugin` 或 `OnMessageRaw`，并断言 WebRTC host 头文件不再暴露通用插件
interface。

### 最终聚焦交付记录

未运行 release-only 的 `build_official.bat`。按 workspace 规则使用
`build_cpp_render_arch_tests.bat 8` 完成构建与统一 CTest，18/18 通过；此外对 13 个
生命周期敏感 suite 执行 `--repeat until-fail:5`，65/65 次通过。RTC lifecycle suite
每次内部对两个动态库各执行 10 轮加载、create、stop、destroy、unload；新增的固定 Host
用例还验证：Host 自身释放后，模块 alias 仍会保持 DLL 存活，最后一个 alias 释放后 DLL
才卸载。该 Host 用例又独立连续运行 5 次通过。

`build_official/dist/deps/rd_plugins` 最终只包含：

| 产物 | 大小 | 最终 SHA-256 |
|---|---:|---|
| `net_rtc.dll` | 28,727,808 B | `217141844791CEC6F470FBCE41AA2C2F3A44BEAB577B2B4518D7EAE4D7E3C139` |
| `net_rtc_local.dll` | 23,812,096 B | `8DAD463F79CE4E9A794DFC8FDB917E915C58CE0EAE26F574A9710AA5969D4645` |

`px_render.exe` 的最终 SHA-256 为
`BC93AE34BC12C8BD71A55A4CF42D37FFC7103630EE3649166CF355D0F465043D`。以上三项均在
最后一次构建后重新发布，并独立比较 build tree 与 dist，结果全部相同；
`render_retired_modules_guard -CheckDist` 通过。

上述结果是本机可自动化执行的 L0-L3、所有权、架构边界、DLL 生命周期和交付验证。
计划中的真实 GPU/多显示器、弱网/断网、端到端音视频主观质量、长时间 8 小时 soak 以及
产品日志隐私抽检属于 L4-L6 发布验收，必须在目标硬件与测试网络中执行；本记录不把这些
外部环境验收误报为已完成。

### 阶段 10：组合 API 与调用锁进一步收口

- Encoder、NetworkIngress、RenderIngress、Panel client、statistics 和 encoded fanout
  不再取得 RTC/UDP/Relay 对象，也不再调用 `VisitAllModules`、`VisitEncoders` 或
  `VisitNetworkTransports`；跨组件调用改为具名 typed 操作。
- WS 专属 IPC/UserProxy、RTC SDP/ICE/媒体、Relay signaling、UDP association、模块信息、
  D3D resource、IDR/RFI 都由 `RenderModuleRegistry` 在组合边界内完成。
- 注册表内部 visitor 改为先复制强所有权快照、释放 `modules_mtx_` 后再调用模块，避免模块
  callback、shutdown 或 re-entry 在持有组合锁时发生。
- 架构门禁新增断言：具名 transport getter 不得回到 registry 公共头文件，三个通用 visitor
  不得出现在组合实现以外的 Render C++ 文件。
- `WebRtcLibraryHost::Load` 的公开结果从 `shared_ptr<PxNetPlugin>` 改为具体
  `WebRtcLibraryLease`。调用方只能观察固定的 remote/local DLL 身份和持有 RAII 生命周期；
  旧 ABI 模块对象只能由组合注册表这一兼容友元取得。

本阶段 `px_render` 聚焦构建通过，统一 Render CTest 18/18 通过，ownership、architecture
guard 和 `git diff --check` 均通过；构建树与 dist 的上述最终哈希重新核对一致。

### 阶段 11：WS 网络能力显式注入

- 静态 WS 模块不再保存 `vector<weak_ptr<PxNetPlugin>>` 网络 peer graph，也不再暴露
  `ConfigureNetworkPeers`、`GetNetworkPeers`、`GetLocalRtcPlugin` 或 `GetUdpTransport`。
- 组合根只向 WS 注入其实际需要的四项能力：网络广播、文件传输定向广播、Local RTC
  分配和 UDP association 更新。每项能力都是具名 typed callback，不允许 WS 枚举、识别
  或持有具体 transport。
- 所有注入 callback 只捕获 `weak_ptr<RenderModuleRegistry>`，调用时先 `lock()`；Registry
  在锁内仅取得目标模块的强所有权快照，释放组合锁后再进入模块，避免异步销毁、重入或
  shutdown 与组合锁形成生命周期耦合。
- UDP 更新现在返回真实的可用性结果。Registry 已销毁或 UDP 模块不可用时，WS 会记录
  明确错误且不再误报 association 已注册。
- 架构守卫扫描整个静态 WS 源码，禁止旧 peer graph API、具体 RTC/UDP getter 和
  `network_peers_` 成员重新出现。
- `render_builtin_linkage` 新增显式能力测试，连续调用 100 轮网络广播、文件传输、RTC 分配
  和 UDP 更新；能力 owner 释放后继续触发迟到调用，验证 weak lifetime 会安全拒绝 RTC/UDP
  操作且不会访问已销毁状态。

本阶段使用目标级入口构建 `px_render`、`test_render_builtin_linkage` 和
`check_cpp_ownership`；关键 linkage 与 architecture guard 各连续 5 轮通过，随后统一
Render CTest 18/18 通过。发布脚本同步运行产物后，独立复核 build tree 与
`build_official/dist`：`px_render.exe` 及两个 RTC DLL 的 SHA-256 均逐项相同，插件目录仍
只包含 `net_rtc.dll` 与 `net_rtc_local.dll`。

### 阶段 12：GameHook IPC 高频媒体退出通用事件路由

- 静态 WS 的 `/ipc` 输入新增显式 `IpcVideoFrameSink` 与 `IpcAudioFrameSink`；组合根只注入
  捕获媒体入口，callback 捕获 `weak_ptr<RdApplication>`，不会反向持有应用或形成环。
- GameHook 视频与音频在 wire 校验完成后直接以 `CaptureVideoFrame`、`CaptureAudioFrame`
  值进入应用，不再创建 `PxPluginCapturedVideoFrameEvent` 或
  `PxPluginRawAudioFrameEvent`，减少高频路径的一次堆分配、RTTI 分派和通用路由跳转。
- wire header 解析不再对 `string_view::data()` 做可能未对齐的 `reinterpret_cast`；固定大小
  POD 先复制到 `std::array`，再用 `std::bit_cast` 得到 owned 值。PCM payload 也直接构造
  owned `Data`，不会保留网络接收缓冲区的借用地址。
- 通用 `RenderEventIngress` 删除已无生产者的 raw video、raw/split audio 和 encoded audio
  兼容分支。架构守卫新增五类高频事件断言，禁止它们重新进入通用入口。
- linkage 测试覆盖显式 IPC sink、视频/音频 frame identity 传递以及 sink owner 销毁后的
  迟到调用安全性；关键 linkage 与 architecture guard 连续 10 轮通过。

本阶段目标级构建和 ownership gate 通过，统一 Render CTest 18/18 通过；发布后
`px_render.exe` 的 SHA-256 更新为
`BC93AE34BC12C8BD71A55A4CF42D37FFC7103630EE3649166CF355D0F465043D`，build tree 与 dist
一致，两个 WebRTC DLL 未变化。

### 阶段 13：强类型 Source、Processor 与 Observer 扩展链

- 新增 `CapturedMediaPipeline` 与弱生命周期 `MediaSourcePort`，数据源只能发布 owned
  `CapturedVideoFrame`/`CapturedAudioFrame`；pipeline owner 销毁后，迟到发布返回稳定 typed
  error，不会访问已析构对象。
- 视频和音频 Processor 使用各自的强类型函数签名、显式顺序和 RAII subscription。注册表只
  保存 weak callback，dispatch 前复制 entry 快照并释放锁；callback 内注销后尚未执行的
  processor 会被跳过，异常转换为 `PIPELINE_PROCESSOR_FAILED`。
- Observer 扩展到 captured video。`EncodedMediaBus` 和 `PipelineStatisticsObserver` 分别提供
  typed fanout 与窗口聚合，Observer 不能修改主链数据；统计日志增加 captured video 帧数、
  字节数和速率。
- 生产音频入口接入 pipeline；没有 Processor 时复用原 `Data` 走快路径，避免不必要的二次
  payload 拷贝。CPU raw video 仅在确有 Processor 注册时进入扩展链，DDA/GameHook 共享纹理
  的零拷贝路径保持不变。
- Source/Processor 输出最终回到既有 encoder 和 RTC Local 音频入口。提交失败使用稳定错误码、
  五秒限频窗口和 suppressed 计数记录，避免逐帧错误风暴。
- 单元测试覆盖处理顺序、callback 内注销、主动丢帧、processor 失败、expired callback 清理、
  pipeline/source 销毁次序以及视频 Observer/统计生命周期。

本阶段目标级构建、ownership gate 与统一 Render CTest 19/19 通过。发布后
`px_render.exe` 的 SHA-256 为
`429D333777399A3F37FE2E72D6CC5ADD580543F708FDA872E2677018B661953B`；build tree 与 dist
逐项一致。两个 WebRTC DLL 哈希保持不变，dist 插件目录仍只包含这两个动态网络库。

### 阶段 14：WS 控制回调收口为 typed co_await

- WS ticket redemption、logical-session admission 和 RTC Local allocation 不再使用
  `condition_variable::wait_for` 阻塞 asio2 I/O callback；I/O callback 只复制 owned 请求值、
  获取 RAII response defer guard，再把工作交给 `PxAsyncScope` 的 control lane。
- WebSocket open 按 ticket -> capability -> admission 顺序执行 typed awaitable。HTTP RTC
  allocation 按 ticket -> admission -> allocation 顺序执行 typed awaitable；每步具有独立
  deadline、稳定错误码和结构化失败日志。
- 新增 `AwaitWsValueCallback` 作为值回调桥。超时、scope cancellation 或 owner 析构后，
  晚到的 accepted admission 会执行显式 binding close 补偿，避免逻辑会话泄漏。
- coroutine 不跨 `co_await` 保存 request/response 引用、socket 借用值或插件裸指针；最终
  WebSocket router 创建和 HTTP response mutation 都投递回 asio2 session queue。
- `WsData` 删除 `map<string, any>` service bag，改为 typed `weak_ptr<WsPlugin>`；三个 WS
  router 同步移除 `Get<WsPlugin*>` 查询。架构门禁禁止 net_ws 重新引入 `std::any`、同步条件
  等待或插件裸指针。
- WS server 的重复 start/stop 为幂等路径；回调线程请求 shutdown 时先取消 scope，再使用
  公共 runtime 的 RAII joiner 异步回收线程，不自等待、不自 join，也不误报 drain timeout。
- 单元测试覆盖 typed completion、timeout 后 late compensation、scope cancellation 后晚到
  callback；相关 callback、linkage 和架构门禁连续 10 轮通过，async lifetime gate 通过。

本阶段目标级构建和 ownership gate 通过。发布后 `px_render.exe` 的 SHA-256 为
`2C82ED214291BB3AB9144757F4E16B14FBC8D5FF5C4AD092F14FC6C5DA371ACA`，build tree 与 dist
一致；WebRTC DLL 未改动。

### 阶段 15：统一测试 runner、日志与交付证据门禁

- `build_cpp_render_arch_tests.bat` 已实现 `quick`、`lifecycle`、`integration`、`hardware`、
  `all` 和 `performance` 六种模式；仍兼容原先以数字作为并行度的调用。runner 只调用精确
  CMake target，不调用 release-only 的 `build_official.bat`。
- Render 自动化测试按 `render-guard`、`render-unit`、`render-lifecycle`、
  `render-integration` 和 `render-hardware` 注册。原来只构建的 RPC state、logical session、
  direct grant、plugin context、WAS reinit/process-loopback 和 recorder writer 测试均已注册到
  CTest；各 focused `build_cpp_*_tests.bat` 也会在构建后真正执行对应测试。
- 真实 WAS 默认设备和 PID process-loopback 测试通过专用 wrapper 检查前置条件；开发机未
  显式提供 `RENDER_TEST_WAS_HARDWARE=1` 或 `RENDER_TEST_AUDIO_PID` 时返回 CTest `SKIP`，
  报告明确标记为 INCOMPLETE，不再把环境缺失伪装成 PASS。
- 每次 runner 自动写入独立的 `test-results/render-architecture/<run-id>/` 证据目录，包括环境、
  Git revision、构建目标、构建/CTest 日志、JUnit、async lifetime、隐私扫描、process metrics
  占位、performance baseline/comparison 状态、artifact SHA-256 和 Go/No-Go 摘要。该目录被
  Git 忽略，不提交大体积运行结果。
- 日志扫描会拒绝 credential-shaped assignment、未脱敏 query value 和成功运行中的非预期
  structured ERROR。`PrivacyLogId` 新增确定性单元断言；`PerformanceWindow` 补齐 bounded
  p50/p95/p99 聚合和 reset 断言。
- `start_render_hook.ps1` 和 voice install validation 删除已经静态化的 live-pusher、NVENC、
  voice-call DLL 假设，避免验收脚本重新发布或要求已退休产物。

`all` 模式的 2 个架构门禁和 28 个 L1-L3 测试全部通过，ownership 与全量 async lifetime
gate 通过，privacy scan 无命中。`hardware` 模式的 2 个用例因本机未注入显式前置条件而
按预期 SKIP；`performance` 模式明确标记需要固定验收硬件/profile。发布后 `px_render.exe`
SHA-256 为 `759D47A96BD244FFCA7C8F6CD90B4EFD18B6BB770F2CFD9A6C68F4F89717FF3F`，build tree 与
dist 一致，两个 WebRTC DLL 哈希仍保持不变。

### 阶段 16：结构化日志合规与 WS 性能窗口

- 架构核心与内置 WS 的所有 WARN/ERROR 均必须带有稳定
  `event/component/code/operation/outcome/recoverable` 字段。架构边界脚本现在会
  扫描这两个生产区域，字段缺失将直接使 L0 门禁失败。
- 显示器、stream、device 和 peer 标识在日志边界统一经过 `PrivacyLogId`；
  WS query 只记录 key，value 一律输出 `<redacted>`；web client 本地路径不再进入
  日志。门禁同时拒绝在这些标识字段上绕过脱敏函数。
- 剪贴板与文件传输权限拒绝使用有界 `RateLimitedLogGate`：首次立即输出，
  五秒窗口内抑制重复日志，后续摘要携带 `suppressed`。限频 key 容量固定，
  不会由动态 session 标识导致无界内存增长。
- 新增 `TransportPerformanceWindow`。WS 高频 callback 只做 relaxed atomic 计数，
  `On1Second` 驱动的 control path 每五秒产生 `event=transport.window`，汇总活跃连接、
  connect/disconnect、收发消息与字节速率、真实 drop、当前队列和 high watermark。
  空闲窗口不输出误导性的零值日志，start 会 reset 旧窗口。
- 统计实现是值类型/RAII owner，不保存回调、transport 或 session 借用对象；
  新增单测使用 manual `steady_clock` 精确验证窗口间隔、聚合、reset、queue high
  watermark 与四线程并发计数。架构门禁还要求 WS 生产接线持续保留
  inbound/outbound/drop 计数、窗口 snapshot 和稳定事件名。
- 本批次继续遵守零裸指针和异步弱引用规则；ownership 与 async lifetime gate
  均通过。最终统一 runner 的 2 个 L0 门禁和 28 个 L1-L3 测试全部 PASS，
  无非预期 ERROR，privacy scan PASS。
- 发布后 `px_render.exe` 的 SHA-256 为
  `957656CDAE84522B1F1B7BD81B17F4B94948E64B2AD7ECA44943163C25DB2966`；build tree 与
  `build_official/dist` 一致。WebRTC 两个 DLL 保持原哈希，dist 的 Render 插件目录
  仍只有这两个动态网络库。

本机可自动化的架构迁移开发到此闭环。真实 WAS/PID 音频、GPU/多显示器、
LAN 弱网、主观音画、30 分钟压力与 8 小时 soak 仍属于目标硬件上的最终产品验收；
runner 会将未满足的前置条件明确标成 SKIP/INCOMPLETE，不将其误报为 PASS。
