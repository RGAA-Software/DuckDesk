# 反馈和编码准入的源码对照与实测

状态：反馈数据报和进展判断修复已交付，90 实测与清理完成。修复有失败回归和现场日志支持，但高画质 Relay 短抖动、WebView 页面初始化问题仍未解决，不能宣布整体流畅度通过。

## 本轮只读源码对照

- Sunshine `D:/source/Sunshine`，HEAD `3cba9baebac882b336be3ebe129ee612cb189853`：`src/stream.cpp` 的视频发送循环按小于 64KiB、最多 64 个包分批，并在帧内和跨帧之间计入 pacing。该循环没有 Pixels 新增的完整收帧确认额度门槛。它直接控制 UDP 发包，我们的 iroh 路径仍由 QUIC 负责 wire pacing，不能再机械叠加同一套限速器。
- Moonlight Qt `D:/source/moonlight-qt`，HEAD `2e13ed9977bc31c73caf8428f08f58d793313ece`；共享核心 `moonlight-common-c/moonlight-common-c`，HEAD `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac`：`src/VideoDepacketizer.c::reassembleFrame` 在非 DIRECT_SUBMIT 路径将完整帧放进有界解码队列，队列溢出清理后请求 IDR；然后调用 `connectionReceivedCompleteFrame`。`src/ControlStream.c` 的该函数只更新完整帧序号和数量，不直接解码或等待显示。Qt FFmpeg 声明 PULL_RENDERER 能力。
- Pixels 的 `ThunderSdk` 已通过 `PostVideoTask` 分离实际解码。因此“解码直接阻塞整个收帧线程”尚无依据；编码帧回调和消息业务处理是否耗时要测量，不能先假定。
- Pixels 新增的 `VideoFlightWindow` 通过完整帧反馈释放额度，并在额度不足时每 250ms 允许探测。既有实测的 250–300ms 间隔只是排查线索，不构成因果证明。

## 诊断版

不改码率、额度、等待时间或协议。在发送端记录持续超过 80ms 的编码准入暂停/恢复、未确认帧范围、字节数、最老帧年龄、探测剩余时间和反馈年龄；反馈到达时比较服务端到达间隔与客户端生成间隔，记录实际释放额度。客户端记录生成间隔超过 100ms 的反馈；可靠消息记录超过 50ms 的分派排队/回调耗时。

不记录消息正文、凭据或输入内容。诊断实现前的完整文件和实际工作区状态存放在 `backup/iroh_feedback_trace_20261009/`。

本机聚焦直连测试 2 项、私有 TLS Relay 测试 2 项通过；开发 Client/Cloud Node 的 4 对运行产物 SHA 一致。证据为 `20261009_trace_direct.xml`、`20261009_trace_relay.xml` 和 `20261009_trace_development_hashes.json`。

## 原行为实测，21:52–21:55

完整诊断包 `iroh-feedback-trace-20261009` 安装到 90，314 项哈希一致；见 `20261009_trace_delivery.json`。测试临时 Relay PID 46984，状态保存在 90 工作区 `trace-test-state.json`，本轮收尾必须恢复配置并清理该进程。

- 游戏 direct 最大收帧间隔 265.4ms。源端 21:53:43.329 的反馈间隔 229202us，而客户端生成间隔 62834us；随后确认仍落后，最老未确认帧年龄超过 400ms，编码在 250ms 探测时才恢复。反馈到达后额度正常释放，没有证据表明帧号匹配计算错误。
- WebView Relay 加载后最大间隔 297.4ms。源端 21:55:39.246 的反馈到达间隔 337296us，生成间隔 50193us；接着两条旧报告在 283us/244us 后集中交付，而它们各自的生成间隔约 58ms。21:55:39.268 编码准入恢复，累计暂停 249169us。该轮未见对应可靠消息分派排队/回调超过 50ms 的记录。
- WebView 另一次暂停时反馈持续到达，但未确认帧年龄仍超过 200ms，触发年龄限制。不能声称全部短停顿只由可靠反馈重传导致。
- 客户端退出后的宽限期会继续记录额度暂停，此时没有活跃接收方；分析在线卡顿必须排除这部分。

原始证据：`20261009_trace_game_direct.json`、`20261009_trace_game_direct_server.log`、`20261009_trace_webview_relay.json`、`20261009_trace_webview_relay_server.log`。

## 依据源码的修复实验

Moonlight 共享核心 `ControlStream.c::lossStatsThreadFunc` 明确以 `ENET_PACKET_FLAG_UNSEQUENCED` 发送 Sunshine 的帧 FEC 状态，注释说明这是非关键状态，不可靠发送；IDR/RFI 请求仍走可靠消息。它的状态内容与 Pixels 累计收帧状态不同，这里复用的是“新状态替代旧状态，不让旧报告阻塞新报告”的传输选择。

据此将 Pixels 累计反馈改为 40 字节 `PXF1` iroh 数据报，保留累计序号/字节/时间和乱序过滤。接收端在已准入连接的数据报回调中处理，仍检查 view 权限，不要求 audio 权限；不加入第二种认证。发送最多等待 1ms，失败不排队重传，下一条报告包含全部进度。恢复请求仍可靠。未调整额度、250ms 探测、码率或采样周期，便于与原行为对照。

新增聚焦测试验证帧号 0、错误格式拒绝、可靠控制记录未读取时反馈独立到达、跳过若干报告后累计释放额度、迟到旧报告不能释放新帧。直连与私有 Relay 的两项测试均通过，实机 A/B 见下方结果。

## 单独更换反馈通道，22:06–22:07

完整包 `iroh-feedback-datagram-20261009` 同时更新 Node 内 Render 和 Client，314 项安装文件一致。WebView Relay 成功持续收帧，加载后最大仍为 262.3ms；多数窗口约 59–60 FPS。源端 22:06:36.524 仍看到 341557us 到达间隔、约 55ms 生成间隔，随后数据报集中到达。因此 **QUIC 可靠流的重传不是集中迟到的唯一原因**；Relay 的外层仍为 TCP，不能把数据报换通道说成消除所有网络延迟。

在线目标码率大部分为 14.95Mbps，一次回落到 11.96Mbps 后恢复，实际编码约 12–14Mbps。源码确认 iroh-relay 1.3.0 的客户端和服务端均设置 TCP_NODELAY，未盲目增加 TCP 参数。原始证据 `20261009_feedback_datagram_webview_relay.json`、`20261009_feedback_datagram_webview_server.log`，安装证据 `20261009_feedback_datagram_delivery.json`。

## 依据失败回归修正进展判断

`FreshCompleteProgressReleasesCaptureBeforeSparseProbeDeadline` 使用真实 `VideoFlightWindow` 重现现场：8 帧占用 256000 字节，在 240ms 确认第一帧后剩 7 帧/224000 字节，旧逻辑仍因最老帧超过 200ms 拒绝编码。修改前用例稳定失败，见 `20261009_progress_regression_before.xml`。

修正为依据最近一次有效完整帧确认的本地时钟判断停收，而不是最老待确认帧年龄。帧数、字节上限和无进展时稀疏探测保持不变；旧的重复确认和乱序报告不能刷新进展时间。新确认释放额度后可立即编码，不再等 250ms 探测。该用例与直连/私有 Relay 回归均通过，见 `20261009_progress_direct.xml`、`20261009_progress_relay.xml`。

首次生成进展修复安装包因 D 盘空间不足失败，原始日志 `.cache/iroh-progress-package-build.log` 保留。没有删除源码/日志/安装包；对已生成候选的 `installer/3.3.97/app` 输入副本做可逆 NTFS 压缩后重试，见 `.cache/iroh-progress-package-retry.log`。失败打包没有部署到 90。

## 最终候选交付与实测，22:18–22:37

完整 Cloud Node 候选 `iroh-feedback-progress-20261009`，版本 3.3.97，通过 Setup 安装到 90；314 项文件哈希一致、服务 Running。开发 Client/Cloud Node 的 4 对构建/运行文件哈希一致。见 `20261009_progress_delivery.json`、`20261009_progress_development_hashes.json`。

- Setup SHA-256：`BF579A74AFE285BD65DF5851C8F8A9FAD7799127185A2B7409A2F353D81A8838`。
- 安装 manifest SHA-256：`67AE18257B28B3D36AAEDBF04AB77233DC4EB4C29B7E48EE563219FE706BB36E`。
- 安装 Render SHA-256：`6E1F729A3A0A874828F4CC6A2C1DF339D488263340A62ED24131D261E508BD37`。
- 本机 Client SHA-256：`4A726682B0D6C540B2D39379562AD90633C2502A161CAEC5F44592FCDFC33FC2`。

| 实测场景 | 5 秒收帧窗口 FPS 范围 | 最大完整收帧间隔 | 结论 |
| --- | --- | --- | --- |
| 原行为游戏 direct | 47.8–60.5 | 265.4ms | 对照轮次 |
| 修复后游戏 direct | 50.6–60.3 | 153.8ms | 额外等待修复有效，仍有短抖动 |
| 原行为 WebView Relay（排除第一个加载窗口） | 56.4–60.4 | 297.4ms | 对照轮次 |
| 仅反馈数据报 WebView Relay（同口径） | 52.4–60.0 | 262.3ms | 单独更换通道不解决全部停顿 |
| 最终 WebView Relay（同口径） | 41.7–60.1 | 272.8ms | 未达到高画质流畅度验收，不能声称全面改善 |
| 最终游戏 Relay，含 2Mbps/20 秒及恢复 | 17.2–60.1 | 380.0ms | 未断线、无秒级停顿，但弱网流畅度仍未通过 |

以上是单轮现场排查，网络条件不受完全控制，不是严格性能基准；FPS 是完整编码帧交付频率，不是显示端到端延迟。原始样本及汇总见 `20261009_progress_measurements.json` 和同前缀各场景 JSON/server.log。三轮成功场景有截图，游戏镜头和 WebView 视角响应了输入。WebView 截图同时保留短时明显块状画质，不能仅凭源页面显示 60 FPS 判定成功。

**缺陷修复的现场证据**：direct 22:18:46.521 确认帧 1537 后剩 4 帧，最老帧仍有 241ms；22:18:46.524 即恢复编码，反馈到恢复约 3ms，探测还需 124ms。22:18:46.830 新确认后约 11ms 恢复，最老帧仍有 278ms。旧年龄判断会继续阻塞这些情况。该轮仅两次超过 100ms 的收帧间隔，但发生 5 次 RFI；不宣称恢复请求数量也改善。

**仍未解决的具体证据**：WebView 22:33:55.742–55.912 仍有 8 帧/231000 字节未确认，反馈到达并不代表最新完整帧有推进。它到探测时才恢复，这是帧数上限仍满，与已修复的“释放容量后还等旧帧年龄”不同。22:33:56.273 释放 3 帧后约 3ms 恢复，符合新逻辑；随后反馈进展再次不足。在线码率从 14.95Mbps 降到 1.22Mbps，并在约 22:34:29 恢复上限；抖动和画质下降都保留为未通过。尚不能断定是网络丢包、Relay 外层 TCP 队头等待还是 QUIC 调度的单一根因。下一次需对同一帧号补齐 Relay 入/出队与 QUIC 在途/排队测量，继续对照 Sunshine 分批 pacing 和 Moonlight 有界接收/恢复，而不是继续盲改阈值。

**WebView 页面失败单列**：首次最终包测试 `20788f66-b228-49d2-bd43-27fa9b6fb363` 页面只有控件，源端报 `A valid external Instance reference no longer exists`、`Instance dropped in popErrorScope`。截图、日志和客户端样本保存为 `20261009_progress_webview_page_failure.*`。一次重启后场景加载成功；没有强制 WebGL 或修改用户页面设置。失败轮次不纳入有效场景性能表。成功轮次首个窗口最大 969.5ms，包含源页面加载，单独保留。

**限速和收尾**：仅测试 Relay 的 `px_relay_probe.exe` TCP 被限速 2Mbps，90 时间 22:35:55.466–22:36:15.548，策略 finally 删除。解除后窗口仍有波动，后续约 51–58 FPS，未观察到稳定恢复至 60 FPS，故不能称弱网通过。见 `20261009_progress_game_qos.json`。所有测试实例及本机 Client 已结束；原 Console 配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 恢复，临时 Relay PID 46984 停止，QoS 为 0，仅桌面 Render PID 45116，Console Running，验证 CA 和主机名的 HTTPS 请求返回 200。见 `20261009_progress_cleanup.json`。未提交/push。

## 用户要求先分析原因：按同帧号复核

本次只分析现有证据与当前源码，没有改代码、参数或重启服务，也没有将旧实测当作用户当前连接路径。

1. **已确认的产帧暂停机制**：`video_flight_window.h` 的帧数上限使用历史最小 RTT，本轮低基线 RTT 下是 8 帧；同时有 256KiB 字节上限。8 帧在 60 FPS 下只对应约 133ms，收帧反馈本身每 50ms 生成，往返延迟和调度也消耗该窗口。额度耗尽后 `CanEncodeVideo` 直接拒绝新帧，等累计完整帧确认或 250ms 探测。现场 direct 帧 1572 源端间隔 150ms、客户端 153.8ms；这是有证据的产帧暂停，不是单凭客户端 FPS 推测 GPU 性能不足。帧数和字节限制都必须保留有界性，但当前方式会把确认延迟转化为生产端停顿；单纯放大上限也可能加重积压。
2. **不能把不同帧的最大值对应起来**：WebView 源端 269ms 是帧 1393，客户端同帧间隔 184.9ms；客户端最大 272.8ms 是帧 1466，源端没有该帧的 `gap >= 100ms` 日志。客户端帧 1363 为 259.4ms，源端也没有该帧的长间隔日志。按当前完整诊断记录推断，除源端准入暂停外，下游传输/接收处理还改变了交付节奏。两端系统时钟有偏差，只比较同帧的本地相邻间隔，不相减绝对时间；不能把所有接收停顿都解释成源端暂停。
3. **现有发送期限只约束入队**：FFI 调用 noq 1.3.0 的 `send_datagram_wait`；该 API 等待缓冲空间，并非远端接收完成。当前 `frame_deadline` 只检查调用/提交分片的期限，帧一旦进入底层，C++ 无法据此取消后续 QUIC/Relay/TCP 中的旧数据。64KiB 是 QUIC 待发数据报缓冲的配置，不等于整条链路全部在途字节。现场 WebView 5 秒窗口 `max_send_us=130`、`busy=0/failed=0`，紧接着远端确认年龄达 300–500ms，两者并不矛盾。证明的是当前观测边界不足，尚不能证明具体哪个底层队列积压。
4. **块状画质的直接原因有日志**：现有码率控制在额度跳帧、延迟或停收时降低目标码率，再每稳定 2 秒逐步恢复。WebView 本轮在线目标从 14.95Mbps 降到约 1.22Mbps，与截图的块状画质一致。这是拥塞/暂停后的编码质量反应，不能反过来证明物理网络实际容量只有 1.22Mbps。
5. **尚无充分证据的判断**：WebView 有效场景组帧统计全程 `loss=0/malformed=0`，测量窗口无 RFI/IDR，不支持把本轮长间隔直接归因于 FEC 损坏或反复等关键帧；这也不等于底层物理网络绝无丢包。Relay 外层 TCP 的顺序交付可能放大等待，但 direct 同样出现暂停，不能把所有问题归咎于 Relay。缺少 QUIC 在途字节/拥塞窗口、Relay 入出队时间及接收回调耗时，仍无法在它们之间作唯一归因。

源码对照结论：Sunshine 的上述 UDP 发送循环使用分批/pacing，没有 Pixels 这层“完整帧确认额度耗尽就停采集”的闸门；Moonlight 的上述非直接提交路径在解码队列溢出时清理并请求 IDR，FEC 状态为非可靠反馈。它们的边界与 Pixels 的 QUIC/Relay 不同，不能直接复制 UDP 发包参数。下一步应先测清同一帧进入 QUIC、Relay 与接收处理的等待，再决定如何协调有界在途额度、编码前节奏和过期帧恢复，避免叠加独立阈值反复降码率。
