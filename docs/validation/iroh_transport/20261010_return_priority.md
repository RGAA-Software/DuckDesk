# 回程优先级、实际 Relay 路径及混传定位

2026-10-10。回程优先级缺口已修复、构建并完整安装到 90；残余 Relay 交付抖动没有关闭。本文承接 sustained_mixed 批次，不重复已通过的容量、故障恢复、剪贴板或录制检查。

## 改动及验证

`Channel::Open` 原先设置发送优先级，`Channel::Accept` 未设置回程方向。现在接受端在头部校验后，按剩余超时设置相同的业务优先级：输入 100、控制 90、RDP 50、剪贴板 10、文件 -100。读取底层 QUIC 真实优先级的测试覆盖五种通道和双向消息，不只检查项目层缓存。

Client `iroh.path_detail`、Render `iroh.send_path` 记录实际选中 Relay/IP、拥塞窗口、数据报可用缓冲和拥塞事件。缺少路径时返回 unknown，不伪造候选为实际路径。不调整媒体缓冲、QUIC 参数或认证。

- Rust Release、publication 构建通过；聚焦 C++ transport/frontend 构建通过。
- 首次 CTest 的 frontend 通过（26.32 秒）；transport 4/5，通过 URL 比较发现测试配置缺规范化尾斜杠。仅修正测试输入后 transport 5/5，通过（3.41 秒），初次失败日志保留。见 tests_initial/tests_normalized 日志。
- 可读命名检查通过，未修改原始外部参考仓库。Client、Render、Cloud Node 内置 Client 聚焦构建及 dist 发布通过，完整 SHA 对照见 `20261010_return_priority_build.json`。

## 安装身份

90 使用完整 Cloud Node 3.3.97 Setup，候选 `iroh-return-priority-20261010`；安装来源在 `D:\112233`，314 个文件核验通过、服务 Running。证据 `20261010_return_priority_installation.json`。

- Setup SHA-256：`A2C277F51F3187A10F58303C14A56499F7CA6CCE397D71E75C0DE1435F832FA4`
- Manifest SHA-256：`BA239B25D044BDC594E11647517D0421BE94B2E7938F20E61725C3226C866C01`
- 安装 Render SHA-256：`1B3F87B5A146EF42A48A1BEC18E721E1C88AD176BD8912E198902427F9CFF869`

不以手工复制 EXE 替代安装。Server 和两 Relay 产物未变，不重复安装。

## 实测结果

每轮使用真实 Windows Client、自有 GameHook 实例、持续画面/输入及文件上传下载，文件源/下载 SHA 全部一致。FPS 是解码前交付窗口统计，不是显示 FPS 或输入端到端延迟。固定路径通过临时维护另一 Relay 完成，结束后恢复；两端实际路径日志均已采集。

| 场景 | 每方向文件 | 耗时 | 五秒窗口 FPS | 最大交付间隔 | >100 ms |
|---|---:|---:|---:|---:|---:|
| 固定 90 | 1.5 GiB | 61.080 s | 59.9–60.6 | 95.0 ms | 0 |
| 固定 90 | 512 MiB | 21.974 s | 58.5–60.0 | 173.4 ms | 1 |
| 固定 BJ | 512 MiB | 22.984 s | 59.4–60.9 | 122.8 ms | 2 |
| 双候选，实际 90 | 512 MiB | 22.204 s | 47.9–60.0 | 254.6 ms | 8 |
| 双候选，实际 90，路由跟踪 | 1.5 GiB | 69.238 s | 59.4–60.3 | 246.9 ms | 2 |
| 双候选，实际 90，TCP 抓包 | 1.5 GiB | 61.624 s | 60.0–60.4 | 64.4 ms | 0 |

原始报告和日志名称见 `20261010_return_priority_summary.json`。各轮并非同时运行，启动、路由配置传播及机器负载存在差异；不能从这张表推出双候选一定导致抖动或优先级修复已消除旧问题。

固定 90 的 173.4 ms 来自启动帧源间隔约 166.6 ms，接收额外间隔约 8 ms，不当作 173 ms 网络停顿。BJ 异常帧源约 15 ms、收端间隔约 108/123 ms，存在传输交付延迟。

路由跟踪轮在 Client 本地 21:04:10.492 和 21:04:37.065 记录 246898/117693 微秒接收间隔，源间隔 17288/16577 微秒，组帧 8/14 微秒。Client Relay actor 从接入到关闭保持 90，没有切换；Render 周期路径也一直为 90、无 QUIC 丢包记录、媒体提交周期最大耗时为微秒级。这排除上述两帧由源停顿或同步组帧耗时解释，但不排除 Rust runtime 调度、TLS/TCP 队列或承载网络停顿。跨机器墙钟未用来直接计算单向延迟。

后续 NIC 包头捕获限制 TCP 4605、每包 96 bytes，共 25,481 包、捕获无事件丢失。主连接无零窗口，出现一处序列重叠；NIC offload、重排或重复捕获都可能造成重叠，未将其直接称为重传。该轮没有 >100 ms 交付间隔，所以不能据此解释前一轮卡顿。捕获在 `.cache/iroh-priority-tcp-20261010/relay.pcapng`，SHA-256 `c624ebc2ce775cf416ca8249dc3105f7da4d20b9247044dfc651d1270af2f7b7`；分析及限制见 `20261010_dual_priority_tcp_analysis.json`。

## 参考与后续

本批读取只读参考：RustDesk `D:/GoCloud/rustdesk`，HEAD `7aa98d43cf1962a7a29ec16ffef42974377ef11e` 的 framed TCP awaited send/timeout；Sunshine HEAD `3cba9baebac882b336be3ebe129ee612cb189853` 的发送 pacing；Moonlight common-c HEAD `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac` 的有界解码队列和参考恢复。上游方案提供层次划分，不能用解码队列丢帧替代本轮提交后交付前延迟的定位。

下一步在复现异常的同一轮同步记录 Client 与 Relay/Render TCP 包头和任务排队，区别 TCP 队头阻塞、Relay 转发排队及端点调度，随后针对性修改 pacing/背压。没有证据前不扩大缓存、不盲改拥塞控制。NAT 缺失路径、长期稳定性、Android 最后迁移及旧栈退役继续保留。

## 清理

自有 Client、游戏实例和 guest session 已由各脚本 finally 结束；远端测试文件不存在。90 三服务 Running，仅桌面 Render 21528。90/BJ fresh、ready、未禁用、期望/实际维护均 false。Pktmon 已停止，原先无过滤器，临时过滤器已删除。路由 trace 仅属于测试子进程，没有设置机器环境；持久运行配置无变化。
