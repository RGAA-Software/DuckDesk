# 云游戏 UDP 通道(GameStream 风格)当前状态

> 关联文档:
> [udp_gamestream_channel_plan.md](udp_gamestream_channel_plan.md)(分阶段实施规划)
> [gamestream_protocol_analysis.md](gamestream_protocol_analysis.md)(Moonlight/Sunshine 协议分析,含许可证结论)
> 最后更新:2026-08-13

## 1. 实现状态总览

| 模块 | 状态 | 说明 |
|------|------|------|
| P0 插件基建 | ✅ 完成 | `net_udp` 插件重写为裸 UDP,ws 控制面不动 |
| P1 视频面 | ✅ 完成 | render 分 shard 发送,客户端 `GrUdpFrameReassembler` 组帧合成 `kVideoFrame` proto 上送 |
| RS-FEC(视频) | ✅ 完成 | 移植 moonlight `reedsolomon/rs.c` 至 `tc_common_new/reedsolomon/`,帧级 parity,默认 20% |
| 花屏修复(整帧丢失 gap 检测) | ✅ 完成 | 无限 GOP 下丢整帧即请 IDR |
| FRAME_STATUS 动态 FEC | ✅ 完成 | 客户端逐帧回执,render 5s 窗口按丢帧率调 fec%([配置值, 60]) |
| 丢包修复(socket 缓冲) | ✅ 完成 | 客户端 RCVBUF 8MB / render SNDBUF 4MB |
| 发送 pacing | ✅ 完成 | render 20 shard/批、批间 1ms(`timeBeginPeriod(1)`) |
| IDR 请求节流 | ✅ 完成 | 客户端 per mon_slot 1s 去重(moonlight 同款) |
| 音频迁 UDP | ⚠️ 已实现,未通过实测 | Opus 裸帧 + seq + jitter buffer + PLC + inband FEC(15%);wire 正常(探针 50pps),客户端侧仍无声 |
| 音频 jitter buffer 死循环修复 | ✅ 完成(2026-08-13) | 判丢看最新包 + 满时丢新包 + 对端重启 resync;单测 30/30 |
| 加密 | ❌ 未做 | 按规划放最后 |
| RFI 参考帧失效 | ✅ 已实现(NVENC) | 丢整帧优先发 RFI,NVENC `NvEncInvalidateRefFrames`;2s 未恢复回退 IDR |
| 投机式判丢 | ❌ 未做 | 缺包数>可用 parity 立即上报,不等新帧 |
| 输入迁 UDP | ❌ 未做 | 目前输入仍走 ws |
| MTU 钳 1024 | ✅ 已配置化 | `mtu` 参数,LAN 默认 1400,公网可配 1024 |
| UDP 不通回退 ws | ❌ 未做 | |

构建:`build_client.bat`(`GR_SKIP_SERVERS=1`,只编安装包内容,跳过 3 个 rust server)。
探针:`scripts/udp_fec_probe.mjs`(视频 shard/FEC 统计)、`scripts/udp_audio_probe.mjs`(音频 seq/间隔统计)。

## 2. 当前未解决问题(2026-08-13 实测;本次已修两处,待回归)

> 测试口径提醒:2026-08-13 客户端日志显示 `network type: webrtc_direct => 2`,当前复测实际走的是 WebRTC local,不是 `udp_direct`。若目标是 UDP 通道,需在面板 stream item 上选 UDP 网络类型;否则看到的是 `webrtc_local_connection` 行为。

### 2.1 视频仍卡死

- 现象:连接后解出首帧 IDR,随后画面冻结;客户端无 `frame lost` 日志,watchdog 不触发(包还在收)。
- render 侧线索:udp 插件 FEC 窗口整个会话只有 `frames 1`;`encoder_thread` 诊断 `in_fps=30.0, enc_posts=0`——**编码器有输入(30fps)但 5s 窗口内 0 次编码任务投递**,冻结点在 render 编码投递环节,不在网络/客户端。
- 下一步排查方向:`encoder_thread.cpp` 编码投递条件(`target_encoder_plugin` / `can_encode_texture` 分支)、编码插件(NVENC/AMF)是否在首帧 IDR 后进入不产出状态。
- 本次已修:`GrUdpFrameReassembler` 收到 SOF 时会清空 `received_`,SOF 前已到的数据/parity 块被漏计;UDP 乱序下即使 parity 足够也会误判丢帧。现改为 SOF 只补元信息、不清计数器,并新增回归单测 `ReassembleLateSofKeepsPreSofCounters`。端到端是否彻底消除卡顿仍待实机回归。
- 本次另修 WebRTC local 客户端:`kIceStateDisconnected` 被误当终态弹“断开重连”。render 端已把它当 transient,客户端现在同样保留连接,只有 `Failed/Closed` 才触发断线回调。

### 2.2 音频仍无声

- 链路已全部实现:render 提取 Opus payload 打 UDP 音频包(探针实测 50pps、seq 连续、payload 长度正常)→ 客户端 jitter buffer 按序交付 → 合成 `kAudioFrame` proto → thunder_sdk Opus 解码(丢帧走 `DecodeDummy` PLC)。
- **客户端每次启动都是 `audio on: false`**(panel 启动参数 `--audio 0`,见 `ct_main_ws.cpp:209-210`),音频开关未打开时收不到声音是预期行为;带声音开启的端到端实测还没做过。
- 本次已修:`ct_main_ws.cpp` 对 `kUdpDirect` 强制打开本地音频播放,并把 `ThunderSdkParams.enable_audio_` 与 `settings->audio_on_` 对齐;旧 panel stream item 即使传 `--audio=0`,UDP 链路也不会被本地静音开关卡掉。端到端有声仍待实机验证。
- 音频死循环已修(见 3.4),修复后日志无判丢刷屏,但"有声"未验证。

## 3. 已解决的坑(排障记录)

### 3.1 丢帧/花屏(历史)

- **根因**:高动态画面一帧 ~89 个 UDP 包(~125KB)毫秒内突发,打爆默认 64KB socket 接收缓冲。
- **修复**:接收缓冲 8MB;render 发送 pacing(20 shard/1ms);整帧丢失 gap 检测 + IDR 节流。

### 3.2 FEC 不生效 → 动态 FEC

- FRAME_STATUS 逐帧回执打通后,真机实测丢帧率高时 fec 从 20% 自动升到 60% 生效。

### 3.3 同一机器多开 60s 限制 / 浏览器并发(历史,已完成)

- 方案 A:一个浏览器启动一个实例;同浏览器重复连接顶掉旧连接;CMS 停止可通知到连接中的客户端。已提交。

### 3.4 音频 jitter buffer 永久判丢死循环(2026-08-13)

- **现象**:连接约 1 分钟后每个音频包都被判丢(日志 50/s 刷 `PLC conceal`),随后视频卡死。
- **根因**:判丢条件看缓冲里"最老"的包,缓冲满又淘汰"最老"的包——`expected_` 落后 3 帧以上后,追赶速度 = 到达速度,永远追不上。判丢风暴(50 条/s 日志刷盘 + 50 次/s PLC proto 合成)全在 UDP 接收线程上,视频 shard 处理被饿死。
- **修复**(`gr_udp_protocol.h` `GrUdpAudioJitterBuffer`):
  1. 判丢改看"最新"缓冲包(rbegin):缺口等够 60ms 窗口立即收口追平;
  2. 缓冲满时丢弃超前的新包,绝不删最老(断绝死循环);
  3. 对端重启 seq 归零时大幅回退判定为新流,重置重新对齐(否则 render 重启后音频永久无声);
  4. 判丢日志每 50 条汇总一次(`udp_direct_connection.cpp`)。
- **回归单测**:`AudioJitterBurstBehindNeverSpirals`(突发落后必追平,交付/判丢互补不重不漏)、`AudioJitterResyncOnPeerRestart`、`AudioJitterPermanentGapHeals`。

### 3.5 视频 SOF 迟到导致 FEC 恢复被误判丢(2026-08-13)

- **现象**:UDP 乱序时 SOF 包晚于部分数据/parity 包到达,明明 parity 足够恢复,客户端仍判丢并请求 IDR,表现为画面卡顿。
- **根因**:`GrUdpFrameReassembler` 收到 SOF 时无条件把 `received_` 清 0,SOF 前已收到的数据/parity 块不再计入 distinct 块数,`TryRecoverAndEmit` 拿不到足够块数。
- **修复**:SOF 只负责补齐元信息,不清空已收到的块;新增 `meta_ready_` 标记区分“从 SOF 建流”和“从非 SOF 建流后补 SOF”。
- **回归单测**:`ReassembleLateSofKeepsPreSofCounters`(先收数据+parity,再收 SOF,缺一个数据块仍用 parity 恢复,不判丢)。

### 3.6 自动补 IDR 污染动态 FEC 丢帧统计(2026-08-13)

- **现象**:客户端连接后/长时间无完整帧时补发 IDR,render 端把这些请求全部当成“丢帧”,动态 FEC 被刷到 60% 上限。
- **修复**:协议新增 `kCtrlIdrKeepalive`。真实组帧判丢仍发 `kCtrlIdrRequest` 并计入丢帧窗口;连接初始化与无帧兜底发 `kCtrlIdrKeepalive`,只请求关键帧、不计数。
- **涉及文件**:`tc_common_new/gr_udp_protocol.h`、`tc_client_sdk_new/connection/udp_direct_connection.cpp`、`gr_render/plugins/net_udp/udp_plugin.cpp`。

### 3.7 UDP 视频重连后只发首帧的排查/加固(2026-08-13)

- **现象**:客户端第一次 UDP 连接正常,断开后重连常出现“解出首帧后画面冻结”;客户端每秒补 IDR 仍无画面,但 render 编码器和 `net_rtc_local` 仍在持续收到编码帧。
- **加固**:`UdpPlugin` 的视频/音频发送不再依赖 `bound_count_` 单点计数,改为每次检查 `sessions_` 中是否存在 `bound_ && sess_` 的真实会话;同时每 300 帧输出一次 `udp OnEncodedVideoFrame` 节流日志,便于下次复现时直接确认是“未进入发送”还是“发送后客户端未收到”。
- **部署**:`plugin_net_udp.dll` 已重新构建并同步到 `10.0.0.70`。

### 3.8 render 端单连接断开清理(2026-08-13)

- **背景**:render 是长驻进程,`OnStop/OnDestroy` 基本走不到;真正反复发生的是网络闪断、ICE 断开、客户端直接退出造成的“单个连接死掉”。死连接只要不清干净,后续新连接就会被旧会话的缓冲/僵尸编码线程拖住。
- **RTC(`net_rtc_local`)两处缺口**:
  1. `media_data_channel` 收到 `kClosed` 时原先只把 `connected_` 置 false,不通知 `RtcServer` 退出;若 SCTP/DataChannel 先于 ICE 终态关闭,`RtcServer` 会变成“ICE 仍在但媒体面已死”的僵尸连接。
  2. ICE 从 Connected 进入 `Disconnected` 被当瞬态,但若此后长期停在该状态且不进 `Failed/Closed`,原先也不会清理。
- **RTC 修复**:
  - `RtcDataChannel::OnStateChange()`:`media_data_channel` 关闭时调 `RtcServer::RequestExit()` + `RtcLocalPlugin::NotifyRtcServerTerminal()`,由既有的 1s `SweepDeadRtcServers()` 收尾。
  - `RtcServer::On100msTimeout()`:记录 `Disconnected` 起始时间,持续 10s 未恢复则主动 `RequestExit()`。
  - `NotifyRtcServerTerminal()` 增加 `RtcServer* target` 身份校验:重连会复用相同 `conn_id`,旧连接迟到的 ICE/DataChannel 终态回调不能把新连接误标退出。
  - 已退出待清扫的 `RtcServer` 不再接收采集帧/音频,避免僵尸连接继续空转编码。
- **UDP(`net_udp`)两处缺口**:
  1. 新连接按 `stream_id` 互踢旧 endpoint 后,旧 endpoint 仍留在 `sessions_` 中;若旧客户端/旧 socket 继续发 heartbeat,会凭相同 `stream_id` 把绑定又抢回去,导致新连接无画面。
  2. 被踢或从未绑定的 `UdpSession` 没有超时清理,底层 asio2 disconnect 在裸 UDP 上可能很晚才触发,连接数/资源缓慢泄漏。
- **UDP 修复**:
  - `UdpSession` 增加 `kicked_` 与 `last_seen_ms_`;被新连接踢掉的旧会话打 `kicked_` 标记,`HandleHeartbeat()` 直接忽略,禁止抢回绑定。
  - 互踢策略与 RTC local 对齐:任意已绑定 UDP 会话都算占用,新 Hello 直接顶掉旧绑定会话(不再只按 `stream_id` 互踢)。
  - `SweepDeadSessions()` 同时清理两类会话:绑定但心跳超时(照旧发断开事件)和未绑定/被踢且 10s 无流量的陈旧会话(只摘除底层 session,不发重复断开事件)。
  - 用 `ConcurrentHashMap::RemoveIf()` 做“按身份移除”:旧会话的迟到 `bind_disconnect` / 定时清扫不会因 UDP 源端口复用(`addr:port` 字符串相同)而误删当前新会话。
- **涉及文件**:`src/gr_render/plugins/net_rtc_local/rtc_data_channel.cpp`、`rtc_server.cpp/h`、`src/gr_render/plugins/net_udp/udp_plugin.cpp/h`。

### 3.9 第二次连接“有首帧但持续卡住”(2026-08-13,修正版)

- **现象**:第一次 UDP 连接正常,第二次连接能解出首帧,之后一直无新帧;客户端每 2s 报 `Udp direct no complete video frame for >2s, request IDR`,render 侧却持续发编码帧且 FEC 窗口近乎 0 丢。
- **准确时序(即使客户端是新进程也一样)**:
  1. 新连接建立后,render 先沿旧编码序列发了一个关键帧,`frame_index` 约 836,客户端成功解出首帧;
  2. 紧接着客户端 keepalive/重连 IDR 让 render 编码器重启,后续帧 `frame_index` 回退到约 63;
  3. 客户端这个新进程的 reassembler 已把 `finished_[0]` 抬高到 836,于是把 63..836 的新流全当“迟到旧包”丢弃,直到帧号重新追上 836 才恢复。
- **修复(两层)**:
  - render 侧根本修复:`net_udp` 不再透传会回退的编码器 `frame_index`,改用 UDP 插件自己的单调帧序号(`enc_n`)写入 `VideoFrameMeta.frame_index_`,保证同一 render 进程内 UDP 帧号永远不回退。
  - client 侧防御:`UdpDirectConnection::Start()` 重连时清空 reassembler/audio jitter 状态;`GrUdpFrameReassembler` 遇到 `SOF+key` 且帧号回退时,清掉该 `mon_slot` 旧水位立即按新流处理。
- **涉及文件**:`src/gr_render/plugins/net_udp/udp_plugin.cpp`、`src/gr_deps/tc_common_new/gr_udp_protocol.h`、`src/gr_deps/tc_client_sdk_new/connection/udp_direct_connection.cpp`。

### 3.10 直连 UDP 偶发 1~3s 冻结(2026-08-13)

- **现象**:正常播放中偶尔 `Udp direct frame lost` → 请求 IDR → 下一次关键帧才恢复;客户端日志 `diff: 70` 表示中间约 70 帧被丢,P 帧不可解,冻结 1~3s。
- **根因**:高动态画面或网络突发时,当前 20% FEC 仍可能整帧丢包;一旦整帧丢,当前实现只有 IDR 恢复路径,而 IDR 从请求到编码器产出有 1~3s 延迟。
- **处理**:
  - LAN/direct 场景把默认 FEC 从 20% 提到 40%(`plugin_net_udp.dll.toml` 同步改),用可忽略的带宽换掉整帧丢。
  - 动态 FEC 策略改为“只要有整帧丢就 +10%”,且只在 `lost==0 && recovered==0` 的干净窗口才下调;避免低丢帧率(<5%)时永远停在 20%。
- **涉及文件**:`src/gr_render/plugins/net_udp/udp_plugin.cpp`、`udp_plugin.h`、`plugin_net_udp.dll.toml`。

## 4. Moonlight/Sunshine 借鉴对照(我们用了什么、在哪)

> 详细协议分析见 [gamestream_protocol_analysis.md](gamestream_protocol_analysis.md)。项目已转 GPLv3(plan 文档记录),RS 编解码为唯一直接移植的代码。

| Moonlight/Sunshine 做法 | 出处 | 我们的落地 | 位置 |
|---|---|---|---|
| RS-FEC,GF(2^8),帧级分块 | `moonlight-common-c/reedsolomon/rs.c` | **直接移植** + 自研封装 | `tc_common_new/reedsolomon/`、`gr_fec.h` |
| 帧级 parity,默认 20% 冗余 | Sunshine `config.cpp` fec_percentage=20 | SOF 扩展携带 frame_size,parity=max(1,ceil(D*pct/100)) | `gr_udp_protocol.h` ShardVideoFrame |
| 块齐即重建(够用即恢复) | `RtpVideoQueue.c` | reassembler 收到足够 shard 立即恢复,不等齐 | `GrUdpFrameReassembler` |
| FEC 状态逐帧上报驱动主机调 FEC% | `SS_FRAME_FEC_STATUS` | FRAME_STATUS 控制包,render 5s 窗口动态调 fec% | `BuildFrameStatus` / `udp_plugin.cpp` |
| IDR 请求节流(防巨型 IDR 加重拥塞) | moonlight 控制流 IDR 请求去重 | per mon_slot 1s 节流 | `udp_direct_connection.cpp` |
| 无限 GOP + 按需 IDR + intra-refresh | Sunshine NVENC/AMF 低延迟参数 | 编码器已开(无限 GOP、intra-refresh) | `encoder_thread.cpp:277-287` |
| 发送 pacing + 高精度定时 | Sunshine `stream.cpp` pacing 线程 | 20 shard/批、批间 1ms,`timeBeginPeriod(1)` | `udp_plugin.cpp` |
| 大 socket 缓冲抗突发 | (工程实践) | RCVBUF 8MB / SNDBUF 4MB | 双端 udp socket |
| 彻底丢的音频包喂 NULL 触发 Opus PLC | moonlight 音频队列 | 空 data `kAudioFrame` → `DecodeDummy` | `thunder_sdk.cpp:325-327` |
| 音频 seq + jitter buffer 按序交付 | `RtpAudioQueue.c` | `GrUdpAudioJitterBuffer`(60ms 容忍窗口) | `gr_udp_protocol.h` |
| 控制面/媒体面分离 | GameStream 4 通道架构 | ws 控制面不动,UDP 纯媒体面 + 上行小包控制 | `udp_direct` 模式 |
| UDP ping 打洞绑定会话 | `VideoStream.c:55-82` SS_PING | hello 按源地址绑定媒体会话并触发 IDR | `BuildHello` / `udp_plugin.cpp` |
| 音频 RS(4,2) 固定 FEC | `Sunshine/src/stream.cpp:1800` | ❌ 未做(增强项) | — |
| Opus inband FEC | Sunshine Opus 配置 | ✅ 已开 15% | `tc_opus_codec_new/opus_codec.cpp`、`opus_encoder_plugin.cpp` |
| RFI 参考帧失效(丢 P 不请 IDR) | `nvenc_base.cpp:795` | ✅ NVENC 已接;AMF 回退 IDR | `gr_udp_protocol.h`、`nvenc_video_encoder.cpp`、`udp_direct_connection.cpp` |
| 投机式判丢(缺包>parity 立即上报) | `RtpVideoQueue.c:213-219` | ❌ 未做 | — |
| 输入走 UDP + 1ms 微批处理 | `InputStream.c:54-60` | ❌ 未做(输入仍走 ws) | — |
| 公网包大小钳 1024 防分片 | `Connection.c:388-411` | ❌ 未做 | — |
| AES-128-GCM 加密 | `crypto.cpp` | ❌ 未做,按规划最后做 | — |
| ENet 可靠 UDP 控制通道 | `ControlStream.c` | 不采用:控制面复用现有 ws | — |
| 拥塞控制(刻意没有,码率锁死) | GameStream 设计 | 跟随:固定码率 + pacing,不做 GCC | — |

## 5. 剩余规划(按优先级)

1. **修复视频卡死**(2.1,render 编码投递 0 产出)——当前最高优先级。
2. **音频端到端有声实测**(panel 打开音频开关后验证)。
3. **RFI 参考帧失效**:丢 P 帧不请 IDR,编码器后续帧不引用坏帧(NVENC 原生支持,AMF 需查);编码器已具备无限 GOP + intra-refresh 基础。
4. **投机式判丢**:缺包数 > 可用 parity 立即上报,不等新帧到达。
5. P3:输入迁 UDP(1ms 微批处理)、公网 MTU 钳 1024、UDP 不通回退 ws。
6. **加密(AES-128-GCM)**:按既定顺序最后做;公网部署前必须完成。

## 6. 关键文件索引

- 协议/组帧/FEC/音频 jitter:`src/gr_deps/tc_common_new/gr_udp_protocol.h`、`reedsolomon/`、`gr_fec.h`
- render 发送端:`src/gr_render/plugins/net_udp/udp_plugin.cpp`
- 客户端接收端:`src/gr_deps/tc_client_sdk_new/connection/udp_direct_connection.cpp`
- 音频解码/PLC:`src/gr_deps/tc_client_sdk_new/thunder_sdk.cpp:316-345`
- 编码诊断:`src/gr_render/app/encoder_thread.cpp:69-74`
- 单测:`src/gr_deps/tc_common_new/tests/test_gr_udp_protocol.cpp`(31 例)
- 探针:`scripts/udp_fec_probe.mjs`、`scripts/udp_audio_probe.mjs`

## 7. 部署到 10.0.0.70 的固定流程(2026-08-13 实跑)

> 目标机:`10.0.0.70` / `Administrator`。凭据见 `tests/.remote_admin.md`(未入库),不要写进本仓库可提交文档。

> 只改 render 插件(如 `plugin_net_udp.dll` / `plugin_net_rtc_local.dll`)时,不必重跑全量
> `build_client.bat`。在 VsDevCmd 环境里只编这两个目标:
>
> ```bat
> call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
> cmake --build build_official --target plugin_net_udp plugin_net_rtc_local -j18
> ```
>
> 产物在 `build_official\src\gr_render\plugins\net_udp\plugin_net_udp.dll` 和
> `build_official\src\gr_render\plugins\net_rtc_local\plugin_net_rtc_local.dll`,直接按第 4 步
> 覆盖到远端 `gr_plugins\`,不必整目录 `robocopy build_official\dist`。

1. **构建客户端包**

   ```bat
   cmd /c build_client.bat incremental
   ```

   成功后产物在 `build_official\dist\`。

   > 铁律:**任何 render 相关修复,都必须把 `GammaRayRender.exe` 和涉及的 `gr_plugins\*.dll` 重新部署到 `10.0.0.70`;只更新本地 `dist` 不会让远端 render 生效。**

2. **建立 SMB 管理会话**

   ```powershell
   $pass = '<Administrator 密码>'
   & net.exe use '\\10.0.0.70\IPC$' $pass '/user:Administrator'
   & net.exe use '\\10.0.0.70\C$' $pass '/user:Administrator'
   ```

3. **停止服务并退出所有相关程序**

   优先用远端 `GammaRayServiceManager.exe stop` 直接停服务，不要只用 `sc stop`:

   ```powershell
   # 远端 App 目录下
   # C:\Program Files\GoDesk\App\GammaRayServiceManager.exe stop
   ```

   再清理所有 GammaRay 进程。这里用远端一次性 SCM 服务执行一个本地批处理:

   ```powershell
   $remoteBat = @'
   @echo off
   "C:\Program Files\GoDesk\App\GammaRayServiceManager.exe" stop > C:\Users\Public\_svc_stop.txt 2>&1
   taskkill /f /im GammaRay.exe /t >nul 2>&1
   taskkill /f /im GammaRayRender.exe /t >nul 2>&1
   taskkill /f /im GammaRayClientInner.exe /t >nul 2>&1
   taskkill /f /im GammaRayService.exe /t >nul 2>&1
   taskkill /f /im GammaRayServiceManager.exe /t >nul 2>&1
   taskkill /f /im GammaRaySysInfo.exe /t >nul 2>&1
   taskkill /f /im GammaRayUserProxy.exe /t >nul 2>&1
   taskkill /f /im GammaRayCrashReporter.exe /t >nul 2>&1
   echo DONE > C:\Users\Public\_stop_gammaray_done.txt
   '@
   Set-Content -LiteralPath '\\10.0.0.70\C$\Users\Public\_stop_gammaray_70.cmd' -Value $remoteBat -Encoding Ascii
   & sc.exe '\\10.0.0.70' delete godesk_rk | Out-Null
   & sc.exe '\\10.0.0.70' create godesk_rk binPath= 'cmd /c C:\Users\Public\_stop_gammaray_70.cmd'
   & sc.exe '\\10.0.0.70' start godesk_rk
   Start-Sleep -Seconds 45
   & sc.exe '\\10.0.0.70' delete godesk_rk | Out-Null
   ```

   注意:一次性服务 `godesk_rk` 的 `sc start` 会报 1053(服务未在启动超时内响应),但批处理实际会执行完;以 `C:\Users\Public\_stop_gammaray_done.txt` 出现为准。

4. **覆盖部署 dist**

   ```powershell
   & robocopy.exe 'D:\source\GoCloud\GammaRayPremium\build_official\dist' `
       '\\10.0.0.70\C$\Program Files\GoDesk\App' `
       /E /XO /R:3 /W:2 /NP /NFL /NDL /NJH /NJS
   ```

   `/E` 只覆盖/补文件,不用 `/MIR`,避免删掉远端多出的 `GammaRayGuard.exe` 等文件。robocopy 退出码 `<8` 均为成功;常见 `3` 表示有拷贝 + 存在额外文件。

5. **核对关键文件哈希**

   至少核对:

   - `GammaRay.exe`
   - `GammaRayClientInner.exe`
   - `GammaRayRender.exe`
   - `GammaRayService.exe`
   - `GammaRayServiceManager.exe`
   - `gr_plugins\plugin_net_udp.dll`
   - `gr_plugins\plugin_net_ws.dll`

6. **用 GammaRay.exe 启动**

   GUI 程序不要用 `sc create` 的 SYSTEM session 启动,否则会跑到 session 0 看不到界面。用远端计划任务 `/IT` 以登录用户交互启动:

   ```powershell
   $pass = '<Administrator 密码>'
   $tn   = 'GammaRayStart_' + (Get-Date -Format 'yyyyMMdd_HHmmss')
   $start = (Get-Date).AddMinutes(2).ToString('HH:mm')
   $tr    = "'C:\Program Files\GoDesk\App\GammaRay.exe'"
   & schtasks.exe @(
       '/create','/s','10.0.0.70','/u','Administrator','/p',$pass,
       '/ru','Administrator','/rp',$pass,
       '/it','/sc','once','/st',$start,'/et','23:59',
       '/tn',$tn,'/tr',$tr,'/f'
   )
   & schtasks.exe @('/run','/s','10.0.0.70','/u','Administrator','/p',$pass,'/tn',$tn)
   ```

   查询到任务 `Status: Running` 即已拉起。日志仍看 `\\10.0.0.70\C$\Users\Public\GoDesk\gr_logs\`。
