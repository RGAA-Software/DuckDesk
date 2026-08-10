# RTC Local 多屏混流问题与多屏同显方案

日期: 2026-08-10
状态: 已完成(2026-08-10 晚)——多 track/硬解 sink/datachannel 洪水修复/音频 sink 全部落地并验证,
安装包 GoDesk_3.3.13_Official_Setup.exe 已出(output/build_official/3.3.13/)

## 实施结果摘要(2026-08-10 晚)

已全部落地并通过测试:

- **render 多 track**: `rtc_server.cpp` 按 offer 的 video m-line 数建每屏一条 track
  (`video_track_{i}` / stream `godesk_media_{i}`),单 m-line(旧客户端/web)回退 legacy 单动态 track;
  信令应答带 `monitors`(name/宽高/虚拟桌面坐标)。
- **多 track 自动全屏采集**: 新增 `RtcLocalPlugin::EnableAllMonitorCapture()`,多 track 会话
  建立时自动切全屏采集——不再依赖客户端 UI 发 `SwitchMonitor("all")`(`split_windows` 关也能出全屏帧)。
- **客户端 encoded sink**: offer 4 路 video(`AddTransceiver` kRecvOnly)、null decoder factory、
  `AddEncodedSink` 取 AnnexB H264,合成 kVideoFrame proto(mon_name/坐标/extra="rtc_synth")
  注入既有每屏解码链(FFmpeg 硬解优先),无 app 层 ack。
- **重大 bug 修复 — datachannel 帧洪水假过滤**: `IsMediaFrameMessage` 原按
  `type` 是 field 1(tag 0x08)窥探 wire,实际 `tc.Message.type` 是 **field 10(tag 0x50)**,
  过滤器从未命中,kVideoFrame/kAudioFrame 一直以 ~9Mbps 灌 media datachannel
  (这正是 web 端"帧率低+不跟手"的根因)。已改为按 wire 格式逐字段扫描到 field 10 再判型,
  本地与 web 端均验证洪水消失。
- **测试结果**: 本机单屏(600378210)序列干净、解码正常;.70 双屏(990405157)两 track
  各自出关键帧、两窗(split_windows=true)解码无误码、无 IDR 风暴;
  web_client(headless Chrome CDP)legacy 单 track 回归通过(0 丢包、无 freeze)。
- **音频 sink(第二轮,已落地)**: `rtc_audio_sink.h/.cpp` 实现 `webrtc::AudioTrackSinkInterface`
  (注意不是 `AudioSinkInterface`——audio track 的 `AddSink` 只接受前者),挂到远端 audio track
  拿 webrtc 内置 opus 解码后的 PCM(实测 48kHz/单声道/16bit),经
  `RtcConnection::OnAudioTrack` → `WebRtcLocalConnection::SetOnAudioDataCallback` →
  `NetClient::SetOnRtcLocalAudioCallback` → `thunder_sdk`(PostAudioTask)→
  `ct_base_workspace` 现有 `AudioPlayer`(SDL) 播放。本机与 .70 均验证
  "Init audio player, freq: 48000, channels: 1" 全链打通(dll 内 dummy ADM 本无声,此为必需)。
  另修了 `test_webrtc_local.bat` 的 `--audio=true`(`toInt()` 恒为 0,须用 `--audio=1`)。

历史方案与根因分析保留如下。

---

# 原方案(第三版)

## 问题现象

用 RTC local（强制 RTC）连接双屏设备（如 D-70 / 990405157 / 10.0.0.70，DISPLAY1 + DISPLAY2）时，
单窗口里能"同时看到两路"画面——实际是窗口内容在两块屏之间快速交替，伴随闪屏。

## 根因（已用日志证实）

一条 RTC 连接只有**一路 video track**，但 render 侧把两块屏的帧全部混进了这路 track：

1. DDA 采集同时采集所有显示器，每屏每帧回调 `OnRawVideoFrameSharedTexture`。
2. `rtc_local_plugin.cpp:306` 不做屏幕过滤，把所有屏的帧全部喂给每条连接唯一的 `video_source_`。
3. `rtc_video_encoder.cpp:101` 按 notify 帧携带的 `mon_name` 去该屏的编码缓存取帧
   → 同一条 RTP 流里两屏编码帧交替。
4. 客户端单窗口（`split windows: false`）时 SDK 对 RTC 帧一律报 `mon_index=0`
   → 全部帧渲进主窗口，两屏内容来回翻。

日志铁证（2026-08-10 16:10 会话），每秒约 10 次：

```
rtc_server.cpp:442       Capturing monitor switched: \\.\DISPLAY1 -> \\.\DISPLAY2, reset frame index baseline.
rtc_video_encoder.cpp:104 Encoder detected monitor switch ... wait IDR again.
```

代价: 码率持续烧在 IDR 上、画面闪跳、带宽按两屏合计消耗、键鼠监视器名来回跳。
附带发现: Windows 客户端 RTC local 没有声音（audio track 未挂 sink，kAudioFrame 被丢弃守卫拦截）。

## 关键背景结论（2026-08-10 多次调查汇总）

- **mon_index 仍在使用**,但只是 mon_name 的派生品: render 按 `monitors_info` 枚举顺序
  name→index,proto VideoFrame 同时带 mon_name(字段9)/mon_index(字段14);
  客户端 kSeparate 按 mon_index 路由分窗。权威 key 是 name,机制不用改。
- **libwebrtc 无 Windows 硬解**: 内置 webrtc.lib 的 H264 解码是 ffmpeg 软解
  (`h264_decoder_impl.h`),无 D3D11VA/NVDEC/MF;上游 webrtc 的 Windows 硬解在
  Chrome 浏览器层(media/),不在库里,重编也开不出来。
- **RTC local 不一定是局域网**(用户指出): render 可能在公网,因此 RTP 的
  BWE/NACK/jitter buffer 有价值,否决了"视频改走 datachannel proto"的第二版方案
  (SCTP 可靠有序会队头阻塞,弱网差)。
- **拿未解码视频流的公开接口**(用户指出,"录像用"的接口):
  `VideoTrackInterface::GetSource()` → `VideoTrackSourceInterface::AddEncodedSink(
  rtc::VideoSinkInterface<RecordableEncodedFrame>*)`(`api/media_stream_interface.h:151`)。
  远端 track 的 source 实现是 `pc/video_rtp_track_source.h`,本为录像/转发设计,
  每帧回调未解码码流(encoded_buffer/codec/is_key_frame/resolution/render_time),
  且 AddEncodedSink 会自动向上游请求关键帧(利于首帧)。
- **多 track 可行**: Unified Plan;`RTCOfferAnswerOptions.offer_to_receive_video` 是 int
  (peer_connection_interface.h:714),offer 可声明收 N 路 video;
  render 在协商前 `CreateVideoTrack(source, label)` + `AddTrack(track, {stream_id})`
  加完全部 track,answer 的 msid 自动生成;客户端 `receiver->stream_ids()` 可读。
- **客户端解码硬解链路现成**: `FFmpegVulkanDecoder` 出 `AV_PIX_FMT_VULKAN` AVFrame →
  `pl_vulkan->RenderFrame` 零拷贝直渲;解码器 `video_decoders_` 按 mon_name 分实例;
  ct_workspace 的 `support_vulkan_ && vulkan_av_frame_` 分支、kSeparate 分窗全部现成。

## 方案（全 RTP 架构）

```
render: 每屏一条 video track(每 track 独立帧源 → 混流/IDR 风暴根治,web 也受益)
  ↓ RTP(BWE/NACK/jitter buffer 全保留,公网友好)
客户端: ontrack → track->GetSource()->AddEncodedSink
  → RecordableEncodedFrame(未解码 H264,按 track 区分屏)
  → 注入现有 FFmpegVulkanDecoder 链路(按 mon_name 分实例)
  → pl_vulkan 零拷贝直渲;分窗/键鼠 mon_name/统计全自动正确
音频: audio track → AudioSinkInterface 拿 PCM(libwebrtc 内置 opus 解码)→ 现有播放器
```

### render 侧(net_rtc_local)

1. **per-monitor 帧源**: `mon_name → VideoTrackSourceImpl` map;
   `OnRawVideoFrameSharedTexture` 只把帧喂给对应屏的 source。
   这一步顺带根治混流和 IDR 风暴（原"方案一"),web_client 同步受益。
2. **多 track**: 连接建立时按当前采集屏数（上限 4）创建 video track,
   label = `video_track_{index}`,各自独立 stream id,协商前加完;
   track 0 = 当前选定屏（web offer 只有 1 条 video m-line,只协商到 track 0,
   保持 web 现有行为不变)。
3. `RtcSharedVideoEncoder` 逻辑基本不动: 每个 source 只喂单屏帧,
   现有"按 mon_name 消费对应屏编码缓存"逻辑天然正确,切屏逻辑休眠;
   factory 已为每 track 建独立 encoder 实例。
4. **信令增强**: `/alloc/local/rtc` 响应（或新增轻量查询）返回 monitors 列表
   （name + 分辨率 + index),客户端据此决定 offer 的 video m-line 数和
   track→mon_name 映射。

### 客户端（tc_webrtc_client / tc_client_sdk_new / gr_client)

5. offer 固定声明 `offer_to_receive_video = 4`（上限）;**不需要**建连前拿 monitors——
   monitors 列表随 `/alloc/local/rtc` 应答一起返回，先 offer 后映射；旧 render
   只应答 1 条 track，多余 m-line 自动 inactive。
6. `OnAddTrack`: track id `video_track_{i}` 解析 index（旧 render 的单 track
   按到达顺序归 0);`track->GetSource()->AddEncodedSink` 挂编码帧 sink
   （挂载本身会触发一次关键帧请求，解码链首帧即为 IDR)。
7. 编码帧 sink(`RtcEncodedFrameSink`）收到 `RecordableEncodedFrame` 后，
   **合成与 relay/ws 完全相同的 kVideoFrame proto**（含 mon_name/mon_index/
   mon_left..bottom/frame_index/key/kNetH264/kI420)，经专用回调直接分发到
   `NetClient` 的 video_frame_cbk_——**不过 ParseMessage、不发 app 层 ack**
   (RTP 自带 NACK/PLI，每帧 ack 只会刷爆 media data channel)。
   每 track 独立 frame_index 计数；首帧之前丢弃非关键帧。
8. 注册 **null VideoDecoderFactory**(`RtcNullVideoDecoderFactory`，支持格式与
   OpenH264 template 一致，SDP 协商不变）：每帧立即回 2x2 黑帧，
   喂饱 VideoReceiveStream 的 frame buffer/timing，避免软解空转和 PLI 风暴。
9. 信令 `monitors` 数组含 name/width/height/**left/top/right/bottom**（虚拟桌面
   坐标，分屏布局与鼠标坐标映射必需）。
10. 旧 render（应答无 monitors）回退：单动态 track → ServerConfiguration 的
    capturing_monitor_name(`ThunderSdk` 经 `SetRtcLocalCapturingMonitorNameProvider`
    提供）;config 未到时丢帧等关键帧。
11. ~~**音频**(第二轮)~~ ✅: 远端 audio track `AddSink(AudioTrackSinkInterface)`
    拿解码后 PCM(libwebrtc 内置 opus 解码)→ 送入 ct_audio_player 现有播放器。
12. `OnRtcLocalVideoFrame` I420 软解路径退役（encoded cbk 存在时 `RtcVideoSink`
    不再挂，内置解码器已被 null factory 替换）。

### 不做的事

- 不动 web_client: 它继续走 RTP track + `<video>`(浏览器自己硬解）。
- 不动 `IsMediaFrameMessage` 守卫、不需要 `client=windows` 信令标识。
- 不重编 webrtc 库。

## 实施步骤

1. ~~render: per-monitor 帧源改造（OnRawVideoFrameSharedTexture 按屏路由）~~ ✅ 已编译
2. ~~render: 多 track 创建 + 信令返回 monitors 列表（含虚拟桌面坐标）~~ ✅ 已编译
3. ~~客户端: offer 4 路 video;OnAddTrack 挂 AddEncodedSink;null decoder factory~~ ✅ 已编译
4. ~~客户端: 编码帧→合成 kVideoFrame proto 分发（无 ack）;monitors 映射/回退~~ ✅ 已编译
   - ~~客户端: 音频 sink~~ ✅ 已验证(AudioTrackSinkInterface→PCM 48kHz mono→AudioPlayer)
5. ~~本机（10.0.0.16，单屏）测试~~ ✅ 通过（序列干净/解码正常/无 datachannel 洪水）
6. ~~双屏（10.0.0.70）测试~~ ✅ 通过（两 track 各出关键帧、split_windows 两窗、无 IDR 风暴;
   多 track 会话 render 自动 EnableAllMonitorCapture 全屏采集）
7. ~~web_client 回归（track 0 单屏、声音、键鼠、文件传输）~~ ✅ CDP 回归通过（0 丢包无 freeze;
   文件传输未单独复测,datachannel 路径未动）
8. ~~打安装包 + 提交~~ ✅ GoDesk_3.3.13_Official_Setup.exe 已出包

## 风险与待确认点

- `RecordableEncodedFrame::encoded_buffer()` 的 H264 码流格式（AnnexB 还是 AVCC
  长度前缀）——sink 已实现首帧头字节 dump（日志一次性输出前 8 字节），运行时确认，
  FFmpeg 侧按需转换。
- ~~null decoder 与 webrtc 视频管线的兼容性~~——采用"每帧立即回 2x2 黑帧"方案，
  frame buffer/timing/PLI 逻辑均有产出，风险已规避（待运行验证）。
- Unified Plan 下 answer 的 track↔m-line 顺序映射（track 添加顺序 = m-line 顺序）需验证。
  兜底：track id `video_track_{i}` 显式携带 index，不依赖到达顺序。
- ~~音频 sink 的采样率/声道格式与 ct_audio_player 的匹配~~——实测 webrtc 吐 48kHz/单声道/16bit,
  `AudioPlayer::Init(freq, channels)` 直接按回调参数初始化,本机与 .70 均验证通过。
- RtcConnection 是 dll 单例：重连时新 peer connection 建立后清空旧 encoded sinks
  （旧 pc 先释放、其 track/source 已销毁，再释放 sink，顺序安全）。

## 性能评估（2026-08-10 确认：不影响传输速度，双屏反而省带宽）

- AddEncodedSink 挂在接收端本地（jitter buffer 之后、解码器之前）,RTP 传输、
  BWE、NACK 全在它上游，传输路径不变;RecordableEncodedFrame 是引用计数 buffer,
  零拷贝，每帧一次虚函数调用，开销可忽略（该接口本为录像设计，录像与直播同跑）。
- 多 track 只多 RTP/RTCP 包头（千分之一量级）,各路共享同一 BWE;
  而当前混流每 ~100ms 触发一次 IDR 风暴（关键帧体积是 delta 帧几十倍）,
  改成每屏一 track 后 IDR 风暴消失，双屏总码率显著下降。
- **实现纪律**: sink 回调跑在 webrtc 解码队列线程上，回调里禁止做解码等重活，
  只准引用/拷贝 buffer 后抛到自己的解码线程，否则 jitter buffer 积压、延迟滚雪球。
- 自研硬解替代内置软解是纯客户端 CPU/GPU 的事，与传输无关。


## 测试环境备忘

- 远程机 10.0.0.70（D-70，双屏）凭据见 `tests/.remote_admin.md`（不入库）。
- render 日志: `\\10.0.0.70\C$\Users\Public\GoDesk\gr_logs\`，抓取脚本 `tests/_fetch_logs_70.bat`。
- 客户端日志: 本机 `C:\Users\Public\GoDesk\gr_logs\app.<deviceId>.log`。
- 本机测试脚本: `scripts\test_webrtc_local.bat 600378210 fNdnGBv2 127.0.0.1 20371`。
