# RTC Local 多屏混流问题与多屏同显方案

日期: 2026-08-10
状态: 已定位，待实现

## 问题现象

用 RTC local（强制 RTC）连接双屏设备（如 D-70 / 990405157 / 10.0.0.70，DISPLAY1 + DISPLAY2）时，
单窗口里能"同时看到两路"画面——实际是窗口内容在两块屏之间快速交替，伴随闪屏。

## 根因（已用日志证实）

一条 RTC 连接只有**一路 video track**，但 render 侧把两块屏的帧全部混进了这路 track：

1. DDA 采集同时采集所有显示器，每屏每帧回调 `OnRawVideoFrameSharedTexture`。
2. `src/gr_render/plugins/net_rtc_local/rtc_local_plugin.cpp:306` `OnRawVideoFrameSharedTexture`
   不做屏幕过滤，把所有屏的帧全部喂给每条 RTC 连接唯一的 `video_source_`。
3. 消费端 `src/gr_render/plugins/net_rtc_local/rtc_video_encoder.cpp:101` `RtcSharedVideoEncoder::Encode`
   按 notify 帧携带的 `mon_name` 去该屏的编码缓存取帧 → 同一条 RTP 流里两屏编码帧交替。
4. 客户端 `split windows: false` 时只有一个 game view;SDK 对 RTC 帧一律报 `mon_index=0`
   （`src/gr_deps/tc_client_sdk_new/thunder_sdk.cpp:397`）→ 全部帧渲进主窗口，两屏内容来回翻。

日志铁证（2026-08-10 16:10 会话，plugin_net_rtc_local.dll.log），每秒约 10 次：

```
rtc_server.cpp:442       Capturing monitor switched: \\.\DISPLAY1 -> \\.\DISPLAY2, reset frame index baseline.
rtc_video_encoder.cpp:104 Encoder detected monitor switch ... wait IDR again.
```

## 代价

- 每次交替都触发"等 IDR + 重置序号基线"：码率持续烧在关键帧上，画面闪/跳，带宽按两屏合计消耗。
- 键鼠事件的监视器名跟随画面交替来回跳（客户端日志 DISPLAY1 93 次 / DISPLAY2 242 次），
  坐标换算基准不稳定。

## 方案

### 方案一（短期修正，先做）：RTC 连接锁定单屏

行为对齐 relay 模式：

- render: `rtc_local_plugin.cpp` / `rtc_server.cpp` 只把"当前选定屏"的帧喂给 video source，
  其他屏的帧直接丢弃（编码缓存中非选定屏的帧也可跳过消费）。
- 选定屏来源：`ServerConfiguration.capturing_monitor_name`；客户端发 SwitchMonitor 消息时切换，
  切换时重置序号基线 + 等 IDR（现有切屏逻辑保留，只在真正切屏时触发一次）。
- 客户端无需改动。

### 方案二（独立特性，后做）：真·多屏同显（每屏一条 track)

- render: 每条 RTC 连接为每块屏创建独立 video track（用 msid 区分，如 `video_DISPLAY1` /
  `video_DISPLAY2`)，各自独立的 `RtcSharedVideoEncoder` 实例消费对应屏的编码缓存，
  各自独立的码率控制。
- SDP 协商:offer/answer 携带多条 m=video；客户端按 track 的 msid 识别屏。
- 客户端: `tc_webrtc_client` 支持多 track 回调（带 track/msid 标识）;
  `thunder_sdk.cpp OnRtcLocalVideoFrame` 按 msid 填真实 `mon_index_`;
  `ct_workspace.cpp` kSeparate 模式下现成的 `game_views_[mon_index]` 路由即可复用。
- 键鼠事件按各窗口自己的监视器名上报（现有逻辑天然支持）。

## 测试环境备忘

- 远程机 10.0.0.70（D-70，双屏）凭据见 `tests/.remote_admin.md`（不入库）。
- render 日志: `\\10.0.0.70\C$\Users\Public\GoDesk\gr_logs\`，抓取脚本 `tests/_fetch_logs_70.bat`。
- 客户端日志: 本机 `C:\Users\Public\GoDesk\gr_logs\app.<deviceId>.log`。
