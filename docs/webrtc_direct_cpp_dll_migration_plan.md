# WebRTC 普通 C++ DLL 迁移实施方案

## 1. 产品决定

WebRTC 是与 WS、UDP、Relay 同层的固定网络组件，不是插件，也不是可发现的扩展点。Render 和 Client 与 WebRTC DLL 使用相同的
MSVC、C++ 标准、CRT、构建配置和发布批次，因此使用直接链接的 C++ DLL，不建立稳定 C ABI，不使用运行时插件加载，也不增加
`IWebRtcTransport` 一类虚接口。

主程序允许链接 Windows 为 DLL 生成的 import library，但严禁链接包含 libwebrtc 目标代码的 `webrtc.lib`：

```text
px_render.exe
  -> px_render_rtc_remote.lib (import library) -> px_render_rtc_remote.dll -> PRIVATE webrtc.lib
  -> px_render_rtc.lib (import library)        -> px_render_rtc.dll        -> PRIVATE webrtc.lib

px_client.exe / SDK
  -> px_client_rtc.lib (import library) -> px_client_rtc.dll -> PRIVATE webrtc.lib
```

DLL 缺失视为发布包损坏。功能开关只控制具体 WebRTC runtime 是否启动，不再控制 DLL 是否作为插件被发现或加载。
Render 的两个 DLL 必须与 `px_render.exe` 同目录发布；不再放入 `deps/network` 或任何插件目录，以遵循 Windows import dependency 的标准加载路径。

## 2. 迁移前问题

- Render 的两个 WebRTC DLL 仍继承 `PxNetPlugin`，通过 `GetInstance` 和 `WebRtcLibraryHost` 动态解析。
- WebRTC 仍依赖 `PxPluginContext`、`PxPluginBaseEvent`、`OnCreate/OnStop/OnDestroy/On1Second` 等插件概念。
- WebRTC CMake target 将 `webrtc.lib` 和其他实现依赖声明为 `PUBLIC`；主程序直接链接 DLL target 后会产生传递链接风险。
- Client RTC 仍通过 `QLibrary/GetInstance` 取得 `RtcClientInterface`，并保存非 RAII 的 library/instance 裸指针。
- Render 到 DLL 的事件经过通用插件事件广播，增加消息分类、关闭静默和回调生命周期复杂度。

## 3. 目标代码结构

Render 保留两个职责不同的具体类型，不建立共同基类：

- `WebRtcRemoteTransport`：远程 WebRTC 会话、信令、媒体和数据通道。
- `WebRtcLocalTransport`：本地直连 WebRTC 会话、多屏媒体和数据通道。
- `WebRtcEvent`：DLL 向 Render 输出的 typed value variant。
- `WebRtcExecutionContext`：使用注入的 `PxAsyncRuntime/PxAsyncScope` 串行投递 owned event，并持有 coroutine 周期任务。

Client 使用直接链接的具体 `RtcClient` facade；libwebrtc observer 和 borrowed pointer 继续封闭在
`src/px_deps/px_webrtc_client` 内，不进入 SDK 业务对象。

Client 的 `RtcClient` 导出类采用 PImpl，公共头文件不得包含 libwebrtc 头文件，不得暴露 libwebrtc 指针或对象布局。Render 的两个
具体 transport 只允许由 `WebRtcTransportHost` 的实现文件引用，不能成为业务层公共依赖，也不增加人为的共同虚基类。导出方法可以
使用项目已有的 `std::shared_ptr`、消息 value 和 `PxAwaitable`，因为所有二进制由同一工具链原子构建和发布。

## 4. 所有权和异步规则

- 工厂返回 `std::shared_ptr` 或 `std::expected<std::shared_ptr<T>, Error>`；构造失败不得留下半初始化实例。
- GammaRay 代码不声明、保存、传递、返回或捕获裸指针；libwebrtc borrowed ABI 仅留在既有专用 adapter 内。
- DLL 内异步 callback 捕获 `weak_ptr`，投递前 `lock()`；不得捕获 `this`。
- DLL 输出事件必须先复制或移动成 owned value，再投递到 `PxAsyncScope`；异步任务只捕获 `weak_ptr` 或 owned value。
- 控制事件不得静默丢弃。音视频本身走 RTP 或现有有界帧缓存，不通过 WebRTC 控制事件队列搬运编码帧。
- 生命周期为 `Created -> Starting -> Running -> Stopping -> Stopped`，`StopAsync` 和析构必须幂等。
- WebRTC 内部周期任务由 `PxAsyncScope` 和 coroutine timer 持有，不再由 Render 每秒调用 `On1Second`。
- 主程序不主动 `FreeLibrary`。对象、scope、observer 和 WebRTC worker 完成关闭后，由 Windows 在进程结束时卸载 DLL。

## 5. 分批实施

### R1：链接边界

1. 将逻辑 target `net_rtc`、`net_rtc_local` 和 Client RTC target 的 `webrtc.lib` 改为 `PRIVATE`；对应 Render 产物名固定为
   `px_render_rtc_remote` 和 `px_render_rtc`。
2. 为三个 DLL 增加独立导出宏和仅包含项目类型的 public include。
3. Render/Client 直接链接对应 DLL target 的 import library。
4. 增加 CMake/PowerShell 门禁，禁止主程序链接命令、target interface 和 map 文件出现 `webrtc.lib`。

### R2：Render 具体类型

1. 将 Remote/Local 类改名为 `WebRtcRemoteTransport`、`WebRtcLocalTransport`。
2. 移除 `PxNetPlugin` 继承和所有 `override`。
3. 将 `PxPluginParam`、`PxPluginSettingsInfo` 替换为 typed WebRTC configuration/settings。
4. 将插件基类提供的 helper 迁移为具体成员、注入依赖和 typed event publisher。
5. `RenderModuleRegistry` 分别持有 Remote/Local 具体对象，不使用共同 transport interface。

### R3：事件和协程生命周期

1. 将 WebRTC 生产事件从 `PxPluginBaseEvent` 转成 `WebRtcEvent` value variant。
2. 用 typed `WebRtcEvent` 和受 `PxAsyncScope` 管理的串行任务接入 `RenderEventIngress::ProcessWebRtcEvent`。
3. 将启动、停止、本地分配和需要等待的信令流程改成 typed awaitable。
4. 删除 WebRTC `On1Second` 驱动、跨 DLL 插件 callback 和 unload quarantine。

### R4：Client 具体 DLL

1. `px_client_rtc.dll` 导出具体 C++ factory/facade，通过 import library 直接链接。
2. 删除 SDK 中 `QLibrary`、`GetInstance`、`FnGetInstance` 和 library/instance 裸指针。
3. SDK 仅持有智能指针，并以 `PxAsyncScope` 管理 RTC 启停、ICE restart 和迟到 callback。

### R5：清理

1. 删除 Render/Client WebRTC 的 `GetInstance` 导出。
2. 删除 WebRTC 对 `px_net_plugin`、`PxPluginContext` 和通用插件事件的依赖。
3. 若生产代码已无旧 Render 插件消费者，删除 `src/px_render/plugin_interface` 及只验证旧 ABI 的测试。
4. 更新架构文档、发布清单和边界守卫，禁止重新引入 WebRTC 插件语义。

## 6. 日志与关键性能统计规则

### 6.1 错误和警告日志

新建或改造的 WebRTC 生命周期错误必须使用可检索字段，至少包含：

```text
event=<事件> component=<组件> code=<稳定错误码> operation=<阶段> outcome=<结果> recoverable=<true|false>
```

- `code` 使用稳定的大写标识，不把第三方错误字符串当错误码；第三方文本放在 `reason`。
- 建连日志带 transport、会话/连接的脱敏标识和 attempt/generation；不得输出密码、token、TURN credential、完整 SDP 或 ICE。
- Start/Stop/Destroy、ICE restart、准入拒绝、超时、队列拒绝和 callback quiescence 超时必须记录最终结果。
- 同一错误在循环中必须限频；状态变化立即记录，未变化的周期状态只进入汇总统计。
- DLL 创建失败不回退插件扫描，直接记录 `recoverable=false` 的发布完整性错误。

### 6.2 性能日志

按 5 秒或更长窗口聚合，禁止逐帧 INFO：

- 建连：DNS/信令/ICE/DTLS/SCTP ready 各阶段耗时、总耗时、重试次数和最终路径。
- 发送：media/input/FT data channel 的排队深度、高水位、拒绝数、buffered amount 和恢复次数。
- 视频：按脱敏 monitor 聚合输入 FPS、RTP 输出 FPS、关键帧请求/合并次数、缓存深度、gap/drop 和编码耗时分位值。
- 音频：采样率、声道数、累计帧数、丢帧和 callback 最大间隔；首帧可单独记录一次。
- 生命周期：scope outstanding、callback outstanding、停止耗时和超时阶段。

性能日志只记录计数、耗时、枚举和脱敏 ID；不记录媒体负载、SDP、ICE credential 或业务消息正文。

## 7. 构建与链接验收

- `px_render_rtc_remote.dll`、`px_render_rtc.dll`、`px_client_rtc.dll` 的链接命令必须包含 `webrtc.lib`。
- `px_render.exe`、`px_client.exe`、`px_panel.exe` 的链接命令和 map 文件不得包含 `webrtc.lib` 或 libwebrtc object。
- `net_rtc`、`net_rtc_local`、Client RTC target 的 `INTERFACE_LINK_LIBRARIES` 不得包含 `webrtc.lib`。
- `dumpbin /DEPENDENTS px_render.exe` 应包含 `px_render_rtc_remote.dll` 和 `px_render_rtc.dll`。
- `px_render_rtc_remote.dll` 和 `px_render_rtc.dll` 必须发布到 `build_official/dist` 根目录，与 `px_render.exe` 相邻；发布器必须清理
  `deps/network` 中的新旧 WebRTC 副本。
- 所有 DLL、EXE、语言资源和运行资源发布到 `build_official/dist` 后必须与 build tree 的 SHA-256 一致。
- 日常验证仅使用 `build_cpp_*.bat`；不得调用 release-only 的 `build_official.bat`。

## 8. 详细测试方案

### 8.1 自动化测试

- `test_webrtc_transport_lifecycle`：Remote/Local 具体 DLL 同时创建、Start、StopAsync、Destroy，循环 100 轮；验证停止时
  callback quiescence 归零，DLL 保持进程级加载且不存在主动卸载。
- `test_rtc_client_dll_lifecycle`：通过 import library 调用具体 C++ factory，验证未初始化析构和重复 Exit，循环 10 轮。
- callback 测试覆盖 queued callback 后销毁、注销期间 dispatch、callback 中触发 shutdown 和迟到 ICE/SDP 不访问已销毁 owner。
- 运行 `check_webrtc_dll_link_boundary.ps1`：验证三个 DLL 私有链接 `webrtc.lib`，两个 EXE 只出现 import library，并扫描禁止
  `QLibrary/FnGetInstance/GetInstance` 回归。
- 运行 `check_cpp_ownership.ps1` 和 `test_render_architecture_boundaries.ps1`，验证无新增裸指针、`[this]` 和插件基类依赖。

### 8.2 失败注入

- DLL 缺失/版本不配套：Windows loader 明确失败，不进行目录扫描或插件 fallback。
- runtime 创建失败、scope 已停止、重复 Start/Stop、部分启动失败、停止超时。
- 信令超时、错误 SDP、迟到 ICE、ICE restart 失败、断网恢复和 Relay fallback。
- data channel 高水位、FT 堵塞、视频消费者落后、IDR 请求风暴和本地多屏热插拔。

### 8.3 最终人工与压力验收

- Remote 与 Local：键鼠、剪贴板、文件传输、语音、音频、H264/H265、单屏/多屏、切屏和热插拔。
- 远端网络：首次连接、连续断网重连、ICE restart、TURN/直连路径、Relay fallback、退出中断重连。
- 30 分钟高码率/多屏压力测试检查 CPU、内存、线程、句柄、队列高水位和错误日志。
- 8 小时 soak 检查内存/句柄单调增长、迟到 callback、死连接残留、周期日志限频和停止耗时。
- 验收只使用 `build_official/dist`，并在启动前复核所有变更 EXE/DLL/资源与 build tree 的 SHA-256。

## 9. 当前实施状态与完成定义

截至本次收敛：R1 至 R5 已全部落地。旧 `src/px_render/plugin_interface` 已删除；真正的流程节点扩展契约独立位于
`architecture/extensions`，不再复用旧 Render 插件 ABI、事件总线或生命周期。

- WebRTC 生产类不继承 `PxPluginInterface` 或 `PxNetPlugin`。
- Render 和 Client 中不存在 WebRTC `GetInstance`、`LoadLibrary/QLibrary` 或插件目录加载。
- Client DLL 内部旧 `RtcClientInterface` 已删除，SDK、facade 与内部连接对象均使用具体类型组合。
- 主程序不链接 `webrtc.lib`，只链接 WebRTC DLL import library。
- WebRTC 不进入 flow-node 插件注册表，也不进入通用插件消息广播。
- 新增和触及的项目 C++ 代码满足智能指针、RAII、确定性初始化和 150 列规则。
- 自动化构建、边界门禁、生命周期测试、dist 发布和 SHA-256 校验全部通过。

## 10. 自动化交付记录（2026-09-05）

- `build_cpp_render.bat 8`、`build_cpp_client.bat 8` 与 `build_cpp_panel.bat 8` 通过；未运行 release-only 的 `build_official.bat`。
- Render 生命周期集合通过 19/19，其中 `webrtc_transport_lifecycle` 执行 Remote/Local 100 轮重复创建、启停和销毁，
  `rtc_client_dll_lifecycle` 覆盖 Client 具体 DLL factory、重复 Exit 和销毁。
- `build_cpp_render_arch_tests.bat all 8` 最终通过：2 项架构门禁和 36 项 unit/lifecycle/integration 测试全部成功；证据目录为
  `test-results/render-architecture/20260905-022104-all`。
- ownership、async lifetime、WebRTC link boundary、Render architecture 和 retired-module delivery 五类门禁全部通过。
- `dumpbin /DEPENDENTS px_render.exe` 同时包含 `px_render_rtc_remote.dll` 与 `px_render_rtc.dll`；主程序链接边界不包含静态 `webrtc.lib`。
- `build_official/dist` 中两个 Render RTC DLL 与 `px_render.exe` 相邻；根目录旧名称以及 `deps/network` 下的新旧副本均不存在。

| 产物 | SHA-256（build tree 与 `build_official/dist` 一致） |
|---|---|
| `px_render.exe` | `7EB6698ACEE23AE036A023CEFB9741EB91689A0C2AA0AC0EE759EB31863B32CA` |
| `px_gh.dll` | `1D19C8019FDA6C9B329513356D0D48333A1CC639260E3D0906ADE1DD176DE7E4` |
| `px_render_rtc_remote.dll` | `0F8B9D6AD0FFE782E65EAC64B9EB5498BB405C8671F66A70816E9B653F0A0DAF` |
| `px_render_rtc.dll` | `F65DD80169F8F0D1F951C83CAD8891D84FEFA5038B1E89652CB814C1A8F75F8D` |
| `px_panel.exe` | `295B621FEE51BD1AD0368752B41A939EFEDDE7C490CFA3E857A3E83949DFF21C` |
| `px_client.exe` | `2D8F1CAB1F3FEB98363C8BC00C3BDE94BC1F236FA99B136EC4E1EF8F355A68DC` |
| `px_client_rtc.dll` | `71F53C291224AB3A2AE8DA7642D7ECB596BD1E05E98D9BC187AFEC4315060BF1` |

以上为自动化软件门禁结论；真实音视频设备、多显示器、弱网、压力和长稳仍由最终产品验收执行。

## 11. Render 旧插件基础设施收敛计划

状态：已完成（2026-09-05）。

WebRTC 普通 DLL 迁移完成后，`src/px_render/plugin_interface` 中仍混放了旧插件 ABI、模块执行上下文、运行设置、网络类型、
采集错误和 Render 内部事件。这些类型不再代表真实扩展边界，继续保留会让内建模块误用插件术语和巨型消息总线。

本阶段的完成目标如下：

1. 删除没有生产派生类的 `PxPluginInterface`、`PxNetPlugin`、`PX_PLUGIN_EXPORT/GetInstance` 以及仅验证旧 ABI 的测试。
2. `PxPluginContext` 不再作为内建模块基类能力存在。异步生命周期统一由 `PxAsyncRuntime/PxAsyncScope` 管理；仍需兼容线程/UI
   投递的能力迁入明确命名的 `RenderExecutionContext`，随后按调用方逐步缩小。
3. `PxPluginSettingsInfo` 改为 `RenderRuntimeSettings`，`NetPluginType` 改为 `TransportKind`；采集错误移入
   `architecture/diagnostics`，这些类型不得继续位于插件目录。
4. 将 `PxPluginBaseEvent + PxPluginEventType + std::any` 巨型事件模型拆成 owned、typed event value。采集、编码、网络输入、
   会话控制、RTC 信令和统计分别进入现有 pipeline、ingress、session 与 diagnostics 边界，不再依赖插件身份或 `dynamic_cast`。
5. 内建 WS、UDP、Relay、采集器、编码器、fanout、ingress、`RenderModule` 和 `RenderModuleRegistry` 不得包含
   `plugin_interface` 头文件，也不得继承或模拟旧插件生命周期。
6. Render 唯一保留的插件概念是 `architecture/extensions` 下的流程节点扩展：Source、Processor、Encoder、Observer 和 Sink。
   WebRTC、WS、UDP 与 Relay 永远不注册为流程节点插件。

迁移按“值类型归位 -> typed event -> 执行上下文 -> 死 ABI 删除 -> CMake/测试/文档门禁”顺序进行，保证每一步都能定向编译。
所有新增或触及代码必须满足智能指针、RAII、确定性初始化和 150 列规则。异步 event 必须拥有 payload；同步连续内存视图只允许
使用 `std::span`，不得保存或捕获其底层地址。

验收门禁：

- 生产代码中不存在 `PxPluginInterface`、`PxNetPlugin`、`PxPluginContext`、`PxPluginBaseEvent`、`PX_PLUGIN_EXPORT`。
- `src/px_render/plugin_interface` 目录和对应 `px_plugin/px_net_plugin` CMake target 被删除。
- `rg` 边界检查证明内建模块不包含旧插件路径，新流程节点接口不使用 `std::any`、裸指针或通用事件枚举。
- queued callback 后销毁、dispatch 中注销、callback 中 shutdown、重复 Start/Stop 和 scope drain 测试通过。
- 使用 `build_cpp_*.bat` 完成 Render 定向构建与架构测试；所有改变的运行产物发布到 `build_official/dist` 后 SHA-256 一致。

## 12. Render 旧插件基础设施收敛实施记录（2026-09-05）

- 删除 `src/px_render/plugin_interface` 全目录、`px_plugin`/`px_net_plugin` CMake target 与旧 `test_plugin_context_lifecycle`。
- 内建模块使用 `RenderExecutionContext`，其任务由共享 `PxAsyncRuntime`、独立 `PxAsyncScope` 和 coroutine timer 管理；停止可从
  callback 内安全发起，排队 callback 在停止后保持静默。
- 设置、网络类型和采集错误分别归位到 `RenderRuntimeSettings`、`network/transport_types.h` 与
  `architecture/diagnostics/monitor_capture_error.h`。
- Render 内部事件改为 `RenderEventEnvelope + std::variant` 类型化 owned payload；入口使用 `std::visit`，不再使用事件枚举、
  `std::any`、基类向下转换或插件来源身份。
- `test_render_execution_context_lifecycle` 覆盖 callback 内停止、延迟任务取消、注销后保存 dispatcher、callback 内销毁和 100 轮
  重复创建/投递/停止。
- 架构门禁现在拒绝旧目录、旧类型、旧 include 和旧 CMake target 回归，只允许 `architecture/extensions` 下的 Source、Processor、
  Encoder、Observer 与 Sink 流程节点插件。
- `build_cpp_render_arch_tests.bat all 8` 通过 2 项架构门禁和 36 项 unit/lifecycle/integration 测试；最终证据结论为 GO，目录为
  `test-results/render-architecture/20260905-022104-all`。
- 最终发布哈希：`px_render.exe` 为 `7EB6698ACEE23AE036A023CEFB9741EB91689A0C2AA0AC0EE759EB31863B32CA`，
  `px_render_rtc_remote.dll` 为 `0F8B9D6AD0FFE782E65EAC64B9EB5498BB405C8671F66A70816E9B653F0A0DAF`，
  `px_render_rtc.dll` 为 `F65DD80169F8F0D1F951C83CAD8891D84FEFA5038B1E89652CB814C1A8F75F8D`；build tree 与
  `build_official/dist` 一致。
