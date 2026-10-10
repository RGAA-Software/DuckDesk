# 编码后额度拒绝与弱网在途积压修复

状态：两个针对性修复已构建并在 90 实测。编码后软额度拒绝已消除；本轮 Relay 弱网没有秒级交付停顿且自动恢复。仍有短时采集暂停和限速开始时的积压，不能宣布所有画面抖动消失。

## 范围与依据

本轮只处理用户指定的两个画面问题。WebView 页面加载耗时不作为核心故障或 60 FPS 验收条件；主要实测使用游戏。

前一轮日志确认编码后额度拒绝造成参考链空洞。新增实际 QUIC 回归先在旧逻辑失败：编码前 `CanEncodeVideo` 返回真，`SendVideo` 因整帧跨过在途字节限制返回假。失败证据 `20261010_frame_admission_before.xml`。测试覆盖有/无 FEC 的不同分片开销；第一次 Relay 回归因测试帧总字节未跨额度而失败，已修正测试尺寸，保留原日志。

1. 常规在途额度仅在编码前控制采集，已编码帧不再因整帧跨过软额度而丢弃。实际字节完整计入欠账，下次采集等待收帧确认。原本 4 帧/4MiB 的本地发送队列硬上限以及停止、断链、发送超时仍有效；不是无界排队或承诺断网不丢帧。
2. 在途字节目标按累计确认的实际分片字节估算，250ms 更新，时间范围为最小 RTT 加 100ms（两次反馈周期），保留四个近期帧的空间，限制在 32–256KiB。防止低带宽路径仍长期允许固定 256KiB 积压；稀疏探测、乱序反馈拒绝和停收约束不变。它是采集背压，不替代 QUIC 拥塞控制。

源码快照 `backup/iroh_encoded_frame_delivery_20261010/manifest.json` 保存了修改前的脏文件。没有新增令牌/票据或改动授权。

## 参考与验证

Sunshine `D:/source/Sunshine` HEAD `3cba9baebac882b336be3ebe129ee612cb189853`，`src/stream.cpp` 1605 起按批发送、跨帧 pacing；没有在预测帧编码后按本项目这个确认额度再次拒绝。Moonlight shared core HEAD `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac` 的参考链/恢复原则沿用上一轮分析。只读参考未修改。

直连及私有 TLS Relay 各 2 个 CTest 目标通过，包括 31 个媒体测试和 frontend 生命周期用例；`20261010_frame_admission_direct.xml`、`20261010_frame_admission_relay.xml`。行为覆盖跨额度预测链、弱网窗口缩小/恢复、反馈乱序、停止时回调和可靠流独立性。

Release Client、Node Client、Render 已同步开发 dist，四对运行文件哈希一致，见 `20261010_frame_admission_development_hashes.json`。初次完整打包因 D 盘空间不足不能写入输出失败；将本任务先前两个候选完整移动到 C 盘开发归档，每个 319 文件逐一核验哈希，记录 `20261010_candidate_archive_moves.json`，未删除源代码或运行安装目录。重试完整打包，不用手工替换 90 的运行 EXE 代替安装。

## 第一候选的实测与后续调整

`iroh-frame-admission-20261010` 已通过 Setup 安装到 90，314 文件一致、服务 Running。身份见 `20261010_frame_admission_delivery.json`，Setup SHA `25763DE7DD30F8DE9F060CF5EF82A4B488AEB01DDAEE7E4E51A2FF2334F68DF8`。开发/候选包已归档到 C 盘，见 `20261010_admission_candidate_archive.json`。

- 游戏 direct：50.4–60.0 交付 FPS，最大交付间隔 266.1ms，0 IDR/5 RFI；有实际网络丢包，发送端 busy/failed 为 0。
- 游戏 Relay：49.2–60.0 FPS，最大 638.0ms，0 IDR/0 RFI。最大事件 frame 514 同时有 269ms 源端间隔和 383ms 相对额外传输等待；不能用“组帧很慢”解释，组帧调用约 20us。
- 游戏 Relay，2Mbps/20秒：全程最大 454.4ms 发生在限速前，限速附近最大窗口间隔 260.2ms，0 IDR/0 RFI。限制期间最低 20.1 FPS，解除后恢复约 60 FPS。原始 JSON、截图、服务端日志和 `20261010_frame_admission_qos_policy.json` 均保留。未将交付帧率当作显示器呈现帧率。
- 编码后额度拒绝已消除，但这版始终按低吞吐缩窗，正常静止场景也会留下过小窗口，恢复后产生额外采集暂停。因此不以这版作为最终交付，继续修正适用条件。

第二候选 `iroh-flight-recovery-20261010`：只有完整帧确认延迟比历史最小值增长超过 100ms，才启用按吞吐缩窗，并保持一秒；随后无增长则恢复正常 256KiB 容量。低码率不再直接意味着链路拥塞。新增健康静止场景/恢复容量回归，32 个媒体测试及 frontend 目标在直连、私有 TLS Relay 均通过；见 `20261010_flight_recovery_direct.xml`、`20261010_flight_recovery_relay.xml`。修改前快照 `backup/iroh_flight_recovery_20261010/`。

## 最终候选交付与实机结果

最终候选 `iroh-flight-recovery-20261010` 通过完整 Setup 安装到 90。版本 3.3.97，314 项安装文件哈希全部一致，服务 Running；见 `20261010_flight_recovery_delivery.json`。Setup SHA-256 为 `FD534E95021119BC82BAB3856F60436872130A11642CDB5876A245394D412573`。本机 Client 和 Node 开发 dist 的四对运行文件再次核验一致，见 `20261010_flight_recovery_development_hashes.json`。安装产物与开发产物采用不同构建配置，分别按自己的清单验证。

| 实测 | 收帧窗口 FPS | 最大交付间隔 | 超过 100ms 的间隔 | IDR/RFI 请求 |
| --- | --- | --- | --- | --- |
| 游戏 direct | 52.9–60.2 | 220.0ms | 2 | 0/2 |
| 游戏 Relay，含 2Mbps/20秒限速及恢复 | 36.4–61.1 | 203.4ms | 7 | 0/0 |

这里的 FPS/间隔统计位于参考链检查后、解码前，不是显示器呈现帧率。Relay 正常阶段 57.2–60.3 FPS，最大 98ms；限速解除后的完整窗口为 59.3–60.1 FPS，最大 66.6ms，持续观察约 100 秒。恢复无需重新连接。截图确认游戏实际加载并响应拖动。

限速前视频实际数据报码率约 4.4Mbps，高于 2Mbps 限制，验证确实施加了压力。远端 QoS 生效时间为 01:28:31.911–01:28:52.033；远端时钟比本机约快 4.4 秒，只用于定位阶段，不以跨机器墙上时间差计算传输延迟。原始策略、客户端 JSON、服务端日志、截图和汇总见 `20261010_flight_recovery_qos_policy.json`、`20261010_flight_recovery_game_{direct,qos}.*`、`20261010_flight_recovery_measurements.json`。

在线发送窗口 busy/failed/flight_limited 均为 0。Relay 没有参考链恢复请求，之前编码后跨额度拒绝造成的交付空洞在本轮没有复现。之前 1328.9ms 的弱网最大交付空洞，本轮为 203.4ms；这是不同轮次的实测观察，不是严格同条件 A/B 基准。

仍需明确的限制：限速突变初期，完整组帧的相对额外传输等待峰值仍为 639.740ms，之后下降；不能只看 203.4ms 交付间隔就声称没有积压或输入延迟。direct 的两个短抖动中，源端间隔约 133/218ms、接收约 151/220ms，额外传输等待约 30ms。对应反馈 RTT 上升到约 196–239ms，缩小在途窗口后产生采集背压。组帧调用只有几十微秒，因此剩余短抖动不应再归因于组帧计算耗时。后续若继续收敛，应区分正向视频积压和反向反馈延迟，不能通过取消所有背压来掩盖问题。

## 清理与当前环境

仅停止本轮拥有的游戏实例和 Client。Console 原配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 已恢复；临时 Relay PID 46056 已停止，QoS 剩余 0，仅桌面 Render PID 43664；Console Running，校验证书和主机名的 HTTPS 返回 200。见 `20261010_frame_admission_cleanup.json`、`20261010_frame_admission_operations.json`。

90 保留最终候选安装，开发文件位于 `D:\112233`。临时 iroh 测试配置已关闭，后续实测须重新启用并记录身份；本轮没有把 iroh 宣布为正式全面启用，也没有完成 P0–P7 的其他阶段。未提交或 push。
