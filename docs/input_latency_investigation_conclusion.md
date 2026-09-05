# 输入延迟 / 手感调查总结

> 记录时间：2026-08-14。
> 问题：**拖动窗口不跟手，比网易 UU 慢 1–2 帧（秒表 + 光标距离实测，这是事实）。**
> 测试环境：观看端本地 Windows client（Qt 套壳 + D3D11），被控端 10.0.0.70（i7-13700KF + RTX 3060，无头/虚拟显示，双屏）。

## 0. 最终结论

**整条链路每一段都被量过，全部健康，没有软件瓶颈。** 采集/编码/传输/解码/渲染/输入回环每一段都是毫秒级甚至微秒级。

**但我们确实比 UU 慢 1–2 帧（事实）。** 这 1–2 帧不在任何单一环节，而是**整条视频链路端到端串联 + 两处 60Hz 垂直同步节拍**。截至本次调查结束，**尚未用数据把它精确钉到具体某一段**。

## 1. 关键发现：用户「光标距离」= 端到端视频延迟的尺子

用户判断标准（非常准确）：拖动窗口时，被控端的光标（在视频画面里，叫「残影光标」）与客户端本地光标（0 延迟跟手）之间会拉开距离。**这个距离 = 整条视频链路的端到端延迟**（被控端画面经过采集→编码→传输→解码→渲染，滞后多少，残影就落后多少）。

UU/向日葵的这个距离很小 = 它们端到端延迟比我们低 1–2 帧。**这不是双光标的视觉 bug，是实打实的延迟差。**

## 2. 逐段实测（全部健康）

| 环节 | 实测 | 判定 |
|---|---|---|
| 客户端输入队列 | 18–24µs，busywait=0 | ✅ |
| 输入网络往返（心跳 RTT，与鼠标同走 WS） | 0–3ms | ✅ |
| 被控端注入 SendInput | ~1ms | ✅ |
| 采集（有内容变化时） | 60fps，帧间隔 16.7ms，拷贝 45µs | ✅ |
| 编码 | task_avg 2–3ms，backlog=1 | ✅ |
| UDP 传输 | 0 丢包、0 短写 | ✅ |
| 解码（D3D11VA 硬解） | 0.3–0.4ms | ✅ |
| 渲染（上传+Draw+Present） | 0.3–0.6ms | ✅ |

## 3. 垂直同步（VSync）结论

- **UU/向日葵左右快速拖动时画面有横向撕裂** = 它们上屏时**不垂直同步、直接提交帧**（允许撕裂），用画面撕裂换掉约一帧的 VSync 等待。这是它们快 1 帧左右的来源之一。
- 我们项目不撕裂（平滑）= 客户端上屏这一步实际上被垂直同步锁住，白等一个 60Hz 节拍（0–16.6ms）。
- 曾尝试给客户端上屏加 `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` + `Present(..., DXGI_PRESENT_ALLOW_TEARING)` 对齐 UU 做法，但**未生效**（Qt 的 `showFullScreen()` 未让 DXGI 认可为「无边框全屏」，撕裂没出现、延迟也没省）。

## 4. 排查过程中走过的弯路（记录，避免重犯）

1. 怀疑被控端机器/合成器 → 错（同机 UU 流畅）。
2. 怀疑多路远程软件竞争（AnyDesk/GameViewer/QuickDesk/ToDesk）→ 错（只是安装，未连接）。
3. 怀疑「拖动时采集崩到 10fps」→ 错。**用程序化窗口移动验证：窗口一动，采集立刻从 10fps 恢复到 42–46fps 并忠实跟随**。之前的 10fps 是桌面静止时 DDA 的正常超时（没变化就不出帧）。
4. 建议 GDI 采集对照 → 错（GDI 是 BitBlt 那套，延迟比 DDA 高，是基本常识性错误）。
5. `ShowCursor(FALSE)` 隐藏被控端光标 → 前提错（UU 不隐藏光标；那「距离」就是延迟本身，隐藏只是掩盖症状），已撤。

## 5. 本次代码改动清单

### 保留的改动

| 文件 | 改动 | 性质 |
|---|---|---|
| `src/px_render/plugins/dda_capture/dda_capture.cpp` | `target_duration` 向上取整；`ReleaseFrame` 改标准协议（拷贝后立即释放）；移除配速睡眠；`[LAT-capture]` 打点 | 正确性修正 + 插桩 |
| `src/px_render/hook_capture/win/desktop_capture/dda_capture.cpp` | 同步修 `target_duration` 截断 | 正确性修正 |
| `src/px_render/plugins/event_replayer/win_event_replayer.cpp` | `[LAT-input]` 注入计时 + SendInput 计数 | 插桩 |
| `src/px_client/front_render/ct_video_widget.cpp` | `[LAT-input]` 客户端队列计时 + 写入鼠标发送时刻 | 插桩 |
| `src/px_deps/px_client_sdk/thunder_sdk.{h,cpp}` | `[LAT-decode]`、`[LAT-roundtrip]` 打点 | 插桩 |
| `src/px_client/front_render/d3d11/ct_d3d11_video_widget.cpp` | `[LAT-render]` 渲染计时 | 插桩 |
| `src/px_deps/px_client_sdk/sdk_net_client.cpp` | `[LAT-net]` 心跳 RTT 日志 | 插桩 |
| `src/px_deps/px_common/process_util.{h,cpp}` + `src/px_render/rd_main.cpp` | 新增 `PinToPerformanceCores()`，render 钉大核 | 优化（实测无太大改善，保留） |
| `src/px_client/front_render/d3d11/d3d11_render_manager.cpp` | `FLIP_SEQUENTIAL`→`FLIP_DISCARD`；`SetMaximumFrameLatency(1)` | 边际优化（保留） |
| `tests/dxgi_capture_probe.cpp` | 探针加 60s/分窗口/区分 AccumulatedFrames | 诊断工具 |
| `docs/input_latency_instrumentation_plan.md` | 插桩方案文档 | 文档 |

### 已撤销的改动

- `ShowCursor(FALSE/TRUE)`（隐藏/恢复被控端光标）——前提错误，已撤。
- `DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING` + `Present(..., DXGI_PRESENT_ALLOW_TEARING)`——Qt 全屏下未生效，已撤。

## 6. 剩余未知与下一步

**尚未定位**：那 1–2 帧具体落在「采集→编码→发送」还是「接收→解码→上屏」哪一段。之前每段测的是**平均耗时**，没测**同一帧穿过整条管线的端到端时间戳**。

**唯一能一锤定音的做法**：给同一帧打贯穿时间戳（被控端采集时刻 → 编码完 → UDP 发出 → 客户端收到 → 解码完 → Present），算出每段各占多少毫秒。这样 1–2 帧落在哪一目了然。

**同时建议**：同机对拍 UU 的「玻璃到玻璃」端到端延迟（同一秒表 + 高速相机或注入时间戳），确认 UU 到底快在「采集节拍」还是「上屏节拍」，再决定针对性改。
