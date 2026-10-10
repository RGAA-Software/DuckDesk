# 持续混传、直连对照与真实 Relay 短故障

本批沿用 `iroh-driver-queue-20261010` 完整安装，90 Render SHA-256 为 `18D1EAA212B4160FA436CC93EBBF8A1EAFAA63D41409A83C7D41BD52EA8F3B3E`。没有修改产品代码或安装新包。Windows/Linux Relay 保持原配置，故障测试仅短暂停止/启动 90 的 Pixels.Relay。

## 已完成的实机结果

同一 GameHook 应用、Client、输入脚本，每轮上传/下载 1,610,612,736 bytes。每方向 SHA-256 均为 `ad2e739e4081bb1a800bfa3dbee055a23663815fadd36f639a6bc4d491ee67da`。

| 场景 | 观察窗口 | 视频交付 FPS | 最大交付间隔 | >100 ms 次数 |
|---|---:|---:|---:|---:|
| 强制 Relay + 文件 | 12 个五秒窗口，测试 61.081 秒 | 27.9–57.3 | 628.3 ms | 45 |
| 自动选路，实际 direct + 文件 | 12 个五秒窗口，测试 60.667 秒 | 58.3–60.1 | 149.6 ms | 2 |
| 强制 Relay，无文件 | 15 个五秒窗口 | 46.5–60.1 | 214.5 ms | 27 |
| 另一次正常 Relay 混传（防火墙注入未生效） | 11 个五秒窗口，测试 60.021 秒 | 60.0–60.3 | 53.3 ms | 0 |

这些是解码前交付统计，不是显示 FPS 或输入往返延迟。第一轮 Relay 和直连均投递了三千余条鼠标消息，日志 sent=true；截图检查见游戏画面，不能代替高动态覆盖和输入时延测量。正常 Relay 也有良好一轮，说明不能将性能差异直接写成“所有 Relay 都慢”或“文件必然导致卡顿”。原始报告和汇总位于 `20261010_sustained_mixed_{relay,direct,summary}.json`、`20261010_sustained_relay_baseline.*`。

短故障使用真实服务停止：90 本机时间 20:17:09.020 Stopped、20:17:12.299 Running。Client 同一次连接观察到 5486.1 ms 画面交付间隔，随后继续收帧、完成文件往返及远端清理。两端时钟不同，不能相减声称“重启后多少毫秒恢复”；5.49 秒是 Client 自身统计的总画面间隔。证据 `20261010_mixed_relay_service_injection.log`、`20261010_mixed_relay_service_outage.{json,log}` 及 Render 日志。功能恢复通过，恢复后仍存在短抖动，不算性能通过。

## 定位到哪一层

- 三个无注入对照均未出现达到 50 ms 阈值的 `iroh.message_dispatch` 告警，上一批驱动占住网络控制队列的秒级阻塞未复现。
- 异常 Relay 帧有源间隔约 16–17 ms、接收间隔 368–520 ms、组帧调用仅 4–42 us 的记录；第一轮重组统计没有 loss/malformed/late 增长。延迟已存在于进入组帧之前，不能通过增加组帧缓冲来修复，也不能把所有间隔都归因于源游戏。
- Relay 无文件也有抖动，大文件并发时首轮进一步恶化；第三轮健康混传又说明路径或时间条件有变化。本批没有逐轮记录具体 Relay TCP 对端，不能判定慢的是 BJ 还是 90。后续必须记录实际 TCP 对端和队列/发送时间，不能凭 path=relay 猜测服务器。
- 源码核对：iroh 1.3.0 Relay 通过 TCP/TLS 批次 `send_all` 转发；两端已设置 TCP_NODELAY。Noq 默认 Cubic，不能未经证据套用 BBR ProbeRTT 解释。
- 另发现 `Channel::Open` 设置不同可靠流优先级，但 `Channel::Accept` 的回程发送端未应用同样优先级。是应补齐的双向调度缺口，尚不能宣称它就是本批视频卡顿根因；本批未修改它。

只读参考：Sunshine `3cba9baebac882b336be3ebe129ee612cb189853` 的 `stream.cpp` 对帧内发送做 pacing；Moonlight common-c `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac` 的 `VideoDepacketizer.c` 在解码队列溢出时清队列并请求 IDR。这里的组帧调用/解码耗时未显示对应积压，不能照搬后者掩盖上游传输延迟。配置的 RustDesk `D:/source/rustdesk` 当前不存在，未创建第二份或修改外部参考。

## 测试工具修正与清理

最初的本机临时防火墙规则没有造成可观察的中断，不能算断网恢复；结果保留为 `20261010_mixed_relay_unverified_injection.*`。曾有报告路径碰撞和 PowerShell Process.ExitCode=null 的脚本错误：已修正命名，原始 Relay 数据从保留的 Client 日志及文件重新构建/校验；JSON 记录恢复来源。正式恢复结论只使用后续真实 Relay 服务停止测试。

临时防火墙规则已撤销，自有 Client/游戏实例和远端大文件已清理；90 三服务 Running，仅桌面 Render 15760。没有改持久配置。总结 JSON 的 `functional_transfer_passed` 与性能通过分开，单轮文件成功不能代表稳定性验收。

下一步：先补可靠流回程优先级和 Relay 实际路径/发送排队观测，再用同一负载分别钉住 90/BJ 路径对照。依据排队和 TCP 重传证据决定 pacing/背压修复，不盲调 QUIC 常数或扩大组帧队列。真实 NAT、长期稳定性仍未完成；录制等独立业务不纳入本批。
