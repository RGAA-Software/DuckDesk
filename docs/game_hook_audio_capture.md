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
- PCM16 / 48000 / 2ch（实现内约定；与插件 format callback 对齐）。原 44100 会被 Opus 编码器拒绝（`opus_encoder_create` → `OPUS_BAD_ARG`），Opus 传输路径必然无声；OBS 同样用 48000
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
6. `RdApplication::Exit()`：调 `audio_capture_plugin_->StopProviding()`，确保退出时采集线程与 COM 正确收尾。

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
| IPC | `ws_ipc_client.cpp` → host `net_ws/ws_server.cpp`（AddIpcRouter，仅 loopback）→ `OnIpcAudioFrame` |
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
src/gr_render/plugins/net_ws/ws_server.cpp
  - /ipc 路由（AddIpcRouter）：仅接受 loopback 连接；IPC 音/视频帧转发
  - 视频帧 wire 为定长 152B 纯 POD IpcCaptureVideoFrame（旧含 shared_ptr 的 blob 显式拒绝）
（原 src/gr_render/network/ws_ipc_router.cpp 为死代码，已删除）
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

> 注：本次验证时采集请求为 PCM16/44100，与 Opus 编码器不兼容（`OPUS_BAD_ARG`），Opus 传输路径实际无声（听到的未走 Opus）；本轮已改为 PCM16/48000/2ch 修复（见文末「2026-08-08 修复」）。

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

---

## 13. 2026-08-08 修复

### 13.1 PID 内录音频（was_audio_capture / process_loopback_audio_capture）

1. 采集请求格式 PCM16/44100 → **PCM16/48000/2ch**：44100 被 Opus 编码器拒绝（`opus_encoder_create` → `OPUS_BAD_ARG`），Opus 传输路径必然无声；OBS 也用 48000。
2. `AUDCLNT_BUFFERFLAGS_SILENT` 包不再丢弃，改推等长零 buffer，保持采样时钟连续（对齐 OBS）。
3. `ActivateAudioInterfaceAsync` 激活 handler 增加自引用 + `Cancel()`，修复超时/停止后 COM 回调打到已释放 handler 的 use-after-free；`WaitActivate` 改 100ms 分段等待、可响应停止。
4. 采集循环对 `AUDCLNT_E_DEVICE_INVALIDATED` 等致命错误退出线程并经 stop callback 通知插件层记录；瞬时错误仅节流日志。
5. capture 线程与 `rd_app` 的 PID audio worker 的 `CoInitializeEx` 均配对 `CoUninitialize`（MiniAudio 自己在线程内 init/uninit，不依赖调用线程残留 COM 状态）。
6. `RdApplication::Exit()` 现在调 `audio_capture_plugin_->StopProviding()`。
7. `ProcessLoopbackAudioCapture::Start()` 幂等（starting/running 重入返回 0），消除插件层误判失败 reset。
8. `Initialize` 后 `GetMixFormat` 校验实际格式非 PCM16 明确报错；帧字节数按实际 `nBlockAlign` 计算。

### 13.2 /ipc 安全与 wire 格式（net_ws / capture_message.h / d3d11/d3d12-capture）

1. `/ipc` 路由只接受 loopback（127.0.0.1 / ::1 / ::ffff:127.0.0.1）连接，非 loopback 立即关闭；此前绑 0.0.0.0 无鉴权，远程可推伪造帧并可收到广播下行的用户键鼠事件。server 本体仍 0.0.0.0 服务浏览器。
2. 视频帧 wire 格式改为定长 152 字节纯 POD `IpcCaptureVideoFrame`（magic=`GRCV` 0x47524356 + version=1 + pack(1)），Host 侧逐字段转换构造 `CaptureVideoFrame`，不再对含 `std::shared_ptr` 的结构整体 memcpy（远程可触发崩溃的 UB）；旧格式 blob 显式拒绝 + 限流日志；宽高 clamp 16..8192。**协议变更：Host render 与 `tc_graphics.dll` 必须同批部署。** 音频帧 `IpcCaptureAudioFrame` 本就是 POD 未变。
3. 死代码 `src/gr_render/network/ws_ipc_router.cpp/.h` 已删除（/ipc 实际由 `plugins/net_ws/ws_server.cpp` 的 `AddIpcRouter` 处理）。

### 13.3 注入鲁棒性（app_manager_win / injector / win_helper）

1. 注入挪到独立 worker 线程，`MsgTimer100` 只投递不阻塞；失败固定 100ms 重试不设上限（§13.6 调整，原指数退避/60 次上限已回退）；`ACCESS_DENIED`（游戏管理员权限）明确报错并持续重试。
2. 32 位游戏：注入前 `IsWow64Process` 检测，命中即 permanent failure（「暂不支持 32 位游戏」）；`inject-library.c` 增加纵深检查返回 `INJECT_ERROR_X86_TARGET_NOT_SUPPORTED(-5)`。
3. 游戏重启检测：injected 后每 1s 检查进程存活 + DLL 映射，连续 3 次失败清 `injected_` 并重走注入。
4. Steam 多进程：只选一个候选（优先有可见主窗口，否则最大 pid），成功即 break；补「injector 超时但 DLL 已映射」兜底。
5. `IsDllInjected` 对无对应位数 DLL 的目标快速返回 false；修 `sizeof(name)/sizeof(WCHAR)` 缓冲区 bug；`GetWindowThreadProcessId` 返回值校验。

### 13.4 进程内 hook 音频与 DLL 卸载（hk_audio / graphics-hook.cpp）

1. 完整卸载路径：`HookCoreApi::Shutdown` 恢复全部 vtable 槽（仅当槽仍指向本 DLL detour）+ 三个子 hook `DetourDetach`，幂等；`DLL_PROCESS_DETACH` 区分进程退出（`lpReserved != NULL` 直接返回）与 `FreeLibrary`（置 stop 标志 → 等线程 → Shutdown → free_hook；init 线程超时则跳过 teardown，宁可泄漏）。
2. `HookDirectSound` patch 竞态修复：origin 按 vtable 存 map，patch 序列全程持锁。
3. ~~Detour 注册全部线程~~ **已按实测回退**：全线程 `DetourUpdateThread` 在 commit 时 suspend 游戏全部线程，启动早期高概率挂死 UE4（实测游戏卡死无窗口）；改回单线程 `DetourUpdateThread(GetCurrentThread())`，接受纳秒级半补丁理论窗口。
4. **主动补钩（堵「注入前音频图已建好」的结构性缺口）**：利用 COM 类 vtable 进程内共享——
   - WASAPI：安装时 probe（`Initialize(SHARED, 0 flags)` + `GetService(IAudioRenderClient)`，从不 Start，用完 Release）→ 立即 `PatchRenderClientVtable`；不再只依赖游戏注入后再调 `GetService` 的懒路径。
   - XAudio2：watcher 启动时建临时引擎 + 临时 source voice（不建 mastering voice、不 Start）patch 共享 vtable。
   - DirectSound：attach 后自建 ~10ms secondary buffer（从不播放）patch `IDirectSoundBuffer` vtable。
   - waveOut 无等价补钩（winmm 查不到已开 handle 的格式），注入前已开 handle 的帧按格式未知丢弃（已知限制）。
   - 无头验证：hook 模式两轮 PASS heard=true；pid-loopback 模式 PASS（证明 probe 未静音游戏音频图）。
4. 多源抑制集中化：WASAPI 活跃窗口放宽到 1000ms，期间 XAudio2/WaveOut/DirectSound 来源帧直接丢弃 + 计数（AudioMixer 仍是拼接语义，真时间轴混音未做——保留为已知限制）。
5. 队列按字节限流 16MB 丢最旧 + 计数；XAudio2 单 buffer 按 ~1s 音频分块。
6. 格式未知/不支持（8/24/32-bit int、压缩）丢帧计数，不再硬编码 48k/f32 兜底；`WAVEFORMATEXTENSIBLE` 展开 SubFormat；`g_mixer` 改 atomic `shared_ptr`。
7. `InProcessLoopbackCapture` 目前是无调用者的死代码（保留注明）。

### 13.5 第二轮追加（2026-08-08）

1. 注入前已建 WASAPI render client 格式兜底：probe mix format 按 render client vtable 缓存（`g_probe_fmt_by_vtbl`），`Hook_ReleaseBuffer` 查不到 per-client 格式时按 vtable 兜底（`probe_fmt` 计数可观测），不再整局丢帧。
2. PID 采集致命错误有界自动重启：`IsFatalStop()` 区分致命退出与主动 Stop；重启 worker 延迟 2s 退避至 30s、连续失败 5 次放弃；目标 pid 已退出直接放弃；StopProviding/pid 变更取消挂起重启（generation 防竞态）。
3. /ipc token 鉴权 + boot ACL（校验与管道均已移除）：见 `game_hook_capture_plan.md` §9.1 / §9.3。
4. 验证：pid-loopback 与 hook 两种模式无头均 PASS heard=true。

### 13.6 注入重试策略调整（2026-08-08）

- 13.3 第 1 条中的「指数退避 100ms→5s、60 次上限」已按需求回退：失败后固定 100ms 重试、不设上限（尽快出画面优先），日志节流；worker 线程方案保留。gave_up 仅剩 32 位拒绝场景。
