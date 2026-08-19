# Render 实时推流方案（ZLM 中央服务器 + CMS Web 观看）

> 状态：待评审（v3，已收敛为单路 RTMP）
> 范围：render 已有远控会话期间的主屏直播；CMS Web 的 HLS 观看，以及 RTSP/RTMP 播放。
> 非范围：WebRTC/WHIP/WHEP、无人远控会话时按观看请求拉起采集、非主屏直播、录像回放。

---

## 1. 目标、边界与已决

render 已有 H264/H265 视频编码、PCM 音频采集、Opus 音频编码以及“编码帧扇出给插件”的链路；录像插件已验证该模型可用。本方案新增一条独立的直播旁路：把**已有远控会话**的主屏媒体推到 CMS 侧的 ZLMediaKit（下称 ZLM），由 CMS Web 和标准播放器观看。

已决：

1. **只推单路 RTMP**：H264 + AAC，或在编码器切到 H265 时以 enhanced-RTMP 推 H265 + AAC；不实现 WHIP、WebRTC、Opus 直通和双路推流。
2. **不改变采集按需语义**：render 没有远控客户端时不采集、不编码、不推流。直播页不是“唤醒 render”的入口，而是正在进行远控会话的旁路观看入口。
3. **只推主屏**：桌面模式取系统主显示器；game-hook 模式取该实例的主视频流。非主屏帧一律不进入直播插件。
4. **CMS Web 是正式观看端**：v1 使用 HLS.js / 原生 HLS；H264 可在 Chrome/Edge/Safari 播放，H265 不承诺通用浏览器解码。VLC/PotPlayer 可走 RTSP/RTMP/HLS。
5. **game-hook 应用必须支持直播**：每个运行中的应用实例使用独立流名，不能和同机桌面或其他实例冲突。

这不是远控传输的替代品。远控继续走现有 WebRTC/UDP/WS 等链路；ZLM 直播只负责一对多观看，不承载输入、文件传输或低延迟交互。

## 2. 总体架构

```text
已有远控客户端连接到 render
        │
        │  既有采集/编码开始（没有远控客户端则本链路不存在）
        ▼
主屏 H264 编码帧 ─────────┐
主屏 PCM 音频 ─ AAC 编码 ├─► live_pusher.dll ─ RTMP 主动外连 ─► ZLM（CMS 侧）
                         │                                      │
                         │                                      ├─ HTTP-FLV ─ CMS Web（低延迟）
                         │                                      ├─ HLS ─ CMS Web / iOS / 电视（兼容）
                         │                                      ├─ RTSP ─ VLC / PotPlayer
                         │                                      └─ RTMP ─ 桌面播放器/二次分发
                         │
                    media_recorder.dll（可并存，录像）
```

- **render** 只主动连接 ZLM，适合 render 在 NAT 后、CMS 在上层网段的现有部署。
- **ZLM** 是独立进程，部署在 CMS 主机或 CMS 同网段的媒体主机；不嵌入 render，不随 render 生命周期崩溃。
- **CMS** 是唯一业务入口：授权、流状态、播放地址和 Web 页面均由 CMS 提供；浏览器不接触 ZLM 管理 API/secret。
- 单路输入由 ZLM 做零转码转封装。输入可以是 **H264 + AAC**，也可以是 **H265 + AAC**；H265 经 enhanced-RTMP 发布，ZLM 的 `[rtmp] enhanced=1` 必须开启。CMS Web 对 H265 的兼容性见 §7.2。

## 3. 会话与直播状态语义

### 3.1 启停规则

```text
远控客户端接入 render
  → 现有 HasConnectedPeer() 为真
  → 采集、H264 编码、PCM 音频采集开始
  → live_pusher 收到主屏首个关键帧后连接 ZLM、发布 RTMP
  → CMS 标记为 live，观看端可播放

远控客户端全部退出
  → 现有逻辑停止采集/编码
  → live_pusher 排空极短队列、写 trailer/关闭 RTMP
  → ZLM 注销流
  → CMS 标记为 idle，观看端显示“当前没有远控会话，未直播”
```

`live_pusher` **不得**通过伪造 `GetConnectedClientsCount()` 让 `HasConnectedPeer()` 恒为真；否则会改变当前按需采集和资源释放行为。它只消费已经存在的帧。

### 3.2 断网与重连

- 远控会话仍在而 ZLM/网络故障时：推流器按指数退避重连（例如 1s、2s、5s、10s、最长 30s），每次重建 muxer 后等待主屏关键帧再发布。
- ZLM 恢复后，下一关键帧恢复直播；CMS Web 轮询状态并提示“正在恢复”，播放器可自动重载一次。
- 远控会话结束时取消重试并立即进入 Idle，不能让后台重试保持 render 活跃。

## 4. 流身份与多实例隔离

### 4.1 统一流名

ZLM 的 `app` 固定为 `live`。流 ID 由 CMS/Panel 在启动 render 时确定并显式下发为 `live_stream_id`：

| render 类型 | `live_stream_id` | 示例 |
|---|---|---|
| 桌面模式 | `{device_id}__desktop` | `074723054__desktop` |
| game-hook 实例 | `{device_id}__app__{instance_id}` | `074723054__app__8f19a2` |

最终 RTMP 地址：`rtmp://{zlm-host}:1935/live/{live_stream_id}?sig=...`

- `device_id` 仅能标识机器，不能标识同机多开的 game-hook 实例，因此不能单独作为流名。
- 流 ID 只允许 `[A-Za-z0-9_-]`，最长 128 字符；CMS 生成并校验，render 再做防御校验。不得拼接路径、IP、显示器名或用户输入。
- 同一 `live_stream_id` 第二次发布默认拒绝，而不是无条件踢掉旧流；CMS 在明确“实例重启/接管”后才允许接管，避免错误实例覆盖正在观看的流。

### 4.2 主屏选择

- 桌面模式：由 capture 层确定系统主显示器的稳定 `display_name`，经 `RdSettings`/`PluginManager` 下发 `push_primary_monitor`。直播插件只接受该名称的视频帧。
- game-hook：应用实例只应输出其主游戏画面；若仍出现多路 monitor/frame stream，Panel 启动参数同样明确目标 `push_primary_monitor`。
- 不允许以“最先到达的帧”或硬编码 `DISPLAY1` 判定主屏；多显卡、热插拔和远程桌面下都可能不正确。
- 主屏切换或分辨率变化：丢弃旧屏队列、重建视频流描述、请求 IDR，再恢复发布；CMS 流名不变。

## 5. render 端实现

### 5.1 插件形态与接线

新增独立 `live_pusher.dll`，类型为 `PxStreamPlugin`，与 `media_recorder.dll` 并列。独立 DLL 保持故障隔离：推流出错不影响现有远控和录像。

需要的接线：

1. 新增插件 ID、目录、CMake target、分发/安装规则。
2. 在 `RdSettings` 解析 `[push]`，在 `PluginManager` param cluster 显式注入配置；DLL 不能依赖 exe 内 `RdSettings::Instance()` 的副本。
3. Panel/Service 启动 render 时传入并保存 `live_stream_id`、推流鉴权材料和主屏标识；game-hook 启动必须带 `instance_id` 对应的流 ID。
4. 复用 `OnEncodedVideoFrame` 获取 H264/H265 Annex-B；复用 `OnRawAudioData` 获取 PCM。无需先把 Opus 解码再转 AAC。
5. `OnEncodedVideoFrame` 只保留主屏视频。H264 缓存 SPS/PPS，H265 缓存 VPS/SPS/PPS；不得把 HEVC 标作 H264 发送。

### 5.2 媒体与封装

```text
OnEncodedVideoFrame(main, H264/H265 Annex-B)
  → 缓存 H264 SPS/PPS 或 H265 VPS/SPS/PPS、等待首个关键帧
  → video queue

OnRawAudioData(PCM, 48 kHz)
  → FFmpeg native AAC encoder
  → audio queue

单一 worker
  → flv muxer + RTMP URL
  → av_interleaved_write_frame()
```

- 视频不重编码；AAC 由 FFmpeg native encoder 生成。音频按实际采样率/声道初始化，优先 48 kHz 双声道；不满足时经 `swresample` 规范到 AAC 支持格式。
- 首次连接和每次重连均须等待带完整参数集的关键帧，再创建 `AVFormatContext`/流并写 header；AAC 配置在 header 前就绪。H265 使用 FFmpeg 的 FLV enhanced-RTMP 打包能力；发布前必须由 VPS/SPS/PPS 生成 hvcC codec configuration record，否则 muxer 只会发出 5 字节增强 RTMP 头而被 ZLM 拒绝。ZLM 侧须设 `rtmp.enhanced=1`。
- 使用单调媒体时间基：视频 90 kHz、音频 sample rate；所有 DTS 进行单调钳制。音视频回调本身异步，worker 负责按 DTS 交织，不能在回调线程直接网络写入。
- 队列有界（建议视频 120 帧、音频 200 包）。溢出时丢最旧的 delta/音频并请求 IDR；持续背压打印节流日志。绝不能堵塞共享 Stream 插件线程。
- 输入关键帧不足、AAC 初始化失败、`device_id`/`live_stream_id` 非法、RTMP URL 缺失均应拒绝发布并持续可诊断日志。

### 5.3 状态机

```text
Idle
  └─[push.enabled && 已收到主屏媒体]→ WaitingKeyframe
                                         │
                                         ▼
                                    Connecting ─→ Publishing
                                         │             │
                         会话结束 ──────┴─────────────┘
                                         │
                             断线/写失败 ▼
                                      Backoff
                                         │
                                         └──────────→ WaitingKeyframe
```

`push.enabled=false` 时插件完全旁路；它不应改变远控会话、采集、编码器或录像的行为。

### 5.4 编码格式切换（H264 ↔ H265）

render 的主编码器可以在运行中切换 H264/H265；直播插件必须跟随该选择，不额外启动视频编码器。**切换不能在同一个已发布的 RTMP 会话中无缝完成**：FLV sequence header、ZLM Track 和播放端解码器都需要重新初始化。正确行为是“媒体采集不断、直播受控重推”：

```text
检测到 video_type 改变
  → 停止接受旧编码的 delta 帧，清空视频队列
  → 关闭旧 AVFormatContext / RTMP 发布
  → 请求 IDR，等待新编码的完整参数集关键帧
  → 按新 codec 重建 FLV muxer，重新发布相同 live_stream_id
  → ZLM 流重新注册；CMS Web 自动重连并重新初始化解码器
```

- 不需要停止 render 的采集或远控会话，但观看端会有一次短暂中断；HTTP-FLV 目标为自动重连，HLS 等待下一个切片。具体时长以 P1/P5 实测为准。
- 切换期间 CMS 状态为 `recovering`，不应显示为实例离线；ZLM 的注销/注册事件要按同一 `live_stream_id` 合并处理。
- H264 和 H265 的缓存参数集、时间戳基准、队列和 muxer 均不得复用，避免把旧 codec 的 config 帧送入新会话。

### 5.5 配置与启动参数

```toml
[push]
enabled = false
rtmp_url = "rtmp://zlm.internal:1935/live/{live_stream_id}"
audio_bitrate = 96000
```

- `{live_stream_id}` 只由 Panel/Service 注入，standalone 调试可显式传 `--live_stream_id=test__desktop`。
- 签名不写入仓库的 `settings.toml`。生产环境由启动链路安全下发到插件参数（例如 `push_auth`），推流器每次连接附在 RTMP query；日志必须脱敏。
- 当前 render 的媒体仅在 `HasConnectedPeer()` 为真时产生；此规则保持不变，`push.enabled` 不是采集开关。

## 6. ZLM 与网络部署

### 6.1 服务

- **从上级目录 `D:\GoCloud\ZLMediaKit` 的源码编译**；不采用 vcpkg port 或不明版本的预编译 `MediaServer.exe`。按完整能力构建，保留 WebRTC、FFmpeg 及 RTMP、HLS、HTTP-FLV、RTSP、hook/API，以供后续功能使用：

  ```powershell
  cmake -S D:\GoCloud\ZLMediaKit -B D:\GoCloud\ZLMediaKit\build_px_media -G "Visual Studio 17 2022" -A x64 `
    -DENABLE_WEBRTC=ON -DENABLE_FFMPEG=ON -DENABLE_TESTS=OFF -DENABLE_HLS=ON -DENABLE_SERVER=ON -DENABLE_API=ON
  cmake --build D:\GoCloud\ZLMediaKit\build_px_media --config Release --target MediaServer
  ```

- 打包阶段将源码产物 `MediaServer.exe` 复制并重命名为 **`px_media.exe`**；不修改 ZLM 源码 target 名称，后续升级和冲突处理更简单。
- CMS 安装目录使用固定布局，媒体进程的工作目录也必须是该目录：

  ```text
  {cms_install_dir}/
  ├─ px_cms_server.exe
  ├─ px_media.exe
  ├─ config.ini
  ├─ default.pem                  # 需要 HTTPS 时
  └─ www/                         # ZLM 静态资源
  ```

- `config.ini` 由 ZLM 的 `conf/config.ini` 作为模板生成并由安装包部署。`px_media.exe` 保留 ZLM 的默认配置约定，服务命令直接使用 `px_media.exe`；只有需要使用非默认配置路径时才加 `-c {path}`。
- `px_media.exe` 以独立 Windows service 或 systemd 服务运行；与 `px_cms_server` 同机或同网段。服务名建议为 `px_media`，由安装/升级逻辑先停止、替换二进制和配置模板、再启动；用户改动的密钥和部署项必须保留。
- 本期直播只接入 RTMP ingest、HTTP-FLV、HLS、RTSP、RTMP playback；WebRTC/WHIP/WHEP 虽随完整 ZLM 一并编译和保留配置能力，但不出现在 CMS 播放 URL、鉴权接口或本期验收范围。是否在生产环境开放其端口由部署配置单独决定。
- ZLM 管理 secret 仅供 CMS 服务端使用，不能给浏览器。

### 6.2 端口与反向代理

| 流向 | 端口/协议 | 要求 |
|---|---|---|
| render → ZLM | TCP 1935 / RTMP | render 主动外连；仅允许受控 render 网段/出口 |
| CMS Web → 媒体 | HTTPS 443 → ZLM HTTP | 经 CMS/Nginx 反代，承载 HLS/HTTP-FLV；生产环境不直接暴露 8080 |
| 桌面播放器（可选） | RTSP 8554、RTMP 1935 | 按部署策略暴露；必须有播放鉴权 |

HLS 切片建议 1 秒，预期端到端延迟约 3–6 秒；HTTP-FLV 预期约 1–2 秒。必须按实际网络和浏览器缓冲实测，不将其写成硬性 SLA。

### 6.3 ZLM 状态同步

- 开启 `on_stream_changed`：ZLM 注册/注销 `live/*` 时通知 CMS，CMS 写入内存状态或数据库缓存。
- CMS 以 `getMediaList` 服务端轮询作为兜底校正，不能让浏览器直连该管理 API。
- `on_publish` 成功后才把流状态标为 `live`；ZLM 注销或持续健康检查失败则为 `offline`。

## 7. CMS API 与 Web 观看端

### 7.1 CMS 接口（v1 已实现）

实现位于 `rust_server/px_cms_server/src/live/`，不把 ZLM 管理 API 或其 secret 下发给浏览器：

| 方法 | 路径 | 职责 |
|---|---|---|
| GET | `/api/v1/live/control/status?device_id=&app_id=&appkey=` | 使用 CMS 既有 appkey 鉴权，服务端查询 ZLM 的 `getMediaList`，返回编码、尺寸、观看者数、H264 可播放状态和短期播放 URL |
| GET | `/api/v1/live/control/play/{stream_id}/{asset}?ticket=` | 验证短期、单流、滑动过期的播放票据后，由 CMS 代理 ZLM HLS 清单和切片；清单内切片 URL 会重写为同一 CMS 端点 |

`[live]` 配置位于 `px_cms.toml`：`media_server_url` 仅供 CMS 到 ZLM 的内网访问，`api_secret` 仅留在 CMS 配置文件。流 ID 固定按 `<device_id>__app__<app_id>` 生成并作严格字符校验。H264 在线时才签发播放票据；H265 在线时返回状态和编码提示，但不会给浏览器一个必然黑屏的播放 URL。

以下 hook 型接口仍是公网多租户部署的后续增强，不是当前 v1 的依赖：

| 方法 | 路径 | 职责 |
|---|---|---|
| GET | `/api/v1/live/info?device_id=&instance_id=` | 鉴权后返回指定桌面/应用实例的状态和短时播放 URL |
| POST | `/api/v1/live/hook/publish` | 供 ZLM `on_publish` 校验发布者 |
| POST | `/api/v1/live/hook/play` | 供 ZLM `on_play` 校验播放 token |
| POST | `/api/v1/live/hook/http-access` | 供 HLS 清单/切片、HTTP-FLV 等 HTTP 资源鉴权 |
| POST | `/api/v1/live/hook/stream-changed` | 接收 ZLM 流注册/注销事件 |

`live/info` 的建议响应：

```json
{
  "device_id": "074723054",
  "instance_id": "8f19a2",
  "mode": "game-hook",
  "state": "live",
  "stream_id": "074723054__app__8f19a2",
  "video_codec": "h265",
  "flv_url": "https://cms.example/live/live/...live.flv?token=...",
  "hls_url": "https://cms.example/live/live/.../hls.m3u8?token=...",
  "expires_at": 1780000000
}
```

状态为 `idle` 时不返回可播放地址；页面据此显示“当前没有远控会话，未直播”。`unavailable` 表示 render/实例离线，`recovering` 表示远控会话存在但推流重连中。

### 7.2 CMS Web 页面（v1 已实现）

`web/px_cms/src/views/LiveViewer.vue` 已增加“直播观看”菜单入口。它选择在线设备和应用标识、每 10 秒刷新状态，使用 HLS.js（Safari 优先原生 HLS），并在离开页面时销毁播放器和定时器。该页不嵌入远控 Web 客户端、不建立 WebRTC 会话，也不会因为没有远控会话而唤醒 render。

入口：CMS 设备详情增加“直播”；game-hook 应用实例列表也增加“观看直播”，两者都带目标 `device_id + instance_id` 调 `live/info`。

```text
打开直播页
  → CMS 业务鉴权（设备 ACL / 应用实例权限）
  → GET live/info
  → state=live 且 H264：以 CMS 票据 URL 播放 HLS
  → H265 或不支持的编码：显示兼容性提示，不制造黑屏播放器
  → state=idle/recovering/offline：显示状态，每 3–5 秒重新查询
```

- 页面必须清理 video/flv.js 实例、定时器和过期 URL；token 过期时重新请求 `live/info` 后重连。
- 播放页仅有观看控制（静音、音量、全屏、清晰状态、切换 HLS）；不复用远控 Web 客户端的键鼠/文件传输 data channel。
- 浏览器兼容性取决于 `video_codec`：H264 时 Chrome/Edge 优先 FLV、Safari/iOS 原生 HLS、Android 依能力先 FLV 后 HLS、智能电视承诺 HLS；H265 时 CMS Web 不承诺 Chrome/Edge 的 FLV/MSE 播放，Safari/iOS 优先 HLS，桌面播放器走 RTSP/RTMP。`live/info` 必须返回 `video_codec`，页面在不支持时给出明确提示而非黑屏重试。

## 8. 鉴权与安全

| 环节 | 必须措施 |
|---|---|
| 推流 | RTMP URL 带 `stream_id`、过期时间、随机 nonce 和 HMAC 签名；ZLM `on_publish` 回调 CMS 校验身份、签名、实例是否仍允许运行 |
| 观看 | CMS `status` 先经既有 appkey 鉴权；短时播放 ticket 绑定单一 stream 并在 HLS 请求时滑动续期 |
| HTTP 媒体 | HLS m3u8 和每个切片均通过 CMS 代理和 ticket 验证，浏览器不直连 ZLM HTTP 端口 |
| 运维 | ZLM secret、推流密钥和 token 不写入日志、不下发浏览器；管理 API 仅内网/服务端访问 |
| 公网 | CMS Web 与媒体统一 HTTPS；限流、连接数上限、审计 publish/play/鉴权失败 |

签名校验应将 `stream_id` 与启动实例绑定，不能只验证 `device_id`，否则同机任一 render 可以越权发布到另一个应用实例流。

## 9. 录像、桌面与 game-hook 的关系

| 项目 | 桌面模式 | game-hook 模式 |
|---|---|---|
| 视频来源 | 系统主显示器 | 该应用实例主游戏画面 |
| 流 ID | `{device}__desktop` | `{device}__app__{instance}` |
| 推流开始 | 已有桌面远控会话 | 已有该实例远控会话 |
| 推流停止 | 最后一个远控客户端退出 | 最后一个该实例远控客户端退出或实例结束 |
| 录像 | 可与 `media_recorder` 并行 | 可与 `media_recorder` 并行 |

直播和录像共同消费编码管线，但互不依赖：直播断线不能中断录像；录像写盘慢也不能使推流队列堆积。两者都必须有各自的有界队列和丢帧日志。

## 10. 实施顺序

| 阶段 | 内容 | 完成标准 |
|---|---|---|
| P0：配置与身份 | `[push]`、`live_stream_id`、主屏标识、插件 ID/CMake/安装；Panel 为桌面和 game-hook 实例下发流 ID | 空/非法 ID 拒绝发布；多实例流 ID 唯一 |
| P1：render 单路推流 | `live_pusher.dll`、主屏 H264/H265 筛选、PCM→AAC、FLV/RTMP muxer、队列与重连 | 本地 ZLM `ffprobe` 为选定视频 codec + AAC，断线可恢复 |
| P2：ZLM 与 CMS 状态 | ZLM 服务、协议转封装、publish/stream-changed hook、CMS 状态缓存 | CMS 能准确显示 live/idle/recovering，浏览器无管理 secret |
| P3：CMS Web | 设备直播页、应用实例直播页、FLV 播放、HLS 回退、状态轮询和 token 刷新 | 受权用户可观看；无会话有清晰状态，不会试图唤醒 render |
| P4：鉴权与部署 | publish/play/http-access 鉴权、HTTPS 反代、限流、防火墙、服务托管 | 非法推流和未授权媒体请求均被拒绝 |
| P5：真机与压测 | 桌面、game-hook、多实例、多 render、断网/重启、浏览器与移动端 | 达到 §11 验收矩阵和容量指标 |

## 11. 验收与测试

### 11.1 本地协议验证

1. 启动本地 ZLM；render standalone 带 `--device_id=test001 --live_stream_id=test001__desktop`。
2. 建立一个真实远控客户端连接；确认未连接时 ZLM 没有 `live/test001__desktop`，连接后才注册。
3. 分别用 H264、H265 启动主编码器；`ffprobe rtmp://127.0.0.1:1935/live/test001__desktop` 必须显示选定视频 codec + AAC，采样率/声道正确且时间持续走动。
4. VLC 分别播放 RTMP、RTSP、HLS；Chrome/Edge 播放 HTTP-FLV 和 HLS。
5. 断开远控客户端，确认 RTMP/ZLM 流在合理超时内注销；再次连接后重新出流。
6. 在远控会话仍在时切换 H264↔H265：render 不重启，ZLM 流受控重注册，CMS 状态经过 `recovering` 后回到 `live`，支持该 codec 的播放器恢复播放。

### 11.1.1 当前本机验证记录（2026-08-19）

- 已用 CarGame game-hook、真实浏览器远控连接和本机 `px_media.exe` 验证 H264 + AAC：未连接时 render 日志持续显示不采集；连接后 ZLM API 注册 `live/debug1__app__cargame_debug`，视频为 H264/60fps、音频为 AAC/48 kHz 双声道。
- `px_media.exe` 已以 FFmpeg、OpenSSL、SRTP、WebRTC、HLS、API 全部启用的配置编译，`[rtmp] enhanced=1` 保持开启。
- H265 已用同一 CarGame、真实浏览器远控连接和本机 `px_media.exe` 验证。对 RTX 3060 的 D3D11 设备在启动游戏前预热并按真实 adapter LUID 缓存，避开首次 HEVC 帧到达时的驱动阻塞；随后选中 NVENC。推流器会从 VPS/SPS/PPS 构建 hvcC，再以 enhanced-RTMP 发布。ZLM 已注册 `live/debug1__app__cargame_debug`，日志确认 `H265[1352/760/60] + AAC[48000/2]`，没有再出现 5 字节 RTMP 包拒绝。
- CMS 已完成 HLS 观看端、ZLM `getMediaList` 状态查询及 CMS 短期票据 HLS 代理；Rust 单元测试覆盖流/切片路径校验与 manifest 重写，Web 生产构建通过。H.264 端到端推流此前已实测；H.265 推流已端到端注册，浏览器播放仍按编码兼容性提示处理。

### 11.2 game-hook 与多实例

| 场景 | 预期 |
|---|---|
| game-hook 实例有远控客户端 | 对应 `{device}__app__{instance}` 出流，音画正常 |
| 同机两个实例同时远控 | 两条不同流均可看，互不覆盖 |
| 桌面与一个应用实例并存 | `__desktop` 与 `__app__*` 两条流独立 |
| 非主屏有编码帧 | 不进入直播流；主屏画面稳定 |
| 实例退出 | 对应流注销，不影响同机其他流 |

### 11.3 安全与健壮性

| 场景 | 预期 |
|---|---|
| 空/非法 `live_stream_id` | 插件拒推并记录脱敏日志 |
| 非法 RTMP 签名 | `on_publish` 拒绝 |
| 无 ACL 或过期 token | CMS `live/info` 或 ZLM 播放/HTTP 资源访问拒绝 |
| ZLM 重启 | 会话仍在时推流自动恢复；CMS 从 recovering 回到 live |
| RTMP 断网 | worker 不阻塞 Stream 线程；重连后等待 IDR 恢复 |
| 队列背压 | 有界丢帧、请求 IDR、无内存持续增长 |

### 11.4 性能目标

- 直播增加的编码成本仅为 AAC；视频不重编码。以真实目标分辨率/FPS测量 render CPU 增量。
- render → ZLM 上行约为视频码率 + AAC 码率（不再有双路视频带宽）。
- ZLM 出口按同时观看人数估算；压测目标为预期并发流数 × 预期观众数 × 实测码率，并预留 30% 带宽。
- 连续运行 20 分钟、至少 3 次 ZLM/网络中断恢复后，内存不持续增长，远控与录像不受影响。

## 12. 待确认

1. CMS/ZLM 部署系统（Windows 服务或 Linux systemd）及公网域名/HTTPS 终止位置。
2. `live_stream_id`、主屏标识和推流签名由 Panel 还是 Service 作为唯一签发方；建议由实际启动 render 的一方签发，并由 CMS 校验。
3. CMS 中桌面会话与 game-hook `instance_id` 的权限模型如何映射到现有用户组 ACL；默认应继承设备 ACL，并可叠加应用实例权限。
4. RTSP/RTMP 是否需要对公网开放；若只服务 CMS Web，可仅开放 HTTPS 443 和 render→ZLM 的 1935。

**验收结论：**P0–P4 完成后先在桌面模式与单 game-hook 实例验证；P5 的多实例和容量测试通过后，才能将直播入口默认展示给所有有权限的用户。
