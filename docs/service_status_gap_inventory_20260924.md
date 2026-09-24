# 服务状态最小缺口盘点（2026-09-24）

本盘点以当前运行代码为准；`server_topology.html` 和运维计划中的示意字段不是状态来源。本批只修正已有受权管理页，不新建服务注册中心、跨主机探测器或启停执行器。

| 对象 | 当前权威状态来源 | 后台现状与实际缺口 | 本批处理 |
|---|---|---|---|
| Console | `/health/live`、`/health/ready`；就绪检查包含数据库 | 管理页面本身依赖 Console，不能靠页面自报健康 | 公网 90 的宿主计划任务独立检查 readiness，将故障/恢复写入本机 Windows Application 事件日志；其他部署仍使用各自宿主监控 |
| Relay | 受认证 Relay control 上报，`GET /api/console/managed/relays` 返回状态、代际、新鲜度、排空、容量、流量、最后上报和版本 | 库存页已有，但正常上报/断开未推送管理事件；过期后仍显示旧连接数、流量和实际排空值 | 上报/连接/断开后推送 `relays` 事件；过期数值显示未知，区分“已连接但未就绪” |
| Service 节点 | 受认证 node control 上报，`GET /api/console/managed/nodes` 返回状态、代际、新鲜度、最后上报、版本及采样时间 | 库存页已有，但“近期上报”被直接涂绿，未区分未就绪/排空；旧 CPU/GPU 值被当作当前值 | 连接/断开推送 `nodes` 事件；明确状态分类，30 秒外的遥测隐藏为未知，展示最后上报和版本 |
| Render | 节点管理通道报告的实例/命令状态，没有独立 Render 心跳 | 不能把 Service 在线等同 Render 正常，也不能把保留的 RDP 工作区当作运行中的 Render | 继续使用实例状态；没有独立来源时不新增 Render“健康”标签 |
| Backup | 每个部署本机 `status.json` 与 systemd/SCM 进程状态 | 无跨主机、受权且具新鲜度语义的 Console 状态源；不能直接读取另机本地文件 | 保留本机诊断；不展示猜测的后台健康值 |
| Desk | 独立 `/health/live`、`/health/ready` | 可选服务，尚无 Console 登记与受权的集中状态生产者 | 未部署按不适用处理，不以缺席报故障 |
| Auth | 官方独立 `/health/live`、`/health/ready` | 官方许可证签发域，不属于 Customer Console 管理对象 | 不在私有后台探测或操作官方 Auth |

边界：页面上的“新鲜”只表示所显示快照或样本足够近，不能替代业务调度的服务器端准入检查。浏览器与 Console 失联时，管理事件状态已标记为断开/过期；不能把最后一张缓存快照当作正在运行的进程证明。后续如要把 Backup、Desk 或独立 Render 纳入统一健康页，必须先定义其认证、部署归属、上报时间和失联语义，并按真实运维需求另行实施。

## 公网短测（2026-09-24）

- 将快速 Release Console 与同次构建的四个网页文件一起部署到公网 90；程序及网页文件 SHA-256 均在目标机校验，原程序与网页保留于 `D:\PixelsServer\backups\console-before-20260924135704`。
- `/health/ready` 返回 204，首页及新 JavaScript 资源返回 200；受权管理接口读到一台新鲜、就绪的云节点及两台新鲜、就绪的 Relay。
- SG Relay 在零房间、零连接时短暂排空，接口观测到 `reported_draining=true`，随后恢复并观测到 `ready`、`reported_draining=false`。未进行长时间运行或有用户会话时的切换测试。

## Console 宿主本地事件（2026-09-25）

- `scripts/install_public_console_health.ps1` 在公网 90 安装 `Pixels-Console-Health` 计划任务，以 SYSTEM 身份每分钟独立运行 `scripts/check_public_console_health.ps1`；它不依赖 Console 页面或进程。探针要求 `/health/ready` 精确返回 204，连续两次失败仅写一次 Application 错误事件 4101，恢复后仅写一次信息事件 4102。事件源为 `PixelsConsoleHealth`；状态保存在 `D:\PixelsServer\data\console-health\state.json`。
- 90 当前测试证书没有与公网 IP 匹配的 SAN，探针只信任本机配置的 `console.crt` 精确证书指纹并检查有效期，不使用无条件跳过证书校验。证书轮换后需重新运行一次健康检查；探针若持续失败，会按上述规则记录事件。
- 已验证计划任务真实返回 0、HTTP 204；隔离状态文件的短测得到 `none → failed → none → recovered`，对应事件 4101/4102 各一次。此处是本机事件记录，不是异机监控或短信/邮件/Webhook 送达；主机断电或网络整体失联不能由这台主机自己告警。用户明确选择暂不配置外发通知，因此不把本项算作 P7 的独立告警完整出口。
- 重装探针：在仓库根目录执行 `pwsh -NoProfile -File scripts/install_public_console_health.ps1`；在 90 上查看事件：`Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='PixelsConsoleHealth'}`。不需要启动 Console 管理网页。
