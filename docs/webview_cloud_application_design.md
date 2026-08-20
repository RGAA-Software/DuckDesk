# WebView 云应用设计与验收

> 状态：已完成架构讨论，尚未实施代码。  
> 目标：在现有 px_render 插件宿主中增加 CMS 可调度的 webview 云应用类型。

## 1. 已确认的产品边界

- CMS 增加 webview，与 game-hook 并列。
- CMS 保存网页入口 URL；启动实例时将 URL 以 UTF-8 Base64URL 传给宿主。
- 不新增 WebView 配置文件。
- px_render 是插件宿主；WebView 是新的采集源插件，复用既有编码、流化和客户端协议。
- 首先使用官方 Windows x64 CEF 标准 binary distribution 调通。
- 不代理控制端客户端的摄像头、麦克风或本地文件系统。
- 不保证官方标准 CEF 播放 H.264/AAC 的 MP4、HLS。
- WebView 输入不经 Windows SendInput，也不经 Game Hook IPC。

Base64URL 仅解决命令行传递中的 Unicode、空格、问号、与号等转义问题，不是加密。完整解码 URL 不得写入 Service、宿主或 CEF 日志。

## 2. 组件和进程模型

~~~text
CMS Web / CMS Server
  └─ Application { app_type=webview, entry_url }
       │ StartAppInstance(app_mode=webview, webview_url_b64=...)
       ▼
px_service
  └─ 启动、跟踪、停止一个 px_render 实例
       ▼
px_render（Browser/主进程，插件宿主）
  ├─ WebViewSourcePlugin
  │    ├─ CEF 无窗口 Browser
  │    ├─ GPU/CPU 画面源
  │    ├─ CEF 音频源
  │    └─ CEF 输入适配器
  ├─ 既有编码插件
  └─ 既有 RTC / UDP / RTMP 插件
       ├─ px_render --type=renderer
       ├─ px_render --type=gpu-process
       └─ px_render --type=utility / 其他 CEF 子进程
~~~

CEF renderer、GPU、utility 等进程不是 CMS 调度的额外应用实例。Service 只管理 Browser/主进程及其 Job Object（或等价进程组），异常时回收整个后代进程树。

## 3. CMS、协议和 Service 流程

### 3.1 CMS 数据模型

| 字段 | 含义 |
|---|---|
| app_type | game-hook 或 webview |
| entry_url | WebView 原始入口 URL，仅 webview 必填 |

选择 WebView 后，CMS 表单显示入口 URL，隐藏游戏路径、游戏 EXE 相对路径和游戏参数。URL 在数据库保存原文，便于编辑和审计。

### 3.2 CMS-Service 协议

在 src/px_deps/px_server_protocol/cms_service.proto 的启动消息末尾追加字段，不能修改既有 tag：

~~~proto
string app_mode = 13;          // "game-hook" | "webview"
string webview_url_b64 = 14;  // UTF-8 Base64URL；webview 时必填
~~~

CMS Server 编码后下发；px_service 原样转交。启动参数为：

~~~text
px_render.exe --app_mode=webview --webview_url_b64=<Base64URL>
              --network_listen_port=<port> --encoder_fps=<fps>
              --encoder_bitrate=<bitrate> --encoder_format=<format>
~~~

### 3.3 Service 行为

WebView 启动分支必须：

1. 校验 app_mode=webview 和非空 webview_url_b64。
2. 不解析游戏路径、不检查游戏 EXE、不等待游戏窗口、不注入 DLL。
3. 记录实例 ID、端口、根 PID、模式与参数摘要。
4. 为实例建立 Job Object；正常退出优雅关闭，异常或超时关闭 Job。
5. 仅在“CEF Browser 已创建且已有可编码首帧”后，将实例设为 Ready。

按进程恢复或停止实例时必须匹配 app_mode=webview、实例端口，并排除所有带 --type= 的 CEF 子进程；不能只按进程名匹配。

## 4. CEF 接入与部署

### 4.1 子进程入口

CEF 使用当前 px_render.exe 作为默认 subprocess。宿主 main() 最早位置必须处理 CEF 子进程：

~~~cpp
int cef_child_exit_code = CefExecuteProcess(main_args, cef_app, nullptr);
if (cef_child_exit_code >= 0) {
    return cef_child_exit_code;
}
return PxRenderPluginHostMain(argc, argv);
~~~

这段属于 px_render 核心，不属于插件。它必须早于 gflags、单实例锁、端口监听和 PluginManager。CEF 子进程不得进入业务宿主逻辑。

若现有入口无法安全做到这一点，才增加极小 CEF subprocess helper。该 helper 不是 CMS 应用，不加载插件、不编码、不监听端口。

### 4.2 官方 CEF runtime

使用 [CEF 官方下载页](https://cef-builds.spotifycdn.com/index.html) 的 Windows x64 标准包，固定版本、架构和 MSVC runtime。CEF 不是单个 libcef.dll；必须完整部署版本匹配的 DLL、pak、locales、icudtl.dat、V8 snapshot、ANGLE/Vulkan 依赖等文件。

建议将其安装到 third_party/cef/<version>/ 并由构建/安装脚本复制。禁止从 Chrome 安装目录拆 DLL，禁止随意使用未知来源的“带 H264 CEF”包。

官方标准包以 WebM、WebRTC、VP8/VP9/Opus 为媒体基线。若将来必须播放 H.264/AAC，需要独立决策并自建带 proprietary_codecs=true、ffmpeg_branding=Chrome 的 CEF；不改变本文的插件接口。

### 4.3 实例隔离

每个实例使用独立 CEF profile：

~~~text
<runtime-root>/webview-profile/<instance-id>/
~~~

实例间不得共享 Cookie、IndexedDB、Service Worker 或 profile lock。默认是临时会话，停止后按保留策略清理。

## 5. WebViewSourcePlugin

### 5.1 插件职责

插件负责 URL 解码和校验、CEF 初始化和关闭、OSR 画面、网页音频、CEF 输入、导航/权限/光标、状态回报。

插件不负责编码、WebRTC/UDP/RTMP 协议、CMS 调度、Windows 全局输入注入或游戏进程管理。

### 5.2 Browser 创建和线程

Browser 使用无窗口 OSR：

~~~text
windowless_rendering_enabled = true
shared_texture_enabled        = true
browser_subprocess_path       = 当前 px_render 可执行文件
user_data_path                = 实例独立 profile
~~~

Browser 创建、关闭和全部 CefBrowserHost::Send*Event 调用只能在 CEF UI 线程执行。网络回调、编码线程和宿主线程必须以 CefPostTask(TID_UI, ...) 投递，不能直接调用 BrowserHost。

### 5.3 导航和权限

- 入口只允许 https 和经明确允许的内部 http。
- 默认允许入口页面及同 origin 导航。
- 拦截 file、data、未知 scheme 与无界 popup。
- 不全局忽略证书错误；生产环境关闭远程 DevTools。
- 摄像头、麦克风、文件选择、下载等权限默认拒绝。
- 如有 JS Native Bridge，必须校验调用 frame 的 origin。

## 6. 画面、编码、音频

### 6.1 视频链路

~~~text
CEF compositor
  → OnAcceleratedPaint(D3D11 shared texture)
  → WebViewSourcePlugin
  → 既有 CaptureVideoFrame / 编码插件
  → 既有 RTC / UDP / RTMP 插件
  → 客户端
~~~

CEF、共享纹理桥接和编码器应使用同一 D3D11 adapter。跨 adapter 时不能宣称零拷贝，必须明确 GPU copy、CPU fallback 或不兼容错误。保留 OnPaint 作为受控 CPU fallback；GPU 初始化失败、共享纹理失败、设备移除不得静默黑屏。

帧队列必须有上限。编码落后时丢弃过期画面，只保留最新帧，不能无限积压纹理和任务。

CEF ViewRect 是唯一逻辑画面尺寸。分辨率变化时先调整 ViewRect 并调用 WasResized，再让采集和编码使用新尺寸；不得由桌面显示器或 HWND 推导尺寸。

### 6.2 音频链路

~~~text
CEF OnAudioStreamStarted / OnAudioStreamPacket
  → WebViewSourcePlugin PCM 队列
  → 既有音频处理/编码
  → 既有 Stream Plugin
  → 客户端
~~~

插件处理采样率、声道数和帧时长，必要时重采样。CEF 音频 callback 不得阻塞在网络或编码操作上；队列满时按明确策略丢弃并计数。

## 7. 完整控制与事件回放

### 7.1 输入终点

~~~text
控制端输入
  → PluginNetEventRouter（控制权校验）
      ├─ desktop     → EventReplayerPlugin → SendInput
      ├─ game-hook   → Hook IPC
      └─ webview     → WebViewSourcePlugin → CefBrowserHost
~~~

不能继续使用 IsGlobalReplayMode() 的二元语义描述三种行为。设置和路由层应使用显式 InputTarget：

~~~cpp
enum class InputTarget {
    kSystemSendInput,
    kGameHookIpc,
    kCefBrowser,
};
~~~

每一输入事件只能到达一个终点；WebView 不得同时触发 EventReplayerPlugin。

### 7.2 鼠标

不采用 streamer 的 0..65535、屏幕/应用矩形或 MOUSEEVENTF 语义。现有 GammaRay 输入的 x_ratio/y_ratio、pressed/released、button、delta_x/delta_y 是 WebView 的唯一协议语义：

~~~text
x = clamp(x_ratio, 0, 1) * current_cef_view_width
y = clamp(y_ratio, 0, 1) * current_cef_view_height
~~~

插件负责映射移动、左/右/中/X 键、双击、横纵滚轮和 Pointer Lock 相对位移。双击 click count 在插件内按时间、位置和按钮维护。

客户端若显示 letterbox，必须先把本地点击位置换算为视频内容区的 ratio；服务端不猜测客户端的显示区域。

### 7.3 键盘、文字和输入法

物理键盘要保留 virtual key、scan code、扩展键、modifier、按下/抬起。插件构造 CefKeyEvent：

- 按下为 KEYEVENT_RAWKEYDOWN，抬起为 KEYEVENT_KEYUP。
- native_key_code 使用 Windows scan code，保证 DOM KeyboardEvent.code（如 KeyW）正确。
- Alt/F10 设置 is_system_key。
- 文本输入使用独立 UTF-8 提交消息，映射为一个或多个 KEYEVENT_CHAR。

现有 KeyEvent 只有 virtual key、down 和 lock 状态，不足以完整表达文本和 IME。协议必须追加或新建文本输入消息；中文、Emoji、粘贴文本不能长期做成“中文特殊分支”。

### 7.4 触控、焦点和断连释放

触控消息包含触点 ID、ratio、phase 和多指列表，插件映射到 SendTouchEvent；触点结束必须发 CEF release，不能误发 cancel。

插件维护每个控制会话的已按键、鼠标键和触点。控制端断开、控制权切换、页面重载、CEF renderer 崩溃、Browser 关闭或实例停止时，均在 CEF UI 线程补发全部 keyup/mouseup/touch release，并清空状态。

控制权授予或有效键盘输入时 SetFocus(true)；撤销时 SetFocus(false)。观察者输入在 PluginNetEventRouter 拒绝。

### 7.5 光标和剪贴板

CEF OnCursorChange 映射到客户端光标消息。标准光标用显式枚举映射；自定义光标传 BGRA 位图、尺寸和 hotspot，不能依赖两个枚举整数恰好相同。

剪贴板为浏览器会话内虚拟剪贴板，不读写宿主全局系统剪贴板。Ctrl+C/X/V 与文本选择通过 CEF renderer/browser 通信实现，受既有剪贴板方向策略控制；文件剪贴板不支持。

## 8. 状态、错误与观测

实例状态为：Starting、BrowserReady、StreamingReady、Running、Stopping、Stopped、Failed。

必须记录但不泄漏完整 URL：CEF 版本、实例 ID、根 PID、CEF 子进程数、GPU adapter、CEF 初始化/首帧/编码首帧耗时、GPU/CPU fallback、共享纹理失败、设备移除、采集 FPS、编码 FPS、丢帧、输入队列长度、输入时延、Browser/renderer/GPU 崩溃和停止方式。

页面加载失败、CEF 初始化失败、GPU 不可用、Browser 关闭或子进程崩溃都上报实例状态机。恢复可以重建 Browser 或由 Service 重启实例，但必须有次数上限和退避，不能无限快速重启。

## 9. 预计改动范围

| 模块 | 主要改动 |
|---|---|
| rust_server/px_cms_server | 应用类型、URL 存储、校验、启动下发 |
| web/px_cms | WebView URL 表单和实例状态展示 |
| cms_service.proto 与生成协议 | 追加 app_mode、webview_url_b64 |
| rust_client/px_service | WebView launch spec、实例跟踪、Job Object、停止 |
| src/px_render/rd_main.cpp | 最早的 CEF child dispatch |
| src/px_render/settings | ApplicationMode::WebView 与 InputTarget |
| src/px_render/rd_app.cpp | WebView 模式选择插件，跳过游戏管理 |
| plugin_net_event_router | WebView 独占输入路由、控制权和断连通知 |
| plugins/webview_source | 新插件：CEF、视频、音频、输入、导航、光标、状态 |
| 构建/安装脚本 | CEF headers/libs/runtime 锁定、复制与完整性校验 |

## 10. 测试和验收

### 10.1 受控测试页面

提供内部 webview-e2e 页面，不依赖 H.264/AAC。页面包含 Canvas/WebGL 动画、帧计数、文本/中文输入框、滚动区、拖拽/双击区、触控区和不同光标，并将收到的 DOM 事件回报给测试服务。

验收依据是页面实际收到的 DOM 事件、客户端实际可播放的流和实际的进程回收；不能只检查插件日志或截图。

### 10.2 测试层级

| 层级 | 必测内容 |
|---|---|
| 单元 | Base64URL、URL scheme、ratio 坐标、按键映射、click count、输入释放、队列背压 |
| CEF 集成 | OSR Browser、测试页加载、GPU/CPU 画面、音频 callback、导航/权限、CEF UI 线程 |
| Service 集成 | 不要求游戏 EXE、排除 --type、停止/异常回收、profile lock 释放 |
| 端到端 | CMS 启动→客户端接流→输入→页面 DOM 回报→CMS 停止→进程和端口回收 |

### 10.3 必须通过的端到端场景

- 含中文、空格、问号、与号的 URL 正确加载，日志不泄漏原 URL。
- Canvas/WebGL 动画在客户端持续变化，新客户端连接可收到可解码关键帧。
- 四角、中心、随机鼠标坐标在 DOM 的误差不超过 1 个 CEF view 像素。
- 点击、拖拽、双击、横纵滚轮、X 键、触控和运行中分辨率变更。
- 字母、数字、方向/F 键、Ctrl/Shift/Alt、中文、Emoji、粘贴文本。
- 观察者输入被拒绝；控制权切换和异常断连后无残留按键、鼠标键或触点。
- 文本、链接、等待、自定义光标正确回传。
- reload、页面加载失败、CEF renderer/GPU 子进程异常后恢复或明确 Failed。
- CMS 停止、Service 重启、根进程异常后三类 CEF 子进程和端口无残留。
- 摄像头、麦克风、文件选择、文件下载均按范围明确拒绝。

### 10.4 性能和回归产物

在持续 WebGL 动画下记录首个可播放帧、采集/编码 FPS、输入到 DOM 回报 p50/p95、CPU/GPU/内存、进程数和长时间运行趋势。门槛按部署 GPU、目标分辨率和目标 FPS 固化为验收环境基线，不能只看平均值。

每次验收保存宿主构建号、CEF 版本、GPU 型号、测试页版本、DOM 事件记录、流统计、CEF 日志、进程快照及失败截图/视频。CEF、编码器或输入协议升级必须复跑。

## 11. 对 streamer 的取舍

D:\\dolit\\streamer 可借鉴：控制权校验、WebView 独立输入终点、CEF UI 线程、Windows native key code、CEF 光标回传和 OSR shared texture。

不得照搬：其桌面/窗口坐标预处理、0..65535 协议、MOUSEEVENTF 语义或 Windows 全局回放。GammaRay 已有 ratio 输入协议更适合无窗口 CEF ViewRect。其 WebGL 输入还存在断连释放、触控结束语义、普通文本输入链路不统一等风险；本设计要求在 WebView 插件内补齐。
