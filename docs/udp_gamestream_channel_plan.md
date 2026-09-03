# 云游戏 UDP 传输通道(GameStream 风格)实施规划

> 关联文档:[gamestream_protocol_analysis.md](gamestream_protocol_analysis.md)(Moonlight/Sunshine 协议分析)
> **当前状态见 [udp_gamestream_channel_state.md](udp_gamestream_channel_state.md)**(实现清单、未解决问题、Moonlight/Sunshine 借鉴对照、排障记录)
> 状态:P2 全部完成(2026-08-12);音频已迁 UDP 但实测未过(2026-08-13,视频卡死+无声,详见状态文档)
> 方案 A(自定义轻量协议)| 2026-08-12 立项

## 目标

在 render 的 `net_udp` 插件内实现一条云游戏级 UDP 媒体通道:裸 RTP 风格分包 + RS-FEC + IDR/丢失上报恢复,替代 WebRTC 用于"render 公网 / client 局域网"及直连场景的原生客户端连接。web 客户端继续走 WebRTC,不在本期范围。

**旧 UDP(KCP + proto 广播)全部废弃**,`net_udp` 插件从零重写,只保留插件壳。设计思想来自 GameStream,代码自研(仅 RS 编解码可移植 `moonlight-common-c/src/reedsolomon/rs.c`,项目已转 GPLv3)。

## 现状(勘察结论)

- `src/px_render/plugins/net_udp/udp_plugin.cpp`:asio2 KCP 骨架,未启用(`config_premium.cmake:8` OFF)、无 toml、无 install 规则、收包回调被注释且签名过期。
- 客户端:`px_client_sdk_new/connection/udp_connection.cpp` 是配套废弃 KCP 实现;无 UDP 媒体通路。
- 可复用挂接点:
  - 编码帧:`PxNetPlugin::OnEncodedVideoFrame(...)`(`px_plugin_interface.h:199`,rtc_local 在用)
  - IDR 请求:`CallbackEvent(PxPluginInsertIdrEvent{mon_name})` → `plugin_event_router.cpp:110-119`
  - 会话绑定:`SyncInfo(NetSyncInfo)`(`plugin_net_event_router.cpp:654-664` 对 `kUdpKcp` 下发)
  - 信令:ws 插件 HTTP 端点模式(参照 `http_handler.cpp:121` `/alloc/local/rtc`)
  - 客户端解码链接入:合成标准 `kVideoFrame` proto(首帧必须 IDR、不回 Ack,参照 `webrtc_local_connection.cpp:296-366`)

## 总体设计

**控制面/媒体面分离**(GameStream 核心思想,改动最小):

```
client                                render
  |── ws 控制通道(现有,不动)──────────→|  hello/heartbeat/输入/控制/被踢通知
  |                                    |  (现有状态机、重连、安全密码全复用)
  |── UDP 媒体通道(新增)←─────────────|  视频 shard + FEC / 音频
  |── UDP 上行控制(新增,小包)────────→|  media_hello(绑定)/heartbeat/IDR请求/丢帧反馈
```

- `udp_direct` 模式:客户端同时建 ws(仅控制)+ UDP(仅媒体)。ws 带宽极低,可靠消息全不动,客户端状态机零改动。
- UDP 会话绑定:ws 握手后客户端发 `MEDIA_HELLO{stream_id, token}` 到 render UDP 端口,render 按源地址建会话(等价 Moonlight SS_PING 打洞)并触发该屏 IDR。
- **KCP 弃用**:可靠重传对视频是负优化,改 asio2 裸 UDP。

### 包格式(自定义,不追求 wire 兼容)

公共头(8B):`magic(2)='GU' | version(1) | pkt_type(1):1=video 2=audio 3=ctrl | token(4)`

视频 shard(24B 头):
```
frame_index(u32) | timestamp_ms(u32) | data_shards(u16) | parity_shards(u16)
shard_index(u16) | flags(u8: SOF|EOF|KEY) | fec_block(u8) | payload_len(u16) | payload(≤~1300B)
```

上行 ctrl:`MEDIA_HELLO / HEARTBEAT / IDR_REQUEST{mon_name} / FRAME_STATUS{frame_index, received, lost}`

## 分阶段实施

### P0 — 插件基建
1. `config_premium.cmake` 打开 `PLUGIN_NET_UDP_ENABLED`。
2. 新增 `plugin_net_udp.dll.toml`;`scripts/collect_dist.py` 移除 `plugin_net_udp.dll` 排除项。
3. 重写 `udp_plugin.cpp`:裸 UDP server;收包接 `OnClientEventCame`(补全签名);连接事件填全字段。
4. 编译通过、插件可加载、ws 控制面不受影响。

### P1 — 视频面打通(核心)
render 端:
1. override `OnEncodedVideoFrame`:帧→shard 切分 + 头封装,发到对应会话。
2. `PostProtoMessage` 过滤 `kVideoFrame`/`kAudioFrame`,控制类照旧。
3. `MEDIA_HELLO` 校验 token → 建会话 → 触发该屏 IDR。
4. 上行 `IDR_REQUEST` → 定向 InsertIdr;`FRAME_STATUS` 先记日志。
5. 互踢:同 stream_id 新 MEDIA_HELLO 顶掉旧会话(经 ws 控制面发 `kConnectionTakenOver`)。

client 端:
1. proto:`ClientNetworkType` 加 `kUdpDirect = 4`。
2. 新增 `connection/udp_direct_connection.{h,cpp}`:收包→按 frame_index 组帧(帧边界判丢,不等乱序)→ 合成 `kVideoFrame` proto 送解码;丢帧发 `IDR_REQUEST`。
3. `NetClient::Start()` 加分支:WsConnection(控制)+ UdpDirectConnection(媒体)。
4. 字符串映射 `ct_stream_item_net_type.h` + `ct_main_ws.cpp` 解析。
5. 面板:设备条目 `use_udp_` 开关、`app_stream_list.cpp` 选择逻辑、统计面板类型显示。

验证:局域网双机 1080p60,对比 udp_direct 与 webrtc_direct 延迟 + 丢包恢复。

### P2 — FEC + 音频
1. ~~移植 RS 编解码,render 帧级 FEC(20%),client 恢复;失败再 IDR~~ **已完成**:
   - RS 库移植 `moonlight-common-c/reedsolomon/rs.c`(BSD)→ `px_common_new/reedsolomon/`,C++ 薄封装 `px_common_new/px_fec.h`(`PxFec::Encode/Decode`)。
   - 协议演进(两端一起重建,kVersion 保持 1):SOF 扩展加 `frame_size(u32)`;新增 `kFlagParity=0x8`,parity 包 = 基础头 + P 字节校验块(P = mtu - 24,整包正好 mtu);所有包 `parity_shards` 填实际值,`fec_block` 恒 0(一帧一块)。
   - 发送:`ShardVideoFrame(..., fec_percent)` parity = max(1, ceil(D*percent/100)),D+parity > 255 退化为无 FEC;`net_udp` 插件 `fec-percent` 配置(默认 20,0=关闭)。
   - 接收:`PxUdpFrameReassembler` slot 扩到 D+parity,统一存 P 字节保护块;「够用即恢复」(distinct 块数 == data_shards 即 RS 重建),重组帧按 frame_size 精确截断;shard 0 缺失时 mon_name/分辨率从恢复块取;恢复不了才 DeclareLoss 走原 IDR 路径。
   - 单测 15 个全绿:FEC 布局、丢 1 个/丢 shard 0/丢满 parity 个恢复、丢 parity+1 判丢、fec=0 兼容旧行为、逐 shard 位置遍历恢复(FEC_VALIDATION 风格)。
2. ~~音频走 UDP(Opus 裸帧 + 序号,丢包 PLC)~~ **已完成**:
   - 协议:`kPktAudio=2`,common(4B) + `seq(u32)|timestamp_ms(u32)|payload_len(u16)` + Opus payload;`BuildAudioPacket/ParseAudioPacket`(size 精确匹配)。
   - render:`UdpPlugin::PostProtoMessage` wire 级手扫 `tc.Message`(不引 protobuf 头,仿 `ws_server.cpp` IsMediaFrameMessage)提取 kAudioFrame(40) 的 `AudioFrame.data`(field 80 子消息内 field 5),打 UDP 音频包广播绑定会话;50pps 小包不走帧内 pacing;ws `IsMediaFrameMessage` 过滤加 type==40(仅 `udp_media_` 客户端)。
   - client:`PxUdpAudioJitterBuffer`(px_udp_protocol.h,header-only)按 seq 重排交付,缺口等 2 帧(60ms)判丢,单轮最多连判 5 个,缓冲上限 16 包淘汰最老(先 Drain 再淘汰防误删 expected_);`udp_direct_connection` 合成标准 `kAudioFrame` proto(extra=`udp_synth`)注入既有音频管线,丢帧合成**空 data** proto(extra=`udp_lost`);`sdk_net_client` 对 kUdpDirect 过滤 ws 侧音频(防重复解码)。
   - PLC:`thunder_sdk` 音频回调对空 data 帧调 `OpusAudioDecoder::DecodeDummy(frame_size)` 补 20ms,正常帧走原 Decode,管线零改动。
   - 单测 6 个:音频包 build/parse 往返(含截断拒绝)、jitter 顺序/乱序/缺口判丢后继续/中途加入不补历史/cap 淘汰最老。
   - 后续增强(未做):Opus inband FEC、RS(4,2) 音频冗余块。
3. ~~`FRAME_STATUS` 驱动 render 动态调 FEC 百分比~~ **已完成**:
   - 协议:`BuildFrameStatus/ParseFrameStatus`(定长 `frame_index(u32)|received(u16)|lost(u16)`,不走 ParseCtrl 字符串路径);reassembler 新增 `on_frame_status_`,每帧恰好触发一次——完成帧 received=网络实收数据块、lost=FEC 恢复块;判丢帧 received=已收、lost=缺失(配合 finished_ 去重,迟到包不重复触发)。
   - client:每帧一条 FrameStatus 经 UDP async_send 上行(非阻塞)。
   - render:5s 窗口聚合(完成帧/判丢帧/FEC 恢复块);判丢帧与 kCtrlIdrRequest 1:1,故 lost_frames 按 IDR 请求计。loss_rate = lost/(complete+lost) > 5% 且 fec < 60 → +10(LOGW);< 1% 且高于 toml 初始值 → -5(LOGI);窗口摘要每窗 LOGI 一行。`fec-percent` 初始值仍是下限,上限 60%。

### P3 — 公网加固与调优
1. AES-128-GCM(密钥经 ws 下发,IV=序号+方向字节)。
2. ~~输入迁 UDP 不可靠通道 + 关键事件可靠化~~ **暂缓**：控制输入继续走直连 WebSocket；只有实测证明其为可感知瓶颈时，才以 ACK、状态快照和 WS 可靠降级为前提重新立项。
3. 编码器低延迟参数(单帧 VBV、无限 GOP、按需 IDR、intra-refresh)——与协议无关,可提前独立做。
4. ~~包大小公网钳制(1024B)、pacing、UDP 连通性自检与自动回退直连 WebSocket 媒体面~~ **已实现客户端自检与回退**：首个有效媒体帧前 4 秒超时、或运行中 UDP watchdog 超时，客户端通过已认证的 WebSocket 控制通道发送可靠切换信号，Render 在同一连接上恢复音视频下发并撤销 UDP association。回退不新建连接、不重复兑换一次性 Ticket。公网 MTU 默认与实机矩阵仍待收口。

## 测试计划

- **单元测试**(随 `build_official_tests.bat`,gtest):shard 切分/组帧往返、乱序/丢包判丢、帧边界状态机、包头序列化反序列化;P2 加 RS 编解码往返、FEC 恢复，以及 EOF 到达后缺失超过 parity 上限时的立即判丢。UDP 回退状态机覆盖首媒体成功、超时/watchdog 竞争回退和停止后的迟到回调。
- **集成验证**:P1 双机实测(见上)。

## 风险

- 子模块连锁改动:`px_message_new`、`px_client_sdk_new` 需分别提交。
- 双通道生命周期：WS 断开等于会话断开；UDP 超时通过原 WS 可靠控制消息切到 WS 媒体，不使用 UDP 承载状态逻辑。普通 UDP-direct 会话的文件传输也复用这条已认证 WS，只有独立 file-only 模式才单独连接 `/file/transfer`。
- 互踢沿用 takeover 体系(经 ws 发 550),UDP 插件不自创。

## 方案 B(备选,暂缓):wire 兼容 GameStream

包格式/RTSP/nvhttp 全对齐,Moonlight 客户端可直接连。工作量约 A 的 2~3 倍,与现有面板/ws 体系割裂。A 验证收益后再评估。
