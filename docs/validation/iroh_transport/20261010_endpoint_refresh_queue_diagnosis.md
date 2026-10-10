# 双 Relay 持续视频故障恢复：地址刷新与队列阻塞

本批先接通 Console 原 frontend 凭据的 EndpointAddr 刷新、Client native/RDP 重拨；不改变资源会话修订号、有效期、额度或 EndpointId，不增加认证票据。

## 补丁前的失败与原因

- 空载本地两个 TLS Relay 之间重拨成功，不能覆盖真实视频负载。
- 90 游戏八次故障测试均在取得 BJ 地址后握手超时；一次诊断准备中止未执行停机。汇总见 `20261010_endpoint_refresh_real_attempts.json`。
- 两端 trace 表明 BJ 已连接且 ping/pong 正常，但旧 Relay 队列阻塞公共发送循环。旧 QUIC 关闭时已清理 selected_path；独立新 Endpoint 连接同一 Render 也失败，不能归因于旧客户端缓存。
- 在本地用例加入每 16ms 一个 9000-byte 视频帧，先收到视频再停止当前 Relay：75 秒未完成重新准入，`20261010_dual_relay_business_loaded_failure.xml` 保留原失败。

固定版本为 iroh 1.3.0，上游 revision `0072d7d84b233f9e7185eb676f049beaf557ac03`。
`src/socket/transports/relay/actor.rs` 的公共发送循环等待满的目标 Relay 队列，期间不继续取其他目标的数据报。
断线拨号时三秒清队列计时器每次重新创建；本次连接失败快于三秒，退避期间又不消费数据队列，持续媒体把旧队列填满。
因此健康 BJ 的握手包也可能被挡住。此分析已得到相同负载用例的补丁前后对照支持。

## 最小补丁与本地验证

用户明确批准隔离副本补丁；原 Cargo registry 与原始参考源码只读。
`patches/iroh/manifest.json` 固定发布 crate、原文件、补丁及全部隔离文件哈希。
只在断线拨号和退避阶段消费并丢弃无法送达的数据报；可靠载荷仍由 QUIC 重传。健康 Relay、业务授权及其他依赖不变。

| 相同持续视频场景 | 结果 |
|---|---|
| 补丁前，服务端 auto、客户端强制 Relay | 75 秒断言失败 |
| 补丁后，服务端 auto、客户端强制 Relay | 30.926 秒通过，重新准入且收到视频 |
| 补丁后，双方强制 Relay | 32.654 秒通过，重新准入且收到视频 |

两次通过均保持原 Relay 离线、EndpointId 不变。报告为 `20261010_dual_relay_queue_patch_{loaded,forced}.json/.xml`。
普通 frontend/transport CTest 通过，Rust transport 两个生命周期测试通过。
约三十秒恢复主要等待旧 QUIC 超时，不是连接内无缝迁移，不代表公网实机验收。

## 完整包与公网实测

完整候选 `iroh-relay-queue-20261010` 已通过 Setup 安装 90，314 文件哈希通过；安装和本机开发产物身份见
`20261010_relay_queue_installation.json`。没有向安装目录手工复制运行 EXE。

实际游戏实例 `40c74cb5-9f01-4bb5-a29a-f74956eeefc7`：停掉 Windows 90 Relay，约 6 秒取得 BJ 地址，原客户端
29.928 秒后重新准入且恢复解码。EndpointId、资源会话 `e00a3f9a-3da6-4f2d-986e-3e037c621ca2` 与游戏实例保持，
恢复后的五秒窗口收到 304 帧、60.7 FPS、最大间隔 30.1ms、0 次 >100ms；新 QUIC 握手本身 28ms。
整个测试及自有客户端退出完成前，90 Relay 保持停止。结果见 `20261010_relay_queue_real_game.json`。

这是一次真实 90→BJ 故障恢复通过，不代表双向切换、长期性能或无缝迁移已验收。等待旧 QUIC 超时约三十秒仍需改善。
清理后两 Relay ready/fresh，90 三服务 Running，只有桌面 Render；临时 trace 环境已清理。
后续以 [status.md](status.md) 最新检查点继续，不重复安装或登记。
