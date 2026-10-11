# QUIC 未发送队列准入候选：实现与实测

本批完成排队额度候选并安装到 90，**卡帧整体未解决**。后续执行 [持续任务](../../video_stutter_resolution_task_20261011.md)。

## 实现依据

上一 ACK 分析证实，64KiB QUIC FIFO 在回程确认停顿时继续接受后续视频，增加恢复负担。新增 `VideoQueueAdmission` 按应用视频预算的 16ms 名义发送量限制未发送字节，最小四片（4400）、最大 32768；当前 20Mbps 应用额度为 32768。它是队列容量限制，不保证真实驻留时间只有 16ms。

`PacedMediaBatch` 在每批提交前检查余量，拥塞时通过 1ms 异步 timer 让出执行线程，保持原 deadline/取消行为。编码前检查同一额度，最多保留两帧流水线；已编码帧不随意丢弃参考片。音频并发写入可能令观测总量短暂超过视频准入额度，因此不是精确的所有 DATAGRAM 硬限额。

64KiB 容量对应现有 Rust `TransportEndpoint::bind` 配置，并由真实端点测试守护；只读 noq-proto 1.3.0 `connection/datagrams.rs:167` 确认可用空间为配置容量减未发送字节。未修改 Rust/第三方、拥塞控制或发送缓存配置。

Sunshine `3cba9baebac882b336be3ebe129ee612cb189853` 的 `stream.cpp:1714–1766` 支持跨帧发送节奏；它直接发 UDP，没有此处 QUIC FIFO/ACK 门槛。Moonlight common `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac` 的 `VideoDepacketizer.c:506–525` 表明丢参考队列需要明确 IDR 恢复，不能任意丢片后当作正常连续预测流。原参考树只读。

## 本地验证

45 项媒体、7 项 UDP failure、19 项 Frontend 通过；一个显式 Relay 故障用例跳过，复用既有故障证据。新增模拟 160ms 阻塞验证队列有界、控制能运行、恢复后每片保序；等待超时、从回调停止、重复停止和释放仅完成一次；真实 QUIC 缓存容量和两帧编码准入通过。命名/所有权检查通过。六份 development runtime 与构建树 SHA 相同，见 `20261011_queue_build.json`。

## 实际连接结果

采样排除关闭客户端及应用宽限阶段，不进行截图、抓包或混传。已有应用 20000kbps 保持。

| 路径/样本 | 有效时长 | FPS（完整帧交付） | P99 间隔 | 最大间隔 | >100ms |
| --- | --- | --- | --- | --- | --- |
| direct 游戏基线 | 20s | 60.02 | 37.020ms | 50.339ms | 0 |
| direct 游戏候选 | 20s | 59.30 | 44.126ms | 57.270ms | 0 |
| 强制 90 Relay 游戏候选 | 10s | 54.45 | 91.306ms | 198.132ms | 5 |

这些是完整帧交付指标，不冒称显示器实际呈现测量。两次公网条件不同，direct 候选有 QUIC 丢包和参考恢复；所有收到的 wire 1–1206 对应提交成功、最慢 16.276ms。没有同批旧版本强制 Relay 对照，不能据此宣称改进或证明性能回退。Relay 性能明确未通过。

Relay 事件：wire 280 的提交等待 146.242ms，未发送队列峰值 30633/32768，最终完整提交；随后 `datagram_space=65536`、`queue_ready=true` 时仍因远端完整帧额度暂停，进度年龄约 108–139ms。接收端五个长间隔均为完整可用帧之间的空档，最终组帧调用仅 8–45µs，无参考失效。这不是“组帧耗时 198ms”。是否存在额外误判/过度背压要用回放验证，不立即取消飞行额度。

WebView 5 秒只作连接检查：交付约 33.51FPS/max 642.201ms，源端编码自身有对应长间隔，已收到 wire 1–145 全部提交成功、最慢 8.650ms、无准入等待。不能把加载中的网页当持续 60FPS 源，也不能用此样本结项视频性能。

## 安装与清理

90 目录 `D:\112233\queue-admission-20261011`，使用完整 Setup 安装 Cloud Node 3.3.97，314 文件核验。

- Setup：`96B9A92EF0D81CC1A43E7FC99BAF8D9E86CDB9BDA8B0299E8776757B418E7310`
- Manifest：`6C741EF17F0DA71487C1D7B40B7E390D5452CD87A84A47F1E3201280E87027AB`
- Render：`99B8EEDF75F50ABD661DB7728382C8DD1E9DF9C259EE99FC4F57C3696CCA63ED`
- Client：`1DA5C8661D891F313CC077308FEFCE5D874AA25BD034E030B7F637763B34E653`
- Service/Panel 保持上一包身份，Server、90/BJ Relay 软件与配置未变。

268 资源会话均 closed，三个候选连接及基线按宽限停止，90 仅桌面 Render 7580/session 2，Service Running，trace 变量为空，Windows 登录会话保留。没有 NIC/网络配置改动。

同前缀 JSON/日志包含安装核验、dist SHA、测试、双端原始日志（gzip）及采样元数据。Relay 收集时触发 50MiB 日志轮转，先确认失败，再从 `.1.log` 与当前文件合并恢复该批日志，没有重跑测试。`analyze_queue_admission.py` 重建 direct 对比；Relay 分析与剩余时间线见 `20261011_queue_relay_summary.json`、`20261011_queue_relay_gaps.json`。

下一步：S1 回放 Relay wire 270–290 的接收进度和准入状态，建立可重复复现，先证明额外暂停在哪里，再修改。
