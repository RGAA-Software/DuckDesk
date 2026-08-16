# 输入/采集/渲染全链路延迟插桩方案

> 目标：定位「拖动窗口不跟手、手感比网易 UU 慢 3–5 帧，但秒表截图只差 1–2 帧」的根因。
> 本文是插桩方案与观测口径；实现以 `[LAT-*]` 日志标签落点，全部为**加性打点**（只计时+打日志），
> 不改变任何采集/编码/传输/重放逻辑。

## 0. 结论性前提（已确认，勿重复排查）

- 客户端硬解路径为 **D3D11VA**（`thunder_sdk.cpp:160-184`），解码非瓶颈。
- 客户端渲染走 **D3D11 `Present(0,0)`**（`d3d11_render_manager.cpp:387`），即无 VSync 等待；
  `setSwapInterval(1)` 只在 OpenGL 回退路径（`ct_main_ws.cpp:429`），当前不生效。
- **UDP pacing（80Mbps）不是原因**：目标环境是百兆网，80Mbps 已接近线速，
  每帧只多 1–2.5ms（0.05–0.15 帧）。不调整 `kRateControlBitsPerSec`。
- `AcquireNextFrame(wait_time)` 的 `wait_time` 是**超时上限**（有变化立刻返回），不是固定睡眠；
  真正的固定节奏来自采集成功后的 `DelayBySleep` 配速睡眠（`dda_capture.cpp:417-420`）。
- 默认采集/编码 fps = 60（`ct_settings.h:63` 默认 `kGame`）；`kWork` 模式会把采集压到 30fps
  （`plugin_net_event_router.cpp:585-589`），**必须先确认运行在 kGame**。

## 1. 问题模型

「秒表截图」只测视频链路：`采集 → 编码 → 传输 → 解码 → 渲染`（差 1–2 帧）。

「拖动窗口手感」测完整回环：`鼠标事件 → 客户端发送 → 网络 → 被控端重放 → DWM 合成器刷新 → 再走一遍视频链路`（差 3–5 帧）。

两者之差（2–3 帧，约 33–50ms）集中在**输入回环**，是本次插桩重点。

## 2. 打点总表

统一标签：`[LAT-input]`（输入回环）、`[LAT-capture]`（采集节奏）、`[LAT-render]`（解码→提交）。
所有时间戳用**单调时钟**（`tc::TimeUtil::GetCurrentTimestamp()`，项目内即 steady/单调毫秒）。

| 段 | 打点对 | 目标值 | 嫌疑 |
|---|---|---|---|
| 客户端 UI→入队 | C1→C2 | <1ms | — |
| 客户端队列排队 | C2→C3 | <2ms（拖动时） | **三级队列+忙等背压** |
| 输入网络（跨机） | C3→H1（回显） | <1ms | 待回显对时实现 |
| 被控端收包→重放 | H1→H2 | <1ms | 队列跳转 |
| SendInput 注入 | H3 | <0.5ms | **相对+绝对双重注入** |
| 重放→DWM 出新帧 | H3→G1 | 0–16.6ms | DWM VSync 硬开销 |
| 采集出帧节奏 | G1/G2 | ~16ms（60fps） | **30fps 或配速睡眠** |
| 编码队列 | enc diag `backlog` | ≈0 | 复用现有诊断 |
| 解码→提交 | R1 | <2ms | 硬解基本排除 |
| 网络丢包 | FEC `loss` | ≈0 | 已排除 |

## 3. 各打点实现位置

### 3.1 客户端输入回环（`src/px_client/front_render/ct_video_widget.cpp`）

- **C1**：`OnMouseMoveEvent`（约 83 行）入口记 `ts_qt`。
- **C2**：`SendMouseEvent`（约 321 行）构造完 msg、post 进 `evt_cache_thread_` 之前记 `ts_enqueue`。
- **C3**：`evt_cache_thread_` lambda 内、`sdk_->PostMediaMessage(buffer)` 前记 `ts_net_send`；
  并在忙等（`queuing_count > 16` 的 `DelayBySleep(1)` 循环，约 382-388 行）进入/退出各记一次。
- 输出：`C3-C2` = 三级队列排队；`C2-C1` = UI 线程内耗时；忙等轮数。

### 3.2 被控端输入重放（`src/px_render/plugins/event_replayer/`）

- **H1**：`event_replayer_plugin.cpp` `ProcessMouseEvent`（约 81-86 行）入口记 `host_recv_ms`。
- **H2**：`win_event_replayer.cpp` `HandleMouseEvent`（约 318 行）入口记 `ts_replay_beg`。
- **H3**：`win_event_replayer.cpp` `SendMouseEvent`（约 418 行）内，`WinSendEvent` 前后计时；
  并统计 `InjectServerRelFromAbs`（约 247-316 行）单次事件拆成的 `SendInput` 次数。
- 输出：`H2-H1` = 收包到重放器耗时；`H3` = 注入耗时 + 注入次数。

### 3.3 被控端采集节奏（`src/px_render/plugins/dda_capture/dda_capture.cpp`）

- **G1**：`Capture()`（约 300 行）循环体：循环开始、`AcquireNextFrame` 返回、`OnCaptureFrame` 三处计时。
- **G2**：`capture_gaps_`（约 349-353 行已收集）周期性（5s）输出 min/avg/max/P99。
- 输出：`AcquireNextFrame` 立刻返回 vs 超时比例；采集帧间隔分布（中位≈16ms 为 60fps，≈33ms 为 30fps）。

### 3.4 客户端解码/渲染（`[LAT-decode]` / `[LAT-render]`）

- **解码耗时**：`src/px_deps/px_client_sdk_new/thunder_sdk.cpp` 里 `video_decoder->Decode()` 前后计时，每 5s 输出 `[LAT-decode] frames/avg_us/max_us`（D3D11VA 硬解）。
- **渲染耗时**：`src/px_client/front_render/d3d11/ct_d3d11_video_widget.cpp` 的 `RefreshImage()`（纹理上传 + Draw + `Present(0,0)`）起止计时，每 5s 输出 `[LAT-render] frames/avg_us/max_us`。
- 两者相加即「解码完成 → 上屏提交」的客户端视频尾段。`Present(0,0)` 本身不等 VSync，VSync 等待是另一段固有的 0–16.6ms。

### 3.5 复用现有诊断（零改动）

- 编码：`encoder_thread.cpp` `EncThreadDiag`（每 5s 的 `enc diag` 行：`in_fps/backlog/enc_wait_max`）。
- 传输：`udp_plugin.cpp` `AdjustFecWindow`（每 5s 的 `loss_rate/recovered/short_writes`）。

## 4. 跨机时钟对齐（后续，需改协议，本次不实现）

输入回环是「客户端→网络→被控端」三段，两端时钟不同步，不能直接相减。用回显法：

1. 客户端在 `MouseEvent.timestamp` 写 `client_send_ms`（单调时钟）。
2. 被控端记录 `host_recv_ms` 与 `host_process_us`（收包→`SendInput` 完成）。
3. 被控端把 `(client_send_ms, host_process_us)` 塞进下一条 `OnHeartBeat` 回客户端
   （机制现成，`sdk_net_client.cpp:322-327` 已用 `hb.timestamp()` 估网络延迟）。
4. 客户端算 `input_rtt = now - client_send_ms - host_process_us`，无需对时。

> 注意：`ct_video_widget.cpp:340` 当前 `set_timestamp(cur_time)` 的 `GetCurrentTime()`
> 需先确认是单调时钟；非单调时钟要先换源。

## 5. 部署到 10.0.0.70（固定流程，见 udp_gamestream_channel_state.md §7）

- 只改 render 插件时，仅重编对应目标并覆盖远端 `px_plugins\`，不必全量 `build_client.bat`。
- 覆盖前必须先杀 `px_render.exe`（dll 被占用会静默失败）。
- 日志：`\\10.0.0.70\C$\Users\Public\GoDesk\px_logs\`（`plugin_dda_capture.dll.log`、
  `plugin_event_replayer.dll.log`、`godesk_render_20371.log`）。

## 6. 观测执行顺序

1. 抓日志确认 60fps / DDA / NVENC / `backlog≈0` / `loss≈0`（阶段 0）。
2. 拖动窗口 10–20s，抓 `[LAT-*]` 行，先看 `C3-C2`（客户端队列）与 `H3`（注入）与 `G2`（采集节奏）。
3. 用手机慢动作（240fps）对拍「鼠标动→远端窗口动」端到端帧数，与 UU 同场景对比。
4. 有数据后再决定：简化三级队列忙等 / 键鼠改走 UDP 控制通道 / 简化相对+绝对双重注入。
