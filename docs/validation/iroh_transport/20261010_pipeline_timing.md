# iroh 发送至组帧阶段诊断

状态：诊断与小帧额度候选已安装并完成本轮实测，直连改善，Relay 长停顿仍未通过；临时配置和限速已清理。不能将该候选作为已解决卡顿的版本。

## 测量边界

- 使用现有 RTP 90kHz 发送时间戳与接收端完整组帧时刻，按连续帧间隔累计相对传输变化，以本次连接最小值为基线。`excess_us` 是额外等待，包含底层传输和接收端处理/调度，不是绝对单向网络时延，不依赖机器墙上时钟同步。时间戳回绕、源端主动暂停均有聚焦测试。
- `event=iroh.frame_transit` 同时记录源端帧间隔、完整接收间隔、额外等待及当次组帧函数耗时。仅有长间隔/额外等待时输出，频率不超过每流 10 次/秒。
- `event=iroh.receive_worker` 标识单次接收调用超过 50ms（请求超时是 10ms）或组帧/回调/反馈处理超过 20ms，区分排队与业务处理慢。不能单凭调用耗时确定 OS 调度或 Rust 运行时的唯一原因。
- `event=iroh.capture_transport` 在发送额度暂停/恢复时记录选中路径 RTT、QUIC cwnd、数据报待发缓冲余量、拥塞事件、最近有效完整收帧进展年龄。iroh/noq 公开接口未暴露完整 Relay/TCP 队列及全部在途字节；`datagram_space` 不能冒充整条链路队列长度。

第一阶段只扩展同版 Rust/C++ 内部静态链接 ABI 和诊断字段，不改传输 wire 格式、码率、在途帧上限、探测周期或认证。第二阶段的小帧额度调整见下文。不修改只读第三方。脏文件快照见 `backup/iroh_pipeline_trace_20261010/manifest.json`。

## 构建和候选

聚焦 Rust Release/Publication 完成；C++ Release Client、Node Render/Client 构建并同步开发 dist，4 对哈希一致。直连 3 项、私有 TLS Relay 2 项通过，包含时间戳偏移/回绕诊断回归。见 `20261010_pipeline_direct.xml`、`20261010_pipeline_relay.xml`、`20261010_pipeline_development_hashes.json`。

完整安装候选 `iroh-pipeline-trace-20261010`，3.3.97：

- Setup SHA `8124C118AED0F617CC3E3BD48A065E02CC8C84D767F21067F50EB61D81A68C63`。
- manifest SHA `52D37440E2BAB7D1BD582E20B8707C3BA767CE56255E3491DF9F0824FAD0EFC5`。
- Render SHA `98D23FBA622FA8AC672D8D4F1650B779D78FFD56A52E86EA44490F4180CC1081`。

首次 Rust 编译因新增观测消耗 `selected` 值失败，改为借用后重新构建成功；保留 `.cache/iroh-pipeline-rust-release.log` 和重试日志。

## 不改策略的实测结果

诊断完整包已通过 Setup 安装到 90，314 项哈希一致，见 `20261010_pipeline_delivery.json`。

- 游戏 direct：帧 2738 的发送端间隔 150066us，完整收帧间隔 143536us，相对额外传输等待 3238us，当次组帧 15us。服务端 00:03:05.102 记录 pending=8、仅 26400 字节，QUIC `datagram_space=65536`、cwnd=50264；反馈在 00:03:05.144 到达，报告生成间隔 59856us、到达间隔 250735us，释放 3 帧，约 7ms 后恢复采集。确认链路延迟触发了“8 帧额度耗尽”停产；没有证据说明当时字节缓冲已满。原始证据 `20261010_pipeline_game_direct.json` 和 `_server.log`。
- WebView Relay 成功加载并响应输入；没有 WebGPU 初始化失败。加载约 1 秒的间隔在源端就已形成，额外传输等待约 3ms。正常场景仍复现两类暂停：帧 1856 源端约 216ms、接收 205ms，额外等待 17ms；帧 2430/2431 源端各约 17ms，接收各约 216/197ms，额外等待升至 203/384ms，组帧仅 29/26us。后者不能归因于源端暂停或这一次组帧调用；底层传输/交付等待仍需跟进。对应服务端 QUIC 待发缓冲为空、cwnd 386030、未见新的拥塞事件；这不排除已发包、Relay/TCP 或接收调度排队。
- 上述两轮没有 `receive_worker` 超阈值记录：未观察到单次业务处理超过 20ms 或 ReceiveDatagram 调用超过 50ms。没有这类记录不是对系统调度的完全排除。
- 所有数据是同帧的相对时间测量，未用不同机器的绝对时间相减。没有声称完整定位每一个 Relay 内部队列。

## 针对已证实的额外帧数闸门

新增失败回归复现 8×3300=26400 字节在 133ms 被拒绝；旧逻辑失败证据 `20261010_byte_credit_regression_before.xml`。现取消基于最小 RTT 的 8–36 帧限制，使用原有 64 条历史上限作为内存保护；保留 256KiB 字节限制、原停收年龄判断、250ms 稀疏探测及原码率策略。因此小帧的反馈短抖动不再提前耗尽独立的 8 帧额度，真正字节积压或长期无进展仍限制生产。

这是从实测 26KB/空 QUIC 缓冲的情况中移除重复门槛，不是扩大字节缓存或宣布底层延迟消除。已有真实 QUIC 用例的“8 个小帧必然停产”断言随新约定更新为允许继续生产，并保留累计确认、乱序拒绝和参考链连续性验证。字节耗尽、停收、稀疏探测的测试仍保留。修改前脏文件归档 `backup/iroh_byte_credit_20261010/`。

## 最终候选实测与未解决原因

完整候选 `iroh-byte-credit-20261010`（3.3.97）通过 Setup 安装到 90，314 项哈希一致，服务 Running。Setup SHA `9105153BB4F81FC7C68FF05B006ABE62C4272914C92FD1D79D548F739EEAC2B6`，其余身份见 `20261010_byte_credit_delivery.json`；开发 dist 四对哈希一致见 `20261010_byte_credit_development_hashes.json`。聚焦直连 2 项、私有 TLS Relay 2 项通过，见对应 XML。

**统计口径修正：** `iroh media receive window` 来自 `IrohConnection::ObserveVideo`，统计经过参考链检查后交付解码的编码帧，不是全部完整组帧，也不是显示器呈现帧率。`frame_transit` 在完整组帧后、参考链放行前采集。两者必须分别看。此前将 window 的间隔统称“完整收帧间隔”不够准确，以此处为准。

| 场景 | 交付帧率区间 | 最大交付间隔 | >100ms 次数 | IDR / RFI |
| --- | --- | --- | --- | --- |
| 游戏 direct，约 75 秒 | 59.6–60.3 | 60.0ms | 0 | 0 / 0 |
| WebView Relay，首窗口外 | 24.6–60.1 | 2841.6ms | 27 | 5 / 22 |
| 游戏 Relay，未限速 | 55.8–63.5 | 311.4ms | 23 | 0 / 8 |
| 游戏 Relay，包含 2Mbps/20秒，约 165 秒 | 21.9–61.5 | 1328.9ms | 11 | 1 / 4 |

游戏 direct 和 WebView 均有真实画面截图；WebView 显示 WaterPro 海面/船只，输入改变视角，没有切换强制 WebGL。实际路径由逐窗口日志确认。原始 JSON、截图、服务端日志分别保存为 `20261010_byte_credit_*`，聚合为 `20261010_pipeline_measurements.json`。各轮场景和网络不同，不能当作严格 A/B 性能基准，也不能把单轮直连改善推广为所有路径改善。

WebView Relay 的 2.84 秒交付停顿并非等量的网络停收：00:21:32–35 客户端持续记录完整帧，单次组帧调用多为 7–43us；额外传输等待约 0.15–0.45 秒，同时参考链检查未放行部分帧。服务端 00:21:39.614 的统计窗口 offered=301、busy=82、flight_limited=82、completed=219、failed=0，并伴随 IDR/RFI。代码确认 `CanEncodeVideo` 以 0 个新增字节预检查，`SendVideo` 编码/分片后再以真实字节检查，额度不足直接丢弃已编码帧。这会破坏后续预测帧的参考链；取消小帧数量门槛没有修复这一缺陷。此路径与拥塞下长交付空洞相符，下一步应修复编码准入与发送保留之间的不一致，并验证高运动量下不丢已接受的预测帧，不能只继续加大窗口。

另有 WebView 编码输出本身 388ms、661ms、1008ms 等间隔，发生时 `flight_limited=0`，因此还需独立追踪页面渲染、采集、编码入口/出口；当前日志不足以断言网络是唯一原因。没有观察到 receive_worker 超阈值记录，但这不等于排除全部 OS/运行时调度。

有效 QoS 的远端时间是 00:29:37.050–00:29:57.172，本机时钟约慢 4.4 秒。最大 1328.9ms 落在限速起始窗口；解除后到 00:30:06 窗口恢复约 60 FPS，随后仍偶有 115–126ms 短抖动。服务端限速期间 datagram_space 降到 803 字节、pending queue=4，证实这次是真实待发积压。自动恢复已发生，但弱网卡顿仍未验收通过。

前两次限速时机不合适：一次在短测试结束后才生效，一次仅覆盖短测试尾部，均不能用于恢复验收。已保留操作和时间，第二轮存为 `game_partial_qos.json`；最终改为较长测试，自动延迟启动 QoS，覆盖完整限速与恢复阶段。策略时间见 `20261010_byte_credit_qos_policy.json`。

## 参考与收尾

- Sunshine `D:/source/Sunshine` HEAD `3cba9baebac882b336be3ebe129ee612cb189853`：`stream.cpp` 的 UDP 分批及跨帧 pacing，无本项目这个完整帧反馈数量闸门。
- Moonlight Qt HEAD `2e13ed9977bc31c73caf8428f08f58d793313ece`；shared core `D:/source/moonlight-qt/moonlight-common-c/moonlight-common-c` HEAD `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac`：ControlStream 的可替代 FEC 状态非顺序反馈、可靠恢复请求，以及 VideoDepacketizer 的有界队列与 IDR 恢复。参考实现没有被修改。
- 所有本轮创建的应用实例已停止；Console 原配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 恢复，临时 Relay PID 53380 停止，QoS=0，仅桌面 Render PID 54076；证书及主机名验证开启的 HTTPS 检查返回 200。见 `20261010_pipeline_cleanup.json`。
- 90 保留完整安装候选，但临时 iroh 配置已关闭；下轮验证需重新启用并记录新 Relay 身份。此候选是诊断/开发状态，Relay 未通过；没有提交或 push，没有宣布 P0–P7 完成。
