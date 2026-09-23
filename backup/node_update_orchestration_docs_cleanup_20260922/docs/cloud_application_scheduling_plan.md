# 云应用业务管理与多 GPU 调度设计

> 2026-09-19 · P3 资源预算、PostgreSQL 原子硬过滤、基于物理 GPU 稳定身份的实际 Render 绑定，以及只读调度预览/逐候选拒绝解释均已实施。网页拓扑仍为离线模型。
> 关联：[服务架构](server_refactoring_plan.md)、[运维后台](service_operations_console_plan.md)、[网页模型](server_topology.html#business)。

前置依赖：先完成 [PostgreSQL 数据库阶段 DB0–DB5](postgresql_database_migration_plan.md)，统一持久事务、任务/outbox 和幂等基线。
本专项之后新增逐 GPU 资源向量、排序和预约模型；不先在 MongoDB 上实现再迁移，也不要求 DB 阶段提前实现全部 GPU 调度算法。

## 1. 范围与现状

用户选择应用后，平台自动选择合适且较空闲的 Render 承载机器，不要求用户了解机器、GPU 或端口。
“较空闲”指满足该应用需求后的资源余量，不是在线用户最少、CPU 最低或最后运行时间最早。
管理员可以限制候选机器池或指定机器/GPU，但不能绕过权限、健康、容量、维护或工作区归属门禁。

当前 `rust_server/px_console_server/src/app_schedule/manager.rs` 的 `start_app` 路径按应用节点候选与
`last_run_at/seq_no` 排序；已有 GPU 事件不代表具备逐卡容量预约或 GPU 绑定验证。
本设计替代未来调度中的固定流路选取算法，不恢复旧协议、旧数据导入或固定端口兼容路径。
历史 `console_app_nodes_plan.md` 只记录已有实现，其固定端口和旧迁移说明不作为新方案依据。

## 2. 控制层与对象

| 层级 | 职责 | 禁止越界 |
|---|---|---|
| Console / Cloud Runtime | 应用目录、ACL、资源规格、版本发布、候选过滤、排序、预约、生命周期与审计 | 不直接执行任意 shell，不负责媒体转发 |
| 节点 px_service | 本地资源与 GPU 发现、维护门禁、最终准入、端口分配、启动与精确监督、实际落点确认 | 不收养 Job 外进程、不根据端口识别替代进程 |
| Render / 模式运行时 | Game Hook、CEF WebView、RDP 代理/传输；报告实际能力和运行状态 | 不自行更换未授权目标、不决定全局业务配额 |
| Broker / Relay | 连接授权附着、路径建立和中转 | 不选游戏机器，不管理应用目录或销毁工作区 |

业务分类与运行模式分开存储：`business_kind=game/application`，`runtime_mode=game_hook/webview/rdp`。
只有经过验证的组合才可发布；不是任意分类都支持任意模式。Game Hook/WebView 不增加 Windows 用户/RDP 登录前提。
RDP 保留原生编码，客户端解码；不能按普通 Render 二次编码流水线计算编码槽位。

| 对象 | 含义与主要字段 |
|---|---|
| Application | 用户可见目录、分类、ACL、发布状态、允许的规格 |
| ApplicationRevision | 不可变运行模式、启动配置/URL、资源 profile、数据和生命周期策略 |
| ApplicationDeployment | 指定 revision 在某台机器上的安装/缓存就绪状态、路径、可验证模式与 GPU 组合 |
| MachineNode | 稳定 node_id、运行代际、维护状态、机器级资源与遥测时间 |
| MachinePool | deployment_id、pool_id、用途、硬件/模式约束、区域/故障域标签、容量保底与维护策略；成员关系有 revision，不替代应用 ACL |
| GpuDevice | node_id 下稳定 gpu_id、当前枚举代际、运行期适配器映射、能力及单卡预算 |
| PlacementReservation | request_id、node/gpu/deployment、资源向量、调度 epoch、租约及执行状态 |
| AppInstance | 预约关联、实际运行身份、请求/实际 GPU、资源消耗、端点及状态 |
| Workspace | RDP 持久工作区、所属 Windows 账号、固定 owner 机器、Windows Session 身份与 busy 状态；(application,node) 唯一，不按访问者创建账号 |
| CloudApplicationSession | deployment_id、session_id、owner_user_id、initiator_identity、client_type、target_kind、app_id、instance_id、实例代际、类型化 connection descriptor；关联 Grant/预约，不以设备账号代替业务目标 |

### 2.1 Android 云应用身份与会话目标

Android 使用独立顶层“云应用”入口，并以 `client_type=android` 认证，不冒充 Panel。
`owner_user_id` 是所属部署内的业务用户，来自服务端认证上下文；Workspace 的 owner 机器另用 `owner_node_id`，不得混用。
业务 owner 绑定本次实例/会话占用，不改变 RDP 持久工作区 `(application,node)` 的唯一键；公开应用的 guest 也有独立主体/占用，
不能因匿名访问或访问者变化另建 Windows 账号。准入与秘密交付沿用 [RDP §0.0](rdp_application_mode_design.md)。
启动请求绑定 deployment、app_id、认证主体和 request_id；预约/实例/Session 的 owner 与应用关联由服务端连续校验，
不能信任客户端提交的其他用户 ID。恢复只能返回同一部署、用户有权访问且未终止的实例。

返回客户端的业务目标必须为 `target_kind=cloud_application`，包含 app_id、instance_id、display_name 和经认证的类型化连接描述；
连接描述包含目标实例身份/代际、协议与当前权威端点，不能通过 device/account ID 推测端点。
Android 对应现有 `RemoteSessionTarget.CloudApplication(appId, instanceId, connection, clientNonce)`；
`clientNonce` 只关联一次客户端发起过程，不是 owner、资源 ID 或授权证明。现有 connection 承载类型的名称不改变 CloudApplication 目标语义。
字段缺失、部署/实例归属不符、描述过期均显式失败，不回退 Device/Account target、不填充替代 ID、不恢复旧设备嵌套页面。
上述字段进入持久会话、审计、Grant 与接口样例；DB 阶段保留当前 Android 行为，后续契约扩展不能退化它。

历史 AppNode 的“应用流路”不再与物理 MachineNode 混称“节点”；新后台使用“应用部署”“机器”“GPU”“运行实例”。
多 GPU 是逐卡候选，不将整机平均 GPU 利用率用作一张卡的可用容量。

## 3. 资源需求与供给

应用规格绑定分辨率、帧率、编码格式、运行模式与经验证的硬件等级。首次无实测时使用保守的管理员预算；
之后依据受控压测及历史分位更新版本化 profile，不能仅凭当前一帧低负载自动提高并发。

| 层级 | 需要采集/记账的资源 |
|---|---|
| 机器 | CPU 容量/压力、可分配内存、磁盘空间与 I/O、网络出口预算、启动并发、实例配额、端口、温度/降频/设备错误 |
| 每张 GPU | 型号/驱动、显存总量及安全余量、图形/计算压力、编码能力/吞吐预算、受支持编码会话数、独占/共享策略 |
| 应用部署 | revision/资源已就绪、模式能力、用户环境、路径或 URL 策略、可用 GPU 绑定方案、数据可达性 |
| 使用与预约 | 活动实例、启动中、已预约未启动、断线保留、预热实例、保留工作区实际占用、管理/桌面保底资源 |

“一卡一游戏”可以作为 CloudGame 默认独占策略，不是硬编码硬件上限；较轻云应用可配置共享上限，仍受所有资源维度限制。
编码器数量/吞吐取自能力探测及验证配置，不写死显卡厂商的通用并发数字；编码会话空闲也不代表显存或 3D 有余量。

当前实现进度（2026-09-19）：Windows Service 已对 NVIDIA 使用 NVML，并且只有 PCI vendor/device/subsystem 与 WMI 清单双向唯一时才填充
显存、GPU 和编码器利用率；歧义或不可用保持未知。Service 还会用 D3DKMT 将当前逻辑适配器反查到物理 PnP 身份；只有至少一个当前适配器能映射到
同一稳定 key 时才上报 `runtime_binding_ready=true`。该数据已进入 Console latest、7 天历史、页面、阈值事件和 P3 首批原子硬过滤。
迁移 0027 要求 Game Hook/WebView 部署显式配置单实例显存、GPU/编码器预算、安全余量和最大压力；预约把最终 GPU 身份、库存代际及预算
固化到实例/节点命令，并使用 `max(measured, committed-running)+pending+request` 逐维拒绝超售。RDP 原生链路明确没有该 GPU 编码 profile。
节点执行前会重新采样并校验代际、稳定身份、运行时映射能力、指标和余量。预约可在未固定 GPU 的多卡节点上原子选择具体物理卡，并将 stable key、
inventory revision 和预算固化到 Start 命令；数据库和网络协议不保存易变的 DXGI LUID。WebView 在本进程枚举 DXGI 适配器，通过 D3DKMT 将每个
LUID 反查为物理 PnP key，只允许匹配所选 stable key 的适配器打开 CEF 共享纹理；Game Hook 把首个捕获帧的实际 adapter UID 用同一路径反查后核验。
两种模式都必须向 Service 回报首帧 Ready，落点不符、映射不可用或超时均启动失败，不会静默落到其他卡。一张物理 GPU 即使暴露多个逻辑适配器，
它们也归并到同一物理 stable key，不会被误报成多张可独立预约的卡。
因此逐卡数据库记账和多 GPU 实际绑定验证已经落地。管理员可从应用页调用只读预览，按当前库存、观测、预约和部署状态查看每个
节点/GPU 候选的预计余量、稳定排序及结构化拒绝原因；预览不创建预约，也不替代正式预约事务的二次校验。AMD/Intel provider 仍是后续出口，
不能用节点总实例数替代。
CPU 百分比只在同一容量基准上比较，跨型号通过 profile/基准归一化；不同型号的 30% GPU 利用率不能直接排序。
显存不能跨卡相加来满足单卡需求。跨卡采集/编码会消耗拷贝带宽，首版默认同卡，只允许显式验证的跨卡组合并记账。
缺失关键能力/指标时显示 Unknown 并禁止自动新准入；显式静态保守预算模式须单独验证，不以缺失数据默认 0。

### 3.1 避免双重计数与超售

先扣除系统/桌面保底和安全余量，得到 allocatable；每个维度有唯一单位、来源、采样窗口和 owner。
当测量值包含全部运行实例而台账只包含它们的预算时，示意：

```text
effective_used = max(measured_running_total, committed_running_budget) + pending_not_in_measurement
headroom = allocatable - effective_used
```

新需求必须逐维满足，不能把某一维超限隐藏在平均分里。启动进程进入测量后，pending 转 committed 需要关联实例/代际并对账，
不能既重复相加又误释放。不能可靠归因的过渡期宁可保守重复占用，不多卖资源。
独占卡、端口、启动令牌等离散资源使用显式互斥/计数台账，不套百分比公式。
GPU 瞬时利用率仅作压力与排序辅助，不是隔离保障；共享配置必须说明没有硬件隔离，超出 profile 时阻止新准入并告警。

### 3.2 调度状态从哪里来

节点安装包内的 `px_service` 常驻，通过认证管理长连接直接连接所属 Console，不经 Broker 转交机器管理命令。
Service 采集整机/逐 GPU 指标并监督实例；Render 通过受认证本机 IPC 报告实际 GPU、Ready、用户与断线宽限等事实。
没有 Render 或 Panel 关闭时仍持续上报。Broker 的 Endpoint 在线只说明信令连接，不能代替资源新鲜度或管理可达性。

Console 用“资源快照 + 自身持久预约台账”作候选判断，关键资源/生命周期事件立即推送，普通指标周期汇总；
既不等负载上涨才扣预约，也不因心跳正常就认为每个实例数据新鲜。节点执行前仍二次准入，因此长连接不是原子资源锁。
管理断线/关键数据过期暂停新调度，保持未知占用；重连先认证、完整快照与增量对账、确认预约及任务结果，
再恢复满足其他门禁的准入。维护中的节点不会因重连被自动开放，管理失联不会自动迁移已有实例或注销 RDP 工作区。
完整报告字段、可靠结果传递、控制 owner 与恢复流程见 [运维后台计划第 6 节](service_operations_console_plan.md#6-状态指标和事件如何进入后台)。

## 4. 调度流水线

1. **鉴权与恢复优先**：校验应用、用户、配额、请求幂等性；可恢复实例回到原实例；已有 RDP Workspace 固定 owner 机器。
2. **构造候选**：按应用 revision 找 Ready 部署，再展开机器内已验证的 GPU/模式组合。没有安装应用的空机器不是候选。
3. **硬过滤**：平台归属、池/区域、健康、新鲜度、维护、能力、版本、数据、端到端网络约束、每维资源及独占策略。
4. **排序**：首版平衡策略优先最小化“放入本请求后的最大归一化资源压力”，再比压力均值、经验证的路径延迟、启动成本；
   最后用稳定 ID 保证确定性。近似同分才使用长期轮转，不能因随机性把明显更忙机器排前面。
5. **原子预约**：一次事务/CAS 同时占用机器预算、选定 GPU、应用并发与启动令牌；失败重读后有界重选。
6. **节点准入**：携带 reservation_id、调度 epoch、node incarnation、GPU inventory revision、profile revision；
   节点再检查健康、维护、实时余量和身份，本地接受后分配实际端口并报告，不让两个进程抢同一个槽。
7. **启动与验证**：启动受监督运行时，确认实际图形/采集/编码 GPU、应用身份和 Ready；正确后提交运行占用，生成连接描述。
8. **失败与回收**：区分确定失败、执行结果未知、已运行。只在确认未运行或精确停止完成后释放，并记录每个候选拒绝原因。

首版建议采用确定性的主导压力排序，而非一个缺乏依据的“AI 分数”。后台展示分项、排名、策略版本与决策时间。
资源向量只是估算与准入预算，不承诺 OS 强制隔离或绝对 FPS；运行中持续观测性能目标，超载时限制新请求并通知。
界面模型使用同规格虚拟 GPU 和虚拟可分配预算，显示“预计峰值压力”；不把模型分数称为真实生产算法验收结果。
用户时延有明确上限时是硬约束，未知不能当 0ms；无上限时作为同等资源候选的择优项，未采集单独标注。

### 4.1 单机多显卡的实际绑定

gpu_id 不能是易变化的 GPU 0/1 序号；当前 Windows 实现把规范化物理 PnP key 的 SHA-256 作为 stable key，并使用当前 inventory revision 映射到
运行期适配器。Windows LUID 只在 Service/Render 进程内作为当前枚举代际的跨 API 对应，不进入持久模型或启动协议；设备重启/驱动变化后重新枚举。
分别报告 requested/actual 的应用图形卡、采集卡和编码卡。选择 Render 编码卡不等于外部游戏或 CEF 自动跟随。

- Game Hook：按应用支持的设备选择机制与采集观测验证；保持本次私有 Job AND 规范完整路径的所有权限制。
- WebView：确认实际 CEF GPU 进程/图形设备；共享浏览器 GPU 进程产生的共享资源不得被算作多个独立独占 GPU。
- RDP：记录会话/OS 图形能力与 owner 机器，不能宣称可以任意将既有 Windows 会话固定到某张显卡。
- 无法验证绑定的应用仅放到经过验证的单 GPU/整机容量池；后台显示限制，拒绝假定“多卡等于多份隔离容量”。

GPU 丢失、设备重置或实际落点不符时停止发布 Ready，精确回收本次可回收 runtime；RDP 不注销已有工作区。
重新调度不能静默丢用户数据；活跃游戏不热迁移，已有 RDP Workspace 不因别的机器更空而迁走。

### 4.2 并发、失联与维护

DB0–DB5 至 P4 仅一个活动 Console；P5 才引入升级用新旧双实例，同一节点/调度作用域始终只有一个有效命令 owner，
不承诺任意多活横向扩容。阶段与 Windows 双槽切换见部署计划第 6 节和第 7.6 节。
事务保护持久资源竞争，owner/命令 generation fencing 保护提交后的网络副作用，两者不是二选一。
即使单实例也必须拒绝重启前的旧命令；进程内 mutex 或 PG 行锁不能阻止晚到的旧 Start 在节点执行。
该持久权威使用 PostgreSQL 短事务、明确锁顺序、条件更新与约束；预约/实例/命令 outbox 同事务提交，网络下发在提交之后。
请求超时不等于未启动；租约到期先转 ReconcileRequired，隔离未知占用，不直接另起一份实例。
节点拒绝过期/旧 epoch 命令；旧 Ready 回执不能覆盖新的实例；停止、撤销、重复 Start 均需幂等和代际校验。
节点不可达先保持未知占用。确认 fencing/终止后才能回收、重新放置；不能从云端看到连接数为零推断空闲。
排空和 Start 共用节点准入互锁：排空开始后拒绝新预约/启动，已接受请求按明确维护策略完成或取消并对账；
既有用户重连按原宽限准入，不重新消耗第二份实例预算。升级门禁同时检查预约、启动、用户、保留和任务。

无容量返回可解释的拒绝原因，或进入有最大长度、超时、取消和用户公平配额的队列；默认不无限等待，不强占已有用户。
重新计算有退避/次数限制，避免全部请求同时冲向同一张低负载卡；硬门禁始终优先于管理员指定落点。

### 4.3 首版多机资源池、应用准备与缩容

多 Render 和多 Relay 是首版能力，不等待 Kubernetes、云厂商自动采购或 Console 多活；具体部署分工见
[部署计划第 1.2 节](server_deployment_and_upgrade_plan.md#12-多机资源池与扩缩容阶段)。本节选机指 Render 主机，Relay 由 Broker 独立分配。

1. 管理员准备主机并授权加入本部署，Service 完成身份登记、能力发现和完整状态对账；重复接入使用稳定身份/代际，
   克隆镜像不得复用另一台机器的私钥或 node ID。初始状态不可调度，不能“心跳到了就开放”。
2. 机器加入受限资源池，按应用 revision 准备启动文件/资源、驱动和模式依赖；发行软件验证 Pixels 签名，
   客户自有应用遵循其部署管理员批准的来源与摘要策略，不要求把第三方应用伪装成 Pixels 签名包。
   由受限执行模块执行类型化部署任务，不接受页面传入任意 shell/路径，不改动活动实例使用的资源。
3. 应用部署逐项验证完整性、可运行模式、requested/actual GPU 绑定和所需数据可达性，通过后才发布 Ready。
   同一机器可以对应用 A 就绪、对应用 B 未就绪；池里机器更多不代表 B 的容量增加。
4. 扩容仅增加新请求候选，既有 Game Hook 不自动热迁移，RDP Workspace 仍固定 owner；
   规格变化和移池不能静默驱逐活动用户。监控趋势用于建议准备容量，实时调度使用管理快照 + PG 预约 + 节点最终准入。
5. 应用分发/节点升级先选小批验证，再按池/故障域分批，限制同时下载/启动/升级数量并核算剩余有效容量；
   失败或容量不足暂停后续批次，不能把未安装相应应用的空主机当替代容量。
6. 缩容/退役先关闭新预约/Start，与已有 Start 共用互锁；等待用户、启动中、未决预约、重连宽限、传输和保护实例完成对账。
   RDP 保留工作区、本机存档或未知占用会阻止自动销毁；首版不提供通用工作区跨机迁移，停运行时不等于数据已搬走。
   撤销节点身份在任务收尾/审计完成后进行；主机及数据删除须另有明确授权，不因资源池目标数量降低而执行。

首版人工准备主机、自动登记/验收接入；后续接云厂商 API 时将费用上限、数量上限、冷却时间、预热耗时和失败资产对账纳入扩容任务。
压力测试覆盖池规模、异构 GPU、遥测和关键事件并发、排队/拒绝率、调度延迟及 Console 重启恢复；公布实测上限，不写无限节点承诺。

## 5. 后台与用户页面

| 页面 | 信息与操作 |
|---|---|
| 应用目录 | 分类/模式、图标、ACL、发布状态、默认规格；创建不可变配置版本并发布 |
| 部署与规格 | 机器池、各机器安装版本、资源需求、绑定验证结果、独占/共享、数据/存档归属、停止策略 |
| 调度预览 | `AppsView` 选择应用后调用 `/api/console/managed/scheduling/preview`，逐节点/逐 GPU 展示预计余量、排名及结构化硬过滤原因；接口只读且仅限管理员，预览不保证后续预约成功 |
| 机器与 GPU | 单机多卡展开、当前/预约/保底资源、压力趋势、代际、实际绑定、维护与故障 |
| 资源池与批次 | 机器接入、按应用就绪容量、下载/验证、先行批次、最低剩余容量、失败暂停、安全退役及数据阻塞原因 |
| 实例/工作区 | Application→Deployment→Machine/GPU→Instance→Session 关联；RDP 工作区保留/busy 独立展示 |
| 调度记录/队列 | request/decision/reservation ID、策略版本、候选拒绝原因、租约/回执、重试、等待与取消 |

客户端默认只选应用和公开规格，展示“等待资源/正在启动/资源不足”与重试建议；内部机器地址、用户名和 GPU 管理信息不公开。
管理页面的手动指定是约束，不是特权绕过；后台修改 profile/独占策略默认新实例生效，不能偷偷缩小运行实例已获预算。
应用数据位置也是约束：存档只在本机且无已验证同步时保留机器亲和；首版不承诺运行态/存档跨机自动迁移。
RDP owner 不可达应显示无法恢复，不在空机器复制同名用户冒充原工作区。

## 6. 交付与验收

1. 冻结对象/指标单位/规格及状态机，建立只读机器+GPU 库存、应用部署与调度解释页。
2. 实现逐模式设备发现/实际绑定验证、节点准入与资源账本；先验证单机多卡的真实隔离边界。
3. 实现 Console Filter/Score/Reserve、持久任务、幂等/fencing 和节点对账，接入已有启动链路。
4. 接入多机资源池、应用分发、分批维护/安全退役、队列/公平性、客户端错误状态和告警；在 P7 前完成首版容量压测。

必测：

- 一台双卡机器一忙一闲，选择空闲且满足规格的卡；显存不足不能被其他卡余量抵消。
- GPU 很闲但 CPU/内存/出口/启动令牌不足，拒绝该机器；无应用部署、错误编码能力或未知绑定同样拒绝。
- 独占一张卡只接受一个运行/预约；共享按 profile 多维预算准入，非简单计人数。
- 单活动 Console 内并发请求最后一个槽，只能一个成功；双进程测试用来验证存储竞争，P5 另验唯一 owner 的切换，非多活承诺。
- Android 上报 android 身份，创建/恢复 CloudApplication 目标；缺少 app/instance/descriptor、跨部署/用户及伪造 owner 均拒绝，无 device/account 兜底。
- pending 与实测重叠、不重叠、指标乱序、进程重启、驱动重排均正确记账；失联不释放未知占用。
- Service 管理断线但 Broker 在线、Render IPC 断线但 Service 在线均停止对应不安全准入；重连完成对账前不恢复调度。
- Start 成功回执丢失、租约到期、控制面重启、重复/旧命令不产生双实例或错误清理。
- 预约与排空竞态、无容量、有界队列取消、公平性、反复失败退避、手动指定门禁全部通过。
- Game Hook/CEF 的 requested/actual GPU 对应实测正确；不能验证的组合限制到已验证池。
- RDP 重连固定原工作区、busy 拒绝第二前端、普通停止不注销会话；活动游戏不迁移。
- 模型的中英文/主题/窄屏、候选变化、未知/维护拒绝、重复预约和工作区亲和可交互验证。
- 多机入池先验证身份/应用/实际 GPU，克隆身份拒绝；应用 B 未就绪的节点不增加 B 的可调度容量。
- 分批升级与新预约竞态、失败暂停、容量保底、数据/工作区阻塞退役；新机器不触发既有游戏或 RDP 热迁移。
- 监控中断而管理链路健康时业务不被误停；监控显示空闲但管理数据过期/有预约时不得绕过硬门禁。

网页模型回归：在已安装 Console 开发依赖的工作区运行 `node docs/tests/server_topology.test.cjs`。
默认使用本机 Chrome，其他路径可通过 `PIXELS_TEST_CHROME` 指定。测试离线页面，不代表真实节点/调度后端验收。

### 6.1 2026-09-19 P3 实现证据

- SQLx 元数据 `pg-20260919-085203-70161370` 从三套空库重新生成，共 268 份 Console 查询元数据。实例预约专项
  `pg-20260919-085304-9f18dcd4` 为 14/14 PASS，覆盖固定 GPU 的实测与 pending 预算、未知编码器指标拒绝、物理 stable key 选卡及既有门禁。
- 命令状态机 `pg-20260919-085514-84c609b3` 为 16/16 PASS；节点/部署分别为 `pg-20260919-085416-a29e121f` 9/9、
  `pg-20260919-085618-4365a91b` 6/6；真实节点 WebSocket `pg-20260919-085715-d53b40c1` 为 1/1 PASS；目录/主体 API
  `pg-20260919-090245-02ab41c7` 为 7/7 PASS。
- Windows Service 全目标（含显式硬件用例）为 90/90 PASS；真实 RTX 3060 验证了 NVML、WMI 与多个逻辑 DXGI 适配器到同一物理 PnP stable key
  的映射。Cloud Node/Remote build/stage/dist `px_service.exe` SHA-256 均为
  `9DF69FB05B5D9146BBC18B187B8FBB260EDC15043881254B7DD7D9A673E85E97`，严格 Clippy 通过。
- Cloud Node Render build/dist SHA-256 为 `41EDD79A409D7FDF95A685EDDC8517DD62C5EA146C69B36781BF660F875E3221`；Remote 为
  `629CF629AC1704D9588A72BB60161129591FB1D0CF1B6F0083DB35D7447E3C09`。物理 GPU 身份硬件测试 1/1 PASS，相关运行时依赖逐文件哈希一致。
- Console Web 类型检查、44/44 合同测试和生产构建通过；部署页面具有中英文预算配置，节点页显示运行时 GPU 绑定是否可验证。当前聚焦 Console
  build/output `px_console.exe` SHA-256 均为 `0441DA4A0F24FB82ACCE5DF51D640C20DC1C8B44FD1C7D738BE028A3253A70A4`，严格 Clippy 通过。
- 三库异机备份恢复专项 `pg-20260919-090416-b1a84fe9` 为 1/1 PASS，并按 Console schema 27 个迁移核验恢复结果。
- Windows 全量短验收 `pg-20260919-101132-c975fede` 从三套空库完成 417/417 PASS，覆盖本批 PostgreSQL、API、真实 Chromium、
  Web Client、断库恢复与最终三库恢复冒烟；公网 Windows/Android、Relay 和最终长测仍按 DB5 单独验收。
- 调度预览 SQLx 从空库重新生成 270 份 Console 查询元数据（`pg-20260919-095659-649ec2d9`）；实例专项
  `pg-20260919-095743-5d59163d` 为 15/15 PASS，目录 API `pg-20260919-095843-01f3ca7b` 为 7/7 PASS。用例证明未知指标保持
  `null`、维护/断线等原因按候选返回，并且预览前后预约数为零。
- Console Web 类型检查、45/45 合同测试及生产构建通过；真实 Chromium `pg-20260919-100752-038bfb6e` 已在应用页验证
  `Node not ready`、`Node disconnected` 两项拒绝原因。当前 Console build/output `px_console.exe` SHA-256 均为
  `FF92E90F5805F1C8BF155F58FCF5AC8AFA05EDC308D1B5A0ABB0F78634078A24`。

设计借鉴 [Kubernetes 调度阶段](https://kubernetes.io/docs/concepts/scheduling-eviction/scheduling-framework/) 的过滤、评分和预约分离，
不要求部署 Kubernetes。适配器映射参考 [Microsoft DXCore 标识说明](https://learn.microsoft.com/en-us/windows/win32/api/dxcore_interface/ne-dxcore_interface-dxcoreadapterproperty)；
GPU 偏好枚举不是第三方进程绑定承诺，见 [DXGI 枚举接口](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_6/nf-dxgi1_6-idxgifactory6-enumadapterbygpupreference)。
