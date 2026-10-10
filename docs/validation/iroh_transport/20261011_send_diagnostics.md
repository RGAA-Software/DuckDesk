# 混传发送诊断（2026-10-11）

本批响应“先看看实现，打 log 找原因”。保持用户停止联网压测的约束：只读源码、增加日志、聚焦 Release 编译和本机测试；没有连接 90/BJ、远程安装或更改网卡设置。不能把本机测试通过当作重置/公网抖动修复。

## 实现核对

- 视频 `MediaDatagramSender` → `PacedMediaBatch(unpaced=true)` → FFI `send_datagram_wait`。约 64 KiB 的应用批次之后让出执行器；实际线上分包、拥塞控制和 pacing 由 QUIC 执行。应用批次不是 NIC 批次，写入完成不是对端收帧。
- 文件独立可靠流、优先级 -100、通常 256 KiB 回执额度，支持一个超额度合法消息独占（上限 1 MiB）。回执释放额度后生产者可继续写入；仍共享连接的拥塞窗口与底层链路，独立 stream 不等于物理隔离。
- 本地 `noq-proto 1.3.0/src/connection/mod.rs` 中组包先 DATAGRAM 后 STREAM，因此不能直接断言文件 stream 优先于视频。可靠流优先级也不能撤回已排入底层的数据。
- 默认允许 segmentation offload；`noq-udp 1.3.0/src/windows.rs` 通过 UDP_SEND_MSG_SIZE 探测，失败为单段，成功报告最高 512 段。这只是库的能力探测上限，不证明当前网卡实际使用、实际批次大小或导致 NDIS 重置。
- Sunshine HEAD `3cba9baebac882b336be3ebe129ee612cb189853`，`src/stream.cpp` 1604–1615 / 1714 起：直接 UDP 批次约 64 KiB / 64 包、跨批次/帧 pacing。其 Windows SO_SNDBUF 注释是排查线索，不能把直接 UDP 的限制直接等同于 iroh/QUIC。Moonlight core HEAD 再核实为 `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac`；本批不改已撤回的恢复重试策略。
- 上批强制 Relay 同样出现 NDIS 10400，因此“仅 UDP 发包有问题”不足以解释全部现象。确认重置事件，不确认硬件损坏、不排除产品负载触发因素。

## 新日志

| 事件 | 用途 |
| --- | --- |
| `iroh.reliable_send` | 每通道约 5 秒汇总本地接受字节、待发送字节、最大工作队列等待/Channel::Send 耗时、≥20ms 次数；失败立即记录状态码。耗时包含通道串行化及 QUIC 写入等待。 |
| `iroh.file_flow` | 控制接收循环定期检查，记录额度占用、背压次数及仍有未确认数据时的进展停滞时间；即使回执停了也能输出。新一轮从零占用开始计时，不把两次传输间闲置算停滞。 |
| `iroh.video_admission` | 五秒媒体汇总增加首次写入前排队、最大单 datagram 写入耗时及最大应用批次字节。 |
| `iroh.video_send_slow` | 帧失败或本地提交≥50ms 时记录 stream/真实帧编号、首写前排队、总耗时、单次写入等待、已接受包数和拥塞窗口；每秒最多一条。 |
| `iroh.session_closed` | 首次异常关闭的触发操作、通道/错误码、路径、丢包累计、队列、拥塞窗口和是否可重连。保留原关闭/回调顺序。 |

通道 kind：1 控制、2 输入、3 剪贴板、4 文件、5 RDP；path：0 未知、1 direct、2 Relay；error：0 没有底层错误码、1 超时、2 关闭、3 无效、4 缓冲不足、5 失败。帧取消/到期可能 `local_accepted=false,error=0`，不能将 error=0 视为已送达。队列等待从编码提交到首次 datagram 写入，包含打包/应用队列/执行器调度。统计不是逐包线速证明；日志不包含业务内容、凭据或文件内容。

## 验证和下一步

- 本机 `iroh_media_datagrams` 36 项、`iroh_sdk_connection` 4 项通过；覆盖背压、独立控制通道、回调中关闭和排队取消。媒体测试显式绑定 loopback、空 Relay；SDK 使用本机端点、无 Relay。无公网实机压测。
- Client、Cloud Node 内置 Client、Render 通过聚焦 Release 构建，发布至各自 development dist，SHA 见 `20261011_send_diagnostics_build.json`。原始本机测试日志 `20261011_send_diagnostics_tests.log`，已出现新增诊断事件。
- 90 安装未变，仍为 `iroh-hook-frame-identity-20261010` 完整包，本轮新增 Render 日志尚未在 90 生效。没有改变驱动、拥塞控制、发送窗口、媒体恢复或持久配置。
- 下一步在用户重新允许实机测试后，用诊断版本对齐视频首写前排队/QUIC 写入等待、文件回执停滞、双端收发及系统重置时间；需要远端日志时用完整包部署 90。先按证据选具体环节，不先改驱动卸载或再次盲调恢复定时器。当前根因未确定。
