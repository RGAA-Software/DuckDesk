# BJ 短停顿定位：实际 TCP 序号缺口及发送额度反馈

日期：2026-10-10。本批只定位，不改传输参数、不构建/升级、不恢复旧 Console 配置。

## 结论和边界

本次复现确认，实际强制 BJ Relay 路径出现 **TCP 按序交付阻塞**：90→BJ 有迟到的序号区间，BJ→Client 有 SACK、重传和发送积压。随后产品的在途额度控制收缩并暂停采集，产生额外源端空洞。不是简单的“组帧计算花了几百毫秒”，也没有证据指向固定 Nagle 定时器。

iroh 数据报在此路径仍承载于 Relay 的 TLS/WebSocket/TCP 中。数据报 API 不会跳过底层 TCP 的缺口；已经到达的后续字节也需等待前序字节。UDP 4605 是 QAD 地址发现，不是此连接的 UDP 媒体中继。此次不改变协议设计。

**尚不能确定丢失/迟到发生在哪台路由器、哪块网卡或运营商环节。** 仅在 BJ 抓包，不能区分上行未见的分段是丢包后重传还是严重乱序；不能把全部 513ms 逐字节映射到加密流内的视频帧。这里定位到具体 TCP 连接和阻塞机制，不冒称找到了物理链路根因。

## 新一轮实测

- 沿用已验证拖动脚本，未重复 Console 升级或 BJ 登记。测试实例 `f1223d26-1ea6-4d3c-9f0d-af740ef2446f`、Client PID 82536，实际路径 iroh-relay。
- 15 个五秒窗口、4430 帧，51.1–60.1 交付 FPS，最大间隔 **513.0ms**，6 次 >100ms，0 IDR/0 RFI。比上一轮 258.3ms 更差的结果保留，没有筛除。
- Client frame 3751：源端间隔 49.944ms、接收 331.042ms、组帧调用 11us。
- Client frame 4133：源端间隔 16.566ms、接收 512.998ms、组帧调用 7us。
- 结果和两端时序：`20261010_bj_capture_game_results.json`、`20261010_bj_capture_game_measurements.json`、`20261010_bj_capture_client_timing.log`、`20261010_bj_capture_source_timing.log`。解码前交付 FPS 不是屏幕显示 FPS。

## BJ 抓包和内核证据

tcpdump 仅抓 eth0 上 TCP 4605、snaplen 128 包头及加密前缀；227639 包，内核丢弃 **0**。同步每约 100ms 在容器网络命名空间采样 `ss -tin`，没有调整网卡卸载、拥塞算法或网络限速。

| 方向 | 证据 | 对应现象 |
|---|---|---|
| 90 `39.71.45.66:30870` → BJ | TCP 序号 4093169309 缺口持续 315.917ms；BJ 保持该 ACK 并用 SACK 确认更后的字节，直至 1424 字节缺段到达 | 校准后与 Client 331.1ms 停顿时间吻合；数据已在 BJ 内核乱序队列，应用层不能越过缺口读取 |
| BJ → Client `39.82.198.244:27544` | 重复发送序号、SACK 确认后续字节；累计 ACK 有 387.877ms 停滞；内核 `bytes_retrans` 最大 26755 | 与 513ms/240.9ms 停顿处于同一异常窗口；不能由客户端组帧计算解释 |
| 同一 BJ→Client socket | Send-Q 最大 184926 bytes，notsent 最大 23429 bytes，TCP RTT 最大 328.717ms，正常最小约 11ms | 内核中确实发生积压与重传，不仅是产品统计 RTT 变化 |

活动连接未出现有效 ACK 的 TCP 零接收窗口；RST 报文的零窗口不计入。BJ 接收队列采样最大 4272 bytes，未显示大量已可读数据长期卡在 Relay 应用；但采样不能排除更短的用户态调度停顿。

上行缺口用独立 `tcpdump -S` 解码交叉确认，见 `20261010_bj_tcp_sequence_crosscheck.log`：BJ 时间 1791603697.074382 报 SACK，1791603697.390285 缺失段到达。客户端时钟与 BJ 有约 5.050s 偏差，探测往返 47ms；时间关联误差约几十毫秒，不能直接相减两机墙钟。

抓包、采样源文件保留在本机 `C:/Users/chess/AppData/Local/PixelsRelayBuild/bj-stutter-20261010/` 和 BJ `/home/ubuntu/pixels-relay-stage-20261010/stutter-114027/`。原始 pcap 的 SHA、时钟校准、采样极值和关键时段见 `20261010_bj_stutter_capture_evidence.json`；按连接序号/SACK 的分析结果为 `20261010_bj_tcp_capture_analysis.json`，分析脚本为 `20261010_bj_tcp_analysis.py`。没有把普通 TCP 重排直接统称为网络丢包。

## 产品侧如何放大停顿

源端 frame 4133 所属异常窗口：24 帧共 125400 bytes 在途，预算因正向延迟增长降到 35855 bytes；采集暂停约 249882us，frame 4172 编码间隔 266ms。此前 frame 3774 也有预算约束下的 266ms 空洞。接收端等待先发生，反馈收缩与后续采集暂停属于另一环节，不能全部归于网络或全部归于额度判断。

当前依据不足以直接取消背压或把 250ms 改小：取消背压可能增加 TCP 中旧视频的积压。下一修改应围绕实际中继吞吐、在途旧帧和恢复采集节奏做可验证对照，而不是再调 IDR 冷却；本次两轮均无 IDR/RFI 风暴。

## 上游源码核对

- iroh-relay 锁定 1.3.0：`client/tls.rs`、`server/streams.rs` 均调用 `set_nodelay(true)`；`server/client.rs` 每轮处理后 flush，没有固定 200ms 刷新周期。本轮无对应设置失败证据，不把问题归因于 Nagle。
- Sunshine `D:/source/Sunshine`，HEAD `3cba9baebac882b336be3ebe129ee612cb189853`：`src/stream.cpp` 在帧内分批 pacing，避免突发。
- Moonlight Qt `D:/source/moonlight-qt`，HEAD `2e13ed9977bc31c73caf8428f08f58d793313ece`；实际 common-c 位于嵌套 `moonlight-common-c/moonlight-common-c`，HEAD `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac`。`VideoStream.c` 从 UDP 接收后进入 `RtpVideoQueue.c` 的重排/FEC。它能在 UDP 上处理后续包，但不能让本方案 TLS/TCP 中尚未交付的字节提前进入组帧。
- 所有参考源码只读。本批没有机械修改第三方，也没有新增认证、短期票据或生产协议。

## 清理与接续

测试 Client/实例已退出，90 只剩桌面 Render 54168，Console 持久配置 SHA 仍为 `03352CEEAB08C7B89A342505D398629A6847757A617E19478849BBB567DD4438`。tcpdump/ss 采样已结束，BJ 容器继续受 Console 管理。

接续优先：利用本次确证的两条 TCP 连接定位路径抖动，并评估发送 pacing/额度恢复对第二次停顿的影响。不要重复登记、升级、空跑原型或声称这轮修好了。多 Relay 和 Android 顺序不变。
