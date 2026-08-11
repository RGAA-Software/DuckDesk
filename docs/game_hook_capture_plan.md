# Game-Hook 采集打通计划与流程

> 状态：进行中（2026-08）  
> 目标：`mode = "game-hook"` 时，单独启动一个 `GammaRayRender`，启动游戏、注入采集 DLL，用 web client 看见游戏画面。  
> 约束：本期**不经过** panel / `gr_service`；多 render 编排后置。  
> OBS 对照：`D:\source\obs-studio\plugins\win-capture`（`game-capture.c` / `graphics-hook` / `inject-helper`）。  
> 音频梳理：[`game_hook_audio_capture.md`](./game_hook_audio_capture.md)（PID process-loopback / MiniAudio 补丁 / Hook fallback）。  
> 输入：game-hook **强制** `event-replay-mode=inner`（进程内注入，禁止 OS `SendInput`）。

---

## 1. 目标验收

1. 在 `settings.toml` 填好 `mode = "game-hook"` 与本地 `game-path`
2. 运行 `scripts\start_render_hook.bat`（仅启 Render；或 `run_game_hook_render.bat` 带无头校验）
3. 默认 HTTP 端口 `32000`（`--network_listen_port`）
4. 注入 `tc_graphics.dll`（OBS 移植）
5. 浏览器打开 `http://127.0.0.1:32000/web_client/?deviceId=debug1`，**看到游戏画面**
6. 鼠标/键盘可控制游戏（焦点可在浏览器；经 `/ipc` → `HookManager` PostMessage + RawInput）

---

## 2. 端到端流程

```
settings.toml
  mode = "game-hook"
  game-path = "<exe>"
  capture-method = "search"   # OBS 注入（非 EasyHook prepare）
        │
        ▼
scripts/start_render_hook.bat
  - 工作目录 = build_official/dist
  - 同步 settings.toml（仅 game-path / capture-method 等）
  - 启动时显式传参：--app_mode=game-hook --app_game_path=<Base64 UTF-8> --capture_video_type=inner ...
        │
        ▼
RdSettings::LoadSettings → UpdateSettings(CLI) → ApplyApplicationMode
  - CLI --app_mode 优先；未传则用 toml application.mode
  - --app_game_path 为 Base64(UTF-8)，解码后覆盖 toml game-path（避开会话代码页）
  - game-hook → inner + 启游戏；desktop → 屏幕采集且不启游戏
  - capture-method 仍来自 toml
        │
        ▼
RdApplication::Run
  - mode=game-hook：不启动 DDA/GDI
  - StartProcessWithHook() → CreateProcess(game)
  - AppManager 定时 InjectCaptureDll（tc_graphics_util.exe）
  - 注入前同步写 boot 文件（非 SHM）:
      %PUBLIC%\GoDesk\hook_boot\application_{pid}.bin
      内容: ipc_port + DXGI offsets（仅启动配置）
        │
        ▼
游戏进程内 tc_graphics.dll
  - 读 boot 文件（ipc_port / DXGI offsets）— 不用 SHM
  - Hook Present（D3D11/12/…，对照 OBS graphics-hook）
  - 共享纹理 HANDLE 元数据 → CaptureVideoFrame
  - WsIpcClient → ws://127.0.0.1:{port}/ipc   ※ 进程间通信只走明文 WS；/ipc 仅接受 loopback 连接
        │
        ▼
WsPluginServer(net_ws) /ipc → OnIpcVideoFrame   ※ 仅 loopback；帧为 152B POD IpcCaptureVideoFrame
  → EncoderThread::Encode
  → PluginStreamEventRouter → /media + WebRTC local
        │
        ▼
浏览器 http://127.0.0.1:20371/web_client/?deviceId=debug1
  （空密码时 auth 放行；先连 peer 再出画，HasConnectedPeer 门闩）

输入（game-hook / event-replay-mode=inner）:
  浏览器 mouse/key → PluginNetEventRouter
    → CaptureMessageMaker(Mouse/KeyboardEventMessage)
    → RdApplication::PostIpcMessage → **仅** WsPlugin::PostIpcBinaryMessage → /ipc 下行
    → tc_graphics WsIpcClient → HookManager
       PostMessage(WM_*) + GetRawInputData 队列 + 虚拟 GetCursorPos
  ※ desktop 仍走 EventReplayer → SendInput（global）；inner 时跳过 EventReplayer，避免双路径
  ※ PostIpc 不可 Visit 全部 NetPlugin：旧 DLL 缺 trailing vtable 槽会崩溃
```

---

## 3. 现状与缺口

| 组件 | 状态 |
|------|------|
| `tc_graphics.dll` / `tc_graphics_util.exe`（OBS 移植） | 已有 |
| 定时注入 + file bootstrap（WS `/ipc`，非帧 SHM） | 已有 |
| `OnIpcVideoFrame` → encode → 推流 | 已有 |
| `application.mode` 读取（desktop / game-hook） | 已有 |
| `StartProcessWithHook`（仅 game-hook） | 已有 |
| net_ws 注册 `/ipc`（仅 loopback 连接） | 已有 |
| DLL 明文 WS `/ipc` + shared texture（帧为 152B POD `IpcCaptureVideoFrame`） | 已有 |
| hook-inner 输入 Host→`/ipc`→DLL | 已有 |
| service / 多 render | **后置** |

---

## 4. 实施步骤（本期）

### Phase A — 配置与启动

1. `rd_settings` 读取 `application.mode`；CLI `--app_mode` 可覆盖
2. `game-hook` → `kVideoInner` + 启游戏注入；`desktop` → 屏幕采集且不启 `game-path`
3. panel：`--app_mode=desktop`；hook 脚本：`--app_mode=game-hook` + 其它启动参数
4. `RdApplication::StartProcessWithHook`（只负责起游戏；编码走现有 plugin 路由）

### Phase B — IPC 帧通道

1. `WsPluginServer`（net_ws）注册 `/ipc`（AddIpcRouter，仅 loopback；原 `WsIpcRouter` 死代码已删）
2. Router 使用全局 `rdApp` 调 `OnIpcVideoFrame`
3. DLL 侧 `WsIpcClient` 改为 `asio2::ws_client`（明文），与 host 对齐  
   （对照：host `ws_server.cpp` 用 `http_server`；OBS 自身用共享内存，我们这侧是 WS 传元数据 + shared HANDLE）

### Phase C — 调试脚本

1. `scripts/run_game_hook_render.bat`
2. 同步 toml、杀旧实例、起 render、打开 web client URL
3. 日志检查点：pid → Inject success → ipc connect → encode → peer

### Phase D — 出画联调

1. 戴森球路径验证
2. 失败则对照 OBS `game-capture.c` 的进程/窗口查找与注入时机
3. 多进程游戏必要时增强按窗口找 PID（后置小迭代）

### Phase E — 后置（出画后再做）

- Service 拉起 game-hook render → 见 [`cms_app_schedule_plan.md`](./cms_app_schedule_plan.md)（CMS 多机调度，P3 已落地）
- 多实例端口 / launch spec → 同上
- EasyHook `prepare` 路径

---

## 5. 本地调试命令

```bat
scripts\run_game_hook_render.bat
```

手动等价：

```bat
cd /d build_official\dist
copy /Y ..\..\src\gr_render\settings.toml settings.toml
rem fill game-path in settings.toml
GammaRayRender.exe --logfile --app_mode=game-hook --app_game_path=<Base64 UTF-8 path> --capture_video_type=inner --network_listen_port=32000
```

浏览器：

```
http://127.0.0.1:32000/web_client/?deviceId=debug1
```

日志：

```
%ProgramData%\GoDesk\gr_logs\godesk_render_20371.log
```

（或 `FolderUtil::GetProgramDataPath()` 实际路径下的同名文件）

---

## 6. OBS 对照索引

| GammaRay | OBS |
|----------|-----|
| `hook_capture/win/hk_obs/` → `tc_graphics.dll` | `plugins/win-capture/graphics-hook/` |
| `hk_obs/injector` → `tc_graphics_util.exe` | `plugins/win-capture/inject-helper/` |
| `AppManagerWinImpl::InjectDll` | `game-capture.c` inject 调用链 |
| `AppSharedMessage` / boot file `application_{pid}.bin` | `graphics-hook-info` / hook config |
| WS `/ipc` + shared texture HANDLE | OBS 帧元数据多为 SHM；GPU 纹理仍可走 shared HANDLE / shmem texture |

有注入、Present hook、共享纹理问题时，优先 diff 上述 OBS 文件的最新实现。

---

## 7. 风险

1. 游戏多进程：注入启动器无画面（已缓解：只选一个候选进程，优先有可见主窗口）  
2. `HasConnectedPeer()`：需先开 web client  
3. cwd 必须是 dist（injector 用 `current_path()` 拼 DLL）  
4. 反作弊 / 完整性校验可能导致注入失败  
5. D3D 版本路径差异（11/12/Vulkan）  
6. 32 位游戏：注入前 `IsWow64Process` 检测命中即明确拒绝（「暂不支持 32 位游戏」），不再反复重试  
7. `/ipc` 安全：仅接受 loopback 连接；帧为 POD wire 格式。Host render 与 `tc_graphics.dll` 必须同批部署（协议变更）

---

## 8. 2026-08-08 修复

本轮对 game-hook 链路做了一轮安全与鲁棒性收口，要点（详见 [`game_hook_audio_capture.md`](./game_hook_audio_capture.md) 末节）：

### 8.1 /ipc 安全与 wire 格式

1. `/ipc` 只接受 loopback（127.0.0.1 / ::1 / ::ffff:127.0.0.1）连接，非 loopback 立即关闭；此前绑 0.0.0.0 无鉴权，远程可推伪造帧并可收到广播下行的用户键鼠事件。server 本体仍 0.0.0.0 服务浏览器。
2. 视频帧 wire 改定长 152B 纯 POD `IpcCaptureVideoFrame`（magic=`GRCV` + version=1 + pack(1)），Host 逐字段转换，不再整体 memcpy 含 `std::shared_ptr` 的结构；旧格式显式拒绝 + 限流日志；宽高 clamp 16..8192。**协议变更：Host render 与 `tc_graphics.dll` 必须同批部署。**
3. 死代码 `src/gr_render/network/ws_ipc_router.cpp/.h` 已删除，/ipc 实际由 `plugins/net_ws/ws_server.cpp` 的 `AddIpcRouter` 处理。

### 8.2 注入鲁棒性

1. 注入挪到独立 worker 线程（`MsgTimer100` 只投递不阻塞）；失败固定 100ms 重试不设上限（§9.4 调整，原指数退避/60 次上限已回退）；`ACCESS_DENIED`（游戏管理员权限）明确报错并持续重试。
2. 32 位游戏注入前 `IsWow64Process` 检测命中即明确拒绝（「暂不支持 32 位游戏」）；`inject-library.c` 纵深检查返回 `INJECT_ERROR_X86_TARGET_NOT_SUPPORTED(-5)`。
3. 游戏重启检测：injected 后每 1s 检查进程存活 + DLL 映射，连续 3 次失败清 `injected_` 重走注入。
4. Steam 多进程只选一个候选（优先有可见主窗口，否则最大 pid），成功即 break；补「injector 超时但 DLL 已映射」兜底。
5. `IsDllInjected` 快速失败、修 `sizeof(name)/sizeof(WCHAR)` 缓冲区 bug、`GetWindowThreadProcessId` 返回值校验。

### 8.3 进程内 hook 音频与 DLL 卸载

- `HookCoreApi::Shutdown` 完整卸载路径（vtable 槽恢复 + 子 hook Detach，幂等）；`DLL_PROCESS_DETACH` 区分进程退出与 FreeLibrary；Detour 注册本进程全部线程；多源抑制集中化（WASAPI 活跃 1000ms 内丢弃其它来源帧）；队列 16MB 字节限流；不支持的格式丢帧计数而非硬编码 48k/f32 兜底。细节见音频文档末节。

### 8.4 PID 内录音频

- 采集格式 44100 → 48000/2ch（44100 与 Opus 不兼容，Opus 路径必然无声）；SILENT 包推等长零 buffer 保持采样时钟；激活 handler use-after-free 修复；致命错误经 stop callback 上报。细节见音频文档末节。

---

---

## 9. 2026-08-08 修复（第二轮）

### 9.1 /ipc token 鉴权 + boot 文件 ACL（校验后被 9.3 移除，管道已整体删除）

- `AppSharedMessage` 新增 `ipc_token_[40]`（96→136 字节）；`PrepareGameHookBoot` 每次生成 128-bit 随机 token 写 boot，并经插件接口（`AddIpcAuthToken`）注册到 `WsPluginServer` token 集合。
- boot 文件写入后 ACL 收口（SDDL `D:P` 切断继承，仅当前用户/SYSTEM/Administrators）。

### 9.2 注入 gave_up 复活探测

- gave_up（32 位拒绝）后每 3s 探测：旧 pid 消失或同 exe 名新 pid 出现 → `ResetInjectRetryState()` 重走注入。

### 9.3 /ipc token 校验移除（2026-08-08）

- 同机进程视为可信：`/ipc` open 不再校验 token（仅保留 loopback 校验）。
- 整条 token 管道随后已整体移除（2026-08-08 代码清理）：`AppSharedMessage` 删除 `ipc_token_` 字段（回到 96 字节）、render 不再生成/注册 token、DLL 不再上送、`WsPluginServer` 删除 token 集合与 `AddIpcAuthToken` 插件接口、`WsIpcClient` 不再拼 `?token=`。boot 文件 ACL 收口保留。
- 防远程伪造帧/嗅探依赖 loopback 收口。

### 9.4 注入重试策略调整（2026-08-08）

- 保留独立 worker 线程，但**去掉指数退避与 60 次上限**：失败后固定 100ms 重试、永不放弃（尽快出画面优先）；失败日志节流（第 1 次及每 100 次一条）。gave_up 仅剩 32 位拒绝场景（仍由 9.2 的复活探测覆盖）。

---

## 10. UE 启动器/真游戏分离：boot/view 双路径（2026-08-09）

### 10.1 问题

UE 打包游戏顶层 exe 是 bootstrap 外壳（如 `VehicleGame.exe`），真正渲染进程是 `Binaries\Win64\XXX-Win64-Shipping.exe`。配外壳路径时注入外壳（无 swapchain）永远无画面、音频绑错进程、`wait_game_process` 假通过。

### 10.2 方案（迁移 dolit/streamer 的 boot/view 双路径）

UE Bootstrap 约定：外壳 exe 的 `RT_RCDATA` 资源 **201 = 真 exe 相对路径、202 = 基础参数**（参考 `D:\dolit\streamer\src\common\ue_resource_parser.cc`）。资源给出精确 view 路径，无需 dolit 的窗口/后代启发式猜测。

- **service 层解析**（`service_core/ue_bootstrap.rs`）：`LoadLibraryExW(LOAD_LIBRARY_AS_DATAFILE | DONT_RESOLVE_DLL_REFERENCES)` 读资源（不执行目标代码）；路径 canonicalize 后剥掉 Windows `\\?\` verbatim 前缀（否则与 `QueryFullProcessImageNameW` 比较必失败）；`is_file()` 校验，失败/非 UE 一律回退单路径现状。
- **外壳照常启动**：CMS 配置不变（填外壳或内层 exe 均可）；service 的拉起/`wait_game_process`/监控都还在 boot 上。
- **下发 view 路径**：launch spec 追加 `--app_game_view_path=Base64(view 绝对路径)`；实例记录 `view_game_path`。
- **render 注入 view**：`InjectCaptureDllForNormalApp` 在 view 路径非空时按完整路径精确匹配（UTF-8、忽略大小写与分隔符）发现 view 进程；未出现返回 `attempted=false` 走低频等待（外壳初始化完下一轮命中）；boot 文件本来就是每次注入前按目标 pid 写（`app_manager_win.cpp:542`），view pid 天然正确；`MsgObsInjected.pid_` = view pid → 音频 PID loopback 自动绑对。
- **停止**：杀 boot 树 + 按 view 路径补杀（覆盖外壳拉起真游戏后先退出的孤儿场景）。
- **不合并 202 参数**：boot 外壳自己会读 201/202 并透传命令行给 view 子进程，202 的 base_args 并入我们传给 boot 的参数会导致项目名重复。

### 10.3 顺手修复

- service 发的游戏参数 flag `--app_game_arguments` 改为 `--app_game_args`（render gflags 只认后者；严格解析下配参数 render 起不来）。service 侧 `escape_arg` 会引号包裹含空格的参数，gflags 能整串接收。

### 10.4 验证

- 单测：`service_core` 57/57（含 UE 资源解析、view 参数透传、`\\?\` 剥离）；`UE_BOOT_EXE=<外壳路径> cargo test -p service_core real_ue -- --ignored` 可对真实游戏做集成验证。
- E2E（2026-08-09）：CMS 配外壳路径 `D:\1_test_games\CarGame  汽车\VehicleGame.exe` 启动 → render 日志 `UE view process found` + `Inject success`（pid 为 `VehicleGame-Win64-Shipping.exe`）→ 无头 `PASS mode=pid-loopback heard=true`（视频 62fps）→ CMS 停止后 boot/view/render 全部清理。

### 10.5 已知限制

- 同名多实例游戏按完整路径匹配后取第一个注入（与单实例假设一致）。
- 非 UE 外壳（无 201 资源）不做启发式 view 发现——有需求时再迁移 dolit 的通用 boot/view 状态机。

## 11. 事件重放与焦点保持（2026-08-09/10）

### 11.1 输入链路

Web 客户端 datachannel 鼠标/键盘事件（`x_ratio`/`y_ratio`）→ render `plugin_net_event_router` 按游戏窗口实时 rect 换算成屏幕绝对坐标 → /ipc → DLL `hook_manager` 两路注入：

- **WM 消息路**：`WM_MOUSEMOVE`/按钮/键盘 + `WM_INPUT` 触发游戏消息泵读取；
- **RawInput 路**：`GetRawInputData` hook，用相邻两次绝对坐标差分合成相对位移（Unity/UE 视角转动读这个）。

### 11.2 修复清单

1. **DPI 感知**：`rd_main` 首行 `SetProcessDpiAwarenessContext(PerMonitorV2)`。非 aware 时 4K@150% 下窗口 rect 被虚拟化成 2560x1440，游戏按物理 3840x2160 解释，光标整体偏左上约 2/3。
2. **RawInput 差分 + 纯移动合并**：游戏消费慢于事件到达时，连续纯移动事件合并取最新（中间位置可丢、相对总量不变），治"开始无响应 + 拖动卡顿"。删除 `MouseEventMessage` 的 `delta_x_/delta_y_/absolute_` 字段（客户端只发比例，字段无人赋值，纯误导）。
3. **连接重置**：`kCaptureResetInputMessage`(0x0007)。新客户端连接时 DLL 清积压队列、重置差分基准与修饰键状态；治"上一会话残留导致开局大跳变/几秒无响应"。
4. **断连补发释放**：render 跟踪按下的键/鼠标键，客户端断开时补发全部 release，治游戏内按键卡死。
5. **首连 15s 输入延迟**：Web 端 `initInput` 会轮询 `/get/render/configuration` 的 `monitor_name`（桌面模式按它定位回放坐标系，首帧编码前为空，最多等 15s）。hook 模式按游戏窗口 rect 换算根本不需要它——ws 插件在 game-hook 模式直接返回占位名 `game_hook`。⚠️ 坑：`RdSettings::Instance()` 是头文件内 static，插件 DLL 拿到的是**独立副本单例**（默认值 desktop），模式必须由 exe 经插件创建参数 `app_mode` 显式下发。
6. **`SetCursorPos` hook**：游戏主动居中/锁定光标时只同步内部伪造光标、不动物理光标（UE/Unity 视角模式）。
7. **`GetSystemMetrics(SM_REMOTESESSION)=0`**：伪装本地会话，避免游戏在远程会话下禁用功能。
8. **焦点保持（双保险）**：
   - `AssertGameFocus`：输入事件流中 500ms 节流补发 `WM_ACTIVATE(WA_ACTIVE)` + `WM_ACTIVATEAPP(TRUE)` + `WM_SETFOCUS`，新连接强制立即重断言。参考 streamer 实战：UE4 高频 `WM_ACTIVATE` 会卡按钮（用 `WM_ACTIVATEAPP`），Unity 失焦恢复需要窗口级 `WM_ACTIVATE`（streamer 对 Unity 用 100/300ms 定时器）。
   - `FocusGuard`：子类化游戏窗口 `WndProc`，直接吞掉 OS 真实失焦消息 `WM_KILLFOCUS` / `WM_ACTIVATE(WA_INACTIVE)` / `WM_ACTIVATEAPP(FALSE)`，游戏永远不知道自己失焦。`WH_CALLWNDPROC` 只能观察不能拦截，必须替换 `WndProc`；窗口重建（hwnd 变化）时自动重新子类化。

### 11.3 验证

- 无头：`RENDER_PORT=32101 DEVICE_ID=e2e-machine-1 node scripts/cdp_game_hook_input.mjs`（走页面 `__input.testSend` 真实发送链路；注意 render 同时只服务一个客户端，用户浏览器占线时无头会连不上）。
- 戴森球（Unity，32101）实测：4K@150% 全屏 ↔ 窗口化切换、宿主机切走焦点再回来，输入均保持有效；DLL 日志 `FocusGuard: swallowed` 确认三类失焦消息全被吞掉。

### 11.4 已知限制 / 待办

- 纯移动合并只在 RawInput 路生效，WM 消息路仍逐条投递（游戏消息泵自身有合并）。
- CMS reconcile 曾误判存活实例"心跳缺失"并回收（service 侧 `reap_dead_app_instances` 按端口单次探测，疑似瞬态失败），未复现未修；service 目前 `--console` 无文件日志，再发需先补日志。
- 停止实例偶发报 `render still alive after kill`（实际进程已死），停止流程的存活检查逻辑有误。

## 12. UE5 / D3D12 出画（2026-08-11）

### 12.1 问题

hook game 模式启动 UE5 应用采不到画面；加 `-dx11` 启动则正常。根因链三层：

1. **D3D12 hook 根本没编译**：`d3d12-capture.cpp` 整体被 `#if COMPILE_D3D12_HOOK` 包裹，但该宏从未定义，DXGI Present hook 之外没有 D3D12 纹理拷贝路径。UE5 默认 D3D12 渲染，因此永远无画面；`-dx11` 走的是原有 D3D11 路径所以正常。
2. **UE 引导壳注错进程**：`AAA.exe` 只是 142KB 引导壳（真游戏在 `Engine/Binaries/Win64/UnrealGame-Win64-DebugGame.exe`）。service 侧 `ue_bootstrap.rs` 的 boot/view 双路径已解决（见第 10 节）；本地调试 bat 需手动传 `GAME_VIEW_B64`（base64 的真游戏路径）给 `--app_game_view_path`。
3. **hook_d3d12 假成功**：`manually_get_d3d12_addrs` 失败时 `return true` 让 capture loop 以为已 hook 不再重试；游戏启动早期 `D3D12CreateDevice` 可能失败，必须返回 false 让下轮重试。

### 12.2 修复清单

1. **启用 D3D12 hook**：`hk_obs/CMakeLists.txt` 加 `add_definitions(-DCOMPILE_D3D12_HOOK)`；`hook_d3d12` 失败路径改 `return false`。
2. **`D3D11DeviceWrapper::Release()` 双重释放**（render 崩溃根因，子模块 `tc_common_new/win32/d3d11_wrapper.h`）：原来裸调 `->Release()` 而 ComPtr 仍持指针，wrapper 析构时二次释放，其他持有者的 ComPtr 悬空，device removed 后 `VideoFrameCarrier::Exit` 崩溃。改 `Reset()`。
3. **10bit swapchain 格式**：UE5 默认 `R10G10B10A2`(format 24)，NVENC H264 无法编码。生产端（hook 内 `SharedTexture::CopyCapturedTexture`）统一 shader blit 成 `B8G8R8A8` 再共享；消费端 `plugin_frame_carrier` 保留同款转换作兜底；`encoder_thread` 把非 8bit 捕获格式的 `encoder_config.texture_format` 归一为 BGRA。
4. **GPU TDR（device hung, -2005270522）根因**：消费端（frame carrier）**每帧 `OpenSharedResource` + 释放**共享纹理。11on12 共享资源反复 open/close 会使底层 D3D12 资源状态紊乱，约 9 秒后 device removed。改为**按 handle 缓存长开**（与 OBS 一致，OBS 打开一次终身持有）。⚠️ 排除过的方案：11on12 设备上创建 `SHARED_KEYEDMUTEX` 或 `SHARED_NTHANDLE` 纹理直接 `E_INVALIDARG`（plain SHARED 才行）；NTHANDLE|KEYEDMUTEX 能创建但消费端 `OpenSharedResource1` 也 `E_INVALIDARG`。
5. **`FrameDebuggerPlugin::OnRawVideoFrameRgba` 空指针**：hook 路径 `raw_image_` 为空时直接 `image->data` 崩溃，加 `!image` 判空。

### 12.3 验证

- `scripts/run_game_hook_render.bat` + `GAME_VIEW_B64=<base64 真游戏路径>`，无头 CDP 检查 web client 收到视频帧：`Headless verify OK`（1600x900，fps 150+，framesDecoded 持续增长）。
- 连续运行 3 分钟以上无 `Encode failed` / `HandleD3DDeviceFailure` / dump；NVENC 正常 Reconfigure。
- UE5 默认（D3D12）出画；`-dx11` 路径不受影响。

### 12.4 已知限制

- 共享纹理跨进程无锁（plain SHARED，无 keyed mutex）：理论上有撕裂风险，实测未见；11on12 上 keyed mutex 不可创建（见 12.2.4），暂无更好同步手段。
- 10bit→8bit 转换在 hook 进程内消耗游戏 GPU 的一次全屏 blit，开销可忽略但存在。

## 13. 画面残缺回归：blit 继承游戏管线状态（2026-08-11 第二轮）

### 13.1 问题

第 12 节修复上线后，UE4 / UE5(-dx11) / UE5(D3D12) 画面变成"全黑 + 左上角 UI 文字 +
右上角一小块实时场景"(1920x1080 帧里只有约 260x95 的区域有内容）；Unity 游戏正常。

### 13.2 定位过程

1. 在消费端 `VideoFrameCarrier::CopyTexture` 临时加 `DebugOutDDS` 同时 dump
   **hook 产出的共享纹理**和 **carrier 拷贝后的纹理**——两者内容一致且已残缺,
   证明问题在 hook 生产端,carrier/编码器无辜。
2. 根因：第 12 节新增的 10bit→8bit 转换 `BlitConvertToBgra` 在 **Present 时刻借用
   游戏的 immediate context** 画画,只设了 VS/PS/SRV/RTV/viewport,没有重置其余
   继承状态。UE 的 Slate/UMG 裁剪会在 context 上留下**小的 scissor rect**
   (且光栅状态 ScissorEnable=TRUE),Draw 被裁剪成一小块——每帧只有 scissor 覆盖的
   屏幕区域被拷贝,其余区域保持初始黑色。UI 文字区域因曾被某个 scissor 覆盖而显示
   (且 UI 静止,看起来"正常"),场景区域则只有最后一次 scissor 覆盖的一小块在动。

### 13.3 修复

`BlitConvertToBgra`(hook 端 `hk_video/shared_texture.cpp` 与消费端
`video_frame_carrier.cpp` 两处同款)Draw 前显式重置全部继承状态:

- `RSSetState(nullptr)`(默认光栅 ScissorEnable=FALSE)+ `RSSetScissorRects(全屏)`
- `OMSetBlendState(nullptr)` / `OMSetDepthStencilState(nullptr, 0)`
- `HS/DS/GS` 置空(防止游戏留下的 hull/domain/geometry shader 参与本次 Draw)

### 13.4 验证(.70,CMS 应用调度实例 + headless CDP 截图)

- UE5-AAA `-dx11`(32001):完整画面 ✅
- UE5-AAA 默认 D3D12(32001,临时去掉 -dx11 参数):完整画面,40s+ 无 TDR ✅
- UE4 CarGame(32002):完整画面 ✅
- 验证工具:`scripts/cdp_stream_screenshot.mjs`(WEB_URL 环境变量指向实例
  web_client,等待 video 起来后 Page.captureScreenshot)
- 实例启停走 CMS API:`/api/v1/app/control/app/instance/start|stop`(appkey 见
  CMS 日志 log_spvr*.log 的 `stored_appkey`)


## 14. 调试方法论与踩坑记录(2026-08-11)

本次"画面残缺"问题的完整排查流程,整理成可复用的套路。

### 14.1 排查套路:先分端,再定位

画面类问题(黑屏/残缺/花屏)第一步永远是**区分生产端和消费端**:

1. 在生产端(hook)产出共享纹理处 dump 一帧(`DebugOutDDS`,见
   `video_frame_carrier.cpp` 里曾临时加入的实现),同时在消费端拷贝后再 dump 一帧。
2. 两份 DDS 转 PNG 对比(工具 `tests/dds_to_png.py`,本地跑):
   - 生产端已残缺 → 问题在 hook/capture 链路,carrier、编码器、网络、web 全部排除;
   - 生产端完整、消费端残缺 → 问题在 carrier 拷贝或之后的链路。
3. **不要只看 web_client 的显示效果就下结论**。web 端看到的异常可能来自
   capture、carrier、编码、传输、解码、渲染任何一环,逐环 dump 才能少走弯路。

本次就是靠这个直接锁定:hook 借游戏 immediate context 做 10bit→8bit blit 时,
继承了 UE Slate 留下的小 scissor rect(ScissorEnable=TRUE),Draw 被裁剪。

### 14.2 复现/验证工具链(.70 远程调试全流程)

1. **改代码 + 增量编译**:`cmd //c "build_official\_build_inc.bat <target>"`
   (hook 相关 target:`plugin_frame_carrier`、`tc_graphics`;改 hk_video 头文件
   还会带动 gr_render / gr_service 重编)。
2. **部署 .70**:`tests\_deploy_hookfix_70.bat`(杀 render+游戏进程 → 拷贝 dll →
   服务自动拉起 render)。
3. **起实例**:CMS API `POST /api/v1/app/control/app/instance/start`
   (appkey 从 `output/gr_cms_server/logs/gr_cms_server/log_spvr*.log` 找最新
   `stored_appkey`,会随 CMS 重启轮换)。
4. **无头截图验证**:`scripts/cdp_stream_screenshot.mjs`,用法:
   `WEB_URL="http://10.0.0.70:<port>/web_client/?deviceId=990405157&instanceId=<inst>" OUT=x.png node scripts/cdp_stream_screenshot.mjs`
   等 video 流起来后 Page.captureScreenshot,比远程桌面看画面快且可留档。
5. **收尾**:停测试实例、恢复 CMS 里改过的启动参数、清理 .70 上的调试 dds/bat。

### 14.3 踩过的坑(不要再踩)

- **借用游戏的 D3D11 immediate context 画画,必须重置全部继承状态**:
  scissor rect、光栅状态、blend、depth-stencil、HS/DS/GS,缺一不可。只设
  VS/PS/SRV/RTV/viewport 不够——UE 的 UI 裁剪状态一定会留下来(本次根因)。
  更稳妥的长期方向是自建独立 context 或 command list。
- **Git Bash 内联跑 `net use` / `taskkill` 必炸**(密码里的 `&&`、UNC 路径转义)。
  一律写成 bat 文件再执行;bat 必须 CRLF 行尾,Write 之后
  `sed -i 's/$/\r/'` 补一下,否则 cmd 解析出错。
- **部署 .70 前必须先杀 render 和游戏进程**,否则 dll 被占用 copy 静默失败,
  会拿着旧 dll 白测一轮。CMS 实例的游戏进程被杀后实例可能标 failed,
  重启实例即可,不是代码问题。
- **CMS 应用配置是共享状态**:为测试改的启动参数(如临时去掉 `-dx11`)测完
  立刻恢复原值,否则下次别人/自己跑默认路径测的是错的东西。
- **调试代码(`DebugOutDDS` 之类)验证完立刻删掉再编一版部署复验**,
  不要带着 dump 代码收尾——每帧写盘会拖垮采集性能。
- **appkey 会过期**:CMS 重启后旧 appkey 失效,API 返回登录页 HTML
  (status 200 但 body 是 html),表现为前端"保存失败"/接口返回一堆
  `<!DOCTYPE html>`。从 CMS 日志找最新 `stored_appkey` 即可。
- **版本 bump 文件要随修复一起提交**(`rust_*/Cargo.toml`、`setup/proj_version.nsh`、
  `src/gr_base/version.cmake` 等,构建脚本自动改);`tests/` 目录不要提交
  (含 `.remote_admin.md` 明文凭据)。
