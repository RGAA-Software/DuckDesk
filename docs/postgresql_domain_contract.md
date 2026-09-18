# PostgreSQL DB0：剩余领域、权限与恢复边界

> 2026-09-17。实施约束，不是已完成声明。身份/组与 Desk 的已实现契约分别见
> [身份存储](postgresql_identity_contract.md)、[Desk](postgresql_desk_contract.md)；
> 各模块落地时须补 SQL、查询元数据、正常/拒绝/故障用例及实际证据。
> 不搬迁旧数据，不接受旧产品别名、默认补字段、旧凭据解码或 Mongo fallback。

## 1. Console 领域清单

所有新主键为 UUID；外部进程 PID、Windows Session ID、字节数等不是数据库身份。
时间为 UTC timestamptz；revision/generation 为正 BIGINT；u64 来源必须先校验 <= i64::MAX，不可截断。
关系、授权、状态、调度维度使用显式列/FK/CHECK，不把整个旧对象塞进 JSONB。
JSONB 只允许版本化、限长且经过 DTO 验证的事件详情/遥测附加字段，不能成为身份或权限权威。

| 领域 | 必需字段/关系 | 不变量与索引 |
|---|---|---|
| 用户与管理角色 | users 增加明确 role=user/admin/viewer；沿用身份契约的 hash、禁用/删除、双 revision | 公开注册只能 user；请求的 client_type 不是 role。首个管理员由专用初始化工具创建，不由注册接口抢占 |
| 用户会话 | login_sessions；签发角色从 users 读取；用户或管理终端类型显式 | 管理操作每次联查实时 role/state/revision；角色变化同时递增授权版本 |
| 访客 | guest_sessions：id、token_hash、可信来源 HMAC、client_type、revision、created/expires/revoked；guest_blocks 与 guest_source_blocks：guest FK、原因码、操作者、时间/期限；guest_events | 与 users 分表、分身份类型；已阻止/过期访客不得因重连生成同身份有效授权；来源阻止与签发共用事务 gate；不将 IP 当 owner |
| 设备 | devices：UUID id、独立 12 位 public_code、name、platform、registered_at、disabled/deleted、revision、enrollment hash | 公开编号不是身份/口令；设备登记身份与用户关联分离；不保存供管理员查看的明文远控密码；所有配置改变 CAS；已实现子集见[设备契约](postgresql_device_contract.md) |
| 用户设备与授权 | user_devices(user,device)；group_device_grants(group,device)；group_app_grants(group,application) | 双 FK/复合 PK/资源反向索引；关联与组授权不是同一概念，访问使用统一策略 |
| 节点控制身份 | nodes：id、device FK、credential hash/fingerprint、generation、last_seen、reconciliation_state | node 与设备/连接 PID 不混同；报告携带节点身份和代际，过期/未知状态不准入；启动不能用旧在线快照恢复健康 |
| 节点硬件/容量 | node_gpus(node,gpu stable key)、可用实例/编码槽、显存及预算；节点 CPU/内存/网络预算与 observed_at | 单机多卡有独立 key；数值 >=0、已用 <= 合理范围，未知使用 NULL/显式状态而不是 0；P3 完成实际硬过滤/评分，不以 DB 建表冒充调度完成 |
| 应用目录 | applications：id、name、不可变 kind game_hook/webview/rdp、access public/acl、mode-specific launch fields、媒体规格、revision/access_revision、deleted_at | 三种模式字段组合 CHECK；RDP 不引入二次采集编码；WebView URL、Windows 路径/参数校验分开；无旧 placement 自动转换；子集见[应用契约](postgresql_application_contract.md) |
| 应用部署 | application_deployments：id、application FK、node FK、install_root、gpu key、desired spec revision、ready revision、capacity、disabled、revision | 未准备好不能启动；同节点端口预约独立唯一；Game Hook 路径包含空格/Unicode/参数引号，不按 basename 授权 |
| 实例预约 | instances：id、application/deployment/node FK、owner_user 或 owner_guest、request_id、body hash、state、generation、revision、port、GPU、reserved/started/ended 时间 | owner 二选一有 FK；活跃资源槽/端口部分唯一；同主体 request_id 唯一且不同正文 409；新建事务内复查 ACL 与容量 |
| 持久命令 | commands：id、instance FK、node FK、node generation、instance revision、kind start/stop/reconcile、state、attempt、next_attempt、deadline、ack 时间 | 命令与实例转换同事务；发送在提交之后；ack 必须匹配三重身份与代际；重复投递由节点 command ID 去重，不按端口认领进程 |
| Outbox | outbox：id、事件类型、目标主体/节点、相关资源、授权 revision、created/available/claimed/processed、attempt | 权限收缩与通知同事务；单 Console 活跃，事务/CAS/有界 claim 防重复处理；没有悬空的多活 fencing 承诺 |
| RDP 工作区 | workspaces：id、application/deployment/node FK、Windows account_name/SID、credential revision、key ID、nonce、ciphertext、state、revision | (application,node) 唯一；owner_node 是工作区所在机器，不是访问者 user。AES-256-GCM AAD 绑定 deployment/workspace/application/node/account/credential revision；缺 key 拒绝，不重建覆盖；凭据仅过受保护的既定 RDP 准入边界 |
| 资源会话 | resource_sessions：id、login 或 guest session FK、owner、target_kind、target 关系、client_type、state、created/ended/revision | Desktop 指定 device；CloudApplication 指定 application + instance + owner + descriptor revision，device/account 不得兜底。Android 必须 android |
| 描述符 | resource_sessions 中当前 descriptor_hash/expires_at/revision，resource_session_events 追加签发事件；generation/endpoint revision 与目标绑定 | endpoint 来自当前节点报告，不能把 host/port 当授权；短期授权最长 30 秒且续租在线复查；实际端口同时承载该 Render 的 TCP/WS 与 UDP；缺失/过期拒绝 |
| 流与连接 | 具名连接设置的 owner/明确 target/有界选项；client connection observations 的 resource_session FK、node/客户端连接代际、观察时间、断开时间 | c_stream 同时包含配置，不能只建在线快照表就声称替换；持久记录不作为在线证据；Console 重启先对账，不把旧连接/流直接恢复 Running；见[活动记录契约](postgresql_activity_contract.md) |
| 访问/传输/录制/事件 | visits、file_transfers、会话 recording events、独立 node 录像库/缓存、resource_session_events、audit_events；主体/目标 FK、UTC、结果码、必要计数、创建游标 | 节点自主录像不强绑访问会话；审计追加而非覆盖；以业务键幂等；文件名/路径按权限最小可见，不记录口令/请求正文；查询稳定排序与有界分页 |
| 更新元数据 | update_releases：明确产品/发行/渠道/OS/architecture/build、URL/hash/大小、签名引用、状态、revision | 与 Desk 外部发布目录通过 API 对账，不跨库 JOIN；无默认发行/平台、旧产品别名或“只按字符串最新” |

表名用于归档业务归属，最终 SQL 新增/拆分必须同步本表，不能遗漏旧实现中的业务能力后宣称替换完成。
删除优先使用明确的软删除/终态；实例、命令和工作区不得通过级联删除触发 Windows 清理。

节点硬件/容量使用 `node_telemetry_latest` 与 `node_gpu_latest` 保存最新快照，并以 `node_telemetry_history` 与
`node_gpu_history` 保存已接受的原始历史。每份机器快照绑定 node generation、
严格递增的报告 sequence、采样/接收时间和 `ready|partial|unavailable` 探测状态；GPU 行以 `(node_id, stable_key)` 唯一，并通过
`(node_id, inventory_revision)` 外键绑定同一次 GPU 库存。报告事务先取得当前节点代际门禁，再原子替换快照与 GPU 清单，旧连接、
越界数值、未来/过期采样、重复 GPU key 或显存已用大于总量全部拒绝。未知值必须为 NULL；Windows Service 在 WMI 清单之上仅对
PCI vendor/device/subsystem 唯一匹配的 NVIDIA NVML 设备填充逐 GPU 利用率、显存及编码器压力。无驱动、查询失败、歧义匹配及其他厂商
继续保持 NULL，P3 调度不得将其解释成 0 或空闲。历史行与 latest 在同一报告事务提交，GPU 历史通过
`(node_id,node_generation,report_sequence)` 外键绑定机器样本；管理查询使用接收时间、代际和序号的完整降序游标。原始样本固定保留
7 天，Console 独立任务每分钟最多清理 5000 个过期机器样本并级联其 GPU 行，运行角色没有 UPDATE 历史的权限。趋势聚合、阈值告警、
断线补报和逐 GPU 预约不因原始历史表存在而视为完成。

阈值告警使用每节点 `node_telemetry_alert_policies`，默认 CPU/内存/固定磁盘 warning/critical 为 850/950‰，GPU 为
900/980‰；默认连续 3 个已接受样本越线才开事件，低于 warning 减 50‰ 的回滞边界连续 3 个样本才恢复。缺失或 NULL 指标不产生、
不刷新也不恢复事件。`node_telemetry_alert_conditions` 只保存去重/回滞工作状态；`node_telemetry_alert_events` 保存 active、
acknowledged、recovered 生命周期、阈值快照、首次/最新/峰值、次数和 CAS revision，同一节点/指标/资源最多一个未恢复事件。
确认人和策略变更分别写追加审计；恢复事件保留 180 天后由每批最多 5000 条的有界任务清理，未恢复事件不自动删除。策略和确认只有
admin 可写，viewer 可读。当前 Windows 采样尚无可信逐 GPU 利用率，因此 GPU 告警字段与调度一样保持“未知不推断”；断线补报、
趋势聚合、管理实时推送和真实公网 GPU 告警验收仍是后续领域。

RDP 工作区沿用[已冻结模式决策 §0.0](rdp_application_mode_design.md)：同一应用/节点使用同一个持久 Windows 账号与桌面，
不因 user/guest、访问者变化或重连另建账号。用户/访客 owner 属于资源会话与占用，不属于工作区唯一键；不同访问者先后访问会看到同一工作区文件。
本数据库草案先前的 owner_user FK/按用户唯一措辞有误，不能据此改变既定隔离边界。第二个前端 busy，不接管、不旁观。

## 2. 统一权限判定

| 主体 | 目录/查询 | 写入/启动 | 机密 |
|---|---|---|---|
| 未认证 | 登录/注册及显式公开入口 | 不能伪造 user/guest owner；先签发访客身份再使用访客业务 | 不返回存在性以外的账户/资源信息 |
| user | 自身资料、关联或授权设备；public 应用与获授 acl 应用 | 本人实例；目录、start、descriptor、reconnect 使用同一授权函数并在副作用事务内复查 | 不可查看密码/节点凭据/RDP 密码 |
| guest | public 应用；仅本人实例状态 | 未过期/撤销/阻止方可启动；不能进入 acl 应用或别人的实例 | 不接受 user ID 兜底 |
| viewer | 管理页面允许的只读去密视图 | 一律拒绝管理写入、启动、代用户操作 | 无明文/密文密码查看入口 |
| admin | 管理目录/用户组/部署/审计 | 经过验证的管理身份 + CAS；敏感重置/撤销显式审计 | 管理员可重置，不能恢复原用户密码；RDP 凭据不是管理 DTO，依既定 SSPI 方案仅向准入客户端内存与受信节点受保护交付 |
| node | 本节点命令与精确代际实例 | 报告/ack；不得自行改 owner、ACL、容量预约、其他节点记录 | 节点凭据不授予管理员权限 |

使用 bearer 的原生客户端不自动拥有浏览器管理权限。网页若使用 cookie，必须采用 Secure/HttpOnly/SameSite
并对写操作验证来源及 CSRF；若使用 bearer，不同时接受任意 cookie/查询参数 token 作为第二条认证路径。
密码计算放入受限阻塞任务池；登录包含统一错误、dummy verify、账号/来源限流，长度在 hash 之前限制。

组/角色/资源 grants 改变须与相应用户 authorization_revision 及撤销 outbox 同事务。
成员、grants 和启动操作统一锁顺序，明确写在 SQL/repository 中；只在内存更新或先发消息再写库均不通过 DB2。

### 2.1 更新目录实现约束

更新目录不是安装授权。Console 的本地审批记录与 Desk 的外部发布目录保持独立，通过显式 API 对账，不跨库连接。
目录以 product/distribution/channel/OS/architecture/build_number 区分制品；不能仅以版本字符串决定最新包，
也不能因为 product=server 就猜操作系统，或用缺省 architecture 指向错误安装包。
Desk 的第二个 schema 增量及 Console 0019 已补齐 OS/architecture、制品大小与签名元数据 URL/hash；
共享 `px_release_catalog` 校验严格字段及平台矩阵。Desk 不补旧开发行默认值，也不导入旧发布目录。
Console 本地登记初始为 pending；admin 管理审批/撤回、viewer 只读；发布身份和正文只读，runtime 仅可改策略状态/revision。
主体 request_id + 正文摘要使原登记可精确重试，但不会撤销后续 withdraw；CAS/事件同事务，事件写失败则策略不改变。
当前最高 build 未审批或已撤回时不自动返回低版本，避免目录查询制造隐式降级；明确再次审批仍须新的 CAS。
单调版本水位、签名有效期和实际安装防回滚属于更新执行器，不能用该查询规则替代。

2026-09-17 Windows 专项：Console 七组、Desk 七组真实 PG/API 测试通过，共同校验四组及 clippy 无警告。
两份专项的 808 个登记源码 hash 已复核。后续完整 pg-20260917-084316-f0f4839f 的 503 项通过，
两平台各 228 个 Rust 用例、三库恢复和最终工具 hash 已核对；不是产品自动升级验收。

发布记录中的内容 hash、大小、产品/发行/平台身份和签名清单引用不可被修改正文覆盖；镜像/审批策略与内容身份分开。
Console 审批/撤回使用管理写权限、revision CAS 和同事务事件，重试原登记请求不撤销后续撤回。
允许查询目录不等于允许安装。实际安装仍必须通过部署计划 §7.3 的可信更新框架、签名、产品/发行、有效期和防回滚检查；
这里不自研替代签名协议，也不以管理员填写一个 key ID 或 SHA-256 当作已验证签名。

## 3. Auth 新许可证边界

Auth 为官方签发组件，不是 Customer 运行依赖；私有 Console 使用预置可信公钥验证本部署离线许可证。
新协议必须显式携带 schema、license UUID、发行/产品、目标 deployment、机器绑定、数量/功能额度、
issued/not_before/expires、license revision、签名 key ID。签名载荷不可携带后台密码、app_secret 或数据库凭据。
签名使用已有 Ed25519 原语；精确编码、字段/时间边界和独立固定向量见[新许可证字节契约](postgresql_license_contract.md)。
签发/验证必须共用该契约，拒绝未知字段/版本/产品别名。
不在读入时 normalize 老 product、补默认 product/mode 或重新序列化另一种格式来“试验签”。

Auth 数据所有权：

- authors 与 author_sessions：管理账号/角色、Argon2id hash、授权 revision、token hash/过期/撤销。
- customers：正式 UUID 与客户显示名/备注，非空壳集合；不改变单部署模型，不引入在线多企业租户路由。
- licenses：customer FK、目标 deployment、明确产品/发行、机器指纹、额度、起止时间、revision、revoked_at；
  唯一业务键防重复发放；撤销持久化且不可被旧签发请求覆盖。
- license_issuances：license FK、签发 revision、key ID、精确 payload bytes/hash、signature、签发时间；
  先提交签发事实再返回，未知提交结果按 request_id 查重，不盲目再签发。
- license_requests/audit/outbox：主体请求 ID + 正文 hash、结果引用、撤销/签发审计、通知状态；事务边界完整。

在线验证必须检查撤销和当前 revision；离线许可不能声称“官方撤销瞬间生效”。
私有离线部署以本地受控更新许可证/撤销水位或到期为界；允许离线的最大有效期是显式许可证字段，不是无限延续。
恢复旧库后必须先与独立保存的签发/撤销高水位对账；证据不足保持 RecoveryRequired，不签发、不恢复访问。

## 4. 三库恢复集与 Windows 执行器

恢复集 manifest 至少包含：set UUID、deployment、成员 required/N/A、数据库/schema/hash、
备份起止/一致性边界、授权/签发/发布水位、外部密钥 ID（不含 key）、应用包 hash、
上一有效集/依赖 WAL、Verified/OffsiteVerified/RestoreTested 状态及失败码。
Customer Auth 为 N/A 时必须与发行/组件清单一致，不能因备份失败临时改 N/A。

三库不共享事务快照。第一版完整逻辑恢复集采用受控写入屏障：
先让 Console/Auth/Desk 停止接受业务写入并排空已接纳事务，持有屏障期间分别备份和记录水位；
任一成员失败则该集不完整，不挤掉最后成功集。普通单库备份可无屏障，但不得标成完整跨服务恢复集。
服务停机时执行器独立运行；屏障证明来自进程/服务身份与数据库活动事务检查，不要求 Console HTTP 在线。

恢复顺序：隔离网络/禁用节点命令 → 校验完整集/密钥/备份 → 恢复到新库 → schema/约束/业务摘要 →
Auth 签发/撤销高水位对账（Customer 未部署 Auth 时按清单 N/A）→ Console 许可/授权撤销与未决命令对账 → Desk 发布包存在性/hash →
节点实例与 RDP 工作区身份对账 → 管理员明确解除 RecoveryRequired。缺一不可自动开放。
恢复不向旧节点自动重发 start/stop，不注销 RDP Windows Session，不清理应用/Profile/账号。

Windows 使用独立 `Pixels.Backup.<deployment_short_id>` SCM 服务，低权限专用服务账号，不借用交互登录用户或 Console 进程。
配置/密钥/任务日志/临时目录/已验证仓库分开 ACL；管理员配置，服务账号仅必要读写，普通用户拒绝。
任务以 set ID 和目录所有权标记登记；禁止接受任意路径删除，清理只限仓库内登记的非链接对象。
SCM Stop 可取消当前任务，半成品保持失败；异常重启不把残留文件升级为成功。
Linux 使用对应独立常驻执行器/systemd；同一任务状态机、保留逻辑和恢复 manifest。

调度与保留沿用[数据库计划](postgresql_database_migration_plan.md)：小时 24、日 7、周 4、月 6、升级前 5、
手动 30 天；最后有效集、锁定集、恢复中和被 WAL 依赖的对象不能过期删除。
告警必须独立于 Console 送达；备份成功但上传失败不可标 OffsiteVerified。
DB4 必须实测 SCM/Linux 重启、故障注入、实际还原与生产 WAL/PITR；本节不是执行器实现。

## 5. 合成用例映射与出口

字段/CHECK/索引对应 BASE/PRIV/QUERY；角色矩阵对应 DB2-A/B；最后槽竞争、提交前后故障与晚到回执对应 DB2-C；
CloudApplication owner/target、RDP AAD/保留与历史记录对应 DB2-D；Auth 固定签名向量与 Desk API 对应 DB3；
完整集/保留/SCM/WAL 对应 BK-*；最终 Windows 后 Android 对应 DB5。
各对象都必须有合法、缺字段/越界、重复、越权、断库/重启用例；有事务再加精确故障和竞争用例。
未有实现和运行证据的项目维持 NOT_RUN；不得把此清单写完视为 DB0–DB5 已通过。
