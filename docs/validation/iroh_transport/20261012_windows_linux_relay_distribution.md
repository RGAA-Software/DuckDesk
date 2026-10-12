# Windows／Linux 双 Relay 的策略与实际分配验证（2026-10-12）

本批接续 [单 Relay 连接保留验证](20261012_relay_retention_integration.md)，补最新 Console／Service 策略在两台真实产品 Relay 中的连接分配证据。
Windows 使用已核对 Server 候选中的 `px_relay.exe`；Linux 使用此前已在 BJ 交付的不可变镜像，以独立 Compose 项目在本机运行。
没有重建 Relay、重装远端产品或操作 90。Android、晚间视频短顿、NIC 和饱和混传均未进入本批。

## 实际结果

两侧容量均为 2。先在 Windows Relay 保持两个 Endpoint、一条 QUIC 连接及同一双向可靠流，作为已占用连接；
这是准备满员条件的夹具。后续新端点完全消费 Console 实际下发的新候选，既有端点消费生产 Service HeartBeat 处理的结果。
最终恢复阶段四个新端点从包含两侧的同一候选集合启动，逐个记录其实际 connected home，没有逐个指定目标 Relay。

| 阶段 | Console／Service | 传输与占用 |
|---|---|---|
| Windows 满员，Linux 有余量 | 新候选仅 Linux；运行中 Windows home 保留并包含可用 Linux | 原身份／连接／可靠流保持；新两端点都实际连 Linux。两组各 4 次可靠往返、每方向 4 个数据报校验成功；Console 计数 Windows=2、Linux=2 |
| Windows 的 Console 排空已生效，Linux 容量已释放 | 新候选仍仅 Linux；原 Windows home 保留 | 原连接继续收发；两个新身份再次连 Linux并完成同样收发；Console 计数仍为 2、2；直接尝试已满／排空 Windows 的新端点明确被拒绝 |
| 解除排空并释放原连接 | 两侧实际上报 0，候选恢复为两侧 | 四个新端点实际 home 分布 2、2；两侧 Console 计数也为 2、2；这时新候选再次为空 |
| 最终释放 | 两侧真实上报 0、0 | Linux Compose 项目／网络清理，Windows Relay、probe 和 Rust 测试进程均为 0；隔离 PG 容器和卷清理 |

聚焦用例 `relay_distribution::windows_linux_relays_allocate_new_endpoints_without_disrupting_existing_connections` 通过，46.98 秒。
原单 Relay 用例在本次被明确过滤；已有组件、媒体和安装证据复用，没有重跑整个已通过套件。
PG 报告 `pg-20261012-174208-a54653dd` 确认记录的服务器源文件验收期间不变；入口对辅助程序和 Windows Relay 二进制做前后 SHA 检查。
Linux 容器内运行文件 SHA 与既有镜像声明一致。PG 驱动测试运行于 Windows，容器中的 Relay 则是实际 Linux/amd64 程序；这不是整套 Linux 测试重跑。

## 身份与复现

| 运行组件 | SHA／身份 |
|---|---|
| Windows Relay，已核对 Server 候选 | `9932BC19A68C57F5008A4882C44F20678481513A6BDEEE8E20C696F840C36C2C` |
| Linux Relay image | `sha256:c0d1449319def4b2ef6951027ad8805b57b98b2cd5fff493f2659a51253e6b16` |
| Linux 容器内 Relay | `9fbd130af5009168196ea8190991c03b66b45271d14bfda17641e8353f3963dd` |
| 新传输 probe | `F0F9C7EA359DEB7529190DFC0EC2762EFCBEB16BC22D52308498F5F64FE6D538` |
| 复用 Service unittest helper | `E9C4F7675AA2289F93FDAB1B97F53E2639544C71C26805B86641394E7B6BBC6F` |

仅辅助 probe 和聚焦测试编译，显式 18 jobs、Release 快速增量配置；既有 Relay 镜像／程序不重编译。
复现前按单 Relay 报告编译 probe／Service test fixture，并确保上述镜像已加载；Service 参数使用构建输出的准确路径：

```powershell
pwsh -NoProfile -File scripts/server_validation/relay_retention.ps1 -Distribution -ServicePolicyBinary build_official/cloud_node/cargo/target/release/deps/px_service-c1bb3805df7c551b.exe -EvidenceDirectory .cache/relay-distribution-20261012
```

入口将镜像 tag 解析为不可变 image ID，不拉取新镜像；Compose 项目随机命名，端口仅绑定本机环回，配置只读挂载。
测试进程有期限，子进程 kill-on-drop；失败时 Linux 守卫打印最近日志，并只清理自己创建的项目。
Console 与 Relay／Node 复用现有认证；Service 消费真实 Report、调用生产 HeartBeat，但不是完整 Service 安装进程或真实 GPU／应用验收。
详细结果、报告 SHA、源身份及清理见 [证据索引](20261012_windows_linux_relay_distribution.json)。

## 完成边界与接续

本机 Windows／Linux 联动的新端点分配、现有连接保留、排空和容量释放已补齐。
四端点 2＋2 是容量边界下的真实连接分配，不是带宽均分、十台物理 Relay 扩容、不同地域公平性或长期媒体稳定性的证明。
此前跨宿主四端点、BJ 正式镜像升级和其他功能证据继续保留，不能由本机结果替代。

生产 Service `A06AF7FE…`、Console `C783635B…`及上一批完整 Server／Node 候选未改；没有重新打包或上传。
Company Client `FE8B8257…`、XR Service `636C688E…`／Render `6F9A2D19…`身份沿用，本批没有远端命令或持久配置变更。
下一主线步骤仍是可用服务器窗口中的 Server-first 配套上线及安装后连接确认；公网 Console 在当前排除使用的 90，保持待办。
晚间短顿从既有 Company 467.233 秒／三次 151–165ms 证据接续，无自动启动时间；Android 排除。
