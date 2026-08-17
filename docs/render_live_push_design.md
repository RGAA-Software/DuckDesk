# Render 实时推流方案（ZLM 中央服务器 + 全平台观看）

> 状态：待评审（v2 修订：双路全推，WebRTC 纳入 v1）
> 范围：render 端实时推流（直播），全平台观看（PC/Mac/iOS/Android/浏览器/电视）
> 与录像方案关系：推流 = 实时观看；录像查看 = 事后回看（见 `docs/cms_render_records_view_design.md`），两者互补、控制通道可共用。

---

## 1. 背景与目标

render 端已有完整编码管线（H264/H265 + Opus），录屏已落地（文件落 `C:\Users\Public\Pixels\px_render_records`）。新目标：**实时推流**，让任意平台（VLC/浏览器/iOS/Android/智能电视）随时观看某台 render 的实时画面。

约束与事实（代码依据）：

- render 与 CMS 无长连接、易死、可多开（录像方案已确认：稳定出口是 panel）
- render 静态链接完整 ffmpeg 8.1.1（avformat/avcodec/avutil/swresample），native AAC 编码器、**WHIP muxer** 均可用（openssl feature 已启用，WHIP 的 DTLS 依赖满足），opus 编码器在内
- 多 render 部署：CMS 在上层网段，render 在 NAT 后 → 只能 render **主动外连**
- 本地有 ZLMediaKit 最新源码（`D:\GoCloud\ZLMediaKit`）；vcpkg 的 zlmediakit port 版本旧（2024-09-29#1），**不作为集成依据**

## 2. 总体架构

```
N × render(推流端)                     CMS 机器 = ZLM(中央媒体服务器) + px_cms_server
  同一份编码帧(扇出双路):
  ┌─ H264/H265 + AAC(转码) ──RTMP──►  rtmp://zlm:1935/live/{device_id}
  └─ H264 + Opus(原生)      ──WHIP──►  http://zlm:8080/index/api/whip?app=live&stream={device_id}
                                         │ 按需转封装(零转码)
                                         ├─► HLS(m3u8)    → iOS/Android/电视/浏览器原生(延迟3-6s)
                                         ├─► HTTP-FLV     → PC 浏览器 flv.js(低延迟 ~1-2s)
                                         ├─► WebRTC       → 浏览器(<0.5s, 来自 WHIP 那路)
                                         ├─► RTSP         → VLC/PotPlayer
                                         └─► RTMP         → 桌面播放器/二次分发
```

- **推流端**：render 插件把同一份编码帧扇出成两路主动外连——RTMP(H264+AAC) 服务全容器协议，WHIP(H264+Opus) 服务 WebRTC。视频只编一次；音频 Opus 原生一路、转 AAC 一路。
- **媒体服务器**：ZLM 独立进程部署在 CMS 机器（与 px_cms_server 同机），一路进多路出（转封装零转码，见 §5.5）。
- **观看端**：全平台从 CMS 机器拉流——HLS 保底，FLV 低延迟，WebRTC 亚秒。

## 3. 为什么 ZLM 放 CMS 机器

| 维度 | ZLM 在 CMS 机器 | 内嵌 render / 每设备一个 |
|---|---|---|
| 跨网段 | ✅ render 主动外连推流（NAT 天然穿透） | ❌ 观看端连不到下层 render |
| 多 render | ✅ 一台收全部（stream=device_id 区分） | ❌ 多开冲突、每机一个浪费 |
| 观看端 | ✅ 内外网统一连 CMS 机器 | 仅内网/单机 |
| 与 CMS 协同 | ✅ 设备列表+流列表同机，web 拼 URL 最顺 | 分散 |
| render 可靠性 | ✅ render 崩只断自己那路，ZLM 不受影响 | ❌ 服务器随 render 生死 |

结论：**ZLM 独立进程部署在 CMS 机器；render 永远只做推流端。** 同网段观看端也统一走这台 ZLM（拓扑 1/2 在推流上完全统一，差异仅在网络性能）。

## 4. 多来源区分与拉流端处理

### 4.1 流命名：`stream = device_id`（全局唯一）

```
render A(device_id=074723054):  rtmp://zlm:1935/live/074723054          (RTMP 路)
                                http://zlm:8080/index/api/whip?app=live&stream=074723054  (WHIP 路)
render B(device_id=99aabbcc):   同上替换 stream
```

- 不用 IP/主机名（会变/冲突）；device_id 现有且全局唯一。
- 同 stream 名第二个推流：ZLM 默认踢掉旧连接（`on_publish` hook 可定制"拒绝/接管"），防止误推。
- 双路共存：RTMP 推流注册 rtmp schema 的 MediaSource，WHIP 推流注册 rtc schema 的 MediaSource，同名不同 schema 互不冲突；拉流按协议自然找到对应源。

### 4.2 拉流端三种方式

| 方式 | 说明 |
|---|---|
| A. 固定地址拼设备号（主用） | web 从 CMS 设备列表拿 device_id：HLS 拼 `http://zlm:8080/live/{device_id}/hls.m3u8` 给 `<video>`；WebRTC 调 `http://zlm:8080/index/api/webrtc?app=live&stream={device_id}&type=play`（ZLMRTCClient.js）；VLC 手填 `rtsp://zlm:8554/live/{device_id}` |
| B. 流列表 API（可选） | `GET http://zlm:8080/index/api/getMediaList` 返回活跃流，做"正在直播"预览页 |
| C. 按需推流（可选进阶） | 观看者点开 → CMS 经 `/cms/panel` 通道通知 render 开推；无人看自动停（省带宽/负载） |

## 5. render 推流插件设计

### 5.1 结构（与录屏插件同构，复用度极高）

```
屏幕采集 → 编码器(H264/H265) ──► OnEncodedVideoFrame ─┬─► media_recorder 插件 → MP4(已有, 可并存)
                                                      │
WASAPI → Opus 编码 ──► OnEncodedAudioFrame ───────────┴─► live_pusher 插件(新)
                                                          ├─ RtmpPusher: 视频直通 + 音频 Opus→AAC
                                                          │    avformat(flv muxer) → avio(RTMP) → ZLM
                                                          └─ WhipPusher: 视频直通 + 音频 Opus 直通
                                                               avformat(whip muxer, ffmpeg 8.1.1 内置) → ZLM
```

- 与 `RecordWriter` 同思路：输入同一份编码流，输出从"文件"换成"网络"；pts 墙钟驱动、首关键帧带参数集（SPS/PPS/VPS 缓存机制直接复用）。
- 录屏与推流并存（三个消费者，零重编码）；插件内两路推流也共享同一份编码帧，视频只编一次。
- 插件形态：**独立 dll**（与 `media_recorder.dll` 平级）。代价是各自静态链一份 ffmpeg（体积翻倍），换来隔离性——render 易死，推流插件崩溃不拖累录屏。不复用同一插件进程。

### 5.2 音频双路处理

```
                        ┌──► (直通) ──────────────────────────► WhipPusher(Opus, 浏览器 WebRTC 原生支持)
Opus 包 ──► 扇出 ───────┤
                        └──► avcodec 解码 → PCM(48k) → native AAC 编码器 → RtmpPusher(AAC, 全容器协议需要)
```

- Opus 直通 WHIP：浏览器 WebRTC 音频只认 Opus/G711，render 的 Opus 编码流天然满足，**零转码**。
- Opus→AAC 给 RTMP 路：native AAC encoder 已在静态链接范围内；AAC 保持 48k（现代播放器全支持），无需重采样；单路 AAC 软编 << 1% CPU。
- 两路 pts 都墙钟驱动（复用 RecordWriter 思路），DTS 单调钳制；两路时间基准一致，互不干扰。

### 5.3 推流状态机（每路独立）

```
Idle ──[push.enabled && 有客户端要拉/常推]──► Connecting ──► Publishing
  ▲                                                 │
  └──重试(指数退避)◄── 断线/被踢 ◄───────────────────┘
```

- RtmpPusher 与 WhipPusher 各自独立状态机、独立重连——一路挂不影响另一路（RTMP 被踢不影响 WebRTC 观看，反之亦然）。
- 网络抖动/断线自动重连；ZLM 侧断流后观看端停止，恢复推流后重新可看。
- 限速/缓冲：写队列上限防背压拖垮共享 Stream 任务线程（回调只入队，网络写在工作线程——与录屏插件同模式）。

### 5.4 配置（settings.toml）

```toml
[push]
enabled = false
rtmp_url = "rtmp://127.0.0.1:1935/live/{device_id}"                     # RTMP 路(H264+AAC)
whip_url = "http://127.0.0.1:8080/index/api/whip?app=live&stream={device_id}"  # WHIP 路(H264+Opus)
# 可选:
# audio_bitrate = 96000        # AAC 路码率
```

注意（代码事实）：

- 配置不是 toml 段自动透传：`[record]` 的键是 `rd_settings` + `plugin_manager.cpp` 硬编码注入插件 param map 的。新增 `[push]` 段需同步改 `rd_settings.h/.cpp` + `plugin_manager.cpp` 三处。
- `device_id` 来自 panel 启动参数（`--device_id`）；standalone 调试不传则为空。替换前校验：空则拒推 + 打日志，绝不能推出 `live/` 空流名。

### 5.5 为什么必须双路（而不是单路 + ZLM 转协议）

ZLM 的"一路进多路出"是**转封装（零转码）**，不是转码服务器：HLS/FLV/TS/RTSP 只是给同一组 H264/AAC 字节换容器，输出编码集合 = 输入编码 ∩ 容器支持集合。而浏览器 WebRTC 音频只认 **Opus/PCMU/PCMA**，SDP offer 里永远没有 AAC。源码佐证（`D:\GoCloud\ZLMediaKit`，2026-08 HEAD）：

- `webrtc/WebRtcTransport.cpp`：WebRTC 播放复用 RTSP 源的原始帧做 RTP 打包，无音频转码环节。
- `webrtc/Sdp.cpp`：answer 按 `preferredCodecA=PCMA,PCMU,opus,mpeg4-generic` 取 offer ∩ 源轨道；AAC 源 ∩ 浏览器 offer = 空 → 音频 m-line 被拒（有图无声）。
- `src/Codec/Transcode.h` 的 FFmpeg 解码/重采样工具只用于 C API/测试/`FFmpegSource`，未接入 WebRTC 播放路径。

因此单路 AAC 推流永远出不了带声音的 WebRTC。**双路推是唯一免转码的全协议解法**：AAC/RTMP 服务 HLS/FLV/RTSP/RTMP，Opus/WHIP 服务 WebRTC。可行性已验证：render 静态 ffmpeg 8.1.1 的 `avformat.lib` 内含 `ff_whip_muxer`（WHIP muxer，支持 H264+Opus），vcpkg features 含 `openssl`（WHIP 的 DTLS 依赖）与 `opus`——**零新增依赖**。

## 6. ZLM 部署与配置（CMS 机器）

### 6.1 构建与运行

- 构建 `D:\GoCloud\ZLMediaKit`（cmake，产出 `MediaServer.exe` + `config.ini`），部署到 CMS 机器（与 px_cms_server 同机或同网段）。
- 作为 Windows 服务托管（与 px_service 同模式）或 systemd（若 CMS 机为 Linux）。

### 6.2 关键配置

| 配置项 | 建议 |
|---|---|
| RTMP 端口 | 1935（默认） |
| RTSP 端口 | 554/8554 |
| HTTP 端口 | 80/8080（HLS/FLV/API、WHIP/WHEP 信令） |
| HLS 切片 | 1s 切片（`hls_segDur=1`）+ 清理策略，端到端延迟约 3-6s（切片+播放列表刷新+缓冲） |
| WebRTC | 开启；公网观看时配 `rtc.externIP`；媒体走 UDP 8000（默认），注意 `rtc.port`/`rtc.tcpPort` |
| 鉴权 hook | `on_publish` / `on_play` → CMS 校验接口（见 §7） |
| 推流带宽 | 每流限速可配（防单 render 挤爆） |
| 防火墙 | CMS 机需放行：1935(RTMP)、554(RTSP)、8080(HTTP: HLS/FLV/API/WHIP 信令)、**UDP 8000(WebRTC 媒体)** |

## 7. 鉴权设计（三件套）

| 环节 | 机制 |
|---|---|
| 推流鉴权 | ZLM `on_publish` → CMS HTTP hook：校验 `device_id + 签名(基于设备凭据)`，非法设备拒推（RTMP/WHIP 推流都触发此 hook） |
| 拉流鉴权 | ZLM `on_play` → CMS HTTP hook：校验短时效 token（CMS 发给 web 页，拼进拉流 URL 或 header），防内网/公网任意拉。v1 即上最简版（固定 secret/短时效 token），先开放后补是负资产 |
| 控制通道 | 按需推流指令走现有 `/cms/panel` 长连接（panel→render 下发"开推/停推"） |

## 8. 按需推流（可选进阶，v1 可先常推）

```
web 点"观看设备 X" → CMS 查 X 是否在推(getMediaList) → 未推则经 /cms/panel 通知 panel → panel 通知 render 开推
→ render 双路推流 → web 播放(HLS/FLV/WebRTC 任选) → 关闭页面/超时 → 通知 render 停推
```

- 常推 vs 按需：常推实现最简单（插件 enable 即推）；按需省带宽/ZLM 负载，适合大量 render。注意双路推流带宽×2，render 数量大时按需价值更高。
- v1 常推，v2 加按需（控制通道复用录像方案的 panel 消息族）。

## 9. 与录像方案的关系

| | 实时推流（本文档） | 录像查看（cms_render_records_view_design.md） |
|---|---|---|
| 数据 | 实时编码流 → ZLM | 已录文件 → panel 出口 |
| 出口 | render → ZLM(CMS 机) | panel（同网段直连 / 上层网段按需代理） |
| 观看 | 直播（HLS/FLV/RTSP/RTMP/WebRTC） | 回放（<video> Range） |
| 共用 | 编码流、参数集缓存、/cms/panel 控制通道、CMS 设备列表 | 同左 |

## 10. 工作量预估

| 模块 | 内容 | 量级 |
|---|---|---|
| render 推流插件 | RtmpPusher(flv muxer + RTMP + 音频转 AAC) + WhipPusher(whip muxer + Opus 直通) + 双路状态机/重试 | 中 |
| 配置接线 | `[push]` 段接入 `rd_settings.h/.cpp` + `plugin_manager.cpp`（非自动透传） | 小 |
| ZLM 部署 | CMS 机构建 + 配置（含 WebRTC/externIP）+ 服务托管脚本 + on_publish/on_play hook 对接 | 中 |
| CMS hook 接口 | publish/play 鉴权接口（复用设备凭据） | 小 |
| web/px_cms | 直播页（设备列表→WebRTC 优先/HLS 兜底播放，ZLMRTCClient.js 已随 ZLM 自带） | 小~中 |
| 验证 | VLC×3 协议 + Chrome(HLS/FLV/WebRTC) + iOS/Android HLS | 小 |

## 11. 验证矩阵（验收标准）

| 观看端 | 协议 | 预期 |
|---|---|---|
| VLC / PotPlayer | RTSP / RTMP / HLS | ✅ 秒开、音画同步 |
| Chrome / Edge | WebRTC(<1s) / HTTP-FLV(flv.js, ~1-2s) / HLS(3-6s) | ✅ |
| iOS Safari | HLS | ✅ |
| Android Chrome | HLS / FLV / WebRTC | ✅ |
| 智能电视 | HLS | ✅ |
| 双 render 同时推 | 各自 stream 独立可看、互不干扰 | ✅ |
| 单路断开 | RTMP 路断不影响 WebRTC 观看，反之亦然；各自独立重连恢复 | ✅ |
| render 断线重推 | 观看端自动恢复（或手动刷新可看） | ✅ |
| 非法设备推流 | on_publish 拒绝（RTMP 与 WHIP 都验） | ✅ |
| 未授权拉流 | on_play 拒绝 | ✅ |
| 空 device_id 推流 | 插件侧拒推 + 日志，不出现 `live/` 空流名 | ✅ |

## 12. 测试执行计划

原则：**自底向上分层验证，先用本地环回把协议打通，再上真机集成**。开发机一台即可完成 P0-P3，不依赖 CMS。

### P0 本地环回（开发机，127.0.0.1）

- 本地构建运行 ZLM（`MediaServer.exe` + 默认 config.ini，开 RTC）。
- render 用 standalone 调试模式（`scripts/start_render_hook.bat`）手动传 `--device_id=test001` 启动。
- **先验负路径**：不传 device_id → 插件拒推 + 日志（§11 空 device_id 用例），通过后再正常推。
- `curl http://127.0.0.1:8080/index/api/getMediaList?secret=xxx` 应同时看到 rtmp 和 rtc 两个 schema 的 `live/test001`。

### P1 协议矩阵（本地，逐协议验编码与音画）

| 检查 | 工具 | 通过标准 |
|---|---|---|
| RTMP 路编码参数 | `ffprobe rtmp://127.0.0.1:1935/live/test001` | h264 + aac(48k)，时长持续走动 |
| WHIP 路编码参数 | ZLM 日志 / getMediaList 的 tracks 字段 | h264 + opus |
| VLC | `rtsp://` / `rtmp://` / `http://.../hls.m3u8` 各播一遍 | 秒开、音画同步 |
| Chrome WebRTC | ZLM 自带 `www/webrtc` 测试页（ZLMRTCClient.js） | <1s 延迟、**有声音**（单路方案的死穴，重点验） |
| Chrome FLV/HLS | flv.js 测试页 + `<video>` 播 hls.m3u8 | FLV ~1-2s、HLS 3-6s |
| 音画同步 | render 屏上开毫秒计时网页 + 口播/敲击声 | 各协议偏差 <200ms |

### P2 健壮性（故障注入）

- 断网重连：拔网/防火墙临时封 1935 → RTMP 路断、**WebRTC 路应不受影响**；解封后指数退避自动恢复。反向封 UDP 8000 同理。
- `taskkill` ZLM → 两路都断；重启 ZLM → 插件自动重推，观看端刷新可看。
- 双 render（两个 standalone 实例，不同 device_id）同时推 → 互不干擾，getMediaList 四条源。
- 同名二次推流 → ZLM 踢旧连接，行为符合预期。
- 大码率/高帧率场景下观察写队列背压（日志无丢帧告警，共享 Stream 线程不卡）。

### P3 鉴权（stub hook）

- 本地起 Python stub HTTP server 充当 CMS 鉴权接口，接入 ZLM `on_publish`/`on_play`。
- 非法 device_id 推流 → 拒；无 token 拉流 → 拒；合法 → 通。RTMP 与 WHIP 推流都过 `on_publish`。

### P4 真机集成

- ZLM 部署到 CMS 机（服务托管），NAT 后 render 经 panel 正常启动（device_id 自动下发），web 页从 CMS 设备列表拼 URL 播放。
- 跨网段各协议复测 P1 矩阵，重点验 WebRTC 的 NAT 穿透（`rtc.externIP` 配置是否生效）。
- iOS Safari / Android Chrome / 智能电视 HLS 真机各验一遍。

### P5 性能与稳定性（render 易死，防泄漏是重点）

- 插件开销：推流中 render 进程 CPU 增量 <2%（AAC 转码 <<1%），双路带宽 ≈ 2×视频码率。
- 20min 常推稳定性：内存曲线平稳无爬升，无崩溃；期间随机断网 2-3 次验证重连。
- 多 render 压 ZLM：按预期规模×2 路流数压测 CMS 机 CPU/带宽。

**验收 = §11 矩阵全过 + P5 性能指标达标。** P0-P3 每步留 getMediaList/ffprobe 输出截图存档，P4-P5 出测试记录。

## 13. 已决与待确认

已决（本轮评审）：

1. **推流模式**：v1 常推（简单）；v2 加按需（省带宽，双路带宽×2 时价值更高）。
2. **双路全推**：RTMP(H264+AAC) + WHIP(H264+Opus)，v1 即覆盖全协议含 WebRTC。
3. **视频编码默认 H264**（全平台）；H265 仅作可选项（桌面播放器兼容；HLS 侧仅 Safari，RTMP 需 enhanced RTMP，WHIP 不支持）。
4. **音频**：Opus 原生(WHIP 路) + 转 AAC(RTMP 路)，不做其他直通选项。
5. **拉流鉴权**：v1 即上最简版 on_play token 校验。

待确认：

1. **ZLM 部署环境**：CMS 机器是 Windows 还是 Linux（影响构建/服务托管方式）？
2. **公网观看**：是否需要公网 WebRTC 观看（决定 `rtc.externIP` 配置与 UDP 8000 是否对公网开放）？
