# Console 管理功能守恒清单

> 建立日期：2026-09-18。历史实现取自提交 `519be7d85`；当前新架构基线取自提交 `5233bd3ae`。
> 本文是功能防丢门禁，不是“全部已完成”声明。

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
| CM-DASHBOARD | 资源总览、数量与近期状态 | 部分迁移 | `ResourcesView` + managed applications/nodes/deployments/sessions/recordings | 当前页面、登录后导航、进程重启数据保持及数据库失联 fail-closed/恢复已做真实浏览器验收；节点列表已展示 Service 最新 CPU、内存、固定磁盘与 GPU 库存快照及采样时间，但仍需历史趋势、告警、陈旧时长和实时推送 |
| CM-DEVICE | 设备目录、在线状态、访问授权、设备详情 | 部分迁移 | managed device API + `DevicesList` | 目录及创建一次性注册凭据已做真实浏览器验收，轮换、启停/删除和用户/组 ACL 已接；节点侧真实 WMI 最新快照已落 PostgreSQL 并进入管理节点详情，GPU 负载/显存/编码器指标、历史详情和运维动作仍待闭环 |
| CM-ONLINE | 在线连接列表、访问主体和会话状态 | 已迁移 | managed resource sessions + `OnlineConnection` | 分页、筛选、主体隔离、敏感 descriptor 不返回、真实节点连接/断开更新及浏览器展示通过 |
| CM-CONNECTION | Service/Panel 连接、远程会话详情和会话事件 | 部分迁移 | managed visits/channels/transfers + `SecurityInternal` | 当前访问、通道、传输和录像历史已接；仍需节点连接代际、命令/会话事件明细、实时刷新和断线陈旧标识 |
| CM-APPLICATION | 应用、节点、部署配置与调度状态 | 已迁移 | managed application/node/deployment API + `AppsView` | 三种模式、显式 deployment target、CAS、节点 generation、容量/维护门禁和部署准备回归持续通过；管理员页面不冒充终端用户启动入口 |
| CM-RECORDING | 录像目录、直读/拉取、下载到本机或 Console、删除 | 待实现 | recording catalog + private cache/read-lease service + 新管理/本人录像页 | 已有不可变录像元数据、缓存预留和非 bearer 读取租约存储模型；还需真实生产者、授权字节流、拉取/下载、保留/删除 API 与 UI、断点/哈希/并发/撤销测试。旧 URL ticket 不恢复 |
| CM-WALL | 多设备视频墙、分页、自动重连、每格媒体统计 | 待实现 | 显式 observer 资源会话 + Web Client 多画面编排 | 每个格子独立 observer session/descriptor/租约；权限撤销、容量、分页切换、弱网重连、统计和全部关闭通过，不使用 `admin_web` 直接媒体准入 |
| CM-LIVE | 选择应用/节点/实例并观看直播流 | 待实现 | observer 资源会话 + 受授权媒体通道 | 明确与视频墙共用或独立的观察者模型；选择运行实例、建流、延迟控制、结束/撤销和并发容量测试通过，不恢复旧 `/api/v1/live` |
| CM-EVENT | CPU、内存、磁盘、GPU 阈值事件查询与详情 | 待实现 | 节点遥测/告警 repository + 运维事件页 | 定义采样者、阈值、去重窗口、严重度、确认/恢复状态和保留策略；四类事件从真实节点上报到 PostgreSQL 并完成分页、筛选、断库补报与浏览器验收 |
| CM-REALTIME | 管理端 WebSocket 实时刷新、心跳和重连 | 待实现 | 独立只读管理事件流；节点控制仍为 `/api/console/node-control` | 管理 bearer 认证、事件序号/游标、重放边界、心跳、反压、重连、授权撤销、断库 fail-closed 和陈旧状态提示通过；不得复用节点控制身份或旧 `/console/website` |
| CM-RTC | STUN/TURN 配置、连通性测试、Coturn 状态 | 待实现 | 服务端私有部署配置 + 运维健康页 | 密钥只在服务端密钥存储；浏览器仅写入受控引用或脱敏配置并读取健康/探测结果；轮换、审计、权限、失败回滚和实际 ICE 探测通过 |
| CM-LICENSE | 机器码、许可证状态、拉取/授权入口 | 部分迁移 | `px_auth_server` + Console 许可证消费者 + 运维状态 | Auth 签发/撤销已在 PostgreSQL；仍需 Console/Service 消费、当前部署绑定、到期/撤销传播、离线策略和只读管理状态。旧浏览器 stub `AuthView` 不恢复 |
| CM-PROFILE | 当前管理员资料、角色、改密、退出 | 已迁移 | admin session API + `ProfileInfo`/`HeaderView` | 当前 bearer 精确绑定、密码 revision 撤销和退出幂等持续通过；真实浏览器已覆盖中英文、明暗主题、进程重启后的会话重验及退出撤销 |
| CM-TRANSFER | 文件传输历史、终态与失败原因 | 部分迁移 | managed transfer history + `SecurityInternal` | 元数据、单调进度、哈希终态和 unknown 状态已接；真实文件生产/消费、取消、重试、字节校验和公网客户端展示仍需 DB5 验收 |
| CM-OBSERVER | 管理员观看但不能控制的权限语义 | 部分迁移 | application `allow_observer` + resource session `observer` | 存储层已区分 observer/control 且 observer 不能创建文件通道；仍需统一的视频墙/直播申请 UI、媒体能力约束、审计、撤销和端到端验证 |

当前节点遥测切片只代表“最新观测值”：Windows Service 每次节点报告时重新采样，Console 以节点 generation 和报告 sequence
原子替换机器快照及同一 inventory revision 的逐 GPU 清单。WMI 当前能可靠提供 CPU、逻辑处理器、内存、固定磁盘和 GPU 身份/名称；
无法可靠得到的逐 GPU 利用率、显存和编码器压力保持 `null`，不会以 0 冒充空闲，也不能作为调度证据。尚未建立时间序列、阈值事件、
断线补报或管理实时事件流，因此 CM-EVENT、CM-REALTIME 仍保持“待实现”。

## 3. DTO 与页面迁移边界

- 旧 `entity/*.ts` 不是权威领域模型。当前已接能力使用 `managed_*_api.ts` 的类型化响应，并与 Rust runtime/storage DTO 对齐。
- DTO 文件删除时，其对应能力必须先映射到本表。若新 DTO 尚不存在，该能力仍保持“待实现/部分迁移”，不能以“后端以后再做”关闭事项。
- 当前 `OnlineConnection` 是资源会话视图，`SecurityInternal` 是访问/通道/传输/录像审计视图；它们只替代已实际展示的数据，不自动等价于旧连接监控、硬件事件或媒体观看页面。
- 节点控制 WebSocket 与管理员实时事件流是两个安全域。前者已实现并认证节点 generation；后者尚未实现。

## 4. 后续实施顺序

1. **DB2 管理闭环**：CM-DASHBOARD、CM-DEVICE、CM-CONNECTION、CM-REALTIME、CM-EVENT。
2. **DB2 媒体闭环**：CM-RECORDING、CM-OBSERVER、CM-WALL、CM-LIVE。
3. **部署私密配置**：CM-RTC、CM-LICENSE。
4. **DB5 产品验收**：CM-TRANSFER 真实文件链，以及上述能力在公网 Windows 节点和 Android 终端的完整回归。

每一项状态变为“已迁移”前，至少需要：类型化服务合同测试、PostgreSQL 权限/事务/断库测试、前端合同测试、真实浏览器流程，以及涉及节点或媒体时的真实公网节点功能验收。仅有页面、mock 或 repository 单元测试不能关闭该项。

## 5. 历史取证

需要核对被替换实现时，使用提交 `519be7d85` 中的 `web/px_console/src`：

- `router/index.ts` 与 `views/AsideView.vue`：历史页面入口全集；
- `model/record_api.ts`、`views/DeviceRecords.vue`：录像目录、ticket、拉取、下载和删除行为；
- `views/VideoWall.vue`、`model/wall_rtc.ts`：视频墙和 RTC 统计；
- `views/LiveViewer.vue`、`model/live_api.ts`：直播选择与播放；
- `model/rtc_api.ts`、`views/RtcTurnSettings.vue`：RTC/TURN 配置与健康；
- `stores/ws.ts`、`ConnectionMonitor.vue`、`EventView.vue`：管理实时刷新、连接详情和硬件事件。

历史代码只用于行为核对，不是兼容层，也不得直接恢复旧端点或旧凭据模型。
