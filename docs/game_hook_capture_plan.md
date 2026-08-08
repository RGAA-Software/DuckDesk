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
