# 正常直连的确认反馈抖动修复

状态：反馈方向延迟导致错误缩窗的缺陷已修复、完整安装并实测；长时间直连仍出现独立的突发丢包/参考链恢复停顿，不能宣布直连画面整体验收通过。

## 定位

用户本轮只要求解决正常直连的 150–220ms 短抖动，不把带宽不足时的正常降级作为阻塞。上一轮 direct frame 2904/2922 的源端输出间隔为 133/218ms；暂停时本地发送队列为 0，QUIC 数据报可用空间 65536 字节，反馈 RTT 升至 196–239ms，在途额度却从正常 256KiB 缩到 67781/88000 字节。编码、收帧和采集日志证实采集背压放大了反馈方向抖动。

`VideoFlightWindow::Observe` 以本机收到确认的时间减去发送时间判断拥塞，包含反向确认延迟。新增 `ReverseFeedbackDelayDoesNotShrinkHealthyForwardCapacity` 回归：视频方向间隔不变，仅确认多延迟 200ms，旧代码把窗口降到 32768 字节并拒绝下一次采集。失败证据 `20261010_forward_delay_before.xml`。

## 修改

改为比较接收端生成报告的时间间隔与对应帧的本地发送时间间隔。两端时钟起点不需要同步，减去历史最小偏移，反向反馈传输时间不参与视频方向拥塞判断。已有报告 50ms 粒度的误差仍在，阈值保留 100ms；没有新增协议字段、票据或认证。长时间反馈完全中断、在途字节硬边界、稀疏恢复探测和 QUIC 拥塞控制保留。

真正的视频方向积压仍会缩窗，新增对应正向延迟用例；日志增加 `forward_delay_growth_us`，用于区别反馈 RTT 与视频方向等待增长。源文件修改前脏内容保存于 `backup/iroh_forward_delay_20261010/`。

参考 Sunshine `D:/source/Sunshine` HEAD `3cba9baebac882b336be3ebe129ee612cb189853` 的 `src/stream.cpp` 1714–1758（跨帧按发送时刻 pacing），Moonlight shared core HEAD `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac` 的 `src/ControlStream.c` 1378–1401（FEC 状态用非可靠、非排序控制消息）。参考说明可替代状态不应变成强制逐帧往返屏障；本次时间差算法是针对本项目现有反馈结构的实现，不声称直接复制了上游算法。第三方源码未修改。

## 聚焦验证与交付

34 个媒体测试和 frontend 生命周期目标，在 direct 与私有 TLS Relay 各 2 个 CTest 目标均通过，见 `20261010_forward_delay_direct.xml`、`20261010_forward_delay_relay.xml`。本轮不重复 2Mbps 实机限速验收，正向积压约束由回归覆盖。

为了容纳完整候选包，将本任务上一候选 `iroh-flight-recovery-20261010` 可逆移动到 C 盘专属开发归档，319 项文件移动前后 SHA-256 一致；见 `20261010_recovery_candidate_archive.json`。不删除源码和安装目录，不以手工替换 90 运行 EXE 代替安装。

## 第一轮不限速实机结果

候选 `iroh-forward-delay-20261010` 已通过完整 Setup 安装到 90，版本 3.3.97、314 项哈希一致，服务 Running。Setup SHA `C0F0F80242B50E90A8C8A3FFCD279CEC252E5F79A9601985787B5F56C44C8790`，安装 Render SHA `F3C39CD9AAB4DE0127C1DA0A1915AEDF0BDC1338C3BEF6E73C76DA2F87C794C8`，见 `20261010_forward_delay_delivery.json`。本机 Client/Node 的四对开发运行产物哈希一致，见 `20261010_forward_delay_development_hashes.json`。

实例 `9c0a5397-3bdb-4c04-909c-3fa88c58ad4f`，测试从本机 09:21:40 左右开始，10 秒预热后持续拖动约 150 秒，全部路径样本为 direct，无 QoS。服务端时钟约快 4.9 秒，跨机事件用帧号关联，不能直接相减墙上时间。

- 34 个窗口中，31 个无 >100ms 交付间隔，59.3–60.2 FPS，最大 67.1ms。
- 完整统计必须保留失败窗口：16.6–60.2 FPS，最大 1751.5ms、7 次 >100ms、4 IDR/35 RFI。首个窗口有 257.4ms 参考恢复间隔和丢包；后续主要故障位于本机 09:23:25–09:23:30。
- 本次目标的正向/反向区分获得实机佐证：远端 09:23:38.677/38.872 的反馈到达间隔为 204269/195585us，报告生成间隔为 51740/51065us。未出现对应在线采集暂停，发送端持续约 60 FPS。此前错误缩窗产生的源端 133/218ms 空洞未复现。长测在线期间没有 >=80ms 的采集暂停日志，也没有 >=100ms 的编码输出空洞。
- 另一路故障不能隐去：服务端 UDP 丢包计数从 6 升到 269，接收端帧丢失计数从 2 升到 18、FEC 恢复 19 个分片；多个预测帧虽组完却因参考链断裂不能交付。发送窗口 busy/failed/flight_limited 为 0，完成率保持约 60 FPS。因此 1751.5ms 不是本次额度误判或组帧计算耗时导致。
- 只读核对发现 `IrohSession::AllowVideoRecovery` 对 RFI 和 IDR 共用 500ms 限制，接收策略的 IDR 重试为 1 秒。这是丢包恢复延迟的具体排查对象，但现有日志没有逐条记录哪些请求被拒绝，**还不能证明它贡献了多少停顿，也没有据此盲目降低常数**。本轮没有修改该策略。

统计位置仍是参考链检查后、解码前，不是显示器呈现 FPS。服务器日志的 09:24:36 之后属于测试客户端关闭后的宽限期，暂停/稀疏探测不算在线卡顿。原始 JSON、截图、服务端日志与汇总全部保留，见 `20261010_forward_delay_game_direct.*`、`20261010_forward_delay_measurements.json`。

## 短复测发现第二个反馈门槛

实例 `31135b55-14fb-401b-ba73-9e70e2c6c598`，同样 direct、无 QoS。全程 57.7–60.4 FPS，最大 200.6ms，0 IDR/2 RFI。保留 `20261010_forward_delay_game_direct_repeat.*`，并没有用首轮正常窗口掩盖这次短抖动。

frame 3988 源端间隔 181ms、客户端间隔 200.6ms。远端 09:28:34.965 暂停时：queued=0、pending=20、bytes=93500、budget=262144、forward_delay_growth=13906us。随后 09:28:35.035 确认到达间隔 371801us，生成间隔仅 52168us。这一次没有错误缩窗，却撞上 `CanSend` 独立的 `200ms + 2*最小RTT` 反馈年龄门槛，导致另一个正常反向抖动变成源端暂停。

后续候选 `iroh-feedback-age-20261010` 去掉这个重复时间门槛，保留最大 64 个未确认帧、32–256KiB 字节目标、完整编码帧跨软额度计账、底层 QUIC 和本地硬队列边界，以及额度耗尽后的 250ms 稀疏探测。完全失去反馈的连接会耗尽有限额度，不会无限正常发送。`FeedbackSilenceDoesNotPauseBeforeBoundedCreditIsConsumed` 先在旧逻辑失败，见 `20261010_feedback_age_before.xml`；新逻辑还验证 64 帧达到后暂停和探测恢复。旧年龄行为的测试同时更新为新的容量约束，并保留重复确认不释放字节额度的验证。修改前脏内容保存在 `backup/iroh_feedback_age_20261010/`。

第二候选通过 35 个媒体测试及 frontend 生命周期目标，direct 和私有 TLS Relay 各 2 个 CTest 均通过，见 `20261010_feedback_age_direct.xml`、`20261010_feedback_age_relay.xml`。完整 Setup 已安装到 90，3.3.97、314 项 SHA-256 一致、服务 Running。Setup SHA `A2DF5889103A89074D0F026DF38D82DA8A5A6074E021FE7C7A5C100D02C88AB5`，Render SHA `7D113B9A85C459D9786CB5A2D2BD119D72F97CECE3A3799A24DC43AABF7C7DFA`。交付记录 `20261010_feedback_age_delivery.json`，四对开发运行产物哈希见 `20261010_feedback_age_development_hashes.json`。上一候选已按 319 项文件哈希清单可逆移动到 C 盘开发归档，见 `20261010_forward_candidate_archive.json`。

## 最终候选实机结果与限制

实例 `9b1b2664-0b7a-41a5-b6e0-37bc95dc8f1b`，10 秒预热后持续拖动约 150 秒，未限速，全部路径样本 direct。完整结果为 47.8–60.3 FPS、最大交付间隔 528.4ms、11 次 >100ms、1 IDR/45 RFI。33 个窗口中 26 个没有 >100ms 间隔，这些窗口为 59.7–60.3 FPS、最大 44.2ms。不得只引用这些正常窗口宣称整体验收通过。证据 `20261010_feedback_age_game_direct.*`、`20261010_feedback_age_measurements.json`。

在线期间不再记录此前反馈误判导致的 >=80ms 采集暂停或 >=100ms 编码输出空洞，发送 busy/failed/flight_limited 为 0。最终故障段在客户端约 09:39:35–09:40:00 出现重复参考链恢复请求；服务端仍每五秒完成约 300 帧，QUIC 丢包计数增加到 256。源码恢复门槛的进一步诊断依然待做，本轮没有将该计数直接等同于物理网卡故障，也没有声称网络丢包根因已经确定。

最终服务端日志中 09:40:10 之后的硬队列满和采集暂停属于客户端退出后的宽限期，不作为在线画面故障。首候选一轮 1751.5ms、另一轮 200.6ms 与最终 528.4ms 的差异不是严格 A/B：各轮丢包不同，不能据此单独量化修复收益。两个反馈错误门槛分别有修改前失败、修改后通过的确定性回归支持。

结论：已修复“返回反馈被误判为正向积压”和“剩余额度尚足却因反馈年龄停采”两处具体缺陷；**正常直连整体流畅度仍未通过**，后续重点是突发丢包后的参考链恢复，首先补充恢复请求准入/执行/生成帧的关联日志，对照 Sunshine 的编码请求合并与 Moonlight 的 RFI/IDR 路径。不得把剩余停顿归为用户主动限速，本轮没有 QoS。

## 清理

只停止本轮测试实例及 Client。Console 原配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 恢复，临时 Relay PID 54032 停止，QoS 0，仅桌面 Render PID 49308；Console Running，校验证书和主机名的 HTTPS 返回 200。见 `20261010_forward_delay_cleanup.json`、`20261010_forward_delay_operations.json`。临时 iroh 配置未启用，后续实测须重新启用并记录身份。

90 保留最终候选安装，开发文件仅在 `D:\112233`；本机开发 dist 四对 SHA 再次核验一致。完整长任务未完成，本轮未提交或 push。
