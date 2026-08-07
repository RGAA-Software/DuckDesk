# Game-Hook 采集打通计划与流程

> 状态：进行中（2026-08）  
> 目标：`mode = "game-hook"` 时，单独启动一个 `GammaRayRender`，启动游戏、注入采集 DLL，用 web client 看见游戏画面。  
> 约束：本期**不经过** panel / `gr_service`；多 render 编排后置。  
> OBS 对照：`D:\source\obs-studio\plugins\win-capture`（`game-capture.c` / `graphics-hook` / `inject-helper`）。

---

## 1. 目标验收

1. 在 `settings.toml` 填好 `mode = "game-hook"` 与本地 `game-path`
2. 运行 `scripts\start_render_hook.bat`（仅启 Render；或 `run_game_hook_render.bat` 带无头校验）
3. 默认 HTTP 端口 `32000`（`--network_listen_port`）
4. 注入 `tc_graphics.dll`（OBS 移植）
5. 浏览器打开 `http://127.0.0.1:32000/web_client/?deviceId=debug1`，**看到游戏画面**

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
  - WsIpcClient → ws://127.0.0.1:{port}/ipc   ※ 进程间通信只走明文 WS
        │
        ▼
WsPluginServer /ipc → WsIpcRouter → OnIpcVideoFrame
  → EncoderThread::Encode
  → PluginStreamEventRouter → /media + WebRTC local
        │
        ▼
浏览器 http://127.0.0.1:20371/web_client/?deviceId=debug1
  （空密码时 auth 放行；先连 peer 再出画，HasConnectedPeer 门闩）
```

---

## 3. 现状与缺口

| 组件 | 状态 |
|------|------|
| `tc_graphics.dll` / `tc_graphics_util.exe`（OBS 移植） | 已有 |
| 定时注入 + SHM bootstrap | 已有 |
| `OnIpcVideoFrame` → encode → 推流 | 已有 |
| `application.mode` 读取（desktop / game-hook） | 已有 |
| `StartProcessWithHook`（仅 game-hook） | 已有 |
| net_ws 注册 `/ipc` | 已有 |
| DLL 明文 WS `/ipc` + shared texture | 已有 |
| service / 多 render | **后置** |

---

## 4. 实施步骤（本期）

### Phase A — 配置与启动

1. `rd_settings` 读取 `application.mode`；CLI `--app_mode` 可覆盖
2. `game-hook` → `kVideoInner` + 启游戏注入；`desktop` → 屏幕采集且不启 `game-path`
3. panel：`--app_mode=desktop`；hook 脚本：`--app_mode=game-hook` + 其它启动参数
4. `RdApplication::StartProcessWithHook`（只负责起游戏；编码走现有 plugin 路由）

### Phase B — IPC 帧通道

1. `WsPluginServer` 注册 `/ipc`，挂 `WsIpcRouter`
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

- Service 拉起 game-hook render
- 多实例端口 / launch spec
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
| `AppSharedMessage` / `application_shm_{pid}` | `graphics-hook-info` / hook config |
| WS `/ipc` + shared HANDLE | OBS 主要为 shared memory / texture |

有注入、Present hook、共享纹理问题时，优先 diff 上述 OBS 文件的最新实现。

---

## 7. 风险

1. 游戏多进程：注入启动器无画面  
2. `HasConnectedPeer()`：需先开 web client  
3. cwd 必须是 dist（injector 用 `current_path()` 拼 DLL）  
4. 反作弊 / 完整性校验可能导致注入失败  
5. D3D 版本路径差异（11/12/Vulkan）
