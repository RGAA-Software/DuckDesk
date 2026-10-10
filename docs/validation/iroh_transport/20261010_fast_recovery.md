# 缩短 iroh 故障恢复等待

固定 iroh 1.3.0（上游 revision `0072d7d84b233f9e7185eb676f049beaf557ac03`）的
`src/endpoint/quic.rs` 明确记录默认连接 idle timeout 为三十秒。Pixels Client 在旧连接结束后才刷新地址和重新准入，
所以此前修复了断线 Relay 队列阻塞，仍要等待约三十秒。

本批在 Pixels `TransportEndpoint` 上通过公开配置设定八秒无应答超时、一秒连接心跳。不改上游补丁；不以视频帧是否到达
判断断线。静态桌面、静音和用户未操作时仍依赖 QUIC 心跳保持连接。实际超时受 QUIC 重传计时影响，八秒不是所有网络条件
下的恢复上限。Console 的应用退出宽限期、原资源会话凭据和八秒业务重拨预算保持原逻辑。

本地证据：

- 同一持续视频双 Relay 故障场景，改动前 30.641 秒，改动后 8.625 秒；原 Relay 保持离线、原 EndpointId 和业务准入保留。
  十五秒恢复检查在旧版失败、新版通过，见 `20261010_fast_recovery_before.json` / `20261010_fast_recovery_after.json`。
- 无应用流量十六秒（两倍 idle timeout）后，原连接仍能双向可靠读写并传数据报。Rust 三个测试通过。
- 单 Relay 重启停机 1013ms，原 QUIC 连接继续使用：可靠往返 175 次，数据报 1156/1201，最大数据报间隔 1339ms。
  短断线没有变成业务重拨；不要求中断期数据报不丢失。见 `20261010_fast_recovery_short_outage.json`。
- 重新链接后的普通 C++ frontend/transport CTest 通过。

完整 `iroh-fast-recovery-20261010` Cloud Node 3.3.97 Setup 已安装 90，314 个文件 SHA 核验通过。
安装/开发产物身份见 `20261010_fast_recovery_installation.json`。

真实游戏以同一个 Client、实例和资源会话连续经历两次故障：

- 停 90 Relay，8.028 秒后经 BJ 重新准入并解码；计时从协调器确认停服后的检测点开始。
- 恢复 90，再停止 BJ 容器，原客户端第二次重新准入并解码。恢复后的五秒窗口 302 帧、60.1 FPS、最大间隔 43.9ms、
  0 次 >100ms。反向计时标记落后于实际停服，原 0.008 秒读数无效，正式报告置空；只认定功能通过。

两次 QUIC 握手分别 29ms 和 7ms，EndpointId、实例 `9b508329-8d85-4043-b37e-bacfd0921cb0`、资源会话
`483f920d-202f-4c9a-adfe-0b9daeebe082` 不变。证据见 `20261010_fast_recovery_real_roundtrip.json`。
这些是新 QUIC 连接上的原业务会话恢复，不是连接内无缝迁移。

测试后自有 Client、会话和实例清理完成，90/BJ Relay 恢复 ready/fresh，三 Windows 服务 Running，持久配置哈希未变。
下一步以 [status.md](status.md) 最新检查点继续动态候选和维护排空。
