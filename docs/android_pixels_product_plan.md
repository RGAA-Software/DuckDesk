# Pixels Android 客户端最终产品规划

> 状态：M0 已完成；M1 设备接入切片进行中  
> 更新日期：2026-09-06  
> 范围：`src/px_android` 及 Android 所需的项目自维护 C++ 公共模块

## 1. 产品决策

当前 Android 应用直接重建为正式的 **Pixels Android Client**。现有应用只作为协议行为和底层算法的参考，不再作为需要兼容的产品版本。

以下决策是本次重建的硬约束：

- 不维护旧 Android UI、数据库、设置、JNI API、Java package、资源名称或安装升级路径。
- 不迁移 GreenDAO 数据、SharedPreferences、已保存设备或旧版设置；开发和发布验收均以清洁安装为准。
- 不建立 legacy module、compat facade、双入口、双写数据库、feature flag 或旧新版并行运行代码。
- 直接修改当前 `src/px_android/app`，达到替换条件后删除旧实现，不额外维护一个 `app-next`。
- 最终应用只保留新架构实际使用的代码和资源。旧代码有可复用算法时，将算法提取到新边界后删除原实现。
- Android 的功能目标是 Windows Client 的移动端完整产品能力，不是旧 Android 应用的功能复刻。
- 实施允许采用一系列始终可编译、可真机安装的小提交，但这些中间提交不能演变为长期兼容层。

## 2. 最终产品身份

| 项目 | 最终值 |
|---|---|
| 产品显示名 | `Pixels` |
| Gradle 根工程名 | `PixelsAndroid` |
| Android namespace | `yun.pixels.client` |
| applicationId | `yun.pixels.client` |
| 原生库目标 | `pixels_android_core` |
| 主题 | `Theme.Pixels` |
| APK 命名 | `pixels-<version>-<buildType>-arm64.apk` |
| 首发 ABI | `arm64-v8a` |
| 最低系统基线 | Android 10 / API 29 |
| 主要形态 | 手机、平板和折叠屏 |

`com.pixels.yun.client` 不再保留，因此新应用不会覆盖旧包。测试设备必须先卸载旧应用及其数据，再安装 Pixels。首个正式版本号在发布阶段统一确定，不沿用旧 Android `3.0.0` 的兼容含义。

只保留一个正式产品维度。当前只有 `official` 的 product flavor 没有产品价值，重建时删除，使用标准 `debug` 和 `release` build type。

## 3. 品牌与视觉系统

Pixels Android 使用独立且统一的品牌资源：

- 新图标采用像素块构成的 `P` 或远程显示窗口意象，优先使用清晰的几何矢量，不复用现有 GammaRay 图标。
- 提供 Adaptive Icon 前景/背景、圆形图标、Android monochrome 图标以及旧启动器 fallback。
- Splash Screen、通知小图标、应用内 Logo 和关于页使用同一视觉母版。
- 主界面采用深色优先的 Pixels 视觉语言，蓝紫与青色作为状态和交互强调色。
- 删除 Android 资源和界面中的 `GammaRay`、`XRay`、`Thunder Cloud`、`ThunderApp` 品牌名称。
- 共享 C++ 中仍有技术意义的类型不会仅为表面改名而扩大修改范围；Android 自有公共接口必须使用 Pixels 或中性的领域名称。

图标的最终设计必须在小尺寸、圆形裁切、深浅背景和 monochrome 模式下分别验收，不能只替换一张 PNG。

页面信息架构、视觉 token、手机/平板线框、远控 Dock、输入手势以及工具流程以 [`android_pixels_ui_design.md`](android_pixels_ui_design.md) 为实施基线。

## 4. 最终用户体验

### 4.1 设备首页

- 支持 Console 账号登录、账号设备同步和短期连接票据，同时保留免登录的连接码/IP/扫码入口。
- 自动发现局域网设备。
- 扫描二维码和手动输入连接信息。
- 展示设备名称、在线状态、连接方式和最近连接时间。
- 支持添加、重命名、删除和刷新设备。
- 清楚区分认证失败、设备离线、网络不可达、协议不支持和服务器忙。

### 4.2 设备详情

- 连接远程桌面。
- 查询、启动和停止远程应用。
- 展示服务器能力、显示器、编码器和可用传输方式。
- 进入文件传输中心。
- 保存针对该设备的画质、输入和音频偏好。

不保留旧版 Steam 专属页面。Steam 游戏与其他程序统一建模为远程应用，产品层不绑定特定商店。

### 4.3 远程工作区

- 硬件视频解码优先，软件解码作为明确回退。
- 低延迟音频播放和音频焦点管理。
- 直接触摸、触摸板、相对鼠标、软键盘和组合键。
- 虚拟手柄以及 USB/蓝牙实体手柄。
- 多显示器查看、切换和虚拟显示管理。
- 分辨率、帧率、码率、解码器和传输策略调整。
- 实时延迟、帧率、丢包、码率和解码统计。
- 剪贴板、文件传输、录制和语音通话入口。
- 断线重连、切网恢复、远端关闭和安全退出。

### 4.4 文件与剪贴板

- 使用 Android Storage Access Framework 选择上传文件和保存下载内容。
- 展示任务进度、速度、剩余时间、成功/失败原因。
- 支持取消、重试、并发限制以及前台通知。
- 支持双向文本剪贴板。
- 图片和文件剪贴板必须转换为 Android 可授权的 URI/流，不暴露不可访问的文件路径。

### 4.5 录制和语音

- 将接收到的编码音视频写入本地录制文件，并通过 MediaStore 发布。
- 显示录制状态、时长、结果和失败原因。
- 语音通话支持麦克风权限、AudioFocus、耳机/蓝牙路由、静音和扬声器切换。
- 来电、切换音频设备、应用退后台和会话结束必须有明确状态机。

### 4.6 设置与诊断

- 默认画质、音频、输入、手柄和网络策略。
- 权限状态和系统能力检查。
- 版本、隐私、开源许可和日志导出。
- 可复制的会话诊断摘要，不暴露令牌、密码和用户文件路径。

旧版音乐频谱、固定 Steam Big Picture 入口、TV/Leanback 页面和演示 Tab 不属于最终产品，全部删除。

## 5. 目标工程结构

Android 工程采用单应用、多职责模块。`app` 是唯一组合根，其他模块不能反向依赖 `app`。

```text
src/px_android
├── app                  # PixelsApplication、MainActivity、导航、主题、组合根
├── core-domain          # 设备、会话、传输、录制、语音的领域模型和工作流
├── core-data            # Room、DataStore、设备仓库和持久化 adapter
├── core-native          # JNI adapter、CMake、pixels_android_core
├── feature-devices      # 发现、扫码、手动添加、设备详情和远程应用
├── feature-remote       # 串流 Surface、控制层、显示器和统计
├── feature-transfer     # 文件任务与剪贴板 UI
└── feature-settings     # 设置、权限、诊断和关于页
```

模块边界以职责和依赖方向为依据，不为每个页面创建一个模块。接口只为真实的平台替换点和测试替身存在，不引入 service locator、通用事件总线或任意类型消息袋。

### 5.1 Kotlin 层

- Android 自有新代码统一使用 Kotlin。
- UI 使用 Jetpack Compose、Navigation、ViewModel、Coroutine 和 `StateFlow`。
- 每个功能暴露不可变 `UiState` 和类型化 `Action`；业务决策留在 domain workflow，不写入 Composable。
- 依赖由 `app` 组合根显式构造。首期使用简单的构造器注入，不为了框架本身引入复杂 DI。
- 数据库使用 Room，普通设置使用 DataStore。
- Java protobuf 生成、GreenDAO、Data Binding、旧 View Binding 和 Fragment Navigation 全部移除。

### 5.2 会话所有权

```text
Compose UI
    │ command / StateFlow
    ▼
RemoteSessionService
    │
    ▼
RemoteSessionWorkflow
    │ typed JNI commands/events
    ▼
PixelsNativeSession
    │
    ▼
px_client_sdk / protocol / media
```

- 活跃远控会话由前台 `RemoteSessionService` 持有，不能由 Activity、Composable 或 Surface 生命周期直接拥有。
- UI 重建、横竖屏切换和短暂退后台不销毁会话。
- `Start`、`Reconnect`、`SuspendVideo`、`Stop` 和 `Destroy` 是显式状态迁移；重复停止和部分启动失败必须安全。
- 网络、渲染、音频、输入、文件和语音各有明确所有者，按依赖构造的逆序停止。
- 后台运行、通知和麦克风使用的 service type 与权限在实现时按目标 SDK 做版本门禁，不能依靠宽泛权限规避系统限制。

## 6. C++ 与 JNI 设计

### 6.1 JNI 边界

现有全局 `g_app`、`g_java_app`、导出函数名绑定 Java package 以及 JSON 消息回调全部删除。

新 JNI adapter 必须满足：

- 使用 `RegisterNatives` 注册小型、稳定、类型化的 JNI 表面。
- Java/Kotlin 对象引用由 RAII `GlobalRef`/`WeakGlobalRef` 封装。
- JavaVM、JNIEnv、Surface 等 ABI 指针只在边界瞬时使用，不存入普通项目对象或异步捕获。
- Kotlin 侧持有不包含地址语义的 `SessionId`；原生侧通过受控、类型化 session registry 获取 `shared_ptr`。
- registry 仅是 JNI 适配器的会话所有权边界，不能演变为任意服务查询或全局业务单例。
- 回调进入 Kotlin 前复制为拥有所有权的值；回调目标失效时立即返回。
- 不用 JSON 承载帧尺寸、光标、连接状态、统计等内部事件。

所有项目自有 C++ 遵守 `docs/cpp_smart_pointer_standard.md`：新代码零裸指针，异步回调使用 `weak_ptr`，资源使用 RAII，所有值确定性初始化。libwebrtc 自身的借用 ABI 只允许留在专用 adapter 内。

### 6.2 原生核心

`pixels_android_core` 负责：

- `px_client_sdk` 会话创建和传输选择。
- protobuf 协议编解码。
- 视频帧分发、MediaCodec adapter 和软件解码回退。
- Oboe 音频输出及语音音频端点。
- 鼠标、键盘、手柄和显示器命令。
- 文件传输、剪贴板、录制、语音和虚拟显示协议核心。
- 统计、错误分类和可取消的关闭流程。

Windows Client 的剪贴板、文件传输和录制模块不能直接携带 Qt/Win32 UI 进入 Android。应抽取平台无关的协议/任务核心，并分别注入 Windows 与 Android platform adapter。

### 6.3 传输能力

最终产品支持：

- WebSocket 控制与媒体。
- UDP Direct 媒体及可靠控制面回退。
- Relay。
- WebRTC Direct 和标准 ICE/TURN WebRTC。
- 网络变化后的恢复、能力协商和可观测降级。

Android 的 WebRTC 实现使用固定版本的第三方预编译 AAR，并由 Kotlin platform adapter 对接现有 SDP、ICE、媒体轨道和数据通道协议；
不尝试把仅有 Windows x64 预编译库的 `px_webrtc_client` 链入 Android。依赖必须固定版本并归档许可证、校验值和 native symbols，
不能使用动态版本。当前选定基线为 `io.github.webrtc-sdk:android:150.7871.01`。

旧 `rtc_client_stub.cpp` 已随 M0 删除，当前 Android 应用不打包 native transport，也不暴露伪造的 RTC 能力。M1 必须在新的 `core-native` 模块中建立真实 transport capability 和 typed JNI 边界；正式发布门禁要求 RTC adapter 实现并通过真机网络测试。

## 7. 渲染、音频和输入

### 7.1 视频

- Compose 通过受控的 Android View/Surface adapter 承载原生视频输出。
- MediaCodec 输出 Surface 为主路径，避免不必要的 CPU 拷贝。
- 软件解码和 GLES 渲染是明确回退策略，不与 UI 生命周期耦合。
- Surface 被替换或销毁时只暂停/重绑视频输出，不误关整个网络会话。
- 支持视频尺寸变化、旋转、宽高比、刘海/挖孔安全区域和外接显示器。

### 7.2 音频

- 串流播放与语音通话使用职责分离的音频端点。
- 正确处理 AudioFocus、静音、蓝牙/有线耳机切换、采样率变化和设备热插拔。
- 音频资源在重复 start/stop、权限拒绝和部分初始化失败后均可安全释放。

### 7.3 输入

- 统一的 `InputCommand` 模型覆盖触摸、鼠标、键盘和手柄。
- Android key code、axis 和 motion event 只存在于 platform adapter，协议层使用项目自己的类型。
- 支持直接触摸与触摸板模式、相对移动、滚轮、长按、右键和多指手势。
- 虚拟手柄布局可保存，但不沿用旧 `ControlLayer` 和 XML 控件实现。

## 8. 安全、权限和发布配置

- 删除 Gradle 中硬编码的 keystore 密码。签名路径和凭据只从本机未跟踪配置或 CI secret 注入。
- 最终 Manifest 只声明真实使用的权限；删除旧外部存储和 TV EPG 权限。
- Console、公网域名和公网地址强制使用受验证的 TLS/WSS/DTLS。根据产品决定，正式版允许私网和 CGNAT 地址使用现有
  Panel/Render HTTP/WS 直连；所有入口必须在解析、重定向和实际连接前重复校验地址范围，并在界面中说明同网段窃听与篡改风险。
- Android Manifest 因私网直连需要允许 cleartext，但应用只能通过统一网络工厂创建明文请求；该工厂必须拒绝公网、DNS
  重绑定和从私网重定向到公网。不得在其他模块直接创建明文客户端。
- 删除宽松 SSL socket、跳过证书验证和未认证的远程控制入口。
- 文件访问全部通过应用私有目录、MediaStore 或 SAF 授权 URI。
- 日志、崩溃信息和诊断页面不得记录密码、token、完整剪贴板内容或用户文件内容。
- Release 开启代码压缩与资源收缩，并验证 native symbol、mapping 和崩溃符号归档流程。

## 9. 删除清单

下列旧实现不进入最终产品：

- `ui` 下的 Fragment、Adapter、Decoration、旧 Dialog 和 XML 导航。
- `events` 自定义事件类。
- `db` GreenDAO 代码及生成文件。
- `effects` 音乐频谱功能。
- 旧 `steam` 页面、广播接收和固定入口。
- `games`、`ui/processes` 的旧 Activity 流程。
- `ThunderApp.java`、`ThunderCallbacks`、JSON native message maker。
- `MainActivity` 手工 Fragment show/hide 实现。
- `App.java`、`AppContext` 可变全局状态。
- Java 侧重复网络栈、宽松 SSL 和零散 HTTP 工具。
- 旧 `ControlLayer`、XML 虚拟手柄和旧 Java OpenGL 包装器。
- 无消费者的图片、布局、字符串、旧截图和主题资源。
- GreenDAO、旧 Compose accompanist pager、Data Binding、Fragment Navigation 等不再使用的依赖。
- `official` product flavor、旧 APK 复制 hook 和 `gammaray_*.apk` 命名。
- Android 模板测试和临时 RTC stub。

删除顺序服从新垂直切片的落地顺序；判断依据是最终依赖图和测试，不以旧代码是否仍能单独编译作为保留理由。

## 10. 实施里程碑

### M0：最终工程基线与品牌，3–5 个工作日

状态：**已完成（2026-09-05）**。

- 为待删除旧实现建立只读 Git tag，供历史追溯，不形成运行时兼容。
- 设置 `PixelsAndroid`、`yun.pixels.client`、Pixels 主题和最终资源命名。
- 新建模块结构、版本目录和统一构建约定。
- 移除 product flavor、GreenDAO、Data Binding、旧签名配置和无关依赖。
- 完成新图标、Splash Screen、空的 Compose 导航壳。

验收：`debug` 清洁构建、清洁安装和启动通过，安装包内不再出现旧产品名。

### M1：设备发现与原生会话骨架，1–2 周

状态：**进行中（2026-09-05 已完成手动主机/`link://` 解析、Panel 信息验证、设备持久化与真机闭环）**。

- 完成设备领域模型、Room/DataStore 和设备首页。
- 完成扫码、手动添加和局域网发现。
- 建立 `RemoteSessionService` 与显式会话状态机。
- 建立 typed JNI adapter 和 `pixels_android_core`。
- 接通基础鉴权、WebSocket 会话和错误分类。
- 删除旧首页、GreenDAO、Event 类和 Java 网络栈。

验收：能够从新 UI 发现、添加、连接和删除设备；旋转、前后台及重复连接不会泄漏会话。

### M2：音视频垂直切片，2–3 周

- 接通 MediaCodec、软件解码回退、Surface 重绑和 Oboe 播放。
- 完成远程工作区、画面适配、音频控制和基础统计。
- 完成断线重连、远端结束和本地退出。
- 删除旧 `ThunderApp`、JSON 回调、旧 FrameRender Activity 和旧渲染包装器。

验收：真机连续串流一小时；切换前后台、旋转和重建 Surface 后会话状态正确，无持续内存增长和音画失步。

### M3：完整输入与多显示器，1–2 周

- 完成触摸、鼠标、键盘、快捷键和虚拟手柄。
- 完成 USB/蓝牙手柄、震动和配置保存。
- 完成多显示器切换、虚拟显示管理和远程应用列表。
- 删除旧 ControlLayer、Steam/Game Activity 和 XML 手柄资源。

验收：桌面操作、游戏控制、显示器切换和应用启动均可在真机完成。

### M4：客户端内置能力，3–4 周

- 文件传输和任务中心。
- 文本、图片和文件剪贴板的平台适配。
- 本地音视频录制与 MediaStore 发布。
- 语音通话和音频路由。
- 模块事件、通知、取消和诊断。

验收：模块覆盖成功、拒绝权限、断线、取消、重连、回调内停止和销毁后排队回调。

### M5：完整网络与质量收口，2–3 周

- UDP Direct、Relay、WebRTC Direct、ICE/TURN WebRTC。
- 传输选择、协商、失败降级、网络切换恢复。
- Wi-Fi/蜂窝、弱网、锁屏、来电、耳机切换和系统资源压力测试。
- 性能、功耗、温度、包体和启动速度优化。

验收：所有宣称传输在目标网络矩阵上有真机证据；能力不可用时明确失败，不出现黑屏式无限重试。

### M6：发布，1–2 周

- 完成隐私、许可、签名、混淆、符号和发布元数据。
- 执行 API 29 基线、主流系统版本、手机/平板/折叠屏设备矩阵。
- 生成最终 release APK/AAB，记录 SHA-256、版本和真机安装结果。
- 删除最后的旧源码、资源、依赖、兼容注释和临时构建开关。

验收：满足本文 Definition of Done 后，Pixels Android 才能替代旧 Android 应用。

## 11. 测试与工程门禁

### 11.1 自动化

- Kotlin domain workflow 单元测试。
- Room repository、设置和错误映射测试。
- Compose 导航、关键页面和无障碍测试。
- C++ 协议、会话、输入、文件、剪贴板、录制和语音单元测试。
- JNI 注册、事件编码、引用释放和线程附着测试。
- Android instrumented lifecycle、Surface、Service 和权限测试。

### 11.2 必测生命周期

- 初始化在每个失败点中断后销毁。
- queued callback 到达前 owner 已销毁。
- dispatch 中注销 listener。
- callback 内触发 stop。
- 重复 start/stop 和并发 post/stop。
- Activity 重建、Surface 重建、Service 停止和系统回收。
- 文件、录制和语音进行中断网或撤销权限。

### 11.3 构建约定

- 日常 Android 构建使用 Gradle 的聚焦任务和 `build_cpp_android_*.bat` C++ 入口。
- 不为 Android 日常验证运行仓库的 Windows release-only `build_official.bat`。
- 每个里程碑至少完成一次 arm64 debug 清洁构建、USB 真机覆盖安装、启动和日志检查；日常验证不得主动卸载应用。
- 发布候选必须记录 APK/AAB SHA-256、签名证书、native ABI、version code/name 和测试设备信息。

## 12. Definition of Done

只有同时满足以下条件，Android 重建才算完成：

- 应用、包名、图标、主题、通知和资源全部是 Pixels 品牌。
- 仓库中不存在旧 Android UI、GreenDAO、JSON JNI、临时 RTC stub 或双架构入口。
- 设备发现、连接、音视频、输入、多显示器、远程应用、文件、剪贴板、录制、语音和设置形成完整产品流程。
- WebSocket、UDP Direct、Relay、WebRTC Direct 和标准 WebRTC 能力均有明确协商、错误和降级行为。
- 所有 C++ 改动通过智能指针、初始化、格式和生命周期门禁。
- Android 权限、后台服务、存储、音频、TLS 和签名符合最终生产配置。
- 自动化测试通过，关键生命周期和弱网场景有真机证据。
- release 包清洁安装、冷启动、长时间运行和卸载均通过。
- 文档、构建脚本和实际输出一致，不再引用旧 GammaRay Android 使用流程。

## 13. 当前实施基线

截至 2026-09-05，M0 已形成可构建、可安装、可冷启动的新产品基线，M1 已开始交付真实设备接入切片：

- Gradle 9.3.1、AGP 9.1.1、AGP 内置 Kotlin 2.4.10、Compose BOM 2026.08.00。
- `compileSdk`/`targetSdk` 为 API 37，最低系统为 API 29；正式 native 能力仍只规划 `arm64-v8a`。
- 根工程、包名、显示名、主题、Adaptive/monochrome 图标和 Splash Screen 均已切换到 Pixels。
- 已建立 `app`、`core-domain`、`core-data`、`feature-devices`，设备首页、Quick Connect、空状态和一级导航使用不可变状态与类型化 Action。
- 旧 Fragment/XML UI、GreenDAO、事件类、Steam/频谱/演示页面、宽松网络工具、旧签名文件和旧 Android JNI/C++ 入口已删除。
- Android App 当前不打包项目 native 会话库，不加载旧 JNI，也不存在 RTC stub。M1 将从 `core-native` 的 typed JNI 和显式 capability 开始。
- `core-domain` 单元测试、arm64 产品基线 debug 构建和 Android lint 已通过。删除未加载旧 native 库后的最终 APK 已在同一真机完成覆盖安装、冷启动、视觉和崩溃日志检查。AndroidTest APK 已成功编译，但 MIUI 拒绝安装独立测试包，因此品牌与启动验收使用真机手工检查完成。
- M1 的 Quick Connect 已支持私有网络主机、可选 Panel 端口和 `link://`；客户端真实请求 `/v1/simple/info`，拒绝公网直连和异常协议响应，成功后通过 DataStore 保存设备元数据，并使用 Android Keystore AES-GCM 加密设备临时凭据。
- 设备接入已完成真机不可达错误态、成功添加、进程重启持久化、重启后保守离线、应用内删除及凭据非明文检查。Android 17 / API 37 的 `ACCESS_LOCAL_NETWORK` 运行时权限已接入。

扫码、主动局域网发现、会话鉴权、typed JNI 和远程画面仍未完成；对应动作继续明确提示正在建设，不伪造连接或远程会话成功。M1 完整验收前不能宣称远程会话已经可用。
