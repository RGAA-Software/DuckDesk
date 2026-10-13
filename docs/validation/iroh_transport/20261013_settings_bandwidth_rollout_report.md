# Client 设置、实际路径与 Relay 带宽筛选上线验收

2026-10-13。基于 `3ec1370467bed86e1a96c097bab1f3eeacc83eb7` 的未提交工作树；本批是已安装完整基线之后的授权增量开发交付，不是新 Setup 或正式发布。开发检查复用[客户端报告](20261013_client_settings_report.md)和[带宽报告](20261013_relay_bandwidth_report.md)，不重复第 0.9 节已完成主线。

## 结果

- 本机三个独立 Client 进程及三个新 WebView 实例，以同一 application_id 读取既有 LevelDB。真实界面确认统计显示、静音、英文、浅色、全屏和 30 FPS 在重启后恢复，接收/解码约 30 FPS。源仅提供 1920×1080，分辨率切换只有前批回归证据，不计真机验收。
- 90 Console、Web、数据库升级至 schema 42，Backup 计划同步 42。Server 35 项、Cloud Node 315 项完整文件清单及清单本身 SHA 均通过；Console、Backup、Relay、Service 正常，Panel 已恢复 Administrator session 2。Node Service/Render 不更新、不重启。
- Windows Relay 临时预算 1 Mbps，6 MiB 可靠数据按 30 秒发送；实际报告 1,155,992 bps 越过 90% 阈值。等待节点 15 秒上报周期后，新源 home 为 BJ，90 原生 Client 连续 24 次 selected-path 日志确认 `https://49.232.233.61:4605/`，实际出图。24 个呈现窗口计 7,166 帧/120.269 秒、59.583 FPS，仅为本批功能样本，启动单列，不扩成卡顿或长稳调查。
- 本机旧 Client 同时保持 Windows Relay，截图仍 Connected、接收总量 10.62 MiB、解码 30 FPS。Probe 的原 Endpoint、QUIC 连接和可靠流身份不变，可靠往返和双向数据报保持；三段 2 MiB 内容校验通过。负载回落至 355,368 bps 后结束 Probe，并恢复原预算 null。
- 公网本批没有另起业务实例验收“空闲后再次分配到 Windows”；候选恢复的实际 API 证据复用前批隔离 Console/Service/Relay 测试。此处明确区分实际连接和仅回读负载。

## 交付身份

| 90 运行载荷 | SHA-256 |
| --- | --- |
| Console | `B50A9530996525E64D2B6B8F591946B64811144B5121BD4071F2A64FD54A6FC9` |
| Console admin | `15B1F00DC52BF8EB2E1315ED7E8CE982768596A4472E8B74AABAC9DCAF5C4811` |
| 数据库工具 | `931EC67705465134B14EC2B75FFC799041D657413E8EC811A118806EA5FE4C29` |
| Cloud Node Client | `2C460E5C5757E558F0F4F2F50AA3033EDD794DD3876E62D333FC0BF262C7F152` |
| Cloud Node Panel | `0745F02583E8D0C324D367B23B6DC56790D7C632CDE047D927B5272B224E311A` |

Web 使用 `index-CSfyqPO1.js`。Server 清单 SHA `CB79DC1C…`、Node 清单 `3787B897…`；迁移前 PostgreSQL dump `902CE368…` 保留在 `D:\112233\client-settings-20261013\console\database-before-schema42`。Server/Node 各自增量前文件备份保留。包的原 publisher/version 身份不变，不把未提交增量称为正式优化发行包。

本机 Client/Cloud Node/Remote 的 Client 与 Panel 共六对 build-tree/dist SHA 再核对相等。常规构建均为 Release，明确 18 jobs。两修改 PowerShell 脚本解析、Rust workspace 实际 edition 2024 的 rustfmt 和 diff 检查通过；严格 Clippy 仍有前批列出的既有未改动代码告警，未压制、未宣称全通过。

## 主机选择和清理

用户恢复 90 更新与少量验证后又明确排除 Company，后续仅用本机、90、XRServer03。Company 先前完成的 Client/Panel 增量保留；其本批 Client PID 18864 精确关闭，实例/资源已结束，本 MCP 会话已断开。没有在排除后继续检查或使用 Company；该 Client 实际所选 Relay 未验收，不计通过。

最终六 owned 实例 stopped、六资源 closed；guest 注销 204，owned 应用/部署禁用。本机三个 owned Client 和 90 本批 Client 均退出，90 临时页面 PID 14800 按创建时间/命令路径核对后停止，48778 listener 为 0。90 Panel PID 16188 在 session 2，未触碰 Windows 用户、profile 或 RDP 会话。双 Relay fresh/ready/未排空，Windows 默认 Endpoint 1、BJ 0。原九应用 spec、九部署配置逐项相等，授权 80、RDP 容量 10 保持。

90 Client 新日志 3,706,732 bytes，先 gzip 为 257,752 bytes 再下载，双端 SHA `DEDEF25E65A74101382EDC832DFD796DA84BFD17BCE7A30699FC2B1BF2B6245F` 一致。24 次实际 BJ 路径、呈现统计和图片哈希见[证据 JSON](20261013_settings_bandwidth_rollout_evidence.json)。原文、干净的应用窗口截图和早期无效夹具输出保留于 `.cache/client-settings-rollout-20261013`，启动信封和凭据不进证据或 Git。

## 失败样本与边界

早期夹具把节点刷新当 6 秒（实际 15 秒）、拒绝旧本机 Client、准备后延迟导致未接入，均修正/清理并保留，不当网络失败或通过。90 窗口聚焦被系统拒绝，统计开关点击被跳过；后续以实际日志确认路径，没有绕过系统焦点限制。旧桌面矩形截图混入其他窗口已丢弃，改为独立应用窗口捕获。MCP 截图保存于调用方磁盘，尝试远端下载确认不存在后，直接核对并复制调用方文件。清理夹具的许可证 404、应用禁用后 guest 查询实例 403、迁移表 schema 名错误均留档；最终通过正确管理/guest 路由及原生 READ ONLY 查询核实终态。

带宽配置只筛选新候选，健康旧 home 保留；不进行带宽整形/预留，也不保证平均分流，候选内仍由 iroh 探测选择。友好 Relay 名称映射未实现；真实复杂 NAT、规模和长期稳定性仍是后续缺口。没有构建新的 Linux 镜像或 Android，也没有恢复暂停的短顿、NIC、饱和混传调查。本批配套上线与少量真实验收完成，代码尚未提交/push。
