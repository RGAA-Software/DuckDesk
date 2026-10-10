# BJ Relay 重传交叉验证与发送额度恢复

日期：2026-10-10。本批沿已定位的 TCP 等待继续验证，只修复已复现的应用层放大因素，不修改 QUIC pacing、IDR 常数或鉴权。

## 抓包和基线

测试实例 `b2e715bf-ea98-4079-ae3e-88f2b6c6cd4a`，同一 2dadventure 游戏、本机 Client→BJ→90，预热后持续拖动。16 个五秒窗口、4315 帧，35.9–61.0 交付 FPS，最大 761ms，25 次 >100ms，0 IDR/RFI。不隐藏比上批更差的结果；公网两次采样并非相同丢包轨迹。

90/BJ 同时记录 TCP 包头，BJ 200260 包、内核丢弃 0，90 pktmon 无事件丢失。本机 NIC 采集只有 7642 包，缺少大部分入站游戏字节，因此不作为完整下行证据。三端数据保留在 `C:/Users/chess/AppData/Local/PixelsRelayBuild/bj-threepoint-before`，90 在 `D:\112233\bj-threepoint-before`，BJ 在 `/home/ubuntu/pixels-relay-stage-20261010/stutter-115057`。SHA 和 TCP 序号交叉检查见 `20261010_bj_threepoint_crosscheck.json`。

- 90→BJ，序号 2427651458：90 记录四次发送，同序号首发至末次约 145ms；BJ 在后续收到该区间，按序缺口持续 114.2ms。这不是 Render 尚未调用发送接口的空白。
- BJ→90，序号 971996536：BJ 多次发送，首发 epoch 1791604284.601821，最后一次重传 1791604284.895857，90 首见 1791604284.919181。BJ SACK 等待 267ms，与 Render 反馈到达间隔 294190us 对应。
- BJ→90，序号 974767120：BJ 首发至最后重传约 491ms，90 后续才收到；BJ SACK 停顿 442ms。不能确定丢包发生在哪台路由器或网卡，也不把不同机器墙钟直接当成精确单向耗时。
- BJ→Client 同时有 SACK 停顿（最大 580ms）。底层 TCP 必须补齐前序字节，iroh datagram API 不能绕过它。UDP 4605 仍为 QAD，不是该 Relay 的视频承载。

## 已确认的软件问题及修复

客户端 frame 1144/1160 的源端出帧间隔为 266.8/267.8ms，接收侧增量延迟只剩约 4.6/4.8ms；组帧调用为 6/8us。Render 的正向延迟估计随后从 416ms 降至 41ms、19ms，但额度仍为 32KiB；积压在返回路径的确认消息继续消耗这份缩小的额度，进而产生额外采集暂停。

`VideoFlightWindow` 原实现每次看到正向拥塞都延长一秒冷却。即使新确认已证明正向恢复，仍使用旧限制，直到冷却到期并采到新吞吐窗口。新增 `RecoveredForwardProgressReleasesCapacityDespiteDelayedReturnFeedback`：先制造正向拥塞，再恢复正向、延迟返回确认；旧代码拒绝继续发送，失败证据 `20261010_bj_recovery_before.xml`。

修复为：有效累计确认表明正向延迟不再超过已有阈值时，立即恢复正常 256KiB 额度。不清空未确认字节，不改 64 帧历史、本地硬队列或 250ms 稀疏探测。后续正向再次积压仍按接收吞吐缩窗，回归同时覆盖该行为。修改前文件保存在 `backup/iroh_forward_recovery_20261010`。

只读参考 Sunshine `3cba9baebac882b336be3ebe129ee612cb189853` 的 `src/stream.cpp` 1714–1758（按时刻分批发送），Moonlight common core `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac` 的 `src/ControlStream.c` 1378–1401（FEC 状态用非可靠、非排序报告）。本次是本项目额度恢复策略修正，不声称复制了上游算法，不另加一套 pacing。

## 验证和交付

Direct 与私有 TLS Relay 各两个媒体/生命周期 CTest 目标通过，包含新回归及真实积压、失联限额、停止回调验证。见 `20261010_bj_recovery_direct.xml`、`20261010_bj_recovery_relay.xml`。三个修改过的开发运行 EXE 已同步至各产品 dist，构建/发布 SHA 一致，记录 `20261010_bj_recovery_development_hashes.json`。

完整 Cloud Node 候选 `iroh-bj-recovery-20261010` 已构建并用 Setup 安装到 90，3.3.97、314 项文件 SHA 一致、px_service Running。Setup SHA `ADCDB42D2489443883C132CA3D9D23EAA9A8D6323BF5CFA820A0C0F5F1D22447`，产品 manifest SHA `E389A9E539D309AF84E1FD939F42EFD99673B5370E4672D854D483340A8EA47E`，安装 Render SHA `135C9B0C59E13BB0A3B1B7CF1F1103C3D35AD96A4FCEA1241AB4FB6A28D7C965`。记录 `20261010_bj_recovery_delivery.json`。

## 修复后同场景结果

实例 `8c42ed2f-ce00-44b9-9fb8-719d74d99ecd`、Client 85824，仍为 BJ iroh-relay，3600 次鼠标消息、两张截图核实视角变化且无遮挡。同样 16 个窗口、4814 帧，**59.5–60.8 FPS，最大 125.6ms，1 次 >100ms，0 IDR/RFI**。见 `20261010_bj_threepoint_after_results.json`、`20261010_bj_threepoint_after_measurements.json`。

在线阶段没有 >=80ms 采集暂停，反向确认 159–202ms 抖动时仍保有额度。Render 12:08:22 之后的暂停发生在客户端已结束采样/退出之后：未确认历史达到 64 帧，属于关闭后的保护，不计入在线流畅度。全部原始日志保留，未从原文件删除这些记录。

这次链路本身也更好：BJ 215800 包、内核丢弃 0，90→BJ 及 BJ→90 没有 >20ms 的已完成序号缺口/SACK 等待；BJ→本机有重传，最大 SACK 等待 28.1ms。因此 **不能将 761→125.6ms 全部归因于代码，也不能声称真实相同丢包轨迹的 A/B 已通过**。软件缺陷由先失败后通过的确定性回归证明；公网实测证明新包在此场景可用且没有采集暂停回归。

剩余 frame 3407：源端间隔 16.355ms、接收间隔 125.581ms、组帧调用 4us。按约 5.072 秒钟差定位的附近两秒内，90 发往 BJ、BJ 收上行、BJ 发往本机的最大有载荷分段间隔分别约 14.9/14.0/13.9ms，未见对应源端或 BJ 发送暂停。**剩余等待在 BJ 之后至客户端交付之间，尚未区分本机网络接收与线程调度。**

本机第二轮改为 pktmon 全组件，仍未覆盖大部分入站字节；90 第二轮也缺少部分反向字节。无 ETW 事件丢失不等于完整抓到所有网络流量，不能把抓包中的缺失当成链路丢包。完整三点包级对齐因此仍有采集盲区，后续需更换 Windows 接收侧观测手段，而不是重复同样的 pktmon 参数。原始文件 SHA、采集信息及剩余间隔见 `20261010_bj_threepoint_after_evidence.json`。

## 清理和接续

两个自有实例和 Client 均已退出，三端抓包/采样已停止；90 只剩桌面 Render 41916，Console/Relay/px_service 均 Running、QoS 0。BJ 容器 running/restart=0，Console 上报 ready/fresh、空闲连接 1。Console env SHA 仍为 `03352CEEAB08C7B89A342505D398629A6847757A617E19478849BBB567DD4438`，未回退旧配置。证据 `20261010_bj_recovery_cleanup.json`、`20261010_bj_recovery_final_relays.json`。

本批已完成额度恢复修复、完整安装及同场景复测；不能标记整个 Relay 性能或 P0–P7 已完成。后续保留 Windows 入站观测盲区和 125.6ms 短抖动；主线仍需 BJ WebView/桌面、90 Relay 统一 iroh、双 Relay 切换及 Windows/Linux 交付，Android 最后。未提交/push。
