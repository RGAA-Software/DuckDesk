# StarIsland：Game 中文输入与多 API Hook 实施计划

## 目标与基线

用户于 2026-09-10 指定先提交枚举修复，再继续 Game 中文输入；输入完成后探索 Vulkan/OpenGL 画面 Hook。

- 枚举修复 `70f0ad2f6` 已推送 `origin/master`，未包含已有 Rust 工作区改动。
- 中文输入基线：`e40329672` 协议/工作流、`8048a83e4` Qt/Web 与 Game 后端、`ceecb52dc` WebView 实机修复。
- 设计约束沿用 `application_text_input_design_20260909.md`、`game_text_input_backend_20260909.md`。
- 唯一指定测试游戏：`D:/1_test_games/test_input/dist/StarIsland/StarIsland.exe`。
  本机 `--help` 确认为 Godot `4.6.2.stable.official.71f334935`；项目配置默认 `forward_plus` + `d3d12`。
  起始场景为名字输入页，使用 Godot `LineEdit`，随后进入可移动的 3D 场景。
- OBS 只读参考：`D:/source/obs-studio`，revision `88de106cff4bcdf2a511e2e3801ffa3de729f6bd`。
- 当前 Game 后端已有最终 UTF-8 文本 → 授权 Hook IPC → 目标窗口 `WM_CHAR`；此前 Win32 测试窗口通过不等于 Godot 通过。

## 顺序与完成门禁

| 阶段 | 开发/调查内容 | 完成证据 |
| --- | --- | --- |
| A. 本机基线 | 检查 Console/Service 配置、运行产物、游戏版本；建立专用测试应用/实例，复用真实产品启动链。 | 游戏为本次私有 Job 成员且完整路径匹配；D3D12 远端画面可见。 |
| B. Game 文字闭环 | Qt 和桌面 Web 分别打开输入面板，先验证最终 Unicode，再验证真实系统拼音；依据失败修正焦点、窗口代次、投递或面板仲裁。 | 游戏实际显示中文/emoji；追加、替换选区、连续提交正确，不依赖剪贴板，不自动 Enter。 |
| C. 控制及负向回归 | 编辑屏障释放已按键；拼音 W/Space/Enter/Ctrl 不泄漏；关闭后英文/游戏控制恢复；失焦、退出、重复开关、目标变更和断线保留正确语义。 | Qt/Web 均有明确通过/失败/未覆盖记录；不把 submitted 或模拟 composition 当作实机结果。 |
| D. Vulkan 探索 | 先确认 Godot 真正使用 Vulkan；对照 OBS Layer 协商、实例/设备/交换链、Present、共享纹理与同步；核查本项目是否只编译了源码而未接入启动链。 | 给出定位到源文件的差异、最小改动及短测结果。若需激活 Layer，仅为被授权启动的子进程构建环境，不修改系统全局注册。 |
| E. OpenGL 探索 | 用 compatibility + opengl3 启动同一游戏；检查 SwapBuffers/WGL Hook 触发、共享纹理或 CPU 回读路径、纵向翻转及上下文重建。 | 区分“Hook 安装成功”“收到纹理”“客户端画面正确”；记录 GPU/互操作限制，不静默回退桌面采集。 |
| F. 交付 | 增量构建、自动化测试、短批次实机检查、dist 发布、哈希校验、文档更新。 | Windows 相关目标可编译运行；若触及共享 SDK/协议则补 Android 编译与 Web 检查；未覆盖项明确列出。 |

B/C 中文提交主流程验证后，D/E 以独立短批次执行；修饰键回归继续单独收尾，不把采集结果充当输入通过证据。
D/E 是探索与可行的最小集成，不预先承诺通用 Vulkan/OpenGL、所有显卡或所有 Godot 游戏兼容。

## 渲染启动矩阵

| 场景 | 参数 | 验证方法 |
| --- | --- | --- |
| D3D12 基线 | 默认；必要时显式 `--rendering-method forward_plus --rendering-driver d3d12` | 引擎启动日志及实际加载 API/采集日志。 |
| Vulkan | `--rendering-method forward_plus --rendering-driver vulkan` | 实际驱动日志、Layer/交换链与采集证据，不能只看参数。 |
| OpenGL | `--rendering-method gl_compatibility --rendering-driver opengl3` | 实际驱动日志、WGL/SwapBuffers、图像方向与尺寸。 |

如引擎自行回退，标记原 API 未通过，不把回退结果计入该 API。测试不修改或重新导出外部游戏项目。

## 安全、实现与测试边界

- 每个交互批次最多 **5 分钟**，覆盖旧文档的 10 分钟上限；编译不算压力测试。
- 只操作本次创建的测试实例；不接管同名外部进程，不注销 Windows 用户，不重启机器或停止无关应用。
- Qt 使用既有认证 WS/WSS；Web 使用既有可靠有序 media DataChannel；不增加文本连接，不走全局输入/剪贴板回退。
- 进程准入保持 Job 成员 AND 规范化完整路径；输入目标失效时拒绝，保留不确定性，不自动重发全文。
- C++ 使用 typed RAII/智能指针和显式初始化；排队、销毁、撤权、重复启动停止都需要回归。
- 删除/替换旧分支前完整归档至新的 `backup/` 批次；OBS 与游戏工程均为只读参考。
- 只使用 `scripts_build/build_cpp_*.bat` 增量目标，不运行 release 全量构建。
- 改动 exe/DLL/资源必须同步 `build_official/dist` 并逐项 SHA-256 校验；测试配置及凭证不提交。
- 中文输入范围是 Qt Windows 与桌面 Web；本轮不把 Android/iOS 实机或 RDP/WebView 扩展验收纳入完成承诺。

## 状态

- 枚举修复：已提交、push。
- 计划与本机/参考基线核查：已完成。
- A 已通过：本机专用应用 `app-1-d2bf3f53`、私有 Job 启动的 StarIsland，D3D12 实际画面经 Native UDP 客户端可见。
- B/C：已完成本次范围验收，Qt/Web 中文闭环、独立远程快速 Ctrl+A、控制恢复、草稿保护及自动化负向回归通过；覆盖边界见末节。
- D/E：已执行 Vulkan/OpenGL 各 95 秒的产品链路短测；探索结论见下文，不等于两种 API 原生采集全部接通。

### 2026-09-10 Qt 实测与修复

- 修复能力查询首包丢失：`inst-17-fee505f6` 的客户端在 02:25:55.186 将查询入队，服务端到 02:25:56.331 才建立授权路由。
  旧逻辑只查询一次，入口永久禁用。新增 `ApplicationTextQuery`：只读查询最多 3 次，3 秒等待结果，明确不支持则停止；
  支持后才轮询目标，重连重置。全文提交和输入屏障保持不自动重发。
- `inst-20-291ddc87`（单批 180 秒）：Qt 输入面板成功打开；中文提交、真实系统拼音 `nihao` 候选与空格选词、
  追加“你好”、远端 Ctrl+A 后替换为“星落岛”、追加 U+1F642、关闭后点击进入游戏和恢复 W/Space 均观察到实际画面结果。
  游戏 HUD 显示“旅者 星落岛🙂”。拼音预编辑时远端没有出现拼音，提交不会额外 Enter。
- 最初 emoji 方框来自验收脚本将非 BMP 字符截为单个 WORD；脚本改为完整 UTF-16 代理对后，Qt 编辑框与游戏均显示正确。
  该失败不是产品编码修复，也不能将错误脚本结果记为 emoji 通过。
- IPC 生命周期测试的两个二进制消息 fixture 未设置 WebSocket binary 模式，随 PID 字节内容偶发失败；补齐后通过。
  生产 IPC 本来就是 binary，无需为此改生产载体。
- `client_application_text_input` CTest 通过（1.50 秒），包含新增查询时序测试；IPC 生命周期回归通过（3.60 秒）。
- Qt 当前发布 `px_client.exe` SHA-256：`3963D15DAD993A778668D82935D8917069AFC6702FDFDBEDAE27722B20932BDD`，
  已与 build-tree 对比一致。后续若再次修改，以最终交付记录为准。
- 临时实机证据在 `test-results/starisland-qt-*.png`，不包含连接票据，不提交生成产物。
- 单次本机票据兑换超过既有 3 秒等待而被拒绝，后续新实例重试可连接；尚未将此记录当作已修复的稳定性问题。

### Web 链路与 Game 实例修复

- Web 的诊断 ping channel 不更新服务端协议心跳 watchdog。`inst-23` 在媒体连接后约 15 秒断开；
  现在在已有可靠、有序 media channel 上立即发送并每 2 秒发送 `HeartBeat=20`，关闭/重连时清理计时器及旧回调。
  `inst-32`、`inst-51-03521fde` 超过 140 秒持续出画；后者 150 秒时仍为 1280×720，收发心跳 76 次。
- Game Service 启动 Render 时遗漏 Console instance id；后端以 PID/时间生成的身份被 Web 的严格实例检查拒绝。
  新增 `--app_instance_id`，贯穿 Service launch spec、Render settings、ApplicationTextService；不放宽 Web 身份校验。
  WebView 原有 id 和独立本机启动的临时 id 仍按原场景使用。Rust `app_instance::tests` 24 项通过。
- Web 开始编辑若收到明确 TargetChanged/TargetUnavailable/Busy，且实例、租约相同、输入代次未改变、服务端未进入编辑，
  可以显式关闭面板后重新选目标；草稿保留，不自动再试。未知结果、代次/租约改变及超时仍保持隔离并要求重连。
  新增明确拒绝、关闭先于回复、后续撤销恢复资格、未知结果不解锁的回归；Web 62 项单测及语音 19 项断言通过。
- 临时 IPC 每次查询日志已移除，最终运行版本不记录文字内容，也不按 750 ms 的目标轮询频率输出成功日志。

### 快速组合键：修复独立于本机物理键盘状态

- `inst-55`、`inst-64` 的浏览器合成 Ctrl+A 在游戏里未形成选区，最终文字被插入；明确不计为通过。
  Qt 早期使用真实物理 Ctrl 的单机结果可能借用了宿主系统键盘状态，不能证明纯远程组合键完整。
- Web 发出左右区分 VK，Win32 窗口消息需要通用修饰键 VK，同时以 scan code/extended bit 区分左右。
  [Windows 键盘消息说明](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input) 支持此边界映射。
- 更关键的是 Godot 4.6.2 的 `_get_mods()` 在 GUI 线程调用 `GetKeyboardState`，不是既有 Hook 覆盖的
  GetKeyState/GetAsyncKeyState。其窗口消息进入缓冲时保存修饰键状态，不能在 IPC 线程提前用最终释放状态代替。
  对照 [Godot 4.6.2 Windows 实现](https://github.com/godotengine/godot/blob/4.6.2-stable/platform/windows/display_server_windows.cpp)。
- 新 `window_message_key.h` 为每个远端按键生成值类型修饰键快照，通过注册的私有窗口消息入 GUI 队列；
  GUI 同步调用引擎 WndProc 前临时设置本线程键盘状态，RAII 在返回后完整恢复原状态；保留 TranslateMessage、原始 Raw Input。
  排队载荷不包含指针，不注入全局键盘，不改变 Job/path 准入。窗口消息 VK 归一化不改变协议键码。
- 自动化验证快照不受后来的 Ctrl-up 影响、嵌套状态恢复、重复 20 次恢复，以及左右修饰键的窗口消息映射。
- `inst-76-9dccdb4f`：无物理 Ctrl、无人工增加按键延迟的浏览器 Ctrl+A 后，游戏完整显示“网页旅者🙂”；
  关闭面板后进入游戏并恢复 W/Space，主动关闭控制 channel 后草稿保留、发送禁用且不重发。
- `inst-73` 的本机 UI 辅助脚本在物理 Space 时超出自身 10 秒限制，已停止专用实例；不是文字提交通过证据。
  后续辅助脚本单操作限时 20 秒，整轮仍少于 5 分钟。

### Vulkan / OpenGL 探索结论

| 场景 | 实测 | 已定位的边界 |
| --- | --- | --- |
| Vulkan，`inst-61-7c4d9988` | 95 秒内 Qt 持续显示游戏；加载 vulkan-1，Hook 日志实际为 `d3d12 shared texture capture successful`；`hook Vulkan result: false`。 | 这台机器上的呈现路径被既有 DXGI/D3D12 捕获；不能记作本项目 Vulkan Layer 成功，也不能排除该启动链路的引擎/驱动回退。 |
| 同一游戏随包 console wrapper 独立诊断 | 90 帧后正常退出，明确打印 `Vulkan 1.4.325 - Forward+ - NVIDIA GeForce RTX 3060`。 | 证明游戏和 GPU 可以运行 Vulkan；这是独立诊断，不冒充被 Hook 实例的引擎日志。GUI 子进程的指定日志为空。 |
| OpenGL，`inst-70-e8b9427e` | 95 秒：WGL SwapBuffers Hook 成功，`Shared-texture OpenGL capture available`、`gl shared texture capture successful`，但 Qt 始终等待画面。 | OBS 共享纹理路径已建立，却没有项目自己的 `IpcCaptureVideoFrame` 发布；只看到 Hook 安装成功不能记为出画成功。 |
| OpenGL 独立诊断 | 90 帧正常退出，明确为 `OpenGL 3.3.0 NVIDIA 591.74 - Compatibility`。 | 与产品 Hook 的 OpenGL 调用证据一致；不涉及引擎回退或桌面采集。 |

源码对照及最小后续实现：

1. `hk_obs/graphics/vulkan-capture.c` 的 `hook_vulkan()` 只检查 `OBS_Negotiate` 是否已被 Loader 调用。
   编译进去、后期 LoadLibrary 注入都不足以建立早期 Vulkan 实例/设备/交换链拦截。
2. 本项目 `obs-vulkan64.json` 仍指向不存在于产品包的 `graphics-hook64.dll`，产品实际 DLL 为 `px_gh.dll`。
   OBS 的 `plugins/win-capture/game-capture-file-init.c` 通过 ImplicitLayers 注册表激活；本项目不能照搬全局注册。
3. 如后续实施独立 Vulkan Layer：仅为本次授权子进程构造 Layer 环境和产品 manifest；
   私有 Job 分配之后、恢复主线程之前建立 bootstrap。当前 OwnedGameProcess::Launch 返回时线程已经运行，
   当前 bootstrap 又在后续注入之前才生成，必须先补“两阶段启动/失败回滚”，不能跳过现有缺少 bootstrap 即拒绝的检查。
   未在本次 Job 且 exact-path 未通过的子进程必须继续拒绝捕获。
4. OpenGL 最小补齐点是 `gl-capture.c` 的 GPU 互操作纹理 → typed D3D11 adapter → 项目共享纹理及帧 IPC；
   参考本项目 `d3d12-capture.cpp` 已有 CopyCapturedTexture、adapter LUID、尺寸/格式、Flush、发送帧元数据顺序。
   同时把 GL `flip=true` 变成明确的图像方向处理；跨上下文、窗口 resize、纹理销毁必须有重新发布及生命周期测试。
5. `graphics-hook.cpp::capture_init_shmem` 的实现仍在 `#if 000` 中，不能宣称无 NV_DX_interop 的 GPU 有 CPU 回读兜底。
   GL 每约 5 秒重新初始化的现象需随项目帧/活动状态接入一起回归。未修改 OBS 原始目录、系统 Layer 注册表或外部游戏工程。

本轮 D/E 的交付是实测和接入差异报告，不包含独立 Vulkan Layer 或完整 OpenGL 产品采集实现。

## 最终验收与发布（2026-09-10）

- Qt 最终批次 `inst-85-cee7abce`，100 秒限时：实际显示“最终验收🙂”，关闭面板后进入游戏，W 前进与 Space 跳跃恢复。
  使用的是最终 dist 客户端及 Hook。前一辅助脚本误用不存在的 `VK_W` 名称，已改为脚本支持的 `w` 并重测；非产品失败。
- Web 最终批次 `inst-79-fe3d0269`：完整走真实系统 `nihao` 候选、Space 选词、明确发送；远端实际显示“网页中文🙂你好”。
  随后浏览器合成 Ctrl+A 完整替换为“网页旅者🙂”，关闭面板进入游戏、W/Space 恢复；控制 channel 断开后草稿保留且发送禁用。
  所有测试实例均用产品 API 停止，不按同名 exe 扫描结束游戏；Auth/Console/Service 保留本机可用。
- Web 只读票据实测 `inst-88-cd6fa915`：权限只有 view/audio，1280×720 持续出画，文字入口禁用，正常停止。
  先前另一 guest 试图读取 `inst-85` 票据被 Console 以 404 拒绝；随后改为同一 guest 的独立实例验证只读，不放宽实例访问限制。
- CTest 8/8 组通过，10.62 秒：client_application_text_input、application_text_protocol、game_text_input、
  ws_ipc_client_lifecycle、logical_session_registry、application_text_service、game_process_identity、game_owned_process。
  包含模拟撤权、目标代次改变、超时不重发、排队时销毁、回调取消、重复启停、严格 Job/path 身份等自动化检查。
- Web 最终 62/62 单测通过，语音状态 19 项断言通过；Service app-instance 24 项测试通过。
  C++ 新增指针/生命周期/150 列门禁通过；新文件完整格式检查通过，旧文件仅格式化变更范围，保留原 Windows 行尾。
- 构建仅使用 `scripts_build/build_cpp_*.bat` 指定目标、Web build 和 Service 单包 release build，未运行全量 release 脚本。
  枚举修复阶段的 Android 构建已通过；本次新增 Game 修复没有再改变共享 SDK/协议/JNI，未新增 Android 实机通过声明。

最终运行文件已同步 `build_official/dist`，逐项 SHA-256 与构建源一致：

| 文件 | SHA-256 |
| --- | --- |
| px_client.exe | `7CAC2111B8F8666A31100F5C3AFD8CBAC33F7AB752AC3A62B9B894700843F23F` |
| px_render.exe | `305101673288DA87D9A64DB61426239D6495E916695EFD10D9C83C30BD811BF3` |
| px_gh.dll | `C4B52D04CC30BB36963E409FD1CAA60A6C9BAA88D089A8DF9EF2215BD1EE630A` |
| px_render_rtc.dll | `8C82019F17DCB8CC084A16CF108CA6FE1ACE23036005A4E58D2471FCB2D056E3` |
| px_render_rtc_remote.dll | `FE132C366D9A982A6A5A050B7A27D67C60F8608DD9F3E385ECF8A456D395D76F` |
| px_service.exe | `5E416BC1C624EAD12111EAA0E16E15AD97B9D9A110AC849DAFE7F211DECE98F3` |
| web_client/index.html | `F0E8C5332BA985D9E621C87675EA81E054DD5A24D9DD4CEBEE677DA5F1BAD826` |
| web_client/assets/index-pRl_Q5QP.js | `2E9D17F0A4041D9FF546359EE25E20E526046539F591CC51DAD3DDF177161601` |
| web_client/assets/index-BQEigI-F.css | `CC5C9104FCA26C5D4736D448AAB013F38F30F45C3F5B701714952016CF9831C8` |

语言资源及其他 Client 发布资源也经发布脚本逐项校验。完整输出与截图保留在本机 `test-results/starisland-*`，
不提交生成产物或连接凭证。原有不相关 Rust 工作区改动、配置和私人测试文件均保留。

### 覆盖边界

验收结论限定为本机 StarIsland 的 Qt Native 与桌面 Web 中文输入闭环及上述自动化负向回归。
不包含所有输入法/键盘布局、AltGr、所有引擎、双 GPU、跨机网络性能、长时间压力测试或 Vulkan/OpenGL 全产品支持。
Qt/Web 各负向项若只由自动化覆盖，不升级为真实游戏人工操作通过。
之前单次票据兑换超时、旧 Console 已停止实例的重复停止错误，不属于本次已修复问题；停止成功记录只表示本次测试实例正常清理。

## 下一轮开发执行顺序（2026-09-10 交接）

本节记录准备如何开发，不代表以下功能已经实现。本轮先提交已验收的中文输入修复；下一轮从步骤 1 开始。

1. **先接通 OpenGL 帧输出。** WGL Hook 与 GPU 互操作已触发，优先补最短缺失链路：
   将捕获纹理交给项目维护的 typed D3D11 adapter，接入共享纹理和 `IpcCaptureVideoFrame`，复用现有编码及 Native/Web 传输。
   在实际同步点完成 GPU 写入后再通知消费者；明确处理 GL 上下翻转、格式、adapter LUID 和尺寸变化。
   不把 OBS 的旧共享内存通知机制与项目帧 IPC 并行维护为两套产品链路。
2. **完成 OpenGL 生命周期闭环。** 定位约 5 秒反复初始化的触发条件，补齐活动状态、上下文/窗口重建、resize、
   销毁及重复启停的资源释放与重新发布。验收要求 Qt/Web 都有真实、方向正确且连续更新的游戏画面，中文输入及控制不回归。
   没有 NV_DX_interop 时明确返回不支持；本阶段不启用当前空实现的 CPU 回读分支来伪装兜底。
3. **再建立 Vulkan 安全启动前置条件。** 把游戏启动组织为 suspended 创建 → 私有 Job 分配及 exact-path 核验 →
   bootstrap/实例专属环境准备 → 恢复线程；每个失败点都要回滚本次资源。为按路径相同但不在 Job、非授权后代、
   PID 复用、准备失败、重复停止添加测试。先完成此门禁，再激活 Layer，不在加载失败时放宽准入。
4. **接入产品 Vulkan Layer。** 准备指向实际产品 DLL 的 manifest，仅对授权游戏启动环境启用，不改系统注册表、
   全局环境或 OBS 原始目录。核实 Loader 协商、instance/device/swapchain/Present 的真实回调，
   将 Vulkan 外部内存/同步接入步骤 1 的项目帧发布边界；若确需独立 Layer DLL，则明确其 ABI、所有权及打包责任。
   同时取得被 Hook 的同一实例的 API 证据，排除引擎回退及驱动 DXGI 呈现路径造成的假通过。
5. **三 API 统一验收与交付。** 使用同一 StarIsland 分别运行默认 D3D12、显式 Vulkan、显式 OpenGL；
   每轮最多 5 分钟，检查画面、resize/重建、停止重启、Qt/Web 中文与快速组合键、只读权限及断线草稿保护。
   Vulkan 必须有 Layer 回调和帧消费证据；OpenGL 必须有帧 IPC 和客户端实际图像，不能只凭初始化日志通过。
   增量编译相关目标，发布全部变更运行文件至 dist 并核对 SHA-256，更新本文件的结果矩阵后再声明完成。

实现约束：旧分支修改前归档原始字节；新增项目 C++ 从首个所有权边界使用智能指针/typed RAII，
不为跨 API 桥接引入异步裸指针。只改本项目维护范围，外部 OBS、Godot 游戏和其他参考工程保持只读。
优先解决已证实的链路缺口，不重做已通过的 Native/WebRTC 传输，不扩展公网 P2P/Relay 或 iOS/macOS 适配。

## 本轮实施结果（2026-09-10）

用户确认四 API 目标后，本轮已经补齐 OpenGL 帧 IPC、Vulkan Layer 与安全启动，并完成 Windows 本机验收。
原有探索记录和待实施措辞保留为历史；当前结果以
[四 API 实施与验收记录](game_graphics_hook_delivery_20260910.md) 为准。

| API | 本轮状态 |
| --- | --- |
| D3D11 | 独立 D3D11 硬件 swapchain 样例实际出图；Godot 4 没有 D3D11 后端，不用 D3D12 冒充。 |
| D3D12 | StarIsland Qt 中文 / Emoji、进入场景、移动和跳跃回归通过。 |
| OpenGL | Qt / Web 实际画面、中文 / Emoji 和控制通过；窗口缩放恢复后仍保持 OpenGL 后端。 |
| Vulkan | 产品 Layer 回调与实际共享纹理帧通过；Qt / Web 中文和控制、窗口缩放恢复通过。 |

额外修复了管理员完整性导致 Loader 忽略私有 Layer 路径、Windows manifest 路径格式、
系统 OBS 命名对象冲突、bootstrap 对普通游戏用户的读取权限，以及 Loader 提前加载遗漏窗口 / 音频登记。
具体实例编号、自动测试、产物 hash 和硬件 / 启动方式边界见验收记录；不扩大为任意游戏、显卡或外部启动器都已支持。
