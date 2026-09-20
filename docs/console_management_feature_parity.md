# Console 管理功能守恒清单

> 建立日期：2026-09-18。历史实现取自提交 `519be7d85`；当前新架构基线取自提交 `5233bd3ae`。
> 本文是功能防丢门禁，不是“全部已完成”声明。
>
> 2026-09-19产品决定：ZLMediaKit直播、Coturn/TURN和经Relay中转的WebRTC信令明确退出本轮活动产品；Relay名称及既有数据转发
> 保持，WebRTC只保留Direct Host。跨端边界见[专项计划](direct_host_webrtc_scope_plan_20260919.md)。

## 1. 硬规则

1. 历史页面、DTO、WebSocket 或接口被删除，不等于其产品能力获准删除。每项能力必须归类为“已迁移”“安全替代”“待实现”或“明确退役”。
2. “待实现”必须写明目标归属、验收条件和阶段；在这些条件通过前，不得在进度报告中写成已迁移或功能等价。
3. “安全替代”必须保留用户目标，但不得恢复旧 Cookie/CSRF、`/api/v1`、浏览器可读服务密钥、设备密码直连或裸短期下载 ticket。
4. “明确退役”必须有新的用户产品决策。只有代码清理、页面改版或数据库重构不能构成退役依据。
5. 新实现只使用当前 PostgreSQL、显式主体、资源会话、节点 generation 和 Console 描述符模型，不实现旧协议兼容或数据迁移。
6. 完整 PostgreSQL 门禁必须检查下表全部稳定 ID。允许状态演进，不允许删除条目以隐藏缺口。

状态含义：

- **已迁移**：新 API、UI 和相应自动化证据均已存在。
- **部分迁移**：后端或 UI 已存在，但尚未完成同一用户目标的闭环。
- **安全替代**：旧行为本身不应恢复，用户目标由新安全模型承接。
- **待实现**：当前产品能力缺口，必须进入后续开发与验收。

## 2. 功能矩阵

| 稳定 ID | 历史用户能力与入口 | 当前状态 | 新归属 | 完成/验收条件 |
|---|---|---|---|---|
| CM-IDENTITY | 管理员登录；用户、用户组管理 | 已迁移 | Console 管理身份 API；`LoginView`、`UserManager`、`GroupManager` | PostgreSQL 管理会话、角色/CAS、最后管理员、改密/退出和中英文 UI 合同持续通过；真实浏览器已覆盖登录及用户/组创建，不得回退 Cookie/CSRF |
| CM-DASHBOARD | 资源总览、数量与近期状态 | 部分迁移 | `ResourcesView` + managed applications/nodes/deployments/sessions/recordings | 当前页面、登录后导航、进程重启数据保持及数据库失联 fail-closed/恢复已做真实浏览器验收；节点详情可查最近 100 条原始历史，也可读取由 PostgreSQL 数据库时钟对齐的有界服务端趋势，并显示最新样本年龄、陈旧状态及每项指标的已知/总样本覆盖率；未知值以断点呈现；独立管理事件流会刷新资源视图；仍需更完整的跨资源容量总览与公网高频验收 |
| CM-DEVICE | 设备目录、在线状态、访问授权、设备详情 | 部分迁移 | managed device API + `DevicesList` | 目录及创建一次性注册凭据已做真实浏览器验收，轮换、启停/删除和用户/组 ACL 已接；真实 WMI latest 与 7 天原始历史已落 PostgreSQL 并进入管理节点详情，NVIDIA NVML 的 GPU 负载/显存/编码器指标按唯一 PCI 身份接入，页面可见物理 GPU 的运行时绑定验证状态；AMD/Intel 指标及运维动作仍待闭环 |
| CM-ONLINE | 在线连接列表、访问主体和会话状态 | 已迁移 | managed resource sessions + `OnlineConnection` | 分页、筛选、主体隔离、敏感 descriptor 不返回、真实节点连接/断开更新及浏览器展示通过 |
| CM-CONNECTION | Service/Panel 连接、远程会话详情和会话事件 | 部分迁移 | managed visits/channels/transfers + `SecurityInternal` | 当前访问、通道、传输和录像历史已接，节点上报会实时刷新对应管理视图；仍需节点连接代际、命令/会话事件明细和业务数据陈旧时长 |
| CM-APPLICATION | 应用、节点、部署配置与调度状态 | 部分迁移 | managed application/node/deployment API + `AppsView` | 三种模式、显式 deployment target、CAS、节点 generation、容量/维护门禁持续通过；Game Hook/WebView 已增加版本化 GPU 预算、逐卡原子硬过滤、节点二次准入以及 Render 首帧到物理 GPU stable key 的核验，RDP 明确无 Render GPU profile；管理员只读调度预览已逐节点/逐 GPU 返回预计余量、稳定排名和结构化拒绝原因且不创建预约；仍需公网容量验收 |
| CM-RECORDING | 录像目录、直读/拉取、下载到本机或 Console、删除 | 部分迁移 | recording catalog + private cache/read-lease service + 管理/本人录像页 | Render 完成段、Service 登记/回传、Console 私有缓存、Range 下载、管理员与会话 owner 页面以及 Console 副本保留/释放/驱逐已接通；自动化覆盖 hash、并发、读取租约、撤销、CAS 和 pinned/在读拒绝。仍需真实公网录像与有数据浏览器下载/管理动作验收。旧 URL ticket 不恢复 |
| CM-WALL | 多设备视频墙、分页、自动重连、每格媒体统计 | 待实现（延期） | 多个显式 observer 资源会话 + 多条 Direct Host WebRTC | 不阻塞本轮DB0–DB5；以后恢复时每格独立descriptor/lease并直接连接对应Render，不恢复ZLM或中央媒体转发，容量按浏览器与Render编码槽明确限制 |
| CM-LIVE | 选择应用/节点/实例并通过ZLMediaKit观看直播流 | 明确退役 | `backup/`归档；不属于本轮活动产品 | 归档ZLM/RTMP/HLS/HTTP-FLV、Render live pusher、Console播放代理和短期播放ticket，并从构建/安装/路由/UI移除；录像及未来Direct Host observer不随之退役 |
| CM-EVENT | CPU、内存、磁盘、GPU 阈值事件查询与详情 | 部分迁移 | `TelemetryAlertStore` + `TelemetryAlerts` | 每节点策略、连续样本/回滞、去重、严重度升级、确认/恢复、180 天保留、分页筛选、详情 API、中英文页面和节点事件实时失效通知已接；仍需断库补报、可信真实 GPU 指标及真实公网节点验收 |
| CM-REALTIME | 管理端 WebSocket 实时刷新、心跳和重连 | 部分迁移 | `/api/console/managed/events` 独立只读管理事件流；节点控制仍为 `/api/console/node-control` | 已实现同源握手、首帧管理 bearer、admin/viewer 逐次重验、事件序号/游标、1024 项有界重放、进程流 ID/快照边界、15 秒心跳、5 秒写超时、固定间隔重连、撤权收敛和中英文陈旧状态；真实 Chromium 已验证外部写入自动刷新与 Console 重启重连。仍需公网高频反压及数据库中断期间页面陈旧/恢复专项；不得复用节点身份或旧 `/console/website` |
| CM-RTC | STUN/TURN 配置、连通性测试、Coturn 状态 | 明确退役 | `backup/`归档；Direct Host WebRTC不使用ICE服务器 | 归档Coturn制品、TURN secret/credential、端口池、状态API和页面；当前描述符不得下发`stun:`/`turn:`或Relay RTC signaling参数，直连失败不得回退 |
| CM-LICENSE | 机器码、许可证状态、拉取/授权入口 | 部分迁移 | `px_auth_server` + Console 许可证消费者 + 运维状态 | Auth 签发/撤销已在 PostgreSQL；仍需 Console/Service 消费、当前部署绑定、到期/撤销传播、离线策略和只读管理状态。旧浏览器 stub `AuthView` 不恢复 |
| CM-PROFILE | 当前管理员资料、角色、改密、退出 | 已迁移 | admin session API + `ProfileInfo`/`HeaderView` | 当前 bearer 精确绑定、密码 revision 撤销和退出幂等持续通过；真实浏览器已覆盖中英文、明暗主题、进程重启后的会话重验及退出撤销 |
| CM-TRANSFER | 文件传输历史、终态与失败原因 | 部分迁移 | managed transfer history + `SecurityInternal` + 本人活动页 | Render→Service→Console 的真实生产链、单调进度、实际文件/清单 SHA-256、哈希终态和 unknown 状态已接；开始传输仍由Console在线授权，已授权transfer的进度/终态先写Service LocalMachine DPAPI有界outbox再确认Render，并在通知、重连和周期任务中以同一sequence/正文补报，因此Render退出或Service重启不会丢失待报终态。Console本人活动页已消费owner-scoped历史，提供中英文方向、状态、进度、失败原因、筛选和分页，并明确不冒充可恢复任务队列。仍需公网双端独立字节校验、取消/重试、主机重启故障注入及Console历史对账/展示的真实浏览器验收 |
| CM-OBSERVER | 管理员观看但不能控制的权限语义 | 部分迁移 | application `allow_observer` + resource session `observer` | 存储层已区分observer/control且observer不能创建文件通道；本轮继续保留权限、审计和撤销语义，未来观看只能创建Direct Host observer session，不恢复ZLM直播或中央RTC signaling |

当前节点遥测已同时保存 latest 和 7 天原始历史：Windows Service 每次节点报告时重新采样，Console 以节点 generation 和报告 sequence
在同一事务原子替换 latest 并追加机器/GPU 历史。历史管理 API 使用接收时间、代际、序号完整游标，页面显示最近 100 条；独立清理任务
每分钟有界删除最多 5000 条过期机器样本并级联 GPU 行。WMI 当前能可靠提供 CPU、逻辑处理器、内存、固定磁盘和 GPU 身份/名称；
Windows Service 现以 WMI PNP 身份为稳定清单，并用 NVML PCI vendor/device/subsystem 只匹配唯一 NVIDIA 适配器；匹配成功才写入显存
总量/已用量、GPU 与编码器利用率。无 NVML、查询失败、多张同 PCI 身份无法唯一映射或非 NVIDIA 适配器仍保持 `null`，不会按名称猜测、
不会以 0 冒充空闲，也不能作为调度证据。阈值事件已基于已接受报告在
同一事务评估 CPU、内存、固定磁盘及非 NULL GPU 利用率：默认连续 3 次越线开立、50‰ 回滞、连续 3 次恢复，支持每节点 CAS 策略、
确认审计、稳定分页筛选和恢复后 180 天有界保留；缺失指标不会伪造恢复。管理实时事件流已用独立安全域连接节点上报和成功的管理
写操作；流内只发送类型化失效通知，不发送节点凭据或业务详情。进程重启必定更换 `stream_id` 并要求 HTTP 全量快照，不伪造跨进程
持久重放。管理节点详情已同时保留最近 100 条原始样本并接入服务端趋势聚合：窗口限制为 5 分钟至 7 天，桶宽限制为 30 至
3600 秒且最多 288 桶；聚合使用数据库时钟，空桶和未知指标保持缺口，并返回最新样本年龄、30 秒陈旧判断以及逐指标已知样本数。
Windows Service 已加入 DPAPI 有界断线样本队列，Console 以样本 UUID 幂等追加历史；补报不更新 latest、在线状态或告警，并按真实
采样时间进入趋势。真实公网 GPU 告警和高频反压/断库验收尚未完成，因此 CM-EVENT 与 CM-REALTIME 均保持“部分迁移”。

## 3. DTO 与页面迁移边界

- 旧 `entity/*.ts` 不是权威领域模型。当前已接能力使用 `managed_*_api.ts` 的类型化响应，并与 Rust runtime/storage DTO 对齐。
- DTO 文件删除时，其对应能力必须先映射到本表。若新 DTO 尚不存在，该能力仍保持“待实现/部分迁移”，不能以“后端以后再做”关闭事项。
- 当前 `OnlineConnection` 是资源会话视图，`SecurityInternal` 是访问/通道/传输/录像审计视图；它们只替代已实际展示的数据，不自动等价于旧连接监控、硬件事件或媒体观看页面。
- 节点控制 WebSocket 与管理员实时事件流是两个安全域。前者认证节点 generation；后者只接受同源浏览器，在升级后首帧认证
  `admin_web` bearer，并在事件/心跳时重新核验当前管理员或只读管理员权限。两者不共用身份、消息或路径。

## 4. 后续实施顺序

1. **DB2 管理闭环**：CM-DASHBOARD、CM-DEVICE、CM-CONNECTION、CM-REALTIME、CM-EVENT。
2. **DB2 媒体闭环**：Direct Host WebRTC、CM-RECORDING及CM-OBSERVER权限闭环；CM-WALL延期，CM-LIVE不再实施。
3. **部署私密配置**：CM-LICENSE；CM-RTC不再实施。
4. **DB5 产品验收**：CM-TRANSFER 真实文件链，以及上述能力在公网 Windows 节点和 Android 终端的完整回归。

每一项状态变为“已迁移”前，至少需要：类型化服务合同测试、PostgreSQL 权限/事务/断库测试、前端合同测试、真实浏览器流程，以及涉及节点或媒体时的真实公网节点功能验收。仅有页面、mock 或 repository 单元测试不能关闭该项。

## 5. 历史取证

需要核对被替换实现时，使用提交 `519be7d85` 中的 `web/px_console/src`：

- `router/index.ts` 与 `views/AsideView.vue`：历史页面入口全集；
- `model/record_api.ts`、`views/DeviceRecords.vue`：录像目录、ticket、拉取、下载和删除行为；
- `views/VideoWall.vue`、`model/wall_rtc.ts`：视频墙和 RTC 统计；
- `views/LiveViewer.vue`、`model/live_api.ts`：直播选择与播放；
- `model/rtc_api.ts`、`views/RtcTurnSettings.vue`：仅作为已退役RTC/TURN能力的历史取证，不恢复；
- `stores/ws.ts`、`ConnectionMonitor.vue`、`EventView.vue`：管理实时刷新、连接详情和硬件事件。

历史代码只用于行为核对，不是兼容层，也不得直接恢复旧端点或旧凭据模型。
