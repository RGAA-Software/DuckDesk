# QUIC 包号边界定位（2026-10-11）

接续 `20261011_video_diagnostics.md`，目标是区分应用未取包、QUIC 接收缓存丢弃，以及 QUIC 组包后未到达对端。
本批未修改源码、运行产物、发送/FEC/恢复策略或第三方代码。安装身份继续为 `iroh-video-diagnostics-20261011`。

## 方法和清理

使用已有 `PIXELS_IROH_TRACE_FILE` / `PIXELS_IROH_TRACE` 启用
`warn,noq_proto::connection=trace,iroh::socket::transports::ip=debug`，一次约 20 秒正常 GameHook direct 连接。
90 临时 Machine 环境变量只供新进程读取，测试后核对原值并恢复为空；没有改服务 Environment、没有重启服务。
本机仅子进程继承 trace 环境。测试退出和应用宽限停止成功，本机无测试 Client，90 只保留桌面 Render PID 27888。
90 工作区 `D:\112233\iroh-video-diagnostics-20261011`，未启用混传负载、抓包、驱动或网卡实验。

服务端原始 trace 31,921,505 bytes，SHA `B31CF447B350AD2F5951451B32B376F09D4582D73DC53D4A43BD682BA0646B60`。
详细 trace 有同步写盘开销，不能当作无扰动性能基准。原始媒体逐帧日志仍保留；本轮不再追加相同采样。

## 新证据

- 内部分析窗口 wire 31–1184，共 1154 帧，1153 完整、1152 交付；23443 个已接受视频分片中 16 个未观测。
- wire 1011 缺末尾两片冗余，仍完整交付；wire 1012/encoder 1149 缺 14 片，只有 15/24 个必要分片，无法恢复。
- wire 1013 完整但等待参考，wire 1014/encoder 1151 为恢复帧，重新交付。1011 到 1014 交付间隔 19.463ms。
- 本轮四个窗口 59.6–60.6 FPS，最大间隔 66.4ms，未复现上轮 118/124ms 停顿。不能把本轮短暂恢复当作修复。

更低一层的 QUIC 包号对齐：该应用使用 Data packet space / IP path 1，QAD 的 path 0 排除。
服务端构造了以下两组包，客户端完整 trace 中均未出现；后续 ACK 也排除这两段：

| QUIC 包号（含端点） | 包数 | 内容 |
|---|---:|---|
| 24452–24455 | 4 | 2 个 DATAGRAM，2 个 STREAM |
| 24472–24488 | 17 | 14 个 DATAGRAM，2 个 STREAM，1 个 PATH_ACK_ECN |

客户端 ACK 原文包含半开区间 `[24318..24452,24456..24472,24489..24494]`。
服务端随后两次 `packets lost` 分别报告 4、17 包，合计 21，与包号差集一致。
其中 16 个 DATAGRAM 均长 1100 bytes，与媒体缺片数量和服务端发送时段一致；这里没有将加密 QUIC 包号冒称为直接解析出的 RTP 帧号。
STREAM 缺失的字节区间 85786–85829、85829–85872 后来分别重传在 pn 24517、24518，客户端确认收到对应 offset/len。
这表明同一次事件同时影响 DATAGRAM、可靠 STREAM 和 ACK，并非只在视频组帧/FEC 处理处丢弃。

双方 `dropping stale datagram` 为零；该秒服务端无拥塞窗口阻塞，缺包生成区间（49.562–49.589 秒）没有 pacing 阻塞。
该秒后续七条 pacing 等待均小于 1ms。两端没有 WARN/ERROR 级日志；不能据此推断每次 socket 发送均成功。

## 源码口径核对

原始参考和依赖均只读，版本来自本机实际锁定源码：

- `noq-proto 1.3.0/connection/datagrams.rs`：接收缓存满时先打印 `dropping stale datagram` 再丢旧包。
- `noq-proto 1.3.0/connection/packet_builder.rs`：`wrote packet` 在加密组包完成后产生，并非 WSASendMsg 成功回执。
- `netwatch 0.19.3/udp.rs`：`UdpSender::poll_send` 调用 `noq_udp::UdpSocketState::send`。
- `noq-udp 1.3.0/windows.rs`：`send` 将非 WouldBlock 错误记录后返回 Ok；实际系统边界是 WSASendMsg，现有 trace 不逐次记录其成功/耗时。

因此本批证明的边界是 **QUIC 已构造包 → 对端 QUIC 尚未接收**，排除了这次事件中的 QUIC 接收缓存丢弃和上层组帧丢弃。
还没有定位发送 socket、两端系统栈、驱动或中间链路中的具体位置。上轮无新增缺片的 124ms 背压事件本批未复现，保持独立未解决。
下一步应补这个系统收发边界的证据；不继续调整编码、FEC 或参考恢复定时器，也不据此认定网卡硬件故障。

## 可复核证据

同目录 `20261011_quic_boundary_` 前缀：`server_trace.log.gz`、`client_trace.log.gz`、
`server_media.log.gz`、`client_media.log.gz`，压缩前后 SHA 在 `logs.json`。
`direct.json` 为媒体分片对齐，`packets.json` 为 QUIC 包号差集/ACK/重传，`live.json` 为连接与宽限退出记录，`after.json` 为会话清理状态。
运行 `python docs/validation/iroh_transport/analyze_quic_boundary.py` 可从压缩原始 trace 重建包号报告并核对 ACK 差集和重传接收记录。
