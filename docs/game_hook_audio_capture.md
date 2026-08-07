# Game-Hook 音频采集：修改梳理与说明

> 状态：主机 PID process-loopback 已打通（2026-08）  
> 范围：`mode = "game-hook"` 下的音频路径（Host 采集 / 进程内 Hook 二选一）  
> 相关计划：[`game_hook_capture_plan.md`](./game_hook_capture_plan.md)

---

## 1. 目标与约束

### 1.1 目标

- Game-hook 模式下，浏览器 WebRTC / WS 能听到**当前游戏进程**的声音。
- 多实例场景下，禁止用「默认播放设备 mix loopback」——那会混进系统/其它进程声音。

### 1.2 两条互斥路径

| 条件 | 路径 | 说明 |
|------|------|------|
| OS 支持 process-loopback（Win10 build ≥ 19041） | **Host PID process-loopback**（优先） | Render 进程内按游戏 PID 抓音频；注入侧 `enable_hook_audio_=0` |
| OS 不支持 | **进程内 Hook**（`tc_graphics.dll`） | WASAPI / XAudio2 / WaveOut / DirectSound 等；经 WS `/ipc` → `OnIpcAudioFrame` |

门控函数：`tc::IsProcessLoopbackCaptureSupported()`  
文件：`src/gr_deps/tc_capture_new/process_loopback_support.h`

### 1.3 明确不做的事

- Game-hook 且 OS 支持 PID loopback 时：**不**启动 Host 默认设备 mix loopback。
- 多实例 game-hook：**不允许**用默认设备混音冒充「游戏声音」。

---

## 2. 端到端数据流（当前生产）

```
游戏启动 + 注入成功
        │
        ▼
MsgObsInjected(pid)
        │
        ▼
RdApplication：worker 线程
  SetAudioLoopbackProcessId(pid)
  StartProviding()
        │
        ▼
WasAudioCapturePlugin
  pid != 0 → ProcessLoopbackAudioCapture  (原生 WASAPI)
  pid == 0 → MiniAudioCapture::Make()     (仅 desktop 默认设备)
        │
        ▼
ActivateAudioInterfaceAsync(VAD\Process_Loopback)
  + AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK
        │
        ▼
PCM → CaptureAudioFrame → Encode → WebRTC / WS
```

注入 boot 同步：

```
PrepareGameHookBoot / AppManagerWinImpl
  enable_hook_audio_ = process_loopback_ok ? 0 : 1
```

---

## 3. 问题排查结论（为何一度没声音）

### 3.1 UI 线程启动 PID 采集

**现象**：有视频无音频；`StartProviding` 卡住约 20s 后失败。

**原因**：`ActivateAudioInterfaceAsync` 在 UI/消息线程上调用，异步完成回调无法正常推进。

**修复**：与 desktop MiniAudio 一样，放到 worker 线程（`Thread::MakeOnceTask`）再 `StartProviding`。  
文件：`src/gr_render/rd_app.cpp`（`MsgObsInjected` 监听）。

### 3.2 MiniAudio PID loopback 在 Win32 Desktop 上失败

**现象**：`ma_device_init` → `MA_INVALID_ARGS (-2)`。

**根因（源码级）**：vendored miniaudio（约 v0.11.25）在 **Desktop** 路径里对 process-loopback 调用：

```text
IMMDeviceEnumerator::GetDevice(L"VAD\\Process_Loopback")
```

`VAD\Process_Loopback` **不是**合法 MMDevice id → `E_INVALIDARG (0x80070057)`。

正确 API：

```text
ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, ...)
```

上游只在 **UWP**（`MA_WIN32_UWP`）接了 `ActivateAudioInterfaceAsync`；Desktop 未接。

**对照**：

| 路径 | 结果 |
|------|------|
| A) `IMMDevice::GetDevice("VAD\\Process_Loopback")` | FAIL `0x80070057`（原 MiniAudio Desktop） |
| B) `ActivateAudioInterfaceAsync`（原生 WASAPI / MS Application Loopback） | OK |
| C) 打补丁后的 `MiniAudioCapture::MakeForProcess` | Start OK，有 PCM callback |

### 3.3 「存 WAV」用的不是 MiniAudio

独立自测 `test_game_audio_loopback` / 生产 `ProcessLoopbackAudioCapture` 均为**原生 WASAPI**。  
MiniAudio 的默认设备 loopback、以及（未打补丁前）PID loopback，是另一条实现。

---

## 4. 生产路径：原生 WASAPI PID Loopback

### 4.1 新增实现

| 文件 | 作用 |
|------|------|
| `process_loopback_audio_capture.h/.cpp` | Host 侧 Application Audio Capture；对齐 `test_game_audio_loopback.cpp` |
| `was_audio_capture_plugin.cpp` | `pid != 0` 走 `ProcessLoopbackAudioCapture`；`pid == 0` 仍走 MiniAudio 默认设备 |

要点：

- 在独立 capture 线程上 `CoInitializeEx` + `ActivateAudioInterfaceAsync`
- 在 `ActivateCompleted` 里 `Initialize`（与 MS 样例一致）
- PCM16 / 44100（实现内约定；与插件 format callback 对齐）
### 4.2 Plugin 接口扩展

`GrDataProviderPlugin` / `WasAudioCapturePlugin`：

- `SetAudioLoopbackProcessId(pid)`
- `GetAudioLoopbackProcessId()`
- `IsProviding()`
- `GetLastStartError()`
- `provide_mu_` 保护 Start/Stop

版本：`WasAudioCapturePlugin` → **1.3.0** (code 130)。

### 4.3 `rd_app.cpp` 行为

1. **Desktop**：启动即 worker 上 `StartProviding()`（默认设备 loopback）。
2. **Game-hook + OS 支持 PID loopback**：延迟到 `MsgObsInjected`；worker 上 Stop+Start。
3. **Game-hook + OS 不支持**：不启 Host mix；仅进程内 hook。
4. `OnIpcAudioFrame`：IPC 音频进同一条 `CaptureAudioFrame` bus。
5. `CaptureAudioFrame`：增加节流日志（无 peer drop / encode）。

---

## 5. MiniAudio 补丁（已验证，生产未切换）

生产仍用 WASAPI。MiniAudio PID 路径已修好，可用测试程序验证，待 soak 后再考虑切换。

### 5.1 补丁文件

| 文件 | 改动 |
|------|------|
| `third_party/miniaudio/miniaudio.h` | Desktop + process-loopback 时改调 `ma_godsk_get_IAudioClient_process_loopback`；`pDeviceInterface == NULL` 时合成设备名；避免被默认设备名覆盖 |
| `third_party/miniaudio/miniaudio_desktop_process_loopback.cpp` | `ActivateAudioInterfaceAsync` 实现（`extern "C"`） |
| `miniaudio_audio_capture.cpp` | `MakeForProcess(pid)`；`loopbackProcessID`；诊断日志 |
| `tests/test_miniaudio_pid_loopback.cpp` | A/B/C 对照：IMMDevice vs ActivateAsync vs MiniAudio |
| `scripts/build_miniaudio_pid_test.bat` | 配置并编译上述测试与插件 |

### 5.2 验证命令

```bat
scripts\build_miniaudio_pid_test.bat

rem 先启动会出声的游戏，再：
build_official\src\gr_render\plugins\was_audio_capture\test_miniaudio_pid_loopback.exe <pid>
```

期望：

- `[1] IMMDevice::GetDevice` → FAIL（说明原 bug 仍可复现）
- `[2] ActivateAudioInterfaceAsync` → OK
- `[3] MiniAudio Start() => 0` → WORKS；有 bytes；游戏有声时 peak > 0

### 5.3 切换生产的前置条件

- 有真实游戏声音的 soak（不只是静音 callback）
- 重连 / 换 PID / Stop+Start 稳定
- 再改 `was_audio_capture_plugin.cpp`：`pid != 0` 改为 `MiniAudioCapture::MakeForProcess`

---

## 6. 进程内 Hook 路径（OS 无 PID loopback 时）

当 `enable_hook_audio_ = 1` 时，`tc_graphics.dll` 内启用音频 hook。主要新增/改动：

| 区域 | 文件（节选） |
|------|----------------|
| 编排 | `hk_audio/HookCoreApi.*`、`AudioShare.*`、`AudioMixer.*` |
| API Hook | `HookXAudio2` / `HookWaveOut` / `HookDirectSound` / `InProcessLoopbackCapture` |
| 注入开关 | `graphics-hook.cpp`、`hook_manager.*`、`PrepareGameHookBoot` |
| IPC | `ws_ipc_client.cpp` → host `ws_ipc_router.cpp` → `OnIpcAudioFrame` |
| 自测 | `test_hook_audio_wav.cpp`、`test_game_audio_loopback.cpp`、`test_wasapi_tone.cpp` |

WAV 调试：`AudioShare` 默认 `write_wav_ = false`；WAV 写盘逻辑可按需打开（`#if 0` / flag）。  
**WAV 落盘走的是 hook/原生 PCM，不是 MiniAudio。**

---

## 7. 文件变更清单（按模块）

### 7.1 Host 音频插件（核心生产）

```
src/gr_render/plugins/was_audio_capture/
  was_audio_capture_plugin.{h,cpp}          # PID / 默认设备分支；版本 1.3.0
  process_loopback_audio_capture.{h,cpp}    # 新增：原生 WASAPI PID
  miniaudio_audio_capture.{h,cpp}           # MakeForProcess + 日志
  third_party/miniaudio/miniaudio.h          # Desktop process-loopback 补丁
  third_party/miniaudio/miniaudio_desktop_process_loopback.cpp  # 新增
  tests/test_miniaudio_pid_loopback.cpp     # 新增
  CMakeLists.txt                            # 链接 ole32/mmdevapi；新 target
```

### 7.2 Render 应用

```
src/gr_render/rd_app.cpp / rd_app.h
  - MsgObsInjected → worker StartProviding
  - game-hook 延迟启音频
  - OnIpcAudioFrame
  - boot enable_hook_audio_
src/gr_render/plugin_interface/gr_data_provider_plugin.h
  - SetAudioLoopbackProcessId / IsProviding / GetLastStartError
src/gr_render/app/win/app_manager_win.cpp
  - inject_params.enable_hook_audio 与 boot 对齐
src/gr_render/network/ws_ipc_router.cpp
  - IPC 音频帧转发
```

### 7.3 能力探测

```
src/gr_deps/tc_capture_new/process_loopback_support.h
  - IsProcessLoopbackCaptureSupported()  // build >= 19041
```

### 7.4 进程内 Hook（fallback）

```
src/gr_render/hook_capture/win/hk_audio/*     # AudioShare / Mixer / 各 API Hook
src/gr_render/hook_capture/win/hk_obs/*       # hook_manager、graphics-hook、测试 exe
```

### 7.5 脚本

```
scripts/start_render_hook.ps1 / .bat          # 仅启 Render（端口 32000 等）
scripts/build_miniaudio_pid_test.bat          # 编 MiniAudio PID 诊断
```

---

## 8. 本地联调

### 8.1 启 Render（game-hook）

编辑 `scripts/start_render_hook.ps1` 中的 `$GamePath`，然后：

```bat
scripts\start_render_hook.bat
```

浏览器：

```
http://127.0.0.1:32000/web_client/?deviceId=debug1
```

### 8.2 日志

| 日志 | 路径 |
|------|------|
| Host Render | `C:\Users\Public\GoDesk\gr_logs\godesk_render_32000.log` |

Host 侧关键关键字：

```
PrepareGameHookBoot ... enable_hook_audio=
MsgObsInjected: schedule PID process-loopback
PID audio worker: StartProviding OK
CaptureAudioFrame→encode
[ProcessLoopback] capturing pid=
```

### 8.3 构建注意

- 工具链：VS 2022/2026 + Ninja `build_official`（见 `build_official.bat`）。
- `collect_dist` 若因目录锁定失败，可能导致 `dist` 缺 `web_client` / plugins → 浏览器 `ERR_CONNECTION_REFUSED`。勿在占用 `dist` 时强清；优先增量拷贝 exe/dll。

### 8.4 原生 WASAPI 自测（存 WAV）

```bat
rem 构建后（target 见 hk_obs/CMakeLists.txt）
test_game_audio_loopback.exe <pid>
```

输出示例：`hook_audio_game_pid.wav` —— **原生 WASAPI，非 MiniAudio**。

---

## 9. 决策备忘

| 项 | 当前决策 |
|----|----------|
| 生产 PID 采集 | `ProcessLoopbackAudioCapture`（原生 WASAPI） |
| MiniAudio PID | 已补丁 + 测试通过；**未**接生产 |
| 默认设备 mix | 仅 desktop；game-hook 禁用 |
| 进程内 hook | 仅 OS 无 process-loopback 时启用 |
| WAV dump | 默认关 |

---

## 10. 无头验证（2026-08-07）

用 `scripts/cdp_webrtc_audio_diag.mjs`（Chrome headless + WebRTC stats）分别验证：

| 模式 | 切换方式 | 结果 |
|------|----------|------|
| PID process-loopback | 默认（`prefer_pid_loopback=true`） | **PASS** heard≈−16.5 dB，~26 kbps |
| In-process hook | `GODESK_FORCE_HOOK_AUDIO=1` | **PASS** heard≈−16 dB，~28 kbps |

Hook 路径曾失败的根因：`plugin_net_ws` `/ipc` 只转发 `kCaptureVideoFrame`，静默丢弃音频。已改为同时转发 `IpcCaptureAudioFrame` → `GrPluginRawAudioFrameEvent`。

复测命令摘要：

```bat
rem PID
scripts\start_render_hook.bat
set RENDER_PORT=32000& set AUDIO_MODE=pid-loopback& set SAMPLE_SECONDS=12
node scripts\cdp_webrtc_audio_diag.mjs

rem Hook
set GODESK_FORCE_HOOK_AUDIO=1
scripts\start_render_hook.bat
set AUDIO_MODE=hook& set CDP_PORT=9226& set SAMPLE_SECONDS=15
node scripts\cdp_webrtc_audio_diag.mjs
```

## 11. 后续可选

1. Soak 通过后，生产切换 `MiniAudioCapture::MakeForProcess`（或保留 WASAPI 双实现可配置）。
2. 将 GoDesk MiniAudio Desktop 补丁整理成可向上游贡献的 patch / issue。
3. 进程内 hook 路径在无 19041 机器上做完整听感验收。

---

## 12. 一句话总结

Game-hook 音频在 Win10 19041+ 上走 **Host 原生 WASAPI PID process-loopback**（worker 线程启动）；MiniAudio 曾经因 Desktop 误用 `IMMDevice::GetDevice("VAD\\Process_Loopback")` 失败，现已补丁并通过 `test_miniaudio_pid_loopback`，但生产暂不切换。更老的系统（或 `GODESK_FORCE_HOOK_AUDIO=1`）回落到进程内音频 Hook；两条路径均已无头验证有声。
