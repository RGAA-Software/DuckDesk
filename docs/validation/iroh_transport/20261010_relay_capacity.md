# Relay 容量拒绝后的候选恢复（2026-10-10）

本批已开发、聚焦验证并安装到 90。没有新增认证或短期票据，没有再次修改 iroh 隔离补丁。

## 原因与修复

两个正式 Relay 都达到连接上限时，新 Endpoint 会被拒绝。容量释放后，原健康探测仍可能重复选择仍满的 Relay，超过 FFI 的五秒初始等待窗口。改动前首轮偶然通过，第二轮失败；两份原始结果都保留在 `20261010_relay_capacity_before*.{json,log}`。

项目层使用固定 iroh 1.3.0 公开的 `RelayStatus::auth_denied_reason()`，不解析原因字符串。无健康 home 时暂时跳过被拒绝候选，允许备用候选接管；五秒后允许重试。始终保留至少一个可重试候选。已健康连接不因冷却到期而重新选路；Console 撤下候选优先于本地重试。Console 撤下所有未被抑制候选时，重新激活剩余的已配置候选，避免本地策略留下空活动候选集。

实现为 `rust_transport/px_transport/src/relay_candidates.rs`、`relay_health.rs`，探针为 `px_transport_probe/src/capacity_probe.rs`。正式 Relay 容量检查和 Console 查询本批未改；十节点数据库测试补齐已有过滤条件。Console 按连接占用率排序并不保证 iroh 均匀分配，未宣称负载均衡完成。

## 聚焦结果

| 场景 | 结果 / 证据 |
|---|---|
| 两个正式 Relay 本地进程，各上限两连接，释放另一台 | 476ms；最终构建复核 231ms，`20261010_relay_capacity_{after,final}.json` |
| 先拒绝，随后释放原拒绝节点 | 4168ms，`20261010_relay_capacity_restored.json` |
| 恢复后的可靠流与数据报 | 每轮 6,291,520 bytes 可靠回显及 120/120 数据报；退出后两个 Relay 可重新接入 |
| 保持视频发送的 SDK 双 Relay 故障回归 | 8.666 秒恢复，`20261010_relay_capacity_business_regression.{json,xml}` |
| 动态候选回归 | 两例通过，包含空候选后添加、持有连接替换候选和新 peer；`20261010_relay_capacity_candidate_regression.*` |
| Rust 生命周期/候选更新 | 6/6，含取消等待、重复 stop/drop、关闭退出、闲置十六秒及 Console 撤下替代候选 |
| C++ frontend / transport | 两套 CTest 通过 |
| PostgreSQL Relay 节点 | 4/4；新十节点用例覆盖满额、离线、禁用、过期、维护和释放容量，`20261010_relay_capacity_pg_report.json` |

容量探针运行正式 `px_relay.exe`，通过 `scripts/tests/run_iroh_relay_capacity.py` 可复用。它是单机 loopback TLS 的功能检查，不是十机器性能或跨宿主容量验收。正常故障恢复与满额拒绝恢复是不同场景，不混用耗时。

## 安装与实际业务

90 使用完整 Cloud Node 3.3.97 候选 `iroh-relay-capacity-20261010` Setup；文件在 `D:\112233`，314 文件核验通过，服务 Running。Server/两 Relay 沿用上一批完整安装，本批未覆盖它们。

- Setup SHA-256：`E5497A340DB2F9A3DF37593ED66DDFD9149A743E28C67E56A74BE7948D2BE9D8`。
- 安装 `product-manifest.json` SHA-256：`72B4B2B208E16F586091C670B2DA9188DF39E86113E37D352BA9F84D261631BE`。
- 安装 Render SHA-256：`BBCDD73987E8D1B4615BCCFEE20C94B3AA9D152E501E223BAE11DE238AD1947F`。
- 本机 Client、Cloud Node Client、Render、Service 的开发 build/dist 哈希一致，详见 `20261010_relay_capacity_installation.json`。开发与完整包使用不同 Release 配置，不能比较两种配置的 EXE 哈希来判断交付。

强制 Relay 的真实游戏和 RDP 都通过，记录 `20261010_relay_capacity_live.json` 及各自 log/png。游戏有输入转发和可见游戏画面，八个五秒媒体交付窗口约 60 FPS，最大间隔 56.3ms，无 >100ms、IDR/RFI。该指标是解码前交付，不是显示 FPS，也不是长期稳定性验收。

RDP 通过可靠流接入并发布图像（采样计数 38、56），截图显示原工作区和记事本内容。截图中原工作区的另一个应用有自身页面错误，不作为 Pixels 传输成功与否的判断。测试前后记事本仍是 PID 16496、Session 2、启动时间 `2026-10-02T01:21:24.8049191Z`，未注销会话或重启工作区应用。未用静态桌面宣称 RDP 60 FPS。

测试 Client、实例和会话已退出。三服务 Running，仅桌面 Render 48640；90/BJ fresh、ready、未维护，generation 81/7。持久三份配置 SHA 不变，机器诊断 trace 为空。快照见 `20261010_relay_capacity_remote_{install,cleanup}.json`、`20261010_relay_capacity_relays.json`。

## 接续

不重复本批安装或 BJ 登记。M2 的跨宿主容量、负载分配与长期混合负载仍待补；M3 接下来核对 Windows/RDP 剪贴板、文件、录制、输入与音频等真实用户路径，保留已有通过项。Android 最后。P0–P7 未整体完成。
