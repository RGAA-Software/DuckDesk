# PostgreSQL 数据库前置改造、备份与升级方案

> 决策日期：2026-09-16。用户已确定 PostgreSQL，并要求数据库先改；当前从全新空库开发，旧开发环境没有有效数据，不做任何 Mongo 数据迁移。
> 本文替代旧规划中“首次迁移保留现有数据库”的决定，以及此前针对 MongoDB 的备份/副本集建议。
> 关联：[服务改造](server_refactoring_plan.md)、[部署与升级](server_deployment_and_upgrade_plan.md)、[应用调度](cloud_application_scheduling_plan.md)、[运维后台](service_operations_console_plan.md)。

## 0. 已确定的边界

1. PostgreSQL 是唯一主业务数据库；不建设 MongoDB/MySQL/PostgreSQL 可切换存储层，不保留运行时 MongoDB fallback 或双写。
2. 以 PostgreSQL 18 为首版验证主版本，实际交付锁定当时已验证的受支持补丁版、镜像 digest/安装包 hash；禁止 `latest` 和 Beta。
3. Rust 采用 SQLx PostgreSQL 驱动、`PgPool`、显式 SQL 和版本化 SQL migrations。实施 DB0 锁定工具链匹配版本与 Cargo.lock，不在文档编造未经编译的依赖版本。
4. 核心关系采用表、外键、唯一约束和事务；仅模式扩展配置使用有 schema version 的 JSONB。不是把每个 Mongo 文档原样塞进一列 JSONB。
5. 按全新系统设计 HTTP/WS、配置、ID 和关系模型；不因旧开发版接口或字段而增加兼容层，必要时同步修改服务、客户端及测试。保留已确定的授权、Game Hook Job、WebView 和 RDP 生命周期等产品规则；数据库阶段不同时拆 Broker/Relay 或实现新 GPU 排序算法。
6. 首版直接初始化全新 PostgreSQL 数据库、账号和业务数据；取消旧 Mongo 数据盘点、快照、导入、转换及旧库回退任务。
7. 不开发离线 Mongo 导入器、双写、CDC 或运行 fallback。本文的 SQL migrations 仅指新库建表及未来正式 PostgreSQL schema 升级，不是历史数据搬迁。
8. 基础备份恢复必须随数据库阶段交付；高可用在数据库基线之后单独验收，作为公网商业发布的门槛，不要求每个单机客户部署集群。

## 1. 为什么必须先做

后续节点管理、资源预约、SessionGrant、任务/outbox、滚动升级和恢复都依赖持久事务与唯一控制代际。
先在 MongoDB 上扩建这些能力，再换 PostgreSQL，会重复实现表模型、并发控制、恢复和备份。
数据库改造先完成现有功能与基础事务，再让新服务模块直接使用同一持久基线；不把全部未来业务表预先实现后才算迁移。

### 1.1 当前源码盘点（非生产数据库审计）

| 范围 | 已检查的证据 | 改造影响 |
|---|---|---|
| Console | `rust_server/px_console_server/src/console_database.rs` 声明 23 个集合 | 用户/组/授权、设备、应用、实例、工作区、会话、记录、事件、版本等全部迁移 |
| 应用调度 | `app_schedule/store.rs`、`manager.rs` | 多处 DB 未就绪返回成功或忽略持久化错误；实例选择依赖进程内锁/缓存，须改为 DB 提交后下发 |
| 身份 | `user/session.rs`、`identity/manager.rs` | Mongo TTL、唯一/部分索引、滑动过期和撤销条件需显式 SQL 实现 |
| RDP | `app_schedule/rdp_workspace.rs` | 新工作区的密文 AAD 绑定新部署、owner 和资源身份；无旧密文转换 |
| Auth | `rust_server/px_auth_server/src/author_database.rs` | author、authorization、customer 为领域参考；新许可证协议按评审契约实现，无旧签名格式兼容 |
| Desk | `rust_server/px_desk_server/src/store.rs` | 已改为 PG feedback/versions/admin_sessions；范围与证据见[Desk 契约](postgresql_desk_contract.md)，不代表 Auth/Console 完成 |
| 共享依赖 | `rust_base/px_base`、`rust_base/px_auth_mgr` 的 Cargo.toml 及 `mongodb_util.rs` | 清除 driver 泄漏与不必要依赖，避免 Client 等消费者被带入 MongoDB |
| 配置/交付/测试 | 三服务 settings、Console 本地状态 UI、`scripts/package_px_*_server.bat`、数据库集成测试 | DSN、健康检查、打包说明、隔离测试库和诊断脱敏一起调整 |

集合数量来自当前源码，仅用于检查业务领域覆盖；DB0 直接设计新关系模型及合成测试数据，不再审计旧 Mongo 实例、拓扑、文档或脏数据。
`px_auth_server` 与 `px_desk_server` 纳入新存储实现；签名协议变化须同步签发/验证及测试，不保留旧格式验证分支，不把官方签发服务变成私有部署依赖。
Redis 如仍被现有业务使用，逐项确认缓存/协调用途；本任务不将它变成授权或预约权威，也不附带大规模改造它。

## 2. PostgreSQL 模型与代码边界

### 2.1 服务所有权

- Console 使用独立 `pixels_console` 数据库；Auth 使用 `pixels_auth`；Desk 使用 `pixels_desk`，各有独立运行账号与 migration owner。
- 单机可共用一个 PostgreSQL cluster；服务不能直接读写其他服务表，不使用跨数据库事务代替服务 API。
- Official 与每个 Customer 的数据库、凭据和备份仓库独立，不因统一引擎引入多企业租户架构。
- 共享库只放连接配置脱敏、连接池构建、通用错误分类等基础能力；领域 SQL/模型留在相应服务 repository。
- 同步持久化结果明确返回，禁止“DB 不可用但写入成功”。连接池自带并发，不再把整个数据库客户端包在全局异步 mutex 中串行访问。

### 2.2 业务领域到新关系模型

下表旧集合名仅用于检查功能覆盖，不构成旧字段、接口、ID 或文档格式的兼容要求。

| 当前集合 | PostgreSQL 目标领域 | 关键约束/处理 |
|---|---|---|
| c_user / c_user_session / c_guest_block | users、login_sessions、guest_blocks | 保持规范化用户名唯一规则、撤销/过期校验、token hash；不保存明文 token |
| c_user_group / c_user_group_member | user_groups、group_members | 活跃组名部分唯一索引、成员复合唯一、引用约束 |
| c_group_device_grant / c_group_app_grant / c_user_device | device/app grants、user_devices | 稳定主体/资源关系，授权版本原子变更；用户设备关联不与其他 ACL 混同 |
| c_device / c_stream | devices、streams | 设备身份唯一、描述有版本，机密列隔离 |
| c_app / c_app_placement / c_app_node | applications、application_deployments、application_slots | 按新对象模型建表，应用部署配置通过新环境创建，不导入历史 placement |
| c_app_instance | app_instances、instance_tasks、command_outbox、idempotency_records | 活跃槽唯一、请求幂等、revision CAS、持久状态与发送意图同事务 |
| c_rdp_workspace | rdp_workspaces、workspace_secrets | 新 workspace 唯一、owner/账号约束、密文 AAD 与新身份一致 |
| c_remote_session / c_remote_session_event | remote_sessions、remote_session_events | 逻辑会话/事件 ID 唯一，按时间和目标检索 |
| c_visit / c_file_transfer / c_records | visits、file_transfers、render_record_cache | 新业务记录与可重建缓存分开，明确各自保留政策 |
| c_event / c_client_conn | events、connection_observations | 新事件使用独立 ID；在线快照不作为恢复后的真实在线状态 |
| c_update_info | update_releases | 产品/发行/版本字段显式保存，新下载接口按新协议设计 |
| Auth 三集合 / Desk 三集合 | 各服务独立 schema 模型 | 新许可证、版本发布、咨询问题模型；不保留旧字段表示或旧接口适配 |

表名作为设计基线，DB0 输出新字段、约束、查询索引及合成测试清单；对照当前领域检查无功能遗漏，不生成历史字段转换器。
新生成 ID 优先 UUID；外部协议标识按其契约采用受约束 TEXT。RDP 新工作区的 owner_node/deployment 与密文 AAD 一起设计，验证新建、持久化、重启后解密和访问权限。
RDP 沿用 `(application,node)` 持久工作区唯一键；user/guest 是访问占用的 owner，不加入工作区唯一键，不因访问者切换创建 Windows 账号。
时间持久化为 UTC `timestamptz`，接口毫秒转换显式检查；布尔/空值/缺字段按映射处理，不能盲目套默认值。
`u64` 字段按业务范围选择受约束 BIGINT 或 NUMERIC，禁止溢出截断；排序、大小写、分页和软删除语义需逐项回归。
身份/权限/资源分配字段不能藏在自由 JSONB 中，模式配置 JSONB 有大小限制、校验和版本，不持有可执行任意 SQL/脚本。
新增逐 GPU 预约表属于后续应用调度阶段，沿用本阶段的事务、任务与幂等基础，不复制另一套持久实现。
资源遥测与安全/业务审计分开保留：高频采样批量聚合、设置容量和保留期，不把每个心跳都写入永久审计表。
DB0 根据声明的测试负载测算索引、WAL 与备份增长；大表按实际查询制定分页、分区/清理和 vacuum 方案，不能靠换引擎解决无限增长。

## 3. 事务与异步执行

一次现有应用启动至少在一个短事务内完成：校验主体/配置版本、锁定候选运行单元、检查活动占用、
创建实例/任务、幂等结果与 outbox，再提交。提交成功后 worker 下发节点命令，不能持有事务等待网络回执。

- 唯一约束兜底重复请求与活跃占用，行锁/条件更新维护竞争；默认 READ COMMITTED 配合明确锁定协议，跨行不变量单独评审。
- 全部相关写路径（启动、停止、授权修改、任务接管）遵守相同锁顺序/版本检查，不能只有 Start 加锁。
- 短事务中使用 `FOR UPDATE`；任务领取可用 `SKIP LOCKED` 加持久租约/owner epoch，不能把取到行等同获得永久执行权。
- outbox 至少一次投递、稳定 command_id、节点幂等与旧代际拒绝；DB 提交成功但客户端未收到响应时只查询/重试同一请求。
- 持久化失败返回可重试服务不可用并关闭新业务准入，现存进程/媒体按原授权与宽限继续，不删除 RDP 工作区。
- 内存只做缓存/投影；DB 恢复先与 Service 对账，再恢复调度。TCP 连通或数据库 ping 正常不等于业务可写/迁移已完成。
- Mongo TTL 改为索引支持的到期清理 worker；鉴权查询仍即时检查 expires_at/revoked_at，不能等清理完成才让 token 失效。
- 死锁、序列化冲突有界重试整个事务；外部副作用不在重试事务内执行。密码/密钥、完整 DSN 不进入 SQL 日志。

数据库阶段需要节点支持执行幂等/对账的最小增补时，只修改相关契约并验证旧/晚到消息安全性，不借此启动完整 Broker 重构。

## 4. Schema 与数据库引擎升级

### 4.1 应用 schema

每个服务单独维护追加式 SQL migrations、版本和 checksum；已发布 migration 不原地修改。
独立 migration job 使用专用 owner，advisory lock 确保同一库一个迁移者；运行账号不具备 DDL/superuser 权限。
服务启动校验支持的 schema 范围，不支持即 NotReady，禁止静默自动建表或多个副本同时执行 DDL。
SQLx 查询检查在 CI 的 PostgreSQL 18 干净库上执行，离线 query metadata 必须与 SQL/migration 一起更新并验证。

当前首版落实为完整 migration 清单及 checksum 精确匹配，不接受缺失、未来或被修改的版本。
业务池的每条物理连接（含重连）先取得数据库级共享 schema 锁，再验证角色/身份/schema；
迁移 CLI 串行化后必须取得独占锁，仍有业务池开放则拒绝，不在服务运行中偷偷执行 DDL。
Auth/Desk 可有多个同时持共享锁的实例；升级时全部停止准入、排空并关闭池，迁移成功后启动匹配的新程序。
运行期锁与 Console 单活动锁是两个边界；单独的租约连接存活/失效不替代每条业务连接的 schema 保护。
专项及跨平台证据以[实施状态](server_database_execution_status.md)为准；离线初始化写入、备份屏障和恢复准入仍需各自验收。

首版数据库替换用维护窗口；未来滚动应用升级采用 expand → 分批 backfill → 切换 → contract，
加列/建索引/NOT NULL 等操作有锁等待与执行超时；并发建索引等非事务操作有明确失败恢复记录。
应用版本与 schema 版本独立演进；旧代码不支持新 schema 时禁止回滚旧二进制，不自动执行破坏性 down migration。

### 4.2 PostgreSQL 引擎

- 补丁升级：固定包/镜像，预检、备份验证、维护重启或经验证的主备逐台升级，按该版本 release notes 执行。
- 大版本升级：独立维护任务，先在恢复副本运行 `pg_upgrade --check`、扩展/排序规则检查与功能测试；
  首版使用独立数据副本的受控 `pg_upgrade` 或逻辑导出/导入，不承诺数据库大版本热升级。
- 新库开放写入前可退回未修改的旧库；新库开始写入后旧库已落后，不能只改连接字符串“回滚”。采用前向修复或明确数据损失的计划恢复。
- 新库建表遵守已评审的新密码/签名和身份契约及 RDP 生命周期；无需兼容旧开发数据或接口。

## 5. 全新安装与初始化

1. 创建独立部署身份、三库及分离的运行/建表账号，显式执行版本化建表入口。
2. 初始化管理员与必要配置；通过正常业务 API 创建用户、ACL、设备、应用和测试工作区。
3. 校验数据库约束、权限、密码验证、签名与 RDP 新密文读写；不从旧开发库读取任何业务数据。
4. 使用新环境登记的节点和权威端点完成 Windows、随后 Android 的全链路验收。
5. 新库有数据后按本计划备份和恢复；后续升级只覆盖正式 PostgreSQL 版本边界，不提供回到 Mongo 的产品路径。

本决定取消所有旧数据迁移任务及其门禁。旧开发数据库是否删除属于环境清理，不是本次实施前置；不自动触碰其他服务或已有 Windows 账号/Profile/Session。

## 6. 自动备份与保留策略

### 6.1 两种交付档位，同一个数据库

| 档位 | 机制 | 适用范围 |
|---|---|---|
| 基础备份 | `pg_dump` custom-format + `pg_restore`，独立执行器定时运行 | 小型单机、Windows 原生首版、开发验证；明确不是 PITR |
| 生产备份 | Linux PostgreSQL + pgBackRest 基础/差异/增量备份 + 连续 WAL 归档 | 自营公网默认，私有高可用/生产推荐；Windows 服务可连接该数据库 |

不假设 pgBackRest/Patroni 在 Windows 原生环境可直接套用 Linux 部署。Windows 本地 DB 先交付基础备份，
如客户要求时间点恢复/高可用，连接验证过的 Linux PostgreSQL 服务；不取消现有 Windows Console 运行支持。
基础模式的容量/耗时超过验证范围必须提示升级备份档位，不能把每小时逻辑全量备份宣传为任意规模生产方案。

### 6.2 基础档默认值

| 档位 | 调度（部署时区，默认 UTC） | 保留 |
|---|---|---|
| 小时 | 每小时整点 | 最近 24 份成功备份 |
| 日 | 每天 02:00 的成功备份 | 最近 7 份 |
| 周 | 每周日 02:00 的成功备份 | 最近 4 份 |
| 月 | 每月 1 日 02:00 的成功备份 | 最近 6 份 |
| schema/引擎升级前 | 迁移任务前 | 最近 5 份成功且未被锁定的升级备份，正在依赖的恢复点不可删 |
| 手动 | 管理员触发 | 默认 30 天，可锁定 |

同一个文件可同时被小时/日/周/月引用，所有保留引用过期才清理。锁定备份不自动删除；失败不占成功名额。
本地快速恢复副本保留最近 3 份完整成功备份，独立机器/对象存储执行完整保留策略；未配置异机仓库时只能称本机备份。
设置每库互斥任务，任务 ID 按计划时间去重；耗时超过周期只保留一个待执行请求并告警，不重叠堆积。
停机错过的周期恢复后补做一次，不伪造缺失小时/日备份；时区改变及夏令时按 UTC 计划 ID 去重，页面展示时区。

### 6.3 生产档默认值

- 每周日 02:00 全量，保留最近 4 个完整全量链；每天 02:00 差异（全量当天复用），每小时增量（全量/差异时段复用）。
- 目标保留最近 7 个日恢复点、24 个小时恢复点；必须同时保留它们依赖的全量/差异/增量，实际文件数可能更多。
- 连续 WAL 归档，目标保留最近 7 天可验证的 PITR 窗口；保留窗口起点之前所需的基础备份及连续 WAL，不能直接按 WAL 文件年龄删。
- 每月额外建立独立完整归档集保留 6 份，手动锁定/升级前备份放受独立保留策略保护的归档集；包含恢复该集所需的 WAL/清单。
- 由工具支持的保留机制管理整条依赖链；产品 UI 的“恢复点数量”转换为经过验证的仓库策略，不对 pgBackRest 仓库文件手工删除。
- 本地缓存按完整可恢复链计算，不沿用“只保留三个文件”；生产主仓库在独立故障域，可选不可变副本，写入与删除权限分离。

以上替代 MongoDB 阶段提出的小时全量默认建议。PITR 以实际连续归档终点为准，单有 `pg_dump` 文件无法重放 WAL。
生产档以异机恢复点滞后不超过 5 分钟为工程目标，须实测后才成为承诺；初始归档切段目标 60 秒，网络/归档失败仍可能超标。
基础档只提供小时级计划，不宣称固定 RPO：真实数据损失窗口按最后可恢复备份计算，失败会扩大窗口。

### 6.4 执行、校验和告警

备份执行器独立于 Console UI/进程，使用受限备份身份；Console 负责策略/任务展示，不接收超级用户密码或任意 shell。
备份含数据库、schema/tool/engine 版本、校验和、时间与来源身份；另存必要角色/权限、配置、扩展清单和受保护密钥恢复材料。
`pg_dump` 单库一致，不保证 Console/Auth/Desk 多库同一事务时间点；整体恢复必须按第 6.6 节的恢复集/对账流程执行。
备份库外保存签名/校验清单和任务结果副本，不能数据库坏了连备份位置、解密材料和恢复工具都找不到。

- 完成状态区分 Created、Verified、OffsiteVerified、RestoreTested；校验和成功不等于已验证能恢复。
- 连续两次失败，或超过两个备份周期没有成功结果告警；WAL 归档失败、复制延迟、磁盘/仓库不足另设即时告警。
- 清理仅在新备份验证后进行，不能删除最后有效恢复链或正在恢复/迁移依赖的集；空间不足不能自行突破锁定/保留规则。
- 备份加密、传输认证、凭据脱敏，私有平台默认不上传官方仓库。恢复密钥受独立访问控制，不能只存在待恢复数据库中。
- 基础档可配置一个由部署方挂载并授权的异机文件仓库（Windows UNC/受控网络卷或 Linux 远端挂载）；执行器按完整恢复集及
  `previous_recovery_set_id` 依赖顺序复制，每个归档在目标端重新核对 manifest/hash 后才标 `OffsiteVerified`。配置中的另一目录
  不能自行证明独立故障域；正式验收还必须记录仓库主机/存储身份并做源主机下线后的异机恢复，测试机双目录仅验证复制算法。
- 每月在隔离环境完成实际恢复演练，每次 schema/引擎重大升级前完成专项恢复测试；隔离环境不得向生产节点下发命令。

后台“数据库与备份”展示档位、频率/时区、各档数量、仓库容量、复制/WAL 状态、最近可恢复时间、验证日期与风险，
提供立即备份、锁定、下载及受保护的恢复预检。恢复需维护模式、二次确认、目标身份/路径校验和审计，默认恢复到新库而非覆盖原库。

### 6.5 Windows 备份执行器形态

Windows 基础档采用独立 SCM 服务 `Pixels.Backup.<deployment_short_id>`，不依赖 Console 进程/浏览器存活；
固定支持的 pg_dump/pg_restore 工具版本和摘要，任务参数类型化，不开放任意 shell/SQL 或公网管理端口。
每个部署使用 `NT SERVICE\Pixels.Backup.<deployment_short_id>` 虚拟服务账号，不共用 LocalService；安装器只给该服务 SID、SYSTEM 和
Administrators 配置/凭据读取权及仓库、调度、状态目录写权，并清除开发期遗留的 LocalService ACE。服务二进制支持原生 SCM
Stop/Shutdown 取消，安装/覆盖与卸载入口分别为 `scripts/server_backup/install_windows_service.ps1` 和
`scripts/server_backup/uninstall_windows_service.ps1`；卸载服务不删除恢复集、调度或状态数据。
部署登记唯一服务名，使用专用最小权限账户；配置、队列、执行日志和恢复清单位于受 ACL 保护的
`ProgramData/Pixels/<deployment_id>/backup`，与安装版本目录分离，凭据/解密材料不写普通日志或页面。
本地 OS 管理员通过受 ACL 限制的恢复 CLI/本机 IPC 执行预检和恢复，仍需核对目标库/路径、授权与审计，
Console 停机也可操作；普通运行/备份身份不自动拥有建库、DDL 或覆盖恢复权限。
任务持久化，重启先对账，跨周期互斥、漏跑补一次；断电产生的半成品不标记成功。恢复优先新库，清理只操作清单登记的备份集。
DB0 交付服务身份/ACL/目录/任务协议与离线恢复设计，DB4 实现并测试 SCM 重启、断电、Console 停机、凭据失效和目录访问拒绝。
Linux 使用 `deploy/systemd/pixels-backup@.service` 模板承载同一个执行器，实例参数是 deployment ID；模板固定服务账号、配置与数据根，
启用 systemd 文件系统/内核/能力边界，SIGTERM 复用同一取消语义。发行安装器仍须创建账号、目录和 ACL 后才可 enable/start，
不得把模板文件存在等同于目标发行版真实启动验收。

### 6.6 Console/Auth/Desk 整体恢复集与对账

恢复范围按部署清单确定：官方域包含实际部署的三个服务；私有部署不因此复制官方 Auth 库/签发私钥，未部署 Desk 标记不适用。
每个恢复集使用 `recovery_set_id`，清单包含 deployment、库列表/缺席原因、应用/schema 版本、备份时间范围、hash、
可用的集群 timeline/LSN 或逻辑快照边界、关键授权/签发/发布事件水位，以及关联制品和密钥恢复材料的受保护引用。

1. 需要一致整体恢复点时，对范围内所有服务建立写入屏障，关闭签发/发布及后台 writer，完成在途跨服务请求和 outbox 对账，
   再依次导出各库；屏障保持至全部导出完成。普通小时单库备份可继续，但标为独立恢复点，不能冒称一致整体快照。
2. 同一 PG cluster 的物理备份可以提供同一数据库恢复时间点，仍须对账数据库外节点/签发/制品副作用。
   分离 cluster 没有跨库原子快照；记录各自边界，使用上述业务屏障或经验证的事件水位重放方案，否则禁止自动整体开放。
3. 隔离恢复全部适用库，禁用签发、通知、发布、节点派发和自动 outbox 重放。先核验 Auth 签名/撤销记录与库外防回滚水位，
   再校验 Console 许可证/用户权限、实例/工作区 owner 与未决任务，最后核对 Desk 版本元数据与实际签名制品/hash。
   这里是依赖校验顺序，不允许服务直接跨库读表或恢复工具擅自签发许可证。
4. 备份后已撤销授权不得复活；已发生但 DB 未记录的签发/发布/节点执行不得重复。通过受保护外部审计水位、服务 API 和节点事实对账；
   证据不全或水位不一致时保持维护，显式处理 RecoveryRequired，不猜测“较新的库一定正确”。
5. 提升恢复授权/控制代际，隔离旧 owner，与 Service 对账后按依赖先恢复必要签发/验证和元数据服务，再开放 Console 新业务；
   私有离线验证不等待官方 Auth。记录差异、处理人和恢复验收，不能以三个库各自 pg_restore 成功作为整体成功。

保留策略以完整恢复集及依赖为单位，锁定集的任一成员不得被单库清理删除。DB0 给出屏障/清单/对账样例，DB4 演练缺库、
错版本、签发后未同步、撤销后旧备份、Desk 元数据有而制品缺失、跨库不同时间点恢复等故障；未通过不能作为整体灾备承诺。

## 7. 高可用：单机可交付，公网商业版必须另过门槛

单机版：1 个 PostgreSQL + 异机备份，明确无自动数据库故障接管。
高可用基线：3 个独立主机故障域，各 1 个 PostgreSQL 数据节点（1 主 2 备），Patroni 管理；
3 个 etcd 投票成员可分别部署在这三台主机，数据库节点数与协调投票数是两件事，不套用 MongoDB 选举规则。
客户端连接统一读写入口（冗余代理或已验证的多地址发现），不能只写死当前主库，也不能使代理成为新的单点。

- 首版高可用以同地域低时延部署验证，承受一台机器故障；三节点放同一宿主机或机房不等于异地容灾。
- 默认同步一个备用，Patroni synchronous mode + strict 策略经实测冻结；没有满足条件的同步副本时暂停关键写入，不静默降为异步。
- 开启 fsync、可靠存储和同步提交；只有在已确认提交、正确同步/选主/隔离条件下才讨论零已提交数据丢失，不笼统保证任何故障 RPO=0。
- 失去多数协调或无法证明旧主已隔离时不自动提升另一主；watchdog/fencing、同步副本选择与旧主回归必须演练。
- 客户端断线导致提交结果未知时依赖幂等键查询确认，不盲目重放 Start。Console 控制 owner 和 DB 主角色不是同一个 epoch。
- 目标在一台主机故障后 120 秒内恢复数据库写入，作为测试目标非既有能力；整体业务恢复还包括连接池恢复与 Service 对账。
- 高可用不代替备份，误删会复制；已有用户媒体不经过数据库，故障时停止新授权/预约，按既定宽限处理现有访问。

## 8. 恢复安全边界

恢复备份到隔离新库，校验版本、密钥、约束及业务，再决定切换；隔离旧控制端/旧主、停止后台写入和节点命令后才开放恢复环境。
历史备份可能缺少之后的撤销/禁用/ACL 变更：仅强制重新登录不足以解决旧权限复活。
默认灾难恢复生成新的、不可与历史混淆的 recovery generation，重新建立受信节点/服务控制关系并作废旧登录/Grant/待发命令。
generation 的签发和旧环境隔离由库外受保护恢复流程完成，不能只把已回退库内计数器加一。
有独立可信安全变更日志时对账恢复权限；否则保持业务准入关闭，由恢复管理员审核账号/ACL 和资源归属后再启用。
该流程不会删除账号记录、注销 Windows Session 或清理未知进程；实例/预约逐项向 Service 核实，不能自动重放备份里的 Start。
普通主备自动切换与回退历史备份不同：未回退数据且 generation 未变时不要求所有用户重新登录，但仍检查未决事务与任务。

## 9. 前置执行顺序与完成条件

实施入口：[本机 PostgreSQL 环境与基础测试](../deploy/development/postgres/README.md)、[身份存储契约与测试映射](postgresql_identity_contract.md)、
[DB0 领域/权限/恢复边界](postgresql_domain_contract.md)、[Desk 新服务](px_desk_web_overview.md)。
当前实现 DB1 基础、DB2-A 身份 repository 和 DB3 Desk/Auth 独立子集；Console 及许可证消费者尚待切换，隔离测试通过不代表 DB0–DB5 或三服务整体通过。
Console 已继续扩展设备、应用、访客、节点、部署、实例/命令与工作区 repository；具体增量和证据以后面的状态索引为准。
已执行用例、修复缺陷、报告索引和剩余门禁集中记录在[实施与验收状态](server_database_execution_status.md)。

每步的测试环境、输入/预期、故障注入和留证规则见[逐步开发与测试门禁](server_incremental_validation_plan.md)。其中第 4 节细化必要 P0/DB0/DB1 的八个小步，第 5–7 节定义 DB2–DB5 的出口；缺环境、跳过或未运行均不能算通过。

| 阶段 | 工作 | 出口 |
|---|---|---|
| DB0 模型与契约 | 新关系模型、约束/索引、工具链版本、密钥边界、合成功能基线；未来三库恢复集/对账、Windows SCM 执行器契约 | 业务覆盖完整，合成用例和备份契约可评审；不依赖旧源库 |
| DB1 PostgreSQL 基础 | 连接池、权限、SQL migrations、干净库安装、SQLx 查询检查、跨平台配置与脱敏 | DB 不可用/版本不符正确 NotReady，重复迁移及并发迁移安全 |
| DB2 Console 完整持久化 | 身份/权限/设备/应用/工作区/记录全覆盖；任务/outbox/事务；清理静默写入成功与内存权威 | 登录注册、ACL、应用各模式、并发启动、断库/重启/晚到回执通过 |
| DB3 Auth/Desk 与共享依赖 | 新数据层与新契约、同步签发/验证和 API 消费端、去掉 Mongo 驱动及旧设置/健康探针 | 三服务均不依赖 Mongo；新许可证/版本接口通过，无兼容分支 |
| DB4 备份恢复与引擎升级基线 | 基础/生产档执行器、保留/清理、异机上传、恢复工具、最小后台、schema 升级流程 | 定时任务、过期链保护、失败告警、干净环境恢复、历史权限防复活通过 |
| DB5 全新部署与总体验收 | 空库安装、初始化、节点登记、业务创建、备份恢复、Windows 后 Android 回归 | 新环境全部功能通过，无旧库/旧协议依赖；之后进入 P1/P2/P3 |
| DB-HA 高可用专项 | Patroni/etcd/入口、同步策略、隔离与切换演练 | 公网及私有 HA 发布前必过；可与后续 P 阶段推进，不阻塞单机数据库基线 |

执行优先级：必要的 P0 数据/安全契约 → DB0–DB5 → 后续发行/业务/连接服务改造。
产品启动根只创建一个经过部署/schema/runtime-role 检查的连接池，各领域拿同池的能力句柄，不能每个 repository 单独建立默认池。
业务服务必须拒绝 owner/超级用户/扩权的 runtime 角色启动；离线建表与空库管理员初始化继续使用独立 owner 工具，不由业务启动代办。
P0 非数据库协议可以继续设计，但不得以“数据库未定”为由先扩建 Mongo 上的新调度/集群。无需等 DB-HA 完成才启动后续单机开发。
例行验证使用相关 Rust workspace 的 check/test、隔离 PostgreSQL 集成测试和前端专项检查；文档编辑不触发构建。
完整发布构建与部署仅在用户明确要求对应交付时执行，不能在日常修改中调用 release-only 全量脚本。

### 必测矩阵

- 两个隔离测试进程抢最后一个运行单元、同 request_id 重试、提交成功响应丢失，不重复创建实例或执行副作用；该并发压力测试不是 DB 阶段多活部署承诺。
- DB 暂不可用、池耗尽、磁盘满、故障切主时不返回假成功；已运行实例/工作区不被误删。
- username 规范化、软删除、空值、TTL、毫秒/时区、u64 边界、分页、token 撤销全部保持正确。
- RDP 新密文持久化及重启后可解密、workspace owner 不变、二次前端 busy、普通停止不注销；Game Hook Job 所有权不变。
- Auth 签名材料/验证结果、Desk 咨询/问题/版本接口以及 Windows/Android 登录连接功能回归。
- 新库重复/孤儿引用被约束拒绝；初始化中断可安全恢复；无隐式清库、双写、导入器或 Mongo fallback。
- 备份时持续写入、任务重叠、上传失败、校验失败、保留边界、锁定、跨月/时区和依赖链清理均正确。
- pg_restore 恢复、生产 PITR 到误操作前、缺 WAL 拒绝、密钥缺失阻止开放、外部旧主/旧命令被隔离。
- schema 迁移被杀进程、锁超时、checksum 改动、不支持版本启动和大版本演练均安全失败。
- DB-HA 测主机断电、网络分区、同步副本丢失、etcd 少数派、入口故障和旧主回归；不是只测试手动 promote。

## 10. 官方机制依据

- [PostgreSQL 支持与版本升级政策](https://www.postgresql.org/support/versioning/)：锁定受支持主版本与验证过的补丁制品。
- [SQLx](https://github.com/transact-rs/sqlx)：PostgreSQL 异步访问、SQL 检查和 migration 工具。
- [pg_dump](https://www.postgresql.org/docs/18/app-pgdump.html)：单库一致性逻辑导出及全局对象边界。
- [连续 WAL 归档与 PITR](https://www.postgresql.org/docs/18/continuous-archiving.html)：基础备份、连续日志与恢复窗口。
- [pgBackRest 保留策略](https://pgbackrest.org/configuration.html)：全量/差异/增量及归档依赖，不按裸文件数量删除。
- [Patroni 同步复制模式](https://patroni.readthedocs.io/en/latest/replication_modes.html)：同步/异步与可用性取舍，故障保证有明确条件。
- [pg_upgrade](https://www.postgresql.org/docs/18/pgupgrade.html)：大版本预检与受控升级，不能当普通 schema migration。
