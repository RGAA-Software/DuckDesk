# 服务数据库改造：实施与验收状态

> 更新至 2026-09-20。只记录实际交付范围；这不是 DB0–DB5 或整个商业化计划的完成声明。

本轮范围已确认：先完成 DB0–DB5；DB-HA 和 P1–P7 不作为本轮交付终点。DB4 仍包含其已规划的备份/恢复要求，不因排除 HA 而删除备份验收。
Desk 与 Console 调度无运行依赖，允许先完成 Desk 的独立新库/API/网页纵向验证；这不是提前宣布 DB3 整体完成。

## 已实现的小步

目录 API 首轮 `pg-20260917-100158-fc36814c` 为 FAIL：设备与节点/部署两组通过，但 RDP launch 带 `video` 时返回 201，
没有按严格对象契约拒绝。定位为 internally-tagged unit variant 对空载荷之外字段的忽略，不能将外层 deny_unknown_fields 当成足够证明。
已增加项目自己的严格空结构反序列化边界，覆盖 RDP/部署目标、准备状态、命令结果、传输结果和通道结果的十种空载荷，
不改第三方源码，也不经 JSON Value 重建而丢掉重复键。19 组存储单元测试通过；目录三组专项
`pg-20260917-100641-06999f7e` 和身份五组专项 `pg-20260917-100810-e2a5a0c9` 均通过，847 个源文件及 px_db hash 逐项复核一致。
实现使用 Serde 正式提供的 [variant deserialize_with](https://serde.rs/variant-attrs.html)；空 variant 返回 `()`，有效协议形状不变，未知/重复字段拒绝。

后续增量正在验收：Console/Auth 密码格式的数据库 CHECK 与共享解析器完全一致，用户名显示值和 lowercase 后值均限制 2–64 字符；
业务池每条物理连接持有共享 schema 锁，迁移工具必须取得独占锁，重连先取锁再验证完整 schema。
不是依赖单独租约连接存活来假定其他业务连接安全。`pg-20260917-102255-ab23d8c3` 数据库 13 组、
`pg-20260917-102355-bedb4ef9` schema 互斥 4 组 Windows 专项通过；两份报告的 851 个源文件 hash 已复核，
当前 px_db 与后一份最终制品 hash 相同（后一套件重编译后不再冒称当前二进制等于前一份）。
完整回归 `pg-20260917-102526-980a4962` 在 Auth 签发夹具失败：其 INSERT 仍使用 `synthetic-test-hash` 占位串，
被新的数据库 CHECK 拒绝；Console Windows 与 Desk 在此之前通过，但本轮整体为 FAIL，未进入 Linux/恢复出口。
夹具改用共享密码库生成真实合成 Argon2id hash，不放宽约束；另补离线 Console/Auth 初始化池的共享 schema 锁，
防止离线初始账号写入与 DDL 竞争。修复后 Auth 存储 7 项 `pg-20260917-105329-98bb9413`、Auth API 8 项
`pg-20260917-105419-7737745c`、schema 门禁 4 项 `pg-20260917-105527-b515d4eb`、Console accounts 9 项
`pg-20260917-105607-aca6971a` 均通过且源码 hash 未漂移；失败报告保留，隔离容器/卷已清理。完整跨平台回归仍须重跑。

Console 访客与资源入口新增增量：稳定的部署私钥对直连 socket IP 做域分离 HMAC，IPv4-mapped IPv6 规范化；
访客签发不接受 bearer 或转发来源，公开应用/本人状态/注销及管理员单会话或来源封禁已接 HTTP。
实例和资源会话入口强制唯一 `X-Pixels-Subject-Kind: user|guest`，只查询声明的凭据类型，不在两类 token 之间试探；
CloudApplication 保持显式 application/instance 目标，descriptor 秘密只在数据库事务提交后返回。
Windows 目录/API 五项专项 `pg-20260917-111216-5acb856e` 通过；其中验证了 user/guest/admin 不串权及无就绪部署时拒绝，
成功启动/节点执行/真实描述符仍依赖节点 WS 与 Service 接入，不能用该专项冒充端到端通过。
首次完整回归 `pg-20260917-111427-a4b3fb8b` 在精确测试数量门禁停止：新增 guest HMAC 单元测试后 Console ingress
由 1 项变为 2 项，已执行测试均通过，但报告保持 FAIL 且未进入后续领域/Linux/恢复；门禁同步为新测试清单后从头重跑。
第二次完整回归 `pg-20260917-111804-7f96e5b2` 完成 Windows 全部领域与 Auth/Desk 浏览器/故障流程后，Linux 的
账号边界夹具失败：Windows 已写入固定 64 个中文字符的规范用户名，Linux 在同一隔离库再次使用相同固定值，唯一约束正确拒绝。
修复保留恰好 64 个 Unicode scalar 的断言，并把本轮 UUID 纳入值以避免平台串行冲突；不删除数据、不放宽唯一约束。
第三次完整回归 `pg-20260917-113258-7ae1b574` 中上述 Linux 用户名边界已通过，随后应用 roundtrip 夹具暴露另一项
跨平台累积数据假设：新建应用只在管理列表前 100 条内查找，Windows/API 数据超过窗口后随机 UUID 可能落在后页。
改为沿稳定 UUID 游标逐页查找并设置有界迭代；不扩大服务分页上限，也不绕过管理授权。

用户新约束已登记：[编码风格](coding_style.md)，C++ Google（明确覆盖为 4 空格）、Rust 官方 rustfmt、TypeScript Microsoft。
项目按 Google 基础风格使用 4 空格和 150 列；智能指针/RAII、第三方只读及不做无关全仓重排的约束继续有效。

此前完整回归 `pg-20260917-115055-ce8ce993`：570 项 PASS，Windows/Linux 各 260 个 Rust 用例，
三服务共 274 条 SQLx 查询在线/离线一致（Console 236、Auth 29、Desk 9），Auth/Desk 网页、真实浏览器、进程故障恢复及三库恢复通过。
854 个受验源文件与 Windows/Linux 各 5 个最终工具制品 SHA-256 已在源码继续修改前逐项复核一致。
本报告覆盖密码/用户名边界、每连接 schema 锁、owner bootstrap 门禁、访客来源/API、资源主体入口及此前全部 repository；
仍不代表 Console 产品切换、节点 WS、自动备份执行器或 DB0–DB5 阶段出口完成。

该完整报告之后的 Console 组合根增量已加入严格环境配置和正式私有文件装载：生产数据库固定 full TLS 校验，
显式 deployment/listen/TLS/origin/生命周期/开关，工作区密钥与访客来源密钥必须由权限检查后的文件提供；
缺失、重复、非 32 字节或活动 key 不匹配均在监听前失败，不生成临时 fallback。开发 HTTP 只允许 loopback，
监听端口明确拒绝所有已退役端口。该增量的四项单元测试与全目标 Clippy 已通过，但尚未做新的完整跨平台回归。

新 Console 节点控制入口 `/api/console/node-control` 已接入实际 WebSocket：首帧凭据认证在 5 秒内完成，
每次连接由服务器生成不可从 wire 恢复的 connection key/generation；64 KiB 消息、128 个连接、来源频率、
空闲/写入/数据库截止及严格递增非零 request_id 均有硬边界。节点报告、部署准备报告、完整清单对账、命令领取和精确 ACK
均只使用已认证 `NodeConnection`；断线关闭该 generation，Runtime shutdown 等待连接退出后才关闭数据库池。
Console 和节点共用不依赖存储/传输实现的 `px_node_protocol` 严格 DTO crate，节点响应不暴露完整管理 profile。
实际 TCP + WebSocket + 全新 PostgreSQL 的闭环专项通过：创建节点/部署、上报 ready、
reconcile、Android user 预约、Start/Running、Stop/Absent、重复 request_id 拒绝及断线 offline 均实际执行；源码 hash 未漂移、
隔离容器/卷已清理。它仍是 Console 侧和合成节点协议验收，不代表 Windows Service 已切换或 Game Hook/WebView/RDP OS 行为通过。

第一次含节点长连接的完整回归 `pg-20260917-124558-b763c8fb` 保持 FAIL：`directory_api` 与 `node_control` 两个独立进程
误用同一个夹具数据库，第二次初始化管理员被一次性初始化约束正确拒绝。修复只给 node_control 分配独立 Windows/Linux 新库，
未放宽初始化约束。修正后的专项 `pg-20260917-125114-15ac4dd5` 通过。

当前最新完整回归 `pg-20260917-125204-c6fcbc5c` 通过：576 项 PASS，Windows/Linux 各 263 个 Rust 用例，
274 条 SQLx 查询在线/离线一致（Console 236、Auth 29、Desk 9）；Console 节点实际 WebSocket 闭环、Auth/Desk 网页与真实浏览器、
进程/数据库故障、WSL2 三服务 readiness 及三库备份恢复全部通过。860 个登记源文件在整轮运行期间 hash 不变，
Windows/Linux 各 5 个最终工具制品 hash 已记录并复核。该报告仍不代表旧 Console 产品二进制和 Windows Service 已完成协议切换，
也不代表自动备份执行器、真实媒体/OS 行为或 DB0–DB5 总出口完成。

该完整报告后的 Windows Service 节点接入增量已移除旧 Console/Panel appkey 控制路径，并接入新的
`/api/console/node-control` 客户端、严格单调请求、节点报告、清单对账、命令代际/租约校验、精确 launch Start/Stop 和 ACK。
节点 endpoint/token/public host 由标准输入导入，保存在仅 SYSTEM/Administrators 可访问的目录并使用 machine-scope DPAPI；
正式地址只允许 WSS，loopback 开发可用 WS，旧路由、query token 和退役端口没有 fallback。
`px_service` 73 项、`service_core` 84 项本轮聚焦测试通过（另有 1 项既有真实 UE 样本测试按原规则 ignored）；
期间发现两项测试使用的固定低端口与本机出站连接冲突，已改为在测试范围内选择连续空闲端口，不删除或停止占用进程。
本条尚未进入 PostgreSQL 全量验收报告，也未完成真实 Console→Service→Render、RDP workspace envelope、GPU 绑定、发行部署身份校验，
因此不能替代最新完整报告或宣布 DB0–DB5 完成。
部署清单出口新增 1 条 Console SQLx 查询后，本批离线/在线门禁期望为 Console 237、Auth 29、Desk 9，共 275 条；
历史报告中的 236/274 是当时制品的真实计数，不回写伪装成新结果。
部署 repository 六项专项 `pg-20260917-140031-f24eb3a2` 及真实节点 WebSocket 专项
`pg-20260917-140244-b450cf2e` 均通过；后者已从真实 PostgreSQL 分页读取节点部署，并验证最小准备描述不会下发 WebView URL。
两份报告均完成源码 hash 稳定性复核并清理隔离容器/卷，仍须以随后完整跨平台报告作为本批 DB 总门禁结果。

本批完整回归 `pg-20260917-140526-29a46d6e` 通过：576 项 PASS，Windows/Linux 各 263 个 Rust 用例，
275 条 SQLx 查询在线/离线一致（Console 237、Auth 29、Desk 9）；新部署清单查询和节点 WebSocket 在两平台均执行。
Auth/Desk 网页与真实浏览器、进程/数据库故障、WSL2 三服务 readiness、三库备份恢复和损坏归档拒绝全部通过。
862 个登记源文件在整轮运行期间 hash 不变，Windows/Linux 工具制品 hash 已记录并复核，隔离容器与卷已清理。
本报告证明当前数据库与节点协议增量的完整软件门禁，不替代下表所列真实 Windows OS、自动备份执行器和 DB5 产品验收。

DB4 第一纵向切片新增 `px_backup`：严格恢复集清单、部署绑定私有仓库、跨进程互斥、临时集归档验证与 SHA-256 后原子发布、
小时 24/日 7/周 4/月 6/升级前 5/手动 30 天的引用保留内核，以及锁定、恢复中、最后有效集和依赖链保护。
执行器只允许把当前实现标记为 `Independent`；没有写屏障证明时拒绝生成 `WriteBarrier`/`Physical` 恢复集。
固定 `pg_dump`/`pg_restore` 路径与摘要、私有 pgpass、无口令参数/输出、最长 24 小时边界及取消回收已经进入实现；
清理接口当前只返回安全候选，不执行删除。发布集读取会重新核对精确成员与各归档 SHA-256，并拒绝依赖缺口/环；
Windows 单元门禁 15 项通过。测试专用 Docker 适配器已对 PostgreSQL 18.6 实际执行 Console/Auth/Desk 三库 `pg_dump`、
`pg_restore --list`、分别恢复到全新库、部署身份核对及发布后篡改拒绝，专项报告 `pg-20260917-153411-43fa3235`。
该专项不是生产执行器。

加入该模块后的此前完整回归 `pg-20260917-145244-75f20f67` 通过：603 项 PASS，Windows/Linux 各 276 个 Rust 用例，
275 条 SQLx 查询仍为 Console 237、Auth 29、Desk 9；Auth/Desk 浏览器与故障恢复、三服务 readiness、三库恢复均通过。
868 个登记源文件在整轮运行期间 hash 不变，隔离容器与卷已清理。该报告覆盖备份内核，不代表真实定时任务、SCM/systemd、
实际 `pg_dump` 恢复演练、异机仓库、告警或生产 WAL/PITR 已通过。

加入真实三库备份执行链后的最新完整回归 `pg-20260917-153615-b97315c2` 通过：609 项 PASS，Windows/Linux 各 279 个 Rust 用例，
两平台都实际执行 PostgreSQL 18.6 三库 `pg_dump`、`pg_restore --list`、全新库恢复、部署身份核对与发布后篡改拒绝；
275 条 SQLx 查询仍为 Console 237、Auth 29、Desk 9。Auth/Desk 浏览器与故障恢复、三服务 readiness、既有三库恢复冒烟也继续通过。
869 个登记源文件在整轮运行期间 hash 不变，Windows/Linux 工具制品 hash 已记录，隔离容器与卷已清理。
本报告证明 DB4 逻辑备份核心和测试适配器的跨平台执行链，不代表生产定时服务、宿主 PostgreSQL 工具/最小账号、实际清理、
异机复制、告警、整体恢复准入、权限防复活或 WAL/PITR 已交付。

DB4 第二纵向切片新增独立持久任务内核：计划时间生成稳定 task ID，跨周期只保留一个最新待执行请求，单进程/单活动任务互斥；
重启将遗留活动尝试记录为 `Interrupted`，以同 task ID 和递增 attempt 补跑一次。受保护状态快照采用 current/previous/next 三阶段，
启动时只对登记文件前滚或回滚，未知条目和配置变化 fail-closed。清理必须由最新已验证恢复集触发，重新计算保留链后只逐个删除清单登记文件，
不使用递归目录删除；中断会留下需对账状态而不伪报成功。Windows 专项 20 项报告 `pg-20260917-160418-946e41f1`，
同一 20 项及严格 Clippy 已在 WSL 原生通过。它仍是执行内核，不是已经注册运行的 Windows SCM/Linux systemd 服务；
完整跨平台总门禁及真实服务进程故障验收继续单列。

命名审计同步把本轮维护的 DB0–DB5、Console/Auth/Desk PostgreSQL 与 Windows Service Rust 代码中的单字母/脱离上下文命名
改为领域名称，并增加 `scripts/check_readable_names.ps1`。该门禁当前覆盖 179 个活动 Rust 源文件，在 PostgreSQL 隔离环境创建前执行；
生成代码、只读第三方和 `backup/` 不参与机械改名，也没有通过例外清单放行问题。
最新完整回归 `pg-20260917-164825-ca155018` 通过：620 项 PASS，Windows/Linux 各 284 个 Rust 用例，
275 条 SQLx 查询仍为 Console 237、Auth 29、Desk 9。两平台均实际执行 PostgreSQL 18.6 三库备份、全新库恢复、部署身份核对、
篡改拒绝；Auth/Desk 浏览器与进程故障、Console 节点 WebSocket、断库恢复、三库恢复冒烟继续通过。870 个登记源文件在整轮执行期间
SHA-256 不变，Windows/Linux 工具制品 hash 已记录，隔离容器与卷已精确清理。该报告补齐本切片的跨平台软件门禁，仍不把任务内核
冒充已注册运行的 SCM/systemd 服务，也不代表异机复制、告警、恢复准入、权限防复活、WAL/PITR 或 DB0–DB5 总出口完成。

DB4 第三纵向切片已把内核接成独立 `px_backup` 常驻进程：私有严格配置绑定 deployment、仓库/调度/状态三根目录、固定摘要的
`pg_dump`/`pg_restore` 与三库计划；启动先丢弃仅含登记文件的未发布 `.partial-<recovery_set_id>`，未知内容继续 fail-closed。
每轮任务原子发布脱敏状态，连续两次失败和超过两个周期无成功结果分别产生独立告警码；成功验证后才触发保留清理。
Windows/Linux 各 25 项单元测试及严格 Clippy 已通过，可读命名门禁覆盖 182 个活动 Rust 文件。

Windows SCM 已做真实安装、启动、状态发布、Stop、再次 Start 和卸载验收：服务名
`Pixels.Backup.<deployment_short_id>`，账号为每部署独立虚拟账号 `NT SERVICE\<service_name>`，卸载保留数据。
首轮共享 LocalService/受限 SID 的 ACL 失败按失败保留并修正为服务 SID 精确授权；SCM 启动失败会返回可定位的 service-specific code，
而不是笼统退出。Linux systemd 加入同一进程的加固模板，但尚未在真实 systemd 主机完成 enable/start/TERM/重启验收。
本切片也尚未把状态告警送到独立 Alertmanager/通知端，未实现异机仓库、恢复准入、权限防复活与生产 pgBackRest/WAL/PITR；
因此仍不声明 DB4 或 DB0–DB5 总出口完成。下一份完整 PostgreSQL 报告须覆盖新增 25 项及 Windows 二进制目标编译后才可成为新总基线。

该完整总门禁现已完成：`pg-20260917-175325-e1781fbc` 共 630/630 项 PASS，Windows/Linux 各 289 个 Rust 用例，
`px_backup` 的库、常驻二进制目标及 Linux 信号退出路径均在各自平台编译执行；275 条 SQLx 查询仍为 Console 237、Auth 29、Desk 9。
两平台的 PostgreSQL 18.6 三库逻辑备份、`pg_restore --list`、全新库恢复、部署身份和篡改拒绝，Auth/Desk 浏览器与进程故障、
Console 节点 WebSocket、断库恢复和恢复冒烟全部通过。873 个登记源文件在整轮中 SHA-256 稳定，最终工具摘要已记录，
隔离容器和卷已精确清理。该报告是本切片新的软件总基线；真实 SCM 生命周期证据来自同批独立 OS 验收，Linux systemd 真实宿主、
独立告警送达、异机副本、恢复准入与生产 WAL/PITR 仍按上一段保持未完成。

DB4 第四纵向切片正在实现配置化异机文件仓库：恢复集按依赖链从最早依赖向目标复制，目标端重新核对清单和每个归档 hash 后
才写 `OffsiteVerified`；重复复制幂等，目标已有同 ID 不同内容时 fail-closed。上传或目标保留失败会把本轮任务标为明确失败码，
本地已验证恢复集继续保留且不冒充异机成功；状态快照新增异机配置、最后异机恢复集和最近失败码。配置/状态 schema 直接升为 2，
不读取旧 schema 或添加兼容默认。Windows 聚焦报告 `pg-20260917-182859-170d2044` 的 28 项、Windows/Linux 原生 28 项及
两平台严格 Clippy 均已通过；新的完整总门禁尚待后续告警链与恢复准入继续收敛后统一执行。
测试机上的两个独立目录只能证明复制、依赖、幂等、篡改拒绝和失败语义，不能证明独立主机故障域；正式异机验收仍保持未完成。

DB4 第五纵向切片已把备份状态导出为原子替换的 Prometheus textfile 指标，并提供固定版本可校验的五条告警规则：采集完全缺失、
状态超过三个轮询周期未更新、连续任务失败、验证备份逾期、已配置异机仓库不健康。指标只带 deployment UUID，不导出凭据、路径、
恢复集 ID 或失败详情；状态目录仍拒绝未登记内容。`prom/prometheus:v3.5.0` 的 `promtool check rules` 已校验全部五条规则。
端到端验收 `test-results/backup-alert-a1c18eeed567` 使用固定 Prometheus、node_exporter、Alertmanager 和独立 webhook 接收器，真实收到
`PixelsBackupRepeatedFailures` 的 firing 通知并核对 deployment，随后精确清理本次容器和网络；该链不依赖 Console 进程。
本机容器验收只证明指标 → Prometheus → Alertmanager → receiver 的软件链路，不证明监控位于独立主机故障域，也不替代客户最终
邮件/短信/IM 接收器配置。Linux systemd 真实宿主、源主机下线后的异机恢复、整体恢复准入、权限防复活和生产 WAL/PITR 仍未完成。

DB4 第六纵向切片开始落实恢复准入：manifest schema 直接升为 2，不读取旧 schema。每个恢复集必须明确携带
`security_evidence`；当前可执行的 Independent 逻辑备份只能登记 `independent_backup` 不可用原因，绝不伪造写屏障或安全水位。
WriteBarrier/Physical 清单只有在 consistency proof、所有 Required 服务的单调安全序列/状态摘要及外部 key ID 引用完整时才合法。
独立私有 witness 采用严格 schema、deployment 绑定、受限大小和私有文件 ACL；恢复准入逐服务比较水位，witness 比备份新、比备份旧、
同序列不同摘要、缺服务或缺 key 均保持 `RecoveryRequired`。此外必须完成网络隔离、禁用外部副作用、恢复到新库、部署身份、schema/约束、
业务摘要、未决命令、应用制品及节点/RDP 工作区事实九项检查；全部通过也只到 `ReadyForManualApproval`，不会自动开放服务。
Windows/WSL Linux 各 33 项及严格 Clippy、覆盖 183 个 Rust 文件的可读命名门禁已通过；Windows 聚焦报告为
`pg-20260917-185649-b6a44e6f`。真实 PostgreSQL 18.6 三库归档、分别恢复到全新库、deployment 核对及篡改拒绝在 manifest schema 2
下继续通过，报告 `pg-20260917-185738-4ce566c0`。该切片尚未生成真实 WriteBarrier 水位、持久化人工审批或把门禁接到产品恢复命令，
因此权限防复活仍不能宣告完成；它先把 Independent 备份误开放的路径硬性封死。

DB4 第七纵向切片把恢复审批做成独立持久状态机：每个 deployment/recovery-set/目标环境组合有私有、进程互斥、原子轮换的严格记录，
状态只能按 `RecoveryRequired → ReadyForManualApproval → Admitted` 推进。每次评估保存完整 blocker 集和 manifest+witness+九项检查的 SHA-256；
证据变化会撤销 Ready 并递增 revision，审批必须同时匹配当前 revision 和 evidence hash。审批记录包含不可为空的 approval/admin UUID 和时间，
精确重试幂等，不同审批 ID 不能覆盖；Admitted 后禁止重新评估。中断在 current/previous/next 各原子阶段可确定前滚或回滚，未知文件、
跨部署/跨恢复集复用及第二执行器同时打开均 fail-closed。Windows/WSL Linux 各 37 项和严格 Clippy 已通过，可读命名门禁覆盖 184 个
Rust 文件；Windows 聚焦报告 `pg-20260917-190829-ce35f11a` 保持源码 hash 稳定并完成隔离清理。状态机尚未接入产品恢复 CLI，真实
WriteBarrier 水位仍未生成，因此本条不把准入/防复活出口提前标为完成。

DB4 第八纵向切片把状态机接入 `px_backup restore-evaluate` / `restore-approve`：命令只接受绝对路径的私有严格配置，恢复集从 deployment
绑定仓库重新读取并逐归档校验，witness 在每次评估/审批前重新读取；审批请求必须是私有严格文件并匹配当前 revision/evidence hash。
仓库被备份服务占用时恢复命令直接拒绝，不提供任意 shell、SQL 或归档路径参数。`restore-evaluate` 在存在 blocker 时输出脱敏状态并以专用
`RecoveryRequired` 退出码失败；`restore-approve` 再次评估后才持久化审批。Windows/WSL Linux 各 41 项和严格 Clippy 已通过，其中包含
真实私有文件、仓库重新校验、Ready 记录与审批持久化的命令级测试；命名门禁覆盖 185 个 Rust 文件，Windows 聚焦报告
`pg-20260917-191533-49c2245d`。这些命令目前只完成准入和审批，不执行 `createdb/pg_restore`、不开放服务；真正恢复执行、写屏障水位和
解除产品维护模式仍保持未完成。

DB4 第九纵向切片新增隔离恢复执行内核：执行计划必须绑定 deployment、已验证 recovery set 和独立目标环境 UUID；Console/Auth/Desk
目标库名由目标环境和服务类型确定性生成，owner、恢复账号、主机、端口及私有 pgpass 均做严格校验，拒绝原库名、任意库名、重复服务或
缺少 Required 成员。执行前重新读取发布清单并复核全部归档；按成员顺序只允许“创建全新库 → 恢复归档 → 核对 deployment/schema”，
任一步失败立即停止，保留失败现场供人工取证，不自动删除库，也不开放任何服务。成功报告仍明确 `admission_required=true`，不能绕过
前述恢复准入和人工审批。Windows/WSL Linux 各 44 项和严格 Clippy 已通过，可读命名门禁覆盖 186 个 Rust 文件；Windows 聚焦报告
`pg-20260917-192101-023a8fc9` 保持源文件 hash 稳定并完成隔离清理。该切片当时只交付类型化执行编排与故障语义，尚未接入生产固定版本的
`createdb/pg_restore/psql` 适配器及命令入口；后续进展见下一切片。

DB4 第十纵向切片已接入生产代码中的固定工具恢复适配器及 `px_backup restore-execute`：私有严格配置绑定仓库、deployment、已验证
recovery set、目标环境、仓库外报告路径，以及绝对工具路径/小写 SHA-256/有界超时；每次操作前重新核对三件工具和私有 pgpass。
`createdb` 只建确定性新库，归档经 stdin 交给 `pg_restore --exit-on-error --no-owner`，保留已验证源库中的运行角色授权；`psql` 只执行内置核验语句并精确
比较 service、deployment、目标库名、owner 和成功 migration 数量。命令不接收任意 shell/SQL/归档路径；失败保留现场，成功报告仍为
`admission_required=true`，不会开放服务。Windows 核心报告 `pg-20260918-000049-0fd6f7bb` 的 47 项全部通过；WSL Linux 同一产品源码的
47 项也通过，严格 Clippy 与可读命名门禁通过。真实 PostgreSQL 18 专项报告 `pg-20260917-235905-4a87302e` 通过：测试启动实际
`px_backup restore-execute` 二进制，以非超级用户 `pixels_restore_operator` 对三库执行真实 `createdb/pg_restore/psql`，核对
deployment、owner、22/3/2 条 migration、恢复后的 runtime schema 权限、私有报告及归档篡改拒绝，并清理隔离库/容器/卷。宿主侧固定
代理也受路径和摘要校验；该切片当时尚未提供账号创建/轮换，后续进展见下一切片。正式 PostgreSQL 客户端安装及源主机下线后的
异机恢复仍须独立验收。

DB4 第十一纵向切片新增正式代码路径 `px_backup restore-provision`：绝对私有严格配置固定管理连接、服务范围、`psql` 绝对路径、
小写 SHA-256 和有界超时；管理密码只经私有 `PGPASSFILE`，恢复密码只经清理后的子进程环境与内置 `psql \getenv` 进入固定 SQL，
不进入命令行和普通日志。命令创建或轮换 `pixels_restore_operator`，强制 `LOGIN + CREATEDB + NOSUPERUSER + NOCREATEROLE +
NOREPLICATION + NOBYPASSRLS`，重置角色参数，并只保留所选 Console/Auth/Desk owner 成员关系和 maintenance DB 连接权；结束前
精确查询全部标志、全部角色成员关系及连接权，额外成员关系也会失败关闭。真实 PostgreSQL 18 专项报告
`pg-20260918-002028-fdc138b2` 已通过：实际 `px_backup` 二进制先创建最小角色，再轮换密码，确认旧密码拒绝、新密码可继续完成三库
`restore-execute` 和固定核验。Windows 核心 49 项报告 `pg-20260918-002117-c6055918` 通过，WSL Linux 同源码 49 项、严格 Clippy
也通过。账号生命周期核心和真实数据库行为已有证据；仍待发行安装器调用、生产密钥托管/撤销、正式 PostgreSQL 客户端安装，以及
源主机下线后的异机故障域验收。

DB4 第十二纵向切片补齐“本地恢复源消失后只使用异机副本”的真实数据库路径。PostgreSQL 18 专项先生成 Console/Auth/Desk 三库
custom-format 归档，将完整恢复集复制到独立私有仓库并核对 `OffsiteVerified`；随后关闭两个仓库句柄并把本地仓库路径整体移走，断言原路径
不可访问。之后启动的 `px_backup restore-execute` 配置只指向异机仓库，仍成功创建三个全新目标库并核对 deployment、owner、migration
数量和 runtime schema 权限；最后在异机归档上注入篡改并确认仓库重新打开时失败关闭。专项报告
`pg-20260918-002909-552cb767` 通过且测试全过程源文件 hash 不变。该证据证明产品无需本地仓库即可从已验证副本恢复，但同一开发机的
两个目录仍不是独立主机故障域；正式验收必须在另一主机或受控网络/对象存储挂载上重复执行并记录存储身份。

DB4 第十三纵向切片实现写屏障证明的备份端强门禁。`BackupPlan` 对 `Independent` 明确禁止证明文件，对 `WriteBarrier` 则强制绝对私有证明；
证明绑定 deployment、非空 consistency proof ID、最多 1 小时统一租约、全部 Required 服务排空时间和租约、写门令牌 SHA-256、安全序列/
状态 SHA-256 及外部密钥摘要。`px_backup` 在开始建集前、每个数据库导出前后和发布 manifest 前重新读取并比较证明对象与文件摘要，任何
缺失、过期、内容改变、重复/缺失服务或错误 deployment 都失败关闭；只有全过程保持有效才写入 `Captured` 安全证据并发布
`WriteBarrier` 恢复集。证明在首库导出时被改写的故障注入已确认不会发布，遗留 partial 可按既有对账清理。Windows 51 项报告
`pg-20260918-003633-aea64c5b`、WSL Linux 同源码 51 项、严格 Clippy 和可读命名门禁均已通过；真实 PostgreSQL 18 回归报告
`pg-20260918-003821-d472be26` 也保持三库异机副本恢复通过。当前只交付备份消费者和清单边界；Console/Auth/Desk 尚未实际停写、排空、生成/撤销证明，
因此不能把本切片称为业务一致备份或权限防复活完成。

DB4 第十四纵向切片把安全水位落到三库 schema：Console/Auth/Desk 当前 migration 数量为 `23/4/3`，各库新增 owner 管理、runtime
只读的 `pixels.recovery_security_state`，初始随机 recovery generation 与正数单调序列不依赖待恢复库外的进程内状态。migration 为
当时全部产品业务表安装 `AFTER INSERT OR UPDATE OR DELETE OR TRUNCATE FOR EACH STATEMENT` 的 SECURITY DEFINER 触发器；即使业务语句
零行匹配也推进序列，runtime 无法直接更新水位或调用受保护函数。PostgreSQL 专项逐库核对产品表/触发器数量完全一致、业务写使序列精确
前进、runtime 篡改被权限拒绝。首轮报告 `pg-20260918-004340-9e4091d8` 正确发现 migration 之后由测试脚本临时创建的 `pg_fixture`
没有产品触发器；将该明确测试夹具排除后，第二轮功能 14 项通过但因并行前端编辑触发源 hash 门禁而留下失败报告
`pg-20260918-004504-fe7068d6`。聚焦原生套件现统一排除不会构建的 web 源，最终报告 `pg-20260918-004611-7df45844` 通过且源 hash
稳定；真实三库备份、异机副本恢复及 `23/4/3` migration 核对报告 `pg-20260918-004708-7c260cde` 也通过。此切片建立库内单调事实，
库外见证持久化、只读连接切换/排空、证明签发和新业务表迁移门禁仍在下一切片完成。

DB4 第十五纵向切片完成三库写屏障生产与进程间恢复。三库水位行加入同生共灭的 proof ID、租约到期和写门令牌 SHA-256；业务触发器
对活动租约返回 SQLSTATE `25006`，`px_pg` 映射为独立 `WriteBarrier` 错误，不能误报普通冲突或假成功。`px_backup barrier-acquire`
使用固定摘要 `psql` 和私有管理 pgpass，先写入相同 proof ID；该更新与所有业务触发器争用同一水位行，所以已进入写事务必须先提交或
回滚。随后命令逐库撤销 runtime CONNECT、终止旧 runtime 后端、核对零连接，再生成带 generation/sequence 规范摘要的私有 proof。
任一步失败会尝试逆序补偿；无法完成补偿则保留严格 marker，后续进程必须 `barrier-release` 对账。release 仅清除 marker 对应 proof ID，
逆序恢复 CONNECT 并删除 proof/marker；重复 release 幂等，重复 acquire 拒绝。租约到期允许触发器清理过期库内标记，但不会偷偷恢复
已撤销的 CONNECT。PostgreSQL 水位/写拒绝 14 项报告 `pg-20260918-005330-b72183f6` 通过；真实 PG18 闭环报告
`pg-20260918-010250-65fb88e8` 验证三库 acquire、runtime CONNECT=false、协调备份、release、CONNECT=true、重复 acquire 拒绝、
重复 release 成功，随后继续完成异机副本恢复。Windows 核心 52 项报告 `pg-20260918-010348-18a6f89f` 和 WSL Linux 同源码 52 项
通过。库外最新可信见证、灾难恢复新 generation/旧会话与旧命令作废仍未完成，不能据此宣告权限防复活总出口完成。

DB4 第十六纵向切片已把灾难恢复后的权限防复活落到正式命令 `px_backup restore-seal`。命令只接受私有严格配置和固定摘要 `psql`，
绑定已验证的 WriteBarrier 恢复集、deployment、目标环境、确定性隔离新库及仓库外 lock/marker/report；进程互斥，首库修改前持久化同一
随机新 recovery generation，异常中断后只能沿 marker 幂等复核，不能生成另一代掩盖部分完成。每库单事务先核对 restored generation/
sequence 的规范摘要与 manifest 完全相同，再执行 fail-closed 重置：Console 撤销登录/访客会话、清空三类设备/应用 Grant、清除未投递
outbox、轮换设备与节点凭据摘要、禁用设备/应用/部署/节点、取消待发命令并冻结活跃实例/资源会话；Auth 撤销 author 会话和许可证并删除
未完成签发请求；Desk 撤销管理会话。事务后固定查询全部安全不变量，三库成功才写 seal report，且报告仍为
`admission_required=true`。恢复准入配置 schema 直接升为 2，`restore-evaluate` 和 `restore-approve` 每次都重新验证 seal report 的
deployment/recovery-set/目标环境、源安全水位及新 generation 状态摘要；缺报告、跨环境复用或篡改均在创建审批记录前拒绝。
Windows 与 WSL Linux 同源码的 Rust 核心 48+9 项及严格 Clippy 已通过，Windows 聚焦核心报告为
`pg-20260918-013638-4e877153`；真实 PostgreSQL 18 专项报告
`pg-20260918-013326-e6a789bb` 从含有效 Console 会话、三类 Grant、节点凭据/待投递事件、Auth 会话/许可证/待签发请求及 Desk 管理会话的
三库开始，实际完成 barrier → 协调备份 → 异机副本 → 源路径移走 → 三库新库恢复 → seal → seal 幂等重试 →
`ReadyForManualApproval` → 人工 `Admitted`，并逐项断言失效记录数、新 generation 和清空的 barrier。源码 hash 全程稳定且隔离容器/卷已清理。
这使“历史权限不自动复活”的数据库内基本链路通过；正式生产仍需外部最新见证的独立持久生产者、Auth 签名私钥轮换/旧公钥撤回、真实
节点与 Windows/RDP 工作区事实对账，以及安装器密钥托管后才能宣告 DB4 总出口完成。

DB4 第十七纵向切片实现库外可信见证生产者 `px_backup witness-record`。恢复集 manifest/写屏障 proof/外部 witness schema 分别升为
`3/2/2`，逐服务携带 recovery generation，恢复准入同时比较 generation、sequence 和规范状态摘要。deployment 绑定的私有见证根使用
跨进程锁、每恢复集不可覆盖文件、追加式 SHA-256 journal 与原子 current 指针；普通记录只允许服务集合不变、generation 不变且 sequence
不回退，同 sequence 不同状态同样拒绝。重复记录同一恢复集幂等，未知文件、链断裂、篡改、回退均失败关闭；已完整写 journal 而 current
未推进时可验证重建，只删除尚无 journal 的已知中断孤儿。灾备后的合法 generation 切换必须同时提供源恢复集、schema 2 seal report 和
`Admitted` 恢复记录，逐项绑定目标环境与人工 approval，并把两份证据摘要写入链，不能用随机新 generation 绕过回退保护。
Windows/WSL 同源码 51+10 项和严格 Clippy 通过，Windows 聚焦报告 `pg-20260918-020334-486d419d`；真实 PostgreSQL 18 报告
`pg-20260918-020228-b83d5f05` 已由异机复制后的 manifest 生成见证并用其完成恢复准入，同时保持源仓库丢失恢复和篡改拒绝通过。
这完成了见证生产/持久化代码路径，但本机目录仍不等于独立主机故障域；生产部署、离线副本和访问控制须在真实独立主机再次验收。

DB4 第十八纵向切片建立生产 WAL/PITR 可执行基线。`deploy/production/postgres` 使用摘要固定的 PostgreSQL `18.6` 基础镜像和精确
pgBackRest `2.59.1` 包版本，入口为 repository/log/spool 建立受限目录；配置样例固定 Pixels stanza、依赖链保留、历史清单、并行度和
archive-push 压缩，文档明确本地仓库不是独立故障域，正式环境必须生成私有远端仓库配置并将凭据/加密口令置于部署密钥托管。
`scripts/server_validation/postgres_pitr.ps1` 使用唯一命名且精确清理的 Docker 资源完成真实物理全量备份和连续 WAL：基础备份后切换 WAL，
提交新增记录，创建命名恢复点，再提交 `DROP TABLE`；恢复到新卷后精确得到误删前两条记录。负向路径在正常恢复污染原仓库时间线前复制
仓库，删除包含目标恢复点的确切 WAL 段；恢复实例因无法到达配置目标而不能晋升为可写主库，日志必须出现目标未到达错误。
报告 `pitr-20260918-021927-9d82babb` 通过。该结果关闭单机 Docker 的 BK-PITR 功能门禁，不关闭独立主机/对象仓库、真实带宽与凭据、
7 天窗口保持、故障告警送达和发行镜像签名门禁。

DB4 第十九纵向切片补齐 Linux systemd 安装与生命周期入口。安装器严格校验小写 deployment UUID、绝对二进制/配置，创建无登录
`pixels-backup` 系统身份、deployment 专属 0700 数据目录和 0400 私有配置，停止旧实例后原子发布二进制，再 daemon-reload、enable/start；
注销只 disable/stop 指定实例，明确保留配置、调度状态和恢复集。WSL2 Ubuntu 已启用 systemd 为 PID 1，报告
`systemd-f3f6415a-7d40-44b9-84c9-c23e026ffe7e` 真实执行模板：服务启动并发布 schema 2 状态，restart 前后 repository lock 状态不变，
SIGTERM stop 的 Result/ExecMainStatus 为 `success/0`，再次启动成功，注销后配置和仓库状态仍存在；测试专属实例、目录和替身工具随后精确清理。
这关闭 systemd 模板的 WSL2 生命周期门禁；目标发行版 VM 的正式 PostgreSQL 客户端包、MAC 策略、开机重启和发行安装包仍需单独验收。

DB4 第二十纵向切片实现 Auth 库外签名 keyring 与撤回门禁。严格规范信任根绑定 Auth deployment、数据库
`recovery_generation`、唯一活动 key 和有界受信公钥集合；活动私钥身份、key-id/public-key 配对、排序/重复/未知字段、私有文件权限
任何一项不符即在监听前失败。许可证只按载荷 key_id 选择本地受信公钥，不逐 key 猜测；轮换期旧/新 wire 均可验证，生成不含旧 key
的新根后旧 wire 立即拒绝。`px_auth_admin create-trust-store` 仅 create-new，支持显式受信旧公钥集合，灾难恢复新代际可生成只含新 key 的根。
Windows 7 项密码学契约、9 项真实 Auth 进程/PG 专项及严格 Clippy 已通过；进程专项包含错误恢复代际拒绝，聚焦报告
`pg-20260918-024335-d028f2ff`，隔离容器与卷已清理。正式安装器的密钥托管、独立见证 key 集同步和
目标部署轮换演练仍属于 DB4 总出口，不以开发机文件替代生产验收。

DB4 第二十一纵向切片完成 Windows `px_backup` 正式客户端包与发行生命周期。固定输入清单绑定 EDB PostgreSQL `18.6-3` Windows x64
归档 SHA-256 和最小 `pg_dump`/`pg_restore`/`psql`/`createdb`、DLL、许可证逐文件摘要；构建器只提取白名单闭包，安装器再次拒绝额外、
缺失、重解析或摘要不符文件，并要求外部传入经审核的 package manifest SHA-256。安装按 package ID 使用版本目录，SCM 使用 deployment
专属虚拟服务 SID；配置/凭据只读，仓库/调度/状态目录可写，封闭 ACL 只保留服务 SID、SYSTEM、Administrators，避免增量 `icacls`
遗留开发用户导致服务拒绝私密目录。覆盖启动失败恢复原 ImagePath、配置字节和运行状态，卸载只删除服务且保留恢复材料。
报告 `windows-backup-20260917-190956-a6e90af5` 使用真实固定 Windows 客户端和摘要固定 PostgreSQL 18.6 容器，完成 dump/list/createdb/
restore/query 往返、注入额外 DLL 拒绝、SCM 首装、同包覆盖、坏配置回滚和卸载保留数据七项，测试服务、容器、卷和临时目录均已清理。
这关闭“正式 Windows PostgreSQL 客户端与 px_backup 包装/安装器”代码及本机真实生命周期门禁，不等于 Pixels 外层代码签名、生产密钥托管、
独立故障域或目标客户 Windows 版本验收。

- 新 Console 运行模块已接身份/用户组 HTTP 与单活动生命周期（产品入口尚未切换）。Windows 路由专项
  `pg-20260917-092450-742a93ef` 五组通过，837 个源文件及工具 hash 复核一致；静态检查通过。
  后续审计/管理重置增量独立验收，不把本条当作这些增量、正式产品或 Linux 已通过。
- 审计/管理重置追加后的 `pg-20260917-093048-bcd0b629`（accounts 九组）和
  `pg-20260917-093137-2ae8dbb4`（Console 身份管理 API 五组）均通过；840 个源文件及 px_db hash 复核一致。
  `0021_identity_audit` 补齐空分组生命周期、改密/重置/首次会话撤销的追加审计；审计失败回滚和 HTTP 管理重置拒绝/成功已实测。
  新 Console 共 236 条 SQLx 查询，正在整轮跨平台回归；不沿用变更前完整报告冒充本批验收。

- 独立 PostgreSQL 18.6 开发/测试环境；Console/Auth/Desk 三库、各自 owner/runtime、最小权限、部署身份与 schema 就绪门禁。
- 独立 schema 工具；运行账号不会建表。checksum/脏版本/未来版本拒绝，锁等待有界，迁移进程被杀后回滚，两个独立进程重试仅执行一次。
- Console 身份/会话/用户组 repository，新 UUID/UTC/revision；密码仅 hash、Android 身份独立、到期与撤销即时生效、组成员替换为完整事务。
- 身份/组/管理查询已做新库编译检查与离线元数据对比，Windows/Linux 使用同一 schema 与查询；当前总数以最新完整报告为准。
- Console 管理角色、最后管理员保护、管理终端隔离、权限变更与撤销 outbox/审计同事务；有界共享/独占事务 gate、30 秒事件租约及旧 lease 拒绝。
- 设备目录/关联/组 grants/凭据轮换和应用三模式目录/组 grants/双版本/事件已进入 repository 实现与回归；
  契约见[设备](postgresql_device_contract.md)、[应用](postgresql_application_contract.md)。尚未替换 Console HTTP/WS，不等同于真实节点连接/启动。
- 独立 guest 主体、终端类型、公开应用查询、到期/注销/管理员阻止与持久事件已通过 repository 跨平台回归；
  来源 HMAC、限期来源阻止/签发互锁、去密管理查询和直连 HTTP 入口已接入并通过 Windows 专项；稳定密钥的正式私有文件加载、
  可信代理模式与客户端全链路仍未交付。
- 隔离测试中的三库 pg_dump/pg_restore；逐表摘要、约束、索引、会话引用与重复写入拒绝检查。
- 节点身份、控制 epoch、连接 generation、单调报告、管理 CAS/审计及设备禁用联动已通过跨平台 repository 回归；
  [节点契约](postgresql_node_contract.md)明确 fresh 不等于 ready；数据库侧 challenge 和 Console 实际 WebSocket 已接通并完成 Windows 专项，
  Windows Service 生产者、Linux 对等回归与真实 OS 执行仍待完成。
- 应用部署的模式字段、稳定 application/node 身份、配置 CAS、准备回执版本/连接代际、审计已通过跨平台 repository 回归；
  不能把部署 ready 直接当作启动授权。边界见[部署与预约契约](postgresql_deployment_contract.md)。
- 实例预约已实现 user/guest owner、客户端类型、请求摘要、确定性容量候选、活跃端口唯一、RDP busy、启动快照及命令/事件同事务；
  Windows/Linux 十组数据库测试通过（含两个独立进程实际等待同一 PG 锁后争最后名额），三库恢复回归通过。
- 命令租约/回执/Stop/完整清单 challenge 已通过两平台 16 组数据库回归；补偿 Stop、未知占用、排空保留、旧清单拒绝与审计故障均覆盖。
  边界见[实例与命令契约](postgresql_instance_contract.md)。测试节点仍是适配器，不能替代真实 Service 的命令执行/fence/完整清单。
- [工作区/凭据](postgresql_workspace_contract.md)已通过两平台 6 组数据库与 4 组单元回归：RDP `(application,node)` 唯一、AAD、密钥 rewrap、SID 固定和去密管理查询。
  实际 Windows 账号/会话保留、产品密钥加载与受保护交付仍待端到端验证。
- 每条测试与源文件 hash 留证，命令限时，失败返回非零；清理仅删除当前登记的合成测试容器/卷，不删除开发卷。
- Console 单池启动根与三服务 runtime-role 检查已通过两平台回归；各领域独立连接入口只在 pg-integration feature 下保留供隔离测试使用。
  业务进程不用 owner 身份启动，离线 schema/初始管理员工具不受业务入口替代。该增量以新报告为准。
- Desk 产品已切到 PG：配置/健康门禁、咨询/问题幂等提交、受保护查询/CAS、短期管理会话/撤销/凭据轮换、产品/发行/渠道版本目录。
  删除其 Mongo 和 px_base 依赖、22 个退役模块、旧密码/校验串及路由；无旧路径转发，运行依赖树不含 Mongo/BSON。
- Desk 网页同步接入新契约；9 条 SQLx 查询在线/离线检查，Windows/Linux 原生服务启动与 API 验证，
  真实浏览器提交/登录/处理/退出、真实服务重启与 PG 停机恢复通过。
- DB0 新增[领域/权限/恢复边界](postgresql_domain_contract.md)，覆盖 CloudApplication owner/target、
  Auth 无密钥泄漏的签发边界、三库写屏障恢复集与 Windows SCM 执行器契约；尚不能代替各领域实现和阶段出口。
- Auth 产品已切到 PG：30 条 SQLx 查询、新许可证字节契约及独立 OpenSSL 固定向量、提交后返回/精确幂等、续期 CAS/撤销审计，
  管理会话/改密失效、初始管理员工具、显式不覆盖密钥生成、Windows ACL/Unix 文件权限校验、新接口与中英/明暗管理网页。
  Auth 的 Mongo/px_base/旧签名管理依赖和退役模块已移除；旧 Console 消费者尚待切换，不能混合部署。

源码入口：[通用 PG](../rust_server/px_pg/src/lib.rs)、[Console 存储](../rust_server/px_console_server/storage/src/lib.rs)、
[Windows 测试运行器](../scripts/server_validation/postgres.ps1)、[Linux 原生测试入口](../scripts/server_validation/postgres_wsl.sh)。
使用方式见[本机环境说明](../deploy/development/postgres/README.md)，业务规则见[身份与组契约](postgresql_identity_contract.md)。
Desk 使用[新启动/开发说明](px_desk_web_overview.md)与[契约](postgresql_desk_contract.md)。
Console 接续入口：[PG 产品运行时契约](postgresql_console_runtime_contract.md)，明确单活动进程、身份与节点控制边界，
并列出 RTC/直播/视频墙/用户资料/日志遥测等仍须核对接入的现有业务，不能以删除旧路由为由遗漏功能。

## 已发现并修复的缺陷

pg-20260917-083952-414bb997 完整回归失败于测试夹具：schema 篡改测试恢复时 UPDATE 没有限定版本，
Desk 由一条增加为两条 migration 后，试图把全部记录设为版本 1，触发唯一冲突并影响后续测试。
已仅恢复被篡改的版本 1/999，并逐条比较整个 ledger 与原始基线，保证其他迁移不改变；没有放宽 schema 校验。
隔离容器/卷已清理，开发库未受影响。重跑完整验收，不将此轮算作通过。
随后 pg-20260917-084122-5b8dd0f5 揭示同一夹具还有两处单版本假设：记录总数固定 1、故障探针固定版本 2。
已按实际嵌入的 migration 清单计算预期数及下一探针版本；探针提前退出会立即失败，而非等待十秒错误边界。
修复限定测试夹具，不修改正式 schema 或放宽生产门禁；两份失败报告均保留。

文件传输复查发现：旧上报路径只验证节点/生产者，没有对新的 Progress/Completed 再验原登录及会话租约。
专项 pg-20260917-075945-bd512c95 的新增撤权断言实际失败，确认撤权后仍能推进进度；原测试只覆盖最终失败记账，覆盖不足。
已补上当前 connected/descriptor 到期与原身份/目标授权检查，同时保留既有终态的精确重试和同生产者失败/取消记账。
新增到期和 closing 场景与撤权补充断言已在 pg-20260917-080122-bc88a099 的 Windows 八组专项中通过，源码 hash 已核对。
修复已纳入 pg-20260917-081253-406b91b9 的跨平台完整回归；不将修复前的绿色结果当作该缺陷不存在的证明。

RDP 领域草案曾把访问会话的业务 owner 错写进工作区唯一键；已按 RDP 模式 §0.0 还原为 `(application,node)`，
owner_node 与访问主体分别建模，不因访问者/匿名访问新建 Windows 账号。新工作区 repository 已进入实现/测试，不宣称已做 Windows 运行验收。

`pg-20260916-231750-2da6474f` 曾出现真实并发失败：SQLx 已释放迁移锁，而后续两个 GRANT 同时修改权限目录，
PostgreSQL 返回 `tuple concurrently updated`。不是网络原因，未删除失败报告，也未把重跑一次绿色当作定位完成。

修复把版本建表/升级和 ledger GRANT 放在同一会话 advisory lock 保护内；保留独立连接，异常或进程退出释放锁。
回归增加 100 轮并发迁移，并保留实际杀进程与两进程重试测试。Windows/Linux 均执行这组断言。
WSL 初次入口路径转义失败也保留报告；改用 `wsl --exec`，不经过默认 shell 对 Windows 路径再次解义。
制品复核发现 Cargo 的测试 feature 合并可能重建 schema 工具，初始编译 hash 不能代表后续验证实际使用的二进制。
运行器现分别记录初始化工具 hash 与测试后工具 hash，并用后者重新检查三服务；最终核对报告、当前源文件与二进制一致。

## 当前证据

- 单活动锁 pg-20260917-090128-d8a9ab5d：Windows 六组通过，811 个源码及 schema 工具 hash 已复核，
  px_pg 全目标 clippy 无警告；真实接管窗口/终止 backend/权限扩大/强杀进程均覆盖。Console 产品尚未调用它。
- pg-20260917-084316-f0f4839f 完整通过：503 项 PASS，Windows/Linux 各 228 个 Rust 用例，
  266 条 SQL（Console 228、Auth 29、Desk 9），两套管理网页与三库恢复通过；808 个源码及 10 个最终工具 hash 已复核。
  覆盖更新目录的平台隔离、审批/撤回/并发/故障，以及迁移夹具的多版本修复；之后的运行锁增量另行留证。
- 更新目录增量：pg-20260917-083715-fb48ddc0 的 Console 七组、pg-20260917-083817-0f7ffb90 的 Desk 七组通过，
  两轮各 808 个登记源文件 hash 已复核；共同契约四组单元测试及三 crate clippy 全目标无警告。
  228 条 Console、9 条 Desk、29 条 Auth SQL 元数据已生成检查。后续完整回归见上项，专项本身不替代 Linux/恢复验收。
- pg-20260917-081253-406b91b9 完整通过：479 项 PASS，Windows/Linux 各 216 项 Rust 测试，
  259 条 SQL（Console 221、Auth 29、Desk 9），两套管理网页与三库数据/约束/索引恢复均通过。
  787 份源码及 Windows/Linux 共 10 个最终工具 SHA-256 已在后续源代码修改前逐一复核。
  覆盖传输授权修复、连接观察八组、缓存十六组与十二领域共享池。其后的更新目录增量不借用本报告。
- 连接观察/访问历史专项 pg-20260917-081101-91e119d2 的 Windows 八组通过，787 份源码 hash 已核对；
  新增 12 条 Console SQL，字段边界单元测试及十二领域共享池已纳入接续完整回归。
  后续完整回归见上项，包含传输授权修复与连接观察的父事务回滚。
- 缓存协调器完整报告 pg-20260917-074445-63ed6bed 通过：459 个 PASS，Windows/Linux 各 206 项 Rust 测试，
  247 条 SQL（Console 209、Auth 29、Desk 9）、两套管理网页与三库恢复通过；758 份源码和 10 个最终工具 hash 已逐一核对。
  包含缓存十六组、二十轮保留/清理 CAS 竞争；0016/0017 表、约束与索引已纳入恢复比对。
  真实 HTTP/Range/回传执行器尚未接入。复查发现已有文件传输上报没有重验原会话授权，正在补拒绝场景并修复，
  本报告不覆盖这项后续修复，也不等于 DB0–DB5 完成。
- 缓存事务协调器专项 pg-20260917-073528-e8111e2b：Windows 八组通过，733 份源码及 schema 工具 hash 已复核。
  198 条 Console SQL 已在新库生成离线元数据；申请去重、预算保留、租约过期、真实文件发布证明、原登录撤销、
  重启重新验证、节点/epoch 隔离及审计回滚通过。读取租约、保留/清理与产品 HTTP/WS 尚未交付，不能据此宣称 DB2 完成。
  PrepareQueries 首次 pg-20260917-073132-998258a6 因测试清单先于测试文件登记失败；补齐文件后
  pg-20260917-073348-410c3eed 生成检查通过，失败报告保留，不计入验收成功数。
- 共享私有文件/缓存 IO 完整报告 pg-20260917-070338-cbe8cd86 通过：427 个 PASS，Windows/Linux 各 190 项 Rust 测试，
  215 条 SQL、Auth/Desk 浏览器、三库恢复通过；685 份源码和 10 个最终工具（含仅测试用的缓存抢锁进程）hash 已核对。
  八组实际文件/跨进程用例在两平台执行；Auth 私有文件检查抽取后的行为回归通过。缓存数据库协调器及媒体接口仍未实现。
- 具名设置完整报告 pg-20260917-064614-34466d46 通过：410 个 PASS，Windows/Linux 各 182 项 Rust 测试，
  215 条 SQL（Console 177、Auth 29、Desk 9），三库恢复及浏览器回归通过；675 份源码和 8 个最终工具 hash 已核对。
  原始请求重试、跨 owner/client 隔离、128 活跃条目并发上限、CAS/删除墓碑、权限撤销、到期与审计回滚均覆盖。
  此报告之后正在实现共享私有文件/缓存 IO 库并抽取 Auth 权限检查；新文件层不在这份旧证据范围。
- 具名设置七组专项 pg-20260917-063715-f003a488 通过；随后新增过期登录断言的完整报告
  pg-20260917-063853-f902291e 未通过：测试只回拨 expires_at，触发 expires_at > created_at 的合法约束。
  已修正为同时回拨该合成会话 created_at，不改生产约束；修正后专项 pg-20260917-064532-cfcc06fc 七组通过，完整回归见上一条。
- 完整报告 pg-20260917-061716-591ca543 通过：Windows/Linux 各 174 项 Rust 测试、207 条 SQL
  （Console 169、Auth 29、Desk 9）、三库恢复和浏览器回归通过；655 份源码及 8 个最终工具 hash 已逐一核对。
  包含录像目录六组、十领域共享单池及内容 hash 契约；不包含物理缓存/媒体访问或 Console 产品接口切换。
- 录像目录六组 Windows 专项 pg-20260917-061459-ea532091 通过；独立文件版本 UUID/内容 hash、
  自主录制/可选精确会话归属、代际/序号、设备 ACL 与审计回滚已覆盖；后续完整跨平台报告见上一条。
  缓存 IO/清理/播放和真实节点生产者仍待实现，不把目录元数据当作可用媒体交付。

- pg-20260917-055925-f437e6a6：378 项 PASS，Windows/Linux 各 167 个 Rust 用例，201 条 SQL 在线/离线一致。
  文件传输七组、单元边界、九领域单池、前序回归与三库恢复通过；638 个源文件及两平台 8 个工具 hash 已复核。
  后续录像目录增量不在此报告范围；真实传输文件和产品协议尚未进行本轮端到端验证。

- 文件传输首批六组 Windows 专项 pg-20260917-055514-9ac7878f 通过。已实现明确 session/node/代际、单调幂等进度、
  完整字节/hash 的成功条件、去密查询和节点失联 Unknown；之后加入前端关闭联动及第七组用例，待新回归。
  七组新专项 pg-20260917-055745-8faf6db0 通过；完整 Windows/Linux/三库恢复回归正在执行。

- pg-20260917-054053-9eb21b04：361 项 PASS，Windows/Linux 各 159 个 Rust 用例，192 条 SQL 在线/离线一致。
  资源会话 10 组、单调时钟剩余租期与八领域共享池回归通过；三库恢复包含会话/事件/关闭 challenge 三张表，
  616 个源文件及两平台 8 个工具 hash 复核一致。接下来的文件传输增量不在这份报告范围。

- 资源会话首批 8 组 Windows 专项 pg-20260917-053421-ac4b260c 通过；仅为仓库专项，不包含完整回归。
  已加入独立 Desktop/CloudApplication 目标、短租约、精确前端关闭 challenge 和未知占用保留；
  后续追加观察者/排空/端点用例，完整 Windows/Linux 回归待执行。产品 Console/节点仍未接通。
- pg-20260917-053730-9dfd19a4 为 FAIL：追加的排空用例假定无变更 configure 会增加 revision，
  实际接口按幂等语义保持原 revision，导致测试 CAS 版本错误。修正测试使用原版本和 configure 返回的 revision，
  不修改业务幂等/CAS 规则；需要新报告完成重跑。
  pg-20260917-053855-405043ad 的 10 组专项已全部通过。之后补充了单调时钟剩余租期字段，继续新回归。

- 专项入口 `TestSuite` 已验证 unit、database、workspaces；报告明确 FOCUSED-ONLY，参数误用拒绝，不能代替完整回归。
  TCP 健康检查修正后，`pg-20260917-051345-1d606b48`（workspaces）、`pg-20260917-051412-19bdccfe`（unit）、
  `pg-20260917-051426-278b78b6`（database）连续全新库通过；独立 Node/OpenSSL AES-GCM 固定向量在 Windows 单元测试通过。
  随后的完整报告 `pg-20260917-051558-0c858bb1`：338 项 PASS，Windows/Linux 各 148 个 Rust 用例，175 条 SQL 检查通过；
  三库恢复、576 个源文件以及两平台 8 个工具 hash 已复核一致。独立 AES 固定向量也在 Linux 通过。
  后续资源会话增量不在本报告范围，不沿用该报告替代新实现的验收。
- `pg-20260917-051211-04e90caf` 为 FAIL：专项入口使缓存构建很快，暴露 Compose 健康检查的启动竞争。
  日志显示 socket-only 临时初始化进程先被标记 healthy，随后退出，正式进程才监听 TCP；原生 schema 工具在中间连接失败。
  已把 pg_isready 改为显式 TCP，避免临时 socket 被误认成可供宿主机连接的就绪服务；不增加固定 sleep 或掩盖失败重试。
- `pg-20260917-045706-407f08db`：336 项 PASS，Windows/Linux 各 147 个 Rust 用例；175 条 SQL 在线/离线一致。
  共享池七领域争用、严格两个实际连接、统一关闭/拒绝后续使用，以及三服务 owner/扩权 runtime 拒绝通过；
  Auth/Desk 原生进程实测 owner 连接串导致监听前退出。前序回归、三库恢复、576 个源文件与两平台 8 个工具 hash 复核通过。
  下一步的专项测试入口/独立 AES 固定向量不在本报告范围，不能沿用本报告覆盖后续更改。
- `pg-20260917-045235-20ab5307` 为 FAIL：新增共享池测试用 owner 的 `pg_stat_activity.wait_event` 观察 runtime，
  该字段对其他角色受限不可见，等待断言超时。已改为查看指定数据库/控制 gate 的实际 `pg_locks` 等待者；
  保留两个真实连接的硬断言，不增加监控角色权限、不放宽容量要求。以上新报告完成修复后的完整重跑。
- `pg-20260917-043919-f65b0451`：329 项 PASS，Windows/Linux 各 144 个 Rust 用例；175 条 SQL 在线/离线一致。
  工作区六组数据库、四组加密/SID 单元与前序回归、三库恢复通过；573 个源文件与两平台 8 个工具 hash 复核一致。
  共享连接池/业务 runtime-role 启动门禁是本报告之后的增量，不沿用本报告作为其验收证据。
- `pg-20260917-043743-c18173cb` 为 FAIL：Console 单元套件新增后运行器仍有一处旧摘要数量断言。
  已移除重复的摘要计数检查，统一使用逐测试名称/状态/数量及跨平台目录对照；上述新报告完成重跑。
- `pg-20260917-042540-89a488a9`：308 项 PASS，Windows/Linux 各 134 个 Rust 用例；166 条 SQL 在线/离线一致。
  命令领取/回执/Stop/完整清单 challenge，以及撤权时序、排空不强杀、端点切换、故障回滚 16 组用例通过。
  三库恢复和 549 个源文件、两平台 8 个工具制品 hash 复核通过；工作区增量在此之后，不沿用此报告作为其验收证据。
  节点仍是仓库测试适配器，未验证真实 Service fence、Windows Job 或 RDP OS 工作区保留。
- `pg-20260917-041351-a66131ce` 为 FAIL：两平台 Rust/网页功能通过，但运行器用相同测试数量的摘要出现次数判定 Linux 完整性，
  新增同数量测试套件后误报。已改成比较 Windows/Linux 的完整测试名称及重复次数，并保留逐项通过/总量门禁；以上新报告完成重跑。
- `pg-20260917-035027-9559afdb`：275 项 PASS，Windows/Linux 各 118 个 Rust 用例；145 条 SQL 在线/离线一致。
  新增原始会话/授权版本复合 FK、断开故障回滚、旧命令取消及未知占用保留；两平台回归和三库恢复通过，源码与制品 hash 复核一致。
  此报告仍不包含之后新增的命令 claim/ack/Stop 或完整清单对账；不把过往通过结果用于尚未执行的增量。
- `pg-20260917-033617-04a798a4`：271 项 PASS，Windows/Linux 各 116 个 Rust 用例；142 条 SQL 在线/离线一致。
  实例预约、两个真实进程争抢、命令/事件故障回滚、前序回归及三库恢复通过；源码和两平台制品 hash 已核对。
  命令派发/回执/重连对账尚未实现；此报告不能替代节点实际执行、Windows/Android 端到端或 DB4/DB5 验收。
- `pg-20260917-032141-df7dd583`：252 项 PASS，Windows/Linux 各 107 个 Rust 用例；137 条 SQL 在线/离线一致。
  部署六组数据库用例及路径/容量单元用例、前序回归和三库恢复通过；源码与两平台制品 hash 已再次复核。
  该结果不包括后续实例预约/命令，也不替代真实节点执行/总体验收。
- `pg-20260917-031024-5c4f0ce3`：237 项 PASS，Windows/Linux 各 100 个 Rust 用例；130 条 SQL 在线/离线一致。
  节点七组真实数据库测试和前序回归通过；节点四张表纳入三库逐行/约束/索引恢复检查。
  461 个登记源文件及 Windows/Linux 的四个工具制品 hash 已复核；没有沿用先前报告替代本轮测试。
  设备禁用/启用/删除同时使旧节点连接失效；本结果不代表真实节点长连接、应用部署/实例或 DB4/DB5 已验收。
- `pg-20260917-024933-413a28f8`：220 项 PASS，Windows/Linux 各 92 个 Rust 用例，共 184 个；117 条 SQL 在线/离线一致。
  来源阻止/到期、10 轮每轮 20 路签发竞争、故障完整回滚、去密管理分页通过；全部前序回归和三库恢复通过。
  该增量仍是 repository，不包括 HTTP 可信来源/HMAC 生成和边缘限流；后续入口须接通这些边界。
- `pg-20260917-023705-43947ba2`：212 项 PASS，Windows/Linux 各 88 个 Rust 用例，共 176 个；113 条 SQL 在线/离线一致。
  访客 5 项新增用例和前序回归、三库恢复通过；423 个源文件与 Windows/Linux 四个工具制品 hash 已逐项复核。
  来源阻止和访客管理查询是此报告之后的增量，不沿用此结果替代其回归。
- `pg-20260917-022506-c9531a43`：201 项 PASS，Windows/Linux 各 83 个 Rust 用例，共 166 个；103 条 SQL 在线/离线一致。
  设备 8 项、应用 8 项及应用字段 3 项新增用例和全部前序回归通过；三库数据/约束/索引恢复通过，设备名称约束问题已闭环。
  399 个登记源文件 hash 与报告一致；Windows/Linux 的 px_db/px_desk/px_auth/px_auth_admin 已复核，网页 hash 在报告留证。
  Console storage fmt/clippy（含所有 pg-integration 测试）通过。该报告仍不代表 Console HTTP/节点/客户端、DB4 或 DB5 已验收。
- `pg-20260917-015334-163c46b1`：161 项 PASS；Windows/Linux 各 64 个 Rust 用例，69 条 SQL 查询在线/离线一致。
  新增 8 个 Console 管理/事务/并发/故障用例在两平台均通过；Auth/Desk 网页及三库恢复回归通过。
  322 个登记源文件在整轮验收期间 hash 不变；设备 schema/repository 是此后的新增工作，不借用此报告作为验收结果。
- `pg-20260917-013337-f4ee588f`：143 项 PASS。Windows/Linux 各 56 个 Rust 用例，共 112 个；55 条 SQL 查询在线/离线一致；
  Desk 1 个及 Auth 5 个网页单元测试、Desk 5 项及 Auth 7 项浏览器/进程断言、三库数据/约束/索引恢复均通过。
  Auth 浏览器验证已提交响应丢失后仍使用同一 request_id，返回完全相同 wire；真实续期/撤销、只读拒绝、退出、重启/断库恢复均验证。
  冻结验收时 290 个源文件 hash、Windows/Linux 的 px_db/px_desk/px_auth/px_auth_admin 均与报告核对一致，网页逐文件 hash 留证。
  Auth/存储 clippy 无警告；正式打包脚本只做静态检查，没有正式发行/公网部署，也没有 Console/Android 总体验收。
  此后若改动源码或 schema，以后续新报告为准，不能沿用这次绿色结果。
- Auth 开发期的 SQL 宏类型错误、跨平台夹具固定 token 冲突、缺数据库身份夹具、网页控件标签及过快的 PG 恢复轮询均保留失败报告。
  分别修正查询读取、每轮随机 token、独立空库身份、可访问标签及有间隔的 30 秒恢复截止；没有放宽唯一约束/权限/就绪门禁。
- `pg-20260917-020818-5c532bda` 为 FAIL：设备增量及前序功能在 Windows/Linux 各 72 个 Rust 用例均通过，网页/进程回归通过，
  但最后设备名称 CHECK 的 BETWEEN 嵌套括号在恢复后重解析为等价扁平形式，约束文本精确比较失败。
  已改成显式 `>= AND <=`，保留精确恢复门禁；以之后重跑结果为准，不能把本轮宣称为整体通过。
- `pg-20260917-002642-3c65f131`：Windows/Linux 各 34 个 Rust 用例，共 68 个；26 条 SQLx 查询在线/离线一致；
  前端提交 ID 单元测试、5 项真实浏览器/进程故障恢复断言、三库内容/约束/索引恢复和清理均通过，报告共 88 项 PASS。
  Windows px_desk/px_db、Linux 两个工具及网页逐文件 hash 已记录；该次冻结时已核对源文件与报告 hash 一致。
  三个新/改造 crate 的 fmt/clippy 检查通过。Desk 发布打包入口仅修订/静态检查，未运行正式发行构建，不声称已发布到公网。
- Desk 开发期间的 SQL 拼接/类型检查失败、浏览器选择器失败、恢复约束表达式括号差异均保留失败报告。
  SQL 已改成文件宏编译检查；恢复不放宽校验，用等价的显式范围 CHECK 避免 BETWEEN 展开/重解析差异。
  幂等 ID 补齐成功后 reset，避免新填同正文被误判成上次重试；开发 PG 不打印约束错误的完整敏感行。

- `pg-20260916-233713-4810aa62`：Windows 和 Linux 各 28 个 Rust 测试（4 单元 + 12 PG + 12 身份/组），56 条逐用例记录，无忽略或跳过；
  17 条查询在线/离线一致；断库恢复、三库逻辑恢复冒烟及隔离清理通过。
  最终另核对代码与 Windows/Linux 工具 hash 一致；测试后的环境说明版本措辞修订仅按文档检查，不冒充重新构建。
- Windows 工具链 Rust 1.95.0；Ubuntu-20.04 WSL2 原生 Rust 1.91.1；Docker Linux PostgreSQL 18.6 固定 digest，具体 hash 在报告中。
- `cargo fmt --check`、两个新 crate 的 `cargo clippy --all-targets --features pg-integration -- -D warnings` 通过。
- `node docs/tests/server_topology.test.cjs` 通过：4 种应用、6 负载场景、7 容量边界、10 运维页、权限/过期/未知/空值门禁、
  私有 Auth 排除、3 种升级场景及 users>0 阻塞、中英文与 5 档宽度；没有向后端发网络请求。

完整 JSON 和逐命令日志保存在本机 `test-results/server_validation/<run_id>/`，被 Git 忽略；复现使用 `postgres.ps1 Test -Linux`。
后续代码或 schema 变更须新增报告，不能直接沿用上面的历史结果。网页验证只证明交互模型，不证明真实调度/运维服务。

2026-09-18 最新完整软件基线为 `pg-20260918-040750-839376a0`：712/712 项 PASS。Windows 与 WSL Linux
逐项测试目录一致；Console 237、Desk 9、Auth 30 条 SQLx 查询在线检查与离线元数据一致；三库协调备份、源目录失效后的
异地副本恢复、恢复封印、人工准入、最终三库恢复冒烟、数据库停机/重启、Auth 与 Desk 真实浏览器/进程流程、源文件 hash
冻结和隔离容器/卷清理全部通过。为防止 Windows 测试数据污染 Linux 结果，验收器在 Windows 测试前封存三库干净基线，
进入 WSL 前以正式库名重建三库；没有放宽生产写屏障只接受 `pixels_console`、`pixels_auth`、`pixels_desk` 的约束。

本轮同时适配了 `px_pixels` 新主页：公开咨询仍由浏览器真实提交；新版页面已经移除公开“提交工单”入口，因此验收明确改为
API 建单后由浏览器后台处理，不再把它标成公开页面提交。若产品要求恢复公开工单入口，应作为前端功能决策和独立浏览器门禁，
不能由数据库验收脚本伪装覆盖。Auth 浏览器夹具现在精确清理签名公钥和 trust store 两个私有文件，避免成功用例在清理阶段误报。
该报告仍是 DB0–DB5 的软件基线，不替代下表的产品切换、独立故障域和 Windows/Android 端到端出口。

Console 进程入口增量后的完整基线为 `pg-20260918-044756-0b10e35c`：715/715 项 PASS。新 PostgreSQL
Console 组合根现在有可执行目标，严格装载数据库、deployment、TLS/origin 和私有密钥配置，监听真实 TCP/TLS；收到
Ctrl+C 时有序关闭，数据库租约或权限丢失时停止监听并以非零状态退出。Windows 与 WSL2 都实际启动该进程、读取
`/health/ready`、终止它的 PostgreSQL runtime backend，并验证进程 fail-closed 且 stderr 不泄露测试口令。其余
Console/Auth/Desk、真实三库异地恢复、浏览器流程、SQLx 目录与源文件冻结继续通过。此入口当前仍名为开发期
`px_console_pg`，尚未替换正式 `px_console.exe` 的构建/安装包，也未托管 Console Web；不能据此提前关闭 DB1/DB2 出口。

独立初始化工具增量后的完整基线为 `pg-20260918-052007-6f92a001`：720/720 项 PASS。新增
`px_console_admin bootstrap` 只从私有密码文件读取首个管理员口令，只接受 Console owner 数据库身份，并在空库并发启动时
保证恰有一个进程成功；runtime 身份、第二次初始化和非空库均拒绝。`generate-secrets` 通过操作系统随机源生成互异的访客来源密钥
与工作区密钥，要求显式非 nil key ID、两个不同目标路径，使用私有文件权限且不覆盖既有文件。Windows 与 WSL2 均执行两项测试；
报告同时记录两平台 `px_console_admin` 摘要并复跑三服务、浏览器、异地恢复及源文件冻结门禁。

Console 静态 Web 托管增量后的完整基线为 `pg-20260918-054941-5105a25a`：724/724 项 PASS。进程配置现在必须
显式给出包含有效 `index.html` 的规范化静态目录；文件限制为 32 MiB，按后缀返回固定 MIME 和 `nosniff`，SPA 路径回退首页，
但未知 `/api`、`/health` 路径保持 404，不能用 HTML 伪装接口成功。相对路径、反斜杠、Windows ADS、父目录和越界 symlink
均拒绝。真实 Windows 进程测试读取首页、JS 资源和 SPA fallback，并验证退役 API 为 404；同一目录在 WSL2 原生执行，
其余数据库、浏览器、备份恢复及源码 hash 门禁全部继续通过。现有 `web/px_console` 仍调用旧 API，必须先按新 API 重写并做
真实浏览器验收，才允许正式构建/安装包切换。

Saved Connections 产品入口增量后的完整基线为 `pg-20260918-062037-b41e0a24`：726/726 项 PASS。Console 运行时现在
直接提供 `/api/console/saved-connections` 的创建、分页、读取、修改和删除接口，复用 PostgreSQL 存储层的 owner/client
隔离、严格目标类型、请求幂等、128 条活跃配额、乐观锁、软删除和审计事务；没有旧路径、设备/账号兜底或兼容转发。
真实 HTTP 用例以 Android 身份访问显式授权的桌面目标，并验证 ACL 修改使旧会话失效、重新登录后可创建、Android token
不能冒充 Panel、完全相同请求重试返回同一记录、旧 revision 删除被拒绝以及正确 revision 软删除。Windows 与 WSL2 的
六项目录 API 测试、其余存储/进程/浏览器用例、三库异地恢复、断库恢复和源码冻结均通过。该接口尚未接入 Android/Panel
界面，不能把 HTTP 与数据库验收写成客户端端到端完成。

本人资料与头像入口增量后的完整基线为 `pg-20260918-070557-a3f0ef49`：728/728 项 PASS。新接口为
`/api/console/profile` 与 `/api/console/profile/avatar`；用户名修改、头像上传和删除都要求当前 login 的精确 client type 与用户
revision，头像只接受 PNG/JPEG/WebP、最大 2 MiB，保存内容 SHA-256，读取返回 `nosniff` 与私有禁缓存响应。纯资料修改不提升
authorization revision，不会把正常会话误作权限撤销；每次实际改变写入独立 `profile_events`，事件失败时资料、头像和 revision
全部回滚。旧 `avatar_path` 已从全新 schema 删除，不导入文件路径或提供旧 `/api/v1` 转发。头像内容跟随 Console PostgreSQL
恢复集，Windows/WSL2 原生 HTTP 测试、242 条 Console SQLx 查询、三库异地恢复、断库恢复及源码冻结均通过。Android、Panel
和 Console Web 尚未消费这些接口，故客户端界面与端到端仍属于 DB5。

更新目录产品入口增量后的完整基线为 `pg-20260918-074115-43965960`：730/730 项 PASS。Console 运行时现在提供
`/api/console/managed/updates` 管理接口和 `/api/console/updates/latest` 客户端查询接口；发布登记具备请求幂等，审批与撤回使用
revision 乐观锁，未审批及已撤回制品不会被客户端发现。客户端查询同时严格匹配 token 中的 client type 和完整的 product、
distribution、channel、OS、architecture 五维目标，Android 身份不能冒充 Panel。Windows 与 WSL2 的七项目录 API 测试、其余
存储/进程/浏览器用例、三库恢复、断库恢复和源码冻结均通过。此增量只交付 PostgreSQL 更新元数据入口；Cloud Node、Service、
Render 等无人值守组件的独立服务身份，以及制品下载、签名验证、排空、安装、回滚和热升级执行器仍未交付。

历史记录产品入口增量后的完整基线为 `pg-20260918-092556-79a4d8c4`：730/730 项 PASS。Console 运行时新增本人/访客和
管理员分离的访问记录、连接通道、文件传输历史接口，以及本人可见/管理员管理的录像目录接口；所有分页和筛选参数严格解析，
本人入口要求显式 `x-pixels-subject-kind`，不会从 token 类型猜测主体，访客也不能借录像入口退回用户设备权限。真实节点
WebSocket 流程完成节点认证、部署准备、CloudApplication 实例启动和 Android 用户会话后，验证用户只能读取自己的访问记录、
管理员能读取管理视图、Android 身份不能进入管理入口。因主工作区的 `px_pixels` 同时在编辑，本轮从字节一致的后端源码和当时
前端工作树创建独立冻结快照；Windows、WSL2、`px_pixels` 构建、Auth/Desk 浏览器与进程、断库恢复、三库恢复冒烟及快照源码
hash 门禁全部通过，报告已复制回本项目标准测试结果目录。该增量只提供目录和历史元数据读取；节点协议随后已补齐通道、传输和录像
上报 DTO/Console 处理，但 Service/Render 的实际生产者、真实文件字节传输、录像下载与媒体执行链仍未交付。

Render 前端准入纵向链已经接通：非桌面 Render 在接受 `/alloc/local/rtc` 前必须携带明确的 session UUID、正数 revision 和一次性
frontend token，经本机 Service IPC 转发到当前受认证 Console 节点长连接的 `admit_frontend`；Service 不缓存明文 token，转发和 IPC
耗时从 Console 返回的短租约中扣除。Render 只接受绑定当前 instance、`cloud_application` 目标以及 controller/observer 角色的授权，
节点连接异常、超时、响应错配和业务拒绝均 fail-closed，云应用模式不再退回设备密码。桌面模式的既有入口保持原行为。
`service_core` 86 项（另 1 项既有真实 UE 样本 ignored）、`px_service` 75 项、Render RPC 状态测试及 C++ 所有权门禁通过；Cloud/Remote
Service 与 Render 均用聚焦构建入口发布到各自 `build_official/<product>/dist` 并逐件核对 SHA-256。该证据覆盖真实
Render→Service→Console 授权转发和断线/迟到响应，不等于真实公网客户端已完成首帧、输入、音频、文件或录像功能验收。

资源通道生命周期的首条真实生产链已经接通。Render 在已准入逻辑会话发生实际 WebRTC/RDP connected 后创建独立 source UUID，
通过本机 Service IPC 和受认证 Console 节点长连接执行 `open_channel`；真实 disconnect 产生 `peer_closed` 终态。SDP/端口分配不会提前
写入 connected 记录，非规范 session UUID 不回退设备/账号标识，disconnect-before-open 会等精确 channel ID 返回后再关闭；Service
断线会失败所有待处理请求并拒绝迟到回执。`px_service` 77/77、`service_core` 88/88（另 1 项既有真实 UE 样本 ignored）及 Render RPC
测试通过；Cloud Node 的 `px_service.exe` SHA-256 为 `7BEEFC2C996FC7451FCC7569194E017B19B08EAAE4E45ECCEC84B72A4AA7DCB8`，
重新聚焦构建并同步后的 `px_render.exe` SHA-256 为 `0F643D9999CD5BE50EBFBE644D9C2C4E80954A48796A509BB58F6174B0FB92A1`，
构建树与 `build_official/cloud_node/dist` 已逐件核对。后续 DB2 增量已在这条生命周期上加入单调 sequence、5 秒周期进度和累计
`sent_bytes`/`received_bytes`：Direct Host WebRTC 的成功编码视频、成功数据通道发送以及收到的数据通道载荷会先按真实 connection ID
汇总，再经既有 Render→Service→Console 通道上报；disconnect 终态携带最后累计值，计数采用饱和加法，不会回绕。这里的“字节”定义为
传输载荷字节，不包含 IP/UDP/DTLS 等协议头。Cloud Node 的 `px_render.exe` / `px_render_rtc.dll` SHA-256 分别为
`8D5165F25A53F7DAE749CD58143FC2982B7B3D10C5D0348425491B6FF764E5DF` /
`A829B2AE52F233E90C50361B8D7D53E2161E9DC46EBF474673A97F20250B7494`，Remote 对应为
`29E3DD4E123E8942B7C784AEF719FC8D555E9AF14D7A3C8F43D8C1C302B975A9` /
`590B0AFABA6929FCE8714F50A2AACEA8E90FBE55FA76FDA88FFA7F3CFA5206F0`；两套构建树与各自 dist 哈希一致，RTC 聚焦测试 3/3 通过。
Relay 的出站统计也已从“进入发送队列”收紧为“底层 WebSocket 完整写成功”：媒体与文件数据按活跃 room 映射到真实 connection ID，
只有异步发送成功且写入长度完整时才累计原始业务载荷字节；连接已失效、短写或发送错误均不计入。真实本机 WebSocket 写入回调用例及
Relay 重连/所有者生命周期用例 2/2 通过。重新发布后 Cloud Node 的 `px_render.exe` SHA-256 为
`CB35F11B7DB71DCCAD0A751CBD728B025BDBF2E89C5E61EFFF3FD931D6C4F4A2`，Remote 为
`D17E105D479465C3F4B2736603347B57925A9065871A8C7FDC1B7BE9B3D4C283`，构建树与各自 dist 一致。上述证据只覆盖 Relay 出站载荷
生产者和本机真实套接字，不替代公网 Relay 端到端回归。Relay 入站/音频、RDP、Native 的完整双向计数、真实文件内容和公网客户端可见
结果仍是后续出口，不能据此关闭 DB2 或 DB5。

文件传输真实生产链随后接入。Render 文件引擎为操作生成独立 UUID，以真实递归文件列表计算发送总量，按实际未压缩完成字节上报进度，
并在成功终态提交单文件 SHA-256 或确定性多文件清单摘要；Service 通过当前节点控制长连接转发 begin/report，Console 对未知预声明摘要只在
成功事务中一次性绑定实际接收摘要。Render 真实 payload/摘要单元测试、Service 本机真实 WebSocket 桥和 `service_core`/`px_service`
完整测试通过；PostgreSQL 专项 `pg-20260919-153735-028dba2f` 为 8/8 PASS。聚焦构建并发布后，Cloud Node/Remote 的
`px_service.exe` SHA-256 均为 `E00320B8F8161210FDD4E686C75FB0C5E0738FC4654BF31D5FACDBD90A21A7B2`；Cloud Node 的
`px_render.exe` / `px_render_rtc.dll` 分别为 `5BF56D41DFA9B05D2D9FC5915CAAC0805BF0F414A5DB6227594EEDD2D307552E` /
`6BA370CAAB693061EA3414DF6EE991356A0E7547EB863454BF12BB7EB8696EAD`，Remote 对应为
`857DE8A433E6014DF81855A1BF9B1E06BB631D245FEC9623C7C65DF08A3716E1` /
`B5C4577EABE28AA15BC6B444511A5560CF91D6764705AB3FBEB711BD0B6141EF`；构建树与各自 dist 逐件一致。该切片尚未证明公网双端独立
文件 hash、客户端历史、取消/重试或断线补报，仍不关闭 DB2/DB5。

新 Console 的本人实例列表入口已补齐：`GET /api/console/instances` 只接受显式 `user|guest` 主体和与登录会话一致的 client type，
按实例 UUID 稳定游标分页，页大小固定为 1–100；它不探测另一类 token，也不返回其他用户或访客的实例。历史实例继续按创建时的
owner 归属读取，不因应用 ACL 后续变化而使本人失去停止结果/历史可见性；单实例读取和新启动仍执行当前应用可见性检查。
存储专项 `pg-20260918-145743-13ec61c2` 的 11/11 项和 HTTP 目录专项 `pg-20260918-145847-66e24292` 的 7/7 项通过，
包含双用户/访客隔离、两页连续性、非法分页、错误主体和空列表入口；243 条 Console SQLx 离线元数据已由真实 PostgreSQL 重新生成。
这些是聚焦后端证据，不包含正在并行修改的 `px_pixels`，也不冒充 Console Web/Android 已经消费该入口。

Console 用户门户的首个新 API 纵向切片已完成源码切换：`web/px_console/src/user` 不再调用 `/api/v1`、Cookie 或 CSRF，登录和注册使用
无授权头的 `user_web` 请求，成功登录的 bearer 只保存在当前 tab 的 `sessionStorage`；用户与访客资源请求分别固定显式
`X-Pixels-Subject-Kind: user|guest`，不会互相试探 token。设备、应用、本人实例、资料/头像、密码、启动/停止和资源会话均改用
`/api/console` 类型化契约；CloudApplication 连接必须依次取得 application、instance、resource session 和 descriptor。
一次性 frontend token 只放在 launch URL fragment，不进入初始页面请求；Web Client 读取后立即从地址栏移除，并只在 Render
`/alloc/local/rtc` 准入请求中发送 session ID、revision 和 token，不生成设备密码兜底。另以当前 tab 创建标记区分同账号其他登录创建的
实例，其他登录实例可见、可停止，但 UI 不把它冒充可重连实例。Console Web 21/21、Web Client 65/65 加 19 项语音断言、两套类型检查与
生产构建均通过；真实 PostgreSQL HTTP 专项 `pg-20260918-152258-254e18d1` 的 7/7 项同时覆盖 `user_web` 用户/访客目录与显式主体。
完整门禁已新增两项目的 source freeze、依赖树、构建与测试检查。管理后台后续已完成身份、用户/组、设备、应用、节点、部署、
资源会话与活动审计的 `/api/console` 源码切换；管理员 bearer 仅保存在当前 tab，不再使用 Cookie/CSRF 或旧 `/api/v1`。旧管理
WebSocket、设备密码/链接 DTO、旧调度器、录像下载 ticket、视频墙/直播及浏览器 RTC/TURN 配置页面已从管理路由和源码移除。
本段记录当时的迁移判断；2026-09-19的新决定进一步将ZLMediaKit直播与Coturn/TURN明确退役，视频墙延期且未来只能使用多个
Direct Host observer session。当前仍只交付录像元数据，尚无新录像读取/下载链。管理页面不会请求 descriptor、frontend secret
或另行签发短期 ticket。桌面连接和
CloudApplication 的 30 秒授权续租纵向链路已接通：PostgreSQL 仅在完整在线复核成功的事务内推进租约，Render 使用原
frontend token 周期复核，不签发新的短期 ticket；本地硬截止、精确 RTC allocation 撤销和逻辑绑定关闭已实现。专项数据库报告
`pg-20260918-164447-29ef5736` 为 10/10，逻辑会话测试 25/25、Render 能力注入/弱生命周期测试 5/5，增量 Render/RTC 构建及
`build_official/cloud_node/dist` 哈希同步通过。公网首帧/输入/音频、持续续租、在线撤销、Console 中断及 Android 真机仍待端到端验收，
不能据此宣告 DB2-A 或 DB5 完成。

管理端历史能力已建立独立的[功能守恒清单](console_management_feature_parity.md)，以稳定 ID 逐项记录 DTO、WebSocket、录像读写、
视频墙、直播、RTC/TURN、硬件事件、许可证和文件传输的迁移状态、目标归属与验收条件。稳定ID继续存在以记录“延期”或“明确退役”，
不能通过删除条目隐藏决定；当前边界以[Direct Host专项计划](direct_host_webrtc_scope_plan_20260919.md)为准。完整PostgreSQL门禁会检查这些ID全部存在；
删除页面或旧源码不能删除待办。旧实现只在提交 `519be7d85` 中作为行为取证，不构成兼容层或恢复旧 `/api/v1` 的依据。

此前完整软件基线 `pg-20260918-181943-2051a284` 在 revision `bef878720` 上为 736/736 项 PASS。它包含 frontend
授权续租、硬截止和精确 RTC 撤销，以及 Console 管理端新 API/UI 和 16 类历史功能守恒门禁；从三套空 PostgreSQL
数据库开始，覆盖 Windows 与 WSL Linux 的 Rust 单元/集成测试、Console 243/Desk 9/Auth 30 条 SQLx 在线/离线一致性、四套 Web
项目依赖树与生产构建、Console Web 29 项、Web Client 65 项加 19 项语音断言、Auth/Desk 真实浏览器与断库恢复、三库协调备份、
异机副本恢复和最终恢复冒烟。1048 个登记源文件在整轮中 SHA-256 不变，Windows/Linux 工具摘要均已记录，隔离容器和卷已清理。
该报告取代此前完整软件基线；`pg-20260918-180450-f2c3a183` 的失败只证明旧的 21 项 Console Web 数量门禁正确拒绝了新增测试，
其后已更新为 29 项并由本报告通过。软件门禁通过仍不等于下列正式制品、公网 Windows/Android 或独立灾备故障域出口完成。

Console 管理端真实浏览器聚焦验收 `pg-20260918-193740-f5bcdc38` 的 7/7 项通过。验收启动实际
`px_console_pg.exe`、实际 PostgreSQL 隔离库和 `web/px_console` 生产构建，以 Chromium 完成管理员登录、资源总览、用户/组创建、
一次性设备注册凭据、中英文切换、明暗主题、Console 进程重启后的持久会话重验，以及退出后 bearer 撤销；页面没有外部请求，
浏览器错误为空，日志不包含测试口令。数据库停机时 Console 按既定 fail-closed 语义退出，数据库恢复后由监督者语义重启进程，
原持久会话与新建数据重新验证成功。该验收发现真实浏览器的同源 `GET` 通常不带 `Origin`；Ingress 现仅在唯一
`Sec-Fetch-Site: same-origin` 且唯一 `Sec-Fetch-Mode: cors` 时接受无 `Origin` 的 Web 请求，跨站、上下文缺失、Panel/Android
规则均未放宽。Console Web 本批单元测试增至 31/31，增加中英文目录键完全一致及全部管理导航本地化门禁。
这是一轮未提交 revision 上的聚焦报告；它不替代上述完整跨平台基线；其中视频墙/直播、RTC/TURN是报告生成时尚未收敛的范围，
正式产品安装包或公网 Windows/Android 缺口标为完成。首次全量接入报告 `pg-20260918-192554-83905015` 保留为 FAIL：
浏览器夹具误从已被前序测试写入的主 Console 库克隆，首个管理员初始化因此按设计拒绝非空库。修复没有清数据或放宽初始化门禁，
而是在验收开始时建立专用空 schema 模板，每轮浏览器测试只从该模板派生一次性数据库；修复后的完整 `Test -Linux` 结果见下一段。

最新完整软件基线 `pg-20260918-193943-ec79e930` 在 revision `8a4a63f08` 上为 743/743 项 PASS、0 FAIL。
Windows 与 WSL Linux 各 338 个 Rust 用例目录逐项一致；Console 243、Desk 9、Auth 30 条 SQLx 查询在线/离线一致，四套 Web
生产构建和测试、Console/Auth/Desk 真实浏览器、数据库失联与恢复、三库协调备份/异机副本/全新库恢复、源码冻结和隔离资源清理
全部通过。新增七项 Console 证据覆盖登录与资源总览、用户/组创建、一次性设备注册凭据、中英文和明暗主题、进程重启后的会话与
数据保持、数据库 authority 丢失后的 terminal fail-closed/监督者重启恢复，以及退出撤销。1050 个登记源文件在整轮中 SHA-256
不变，20 组工具/Web 制品摘要已记录。该报告取代 `pg-20260918-181943-2051a284` 作为软件基线；仍不替代正式产品二进制/安装包、
节点遥测与管理实时流、录像实际字节链、当时尚未收敛的视频墙/直播与私有RTC/TURN、独立灾备故障域或公网Windows/Android产品出口。

节点 latest 遥测第一切片已提交到 revision `27afde21e00a8d4bd539b4b99338ac27c17c04a0`。Windows Service 每次节点报告前通过真实 WMI 采集 CPU、逻辑处理器、内存、
固定磁盘及 GPU 身份/名称；本机 79/79 个 `px_service` 测试（包含真实采样）与全 target Clippy `-D warnings` 通过。节点协议要求显式
telemetry，不接受缺字段旧报告；Console 在当前 generation/sequence 门禁内原子写入 `node_telemetry_latest` 并替换同一
inventory revision 的 `node_gpu_latest`，管理 API 和中英文节点详情显示最新值、状态和采样时间。真实 PostgreSQL 专项
`pg-20260918-204609-08e5c0c9` 为 nodes 8/8，`pg-20260918-204814-bc41f36c` 为 node-control 1/1；迁移/权限/恢复水位专项
`pg-20260918-204609-ad4b5c70` 为 14/14，备份核心专项 `pg-20260918-204814-83185236` 为 61/61；SQLx 248 条元数据从迁移 0024
的新空库重新生成，报告为 `pg-20260918-204516-c4282129`。Console Web 类型检查及 32/32 合同测试通过。WMI 当前未提供可信的
逐 GPU 利用率、显存和编码器压力，这些字段明确为 NULL；历史趋势、阈值事件、断线补报、管理实时流、调度硬过滤及公网节点验收
仍未完成，不能把 latest 快照写成 CM-EVENT、CM-REALTIME 或 P3 调度完成。

本切片提交后的完整跨平台门禁 `pg-20260918-205048-9fde7634` 已通过：745/745 项 PASS、0 FAIL；Windows 与 WSL Linux
服务器测试、248 条 Console SQLx、Desk/Auth SQLx、四套 Web 生产构建、Console Web 32 项、Console/Auth/Desk 真实浏览器、
数据库 fail-closed/恢复和三库备份恢复全部通过。1061 个登记源文件在整轮中 SHA-256 不变，20 组工具/Web 制品摘要已记录，
隔离资源已清理。报告自身的 scope 仍明确为“完整 DB0–DB5 验收未完成”，不得借本轮通过关闭下表的阶段出口。

Cloud Node 与 Remote 的产品专属 Rust release 目录已分别重编译。Windows Rust 目标通过 `/Brepro` 消除 CodeView PDB GUID 的
随机差异后，两套独立构建的 `px_service.exe` 在 build、stage、dist 三层 SHA-256 均为
`527A5167277741C221765CD9070AB48634BB541C371366FDA3E42D7DBA5F62F2`，两份 dist 二进制的 `--help` 冒烟均通过。
这只证明本次 Service 聚焦制品同步正确，不会重写完整产品清单：当前 Cloud Node dist 中既有 `px_client.exe`、`px_render.exe`
以及 Remote dist 中既有 `px_render.exe` 与各自旧清单不一致，完整 dist 校验按设计失败，故公网发布器没有绕过预检部署。
这些既有整包不一致必须由下一次获准的正式完整产品构建从干净沙箱重新收集和签章；本次不擅自认可或覆盖无关制品。

节点遥测第二切片已在 revision `1a3926dca` 建立原始历史事实链。迁移 0025 新增 `node_telemetry_history` 和 `node_gpu_history`；
每个通过 generation/sequence 门禁的报告在 latest 更新同一事务追加机器与 GPU 样本，GPU 以复合外键绑定确切报告。管理 API
`/api/console/managed/nodes/{id}/telemetry` 使用 `received_at + node_generation + report_sequence` 完整游标，管理员节点页面显示最近
100 条原始样本；未知值仍保持 NULL。Console 的独立保留任务每分钟最多删除 5000 条超过 7 天的机器样本并级联 GPU 历史，任务与
1 秒 authority 租约续期分离，失败告警后重试；runtime 只有 SELECT/INSERT/DELETE，不获得可篡改历史的 UPDATE 权限。

本切片最终 SQLx PrepareQueries 报告 `pg-20260918-220247-cf897d43` 从迁移 0025 新空库生成 253 条 Console 元数据；节点存储
8/8 报告 `pg-20260918-220325-6e3728d4` 覆盖复合游标、代际隔离、7 天删除及 GPU 级联；真实 WebSocket/API 1/1 报告
`pg-20260918-220524-631c5b60` 覆盖报告后管理历史读取及不完整游标拒绝。迁移权限/恢复水位 14/14 报告
`pg-20260918-215722-736528f6`、备份核心 61/61 报告 `pg-20260918-215824-5a238653` 均通过；前端类型检查、33/33 合同测试、
生产构建以及 storage/runtime 全 target Clippy `-D warnings` 通过。首次保留测试因 `FOR UPDATE` 隐式要求 UPDATE 权限而失败，修复选择
了不扩大权限的受控 DELETE，失败报告保留。趋势聚合、阈值事件、断线补报、管理实时流和公网产品验收仍未完成。

提交 `1a3926dca` 后首次完整门禁 `pg-20260918-221111-6fc19f1b` 在真实三库异机恢复校验处按设计失败：恢复后的 Console
明确返回 25 条成功迁移，但 `px_backup` 测试夹具仍固定预期 24。没有放宽恢复核验；夹具和固定工具单元样本升为当前全新 schema 的
25，真实异机恢复专项 `pg-20260918-221251-08565558` 随后通过。失败报告继续保留，不作为通过证据；修复后的完整跨平台结果见后续报告。

第二次完整门禁 `pg-20260918-221418-2c700604` 又由同一固定工具单元夹具中的两处调用参数正确拒绝：伪造恢复输出已为 25，
但验证器入参仍传 24，导致备份核心 51 项中 50 项通过、1 项失败。两处调用统一为 25 后必须先通过完整备份核心和真实恢复专项，
再重跑跨平台总门禁；该失败同样保留且不作为通过证据。修正后备份核心 61/61 报告
`pg-20260918-221553-67cbf53d` 通过，真实恢复专项继续引用同一 schema 下已经通过的 `pg-20260918-221251-08565558`。

修正后的完整跨平台门禁 `pg-20260918-221714-7b1f394b` 已在 revision
`636e9c2e6cce4793cb0855a15f82e63dc0cca130` 通过：745/745 项 PASS、0 FAIL。Windows 与 WSL Linux
服务器测试、253 条 Console SQLx、Desk/Auth SQLx、四套 Web 生产构建、Console Web 33 项、Console/Auth/Desk 真实浏览器、
数据库失联/恢复、三库协调备份与全新库恢复均通过；1073 个登记源文件在整轮中 SHA-256 不变，20 组工具/Web 制品摘要已记录，
隔离测试资源已清理。报告 scope 仍明确标注“完整 DB0–DB5 验收未完成”；本结果关闭节点原始历史切片及其回归问题，不能关闭阈值事件、
管理实时流、公网产品部署或下表其余阶段出口。

节点遥测第三切片已在 revision `b8d8b5243` 建立阈值事件闭环。迁移 0026 新增每节点告警策略、条件去重状态、事件、策略审计和确认审计；
默认 CPU/内存/固定磁盘 warning/critical 为 850/950‰，GPU 为 900/980‰，连续 3 个已接受样本越线才开立事件，低于 warning 减
50‰ 的回滞边界连续 3 个样本才恢复。事件与 latest/history 在节点报告事务中一起提交，支持 warning→critical 单向升级、active→
acknowledged→recovered 生命周期、CAS 确认和每节点策略变更；缺失指标不会伪装为 0 或恢复。同一节点/指标/资源只允许一个未恢复事件，
恢复事件保留 180 天后每分钟最多清理 5000 条，活动/已确认事件不会自动删除。管理 API 提供精确筛选、完整 `(updated_at,id)` 游标、
详情、确认和策略读写；新中英文“遥测告警”页面提供筛选、详情、确认及节点策略编辑，viewer 只读、admin 写。

专项证据：SQLx PrepareQueries `pg-20260918-233028-2f642822` 从迁移 0026 新空库生成 267 条 Console 元数据；节点存储
`pg-20260918-232838-eb11cef0` 为 9/9，覆盖连续越线、严重度升级、确认、缺失值不恢复、连续恢复、策略 CAS、审计和 180 天清理；
真实节点 WebSocket/API `pg-20260918-232613-0975e2d7` 为 1/1；PostgreSQL 权限/恢复水位
`pg-20260918-231603-486ca545` 为 14/14，备份核心 `pg-20260918-231703-5660f61b` 为 61/61。Console Web 类型检查、
36/36 合同测试和生产构建通过。首次完整门禁 `pg-20260918-233200-aabe4d82` 在新 36 项前端测试全部执行后，被仍固定为 33 的显式
计数门禁正确拒绝；revision `3553d1ad4` 只把预期计数升为 36，没有降低检查，失败报告保留。

修正后的完整跨平台门禁 `pg-20260918-234731-71bdfe0d` 已在 revision
`3553d1ad491c3add3120f9c476eebae4edebad4b` 通过：747/747 项 PASS、0 FAIL。Windows 与 WSL Linux、267 条 Console
SQLx、Desk/Auth SQLx、四套 Web 生产构建、Console Web 36 项、Console/Auth/Desk 真实浏览器、数据库失联/恢复和三库协调备份/
全新库恢复均通过；1108 个登记源文件在整轮中 SHA-256 不变，20 组工具/Web 制品摘要已记录，隔离资源已清理。报告 scope 仍明确
标注“完整 DB0–DB5 验收未完成”。本结果只把 CM-EVENT 提升为部分迁移；告警页真实浏览器流程、断库期间节点补报、可信逐 GPU 指标、
趋势聚合、管理实时流和公网节点验证尚未完成。

### 2026-09-19 媒体范围收敛（聚焦实现与构建已通过）

用户已决定保留Relay名称、协议、路由和现有非WebRTC数据转发，只移除经Relay中转的WebRTC SDP/ICE；Windows Client、Web Client
及适用Android流程的WebRTC只允许直接连接节点当前上报并由实例ACK确认的Render host/port。活动代码已经移除并归档ZLMediaKit、
Coturn/TURN、中央推流、中央RTC signaling、remote RTC DLL和对应构建/打包入口；91个共享文件改前快照具有逐文件SHA-256。
Rust相关workspace检查、现代/旧Web Client、Cloud Node/Remote Render和Windows Client聚焦构建与单元测试均通过，运行制品与各自
`build_official/<product>/dist`副本哈希一致。精确清单、制品哈希和仍待公网/Android验证的边界见
[Direct Host WebRTC 与中央媒体能力收缩计划](direct_host_webrtc_scope_plan_20260919.md#8-实施与验收记录)。本页747/747报告仍发生在
代码清理之前，不能作为新变更的完整PostgreSQL回归或DB5跨端验收证据。

媒体清理后的完整合成基线已由 `pg-20260919-025221-0599733d` 重新建立：747/747 项 PASS、0 FAIL。该轮从三套空
PostgreSQL 数据库开始，覆盖 Windows 与 WSL Linux Rust 单元/集成测试、Console 267/Desk 9/Auth 30 条 SQLx 在线/离线一致性、
四套 Web 生产构建、Console Web 36 项、Web Client 59 项 Direct Host/描述符/媒体/控制合同加 19 项语音断言、Console/Auth/Desk
真实浏览器、数据库 authority 丢失与恢复、三库协调备份/异机副本/全新库恢复、源码冻结及隔离资源清理。首次报告
`pg-20260919-023743-0bf6bb97` 保留为 FAIL：它正确发现总门禁仍要求已归档中央 signaling 测试形成的旧 65 项计数；修复没有恢复
退役能力，而是新增 4 项 Direct Host 策略测试并把当前门禁固定为 59 项。该基线关闭 DB0 本轮合成基线出口，但不替代正式制品、
公网 Windows/Web/Android、真实 Relay 数据转发或最终 DB5 长测。

Console 正式产品入口已在 2026-09-19 切换到 PostgreSQL 组合根：正式二进制名为 `px_console.exe`。初始切换版本为
`3.2.19`，录像缓存下载增量为 `3.2.20`，节点生产/上传增量已独立提升为 `3.2.21`；当前发行
`output/px_console/releases/20260919-044355-633fb95a` 包含 Console、管理员工具、schema 工具、当前 Web
静态资源和运行文档，11 个受管文件逐项 SHA-256 复核一致；不携带配置、私钥、证书、口令、旧 TOML、Mongo/Redis 或已退役
媒体 sidecar。聚焦开发构建及 Web 单独构建也分别同步到 `output/px_console/dev` 并通过源/目标哈希检查。真实进程断库
fail-closed 专项最新报告 `pg-20260919-034035-1b14fc6c` 和 Chromium 管理员登录/目录/用户组/静态资源专项
`pg-20260919-033006-bb1ca5b5` 均通过。因此 DB1-EXIT 的 Console 正式产品切换已完成；这仍不替代 DB2 客户端全链路。

DB2-A 录像读取增量已把部署绑定的私有缓存作为正式 Console 必填配置，新增不覆盖的初始化管理命令、本人/管理员缓存请求、单 Range
授权下载、有界 64 KiB 流、慢接收读租约续期及管理页面下载动作。缓存协调器专项
`pg-20260919-040523-43626a62` 为 16/16 PASS；Console admin 为 3/3、进程断库为 1/1，Web 合同为 37/37。Render 的
Direct Host 观察者首个逻辑会话现在只在认证权限明确包含 `input` 时允许输入；聚焦 CTest 1/1、所有权/150 列门禁通过，Cloud Node
构建树与 dist 的 `px_render.exe` SHA-256 同为 `B334B348C84A5854B517A95E32E378D15685A520E1EAC93B57145D45790B1554`。
节点生产增量随后新增控制面 `poll_recording_cache` 和独立 HTTP 数据面：一次性上传能力绑定当前 generation、attempt、source UUID、大小与
SHA-256；Console 在正文前复核节点权威并有界流式落盘。Windows Service 原子持久化完成事件登记的
source identity/session/codec/sequence/present，跨重连重报并显式报告文件消失。真实 PG+WS+HTTP 上传+令牌重放拒绝+Range 下载专项
`pg-20260919-043253-aa62e48a` 为 1/1。

随后 Render 完成段链已关闭 session 归属缺口：WS、Relay 与 Direct Host ingress 都把既有逻辑 session 送入录像 sink；单 owner 段携带
resource-session UUID，不同/未知 owner 段永久降级为 null。MP4 只有完成 trailer/flush/close 后才通过 bearer 鉴权的本机类型化 IPC 登记；
Service 忽略目录裸文件，严格复核 basename、regular/reparse、大小、hash 和 `.recording` 标记，并对相同事件幂等、冲突事件拒绝。
`service_core` 为 88/89 PASS（1 项真实 UE fixture 按设计 ignored），`px_service` 为 83/83 PASS，录像 CTest 为 2/2 PASS。Cloud Node 与
Remote 的 stage/dist `px_service.exe` SHA-256 均为
`DF0CACA9FE51040287EE1CCE608EE7499267E9A0A0583B85F2D1298A9D9F8107`；Cloud Node Render build/dist SHA-256 均为
`6BAED2CB2A8A0106666B781DD93949A573F87274A5AF3B861C8D4754EA89F2E9`。这些证据关闭通用录像字节链和唯一会话归属链；本人客户端下载页面
与公网真实录制仍未完成，不能宣称 DB2-A 客户端全链路完成。

Console 本人录像入口随后完成：`GET /api/console/recordings` 不再接收浏览器提供的 node authority，只返回当前用户拥有的精确
resource session 所关联录像；`/user/recordings` 提供中英文列表、搜索、B/KB/MB/GB/TB 大小格式化、缓存申请与 Blob 下载。缓存授权在
申请、打开和续租时接受“精确会话 owner”或“当前设备 ACL”之一，绝不把 CloudApplication 权限扩大为整台 Render 录像浏览权。
无归属录像仍只进入管理员链。录像专项 `pg-20260919-053513-42c61f3a` 为 6/6，缓存专项
`pg-20260919-053959-4d73a02a` 为 17/17，目录 HTTP 专项 `pg-20260919-054108-fa248d1f` 为 7/7；Console Web 类型检查及 39/39
合同测试通过。真实 Chromium + Console 进程 + 隔离 PostgreSQL 聚焦验收 `pg-20260919-055719-489784b3` 进一步覆盖普通用户登录、
“我的录像”导航和空 owner 目录；同时补齐测试 Console 的私有缓存初始化/启动配置。当时尚缺公网真实录像/有数据下载及保留/驱逐管理
入口；后者由下一增量关闭，CM-RECORDING 仍因公网有数据流程保持“部分迁移”，DB2/DB5 不关闭。

管理员缓存管理闭环随后完成：管理 API/UI 可以分页查看缓存状态，以 revision CAS 保留或取消保留 Console 副本，并在取得物理独占锁后
驱逐精确 blob；它不删除节点原录像。pinned、旧 revision、活跃读取或物理锁冲突均拒绝。真实 PG + 节点 WS + HTTP 专项
`pg-20260919-061208-281ccc68` 为 1/1，覆盖上传、Range 下载、缓存列表、保留、pinned 拒绝、释放、驱逐、物理删除及删除后下载拒绝；
Console Web 类型检查与 40/40 合同测试通过。CM-RECORDING 余项收敛为公网真实录像的有数据浏览器下载/管理动作，不再包含未接 API/UI。

2026-09-19 公网真实录像短测关闭了本人有数据流程。Render 录像完成事件改用标准 UUID，避免 Service 对短 UUID 正确执行
`INVALID_EVENT_ID` 拒绝；Service 的 HTTP 数据面与控制面现在统一使用系统原生根证书，私有部署 CA 不再只对节点 WSS 生效而在录像上传时
失败。公网 WebView CloudApplication 会话 `af14cb21-b1ed-4538-975f-23aeea8ff70e` 实际生成 H.264 MP4，节点源文件、Console 缓存和
Chrome 下载文件均为 44,875 字节，SHA-256 均为
`40E5B15C1EE934E545D75C3270D4F83B1934D7B5EEB94E62B8645C2C23A05E17`，并校验 MP4 `ftyp` 头。浏览器从真实用户登录、本人录像
列表、缓存请求走到下载，1/1 PASS；指定 CA 临时进入 CurrentUser 根存储，测试后移除，没有关闭 TLS 校验。Cloud Node Service 的
target/stage/dist 与公网安装文件 SHA-256 均为
`3DDE160C0C797F08CBD4044F46017D0133B11B7D2B8E7FEF9B73154D1DABF64E`，服务替换后保持 Running；Render build/dist 与公网安装文件
SHA-256 均为 `C87F6C6EAA7203792FC3C1E716A304C19AFA9A5E6333E95E6AC0153D853AE347`。本人公网有数据流程由此关闭；管理员公网真实文件上的
保留/释放/驱逐浏览器动作仍需单独短测，不能把本结果扩大为整个 DB2 完成。

Console 管理实时链随后建立独立 `/api/console/managed/events` WebSocket，不复用节点控制或旧 website 通道。浏览器升级握手不接受 URL/
Header bearer，必须同源并在 5 秒内发送首条认证消息；admin/viewer 会话在每次事件和 15 秒心跳时重新核验。进程内事件窗有 1024 项，
使用单调 sequence、cursor 和随机 `stream_id`；进程重启、未来/过旧游标及广播落后都会显式要求 HTTP 全量快照，不伪造持久重放。
成功的管理写操作以及节点状态、实例、部署、会话、通道、传输和录像上报会发布类型化失效通知；前端以固定 2 秒重连，显示中英文
实时/陈旧状态并刷新相关页面。Rust 单元 9/9、严格 Clippy、Console Web 类型检查、42/42 合同测试及生产 Web 构建通过；真实 PG+节点
WS 专项 `pg-20260919-063411-302a299c` 覆盖节点/HTTP事件和 viewer 禁用后收敛，真实 Chromium 专项
`pg-20260919-063531-2fcb06ab` 覆盖外部管理写入自动刷新及 Console 重启后新流快照/重连。修复前测试仍假定事件序号从零开始的失败报告
`pg-20260919-063200-bbccaa9d` 保留，不作为通过证据。CM-REALTIME 因公网高频反压与数据库中断期间页面陈旧/恢复尚未专项验收，保持部分迁移。

逐 GPU 生产指标随后从“协议有字段、Service 永远为 NULL”推进到 NVIDIA 实际采集：Windows Service 保留 WMI PNP hash 作为稳定 key，
NVML 只在 PCI vendor/device/subsystem 双向唯一时填入显存总量/已用量、GPU 利用率和编码器利用率；名称相同、同型号多卡但无法唯一映射、
NVML 不可用或单项查询失败时相应字段继续为 NULL。86 项 Service 常规单测与严格 Clippy 通过；本机 RTX 3060 的显式硬件短测 1/1
验证四项实际指标均可读取。Cloud Node 与 Remote 的 build/stage/dist `px_service.exe` SHA-256 均为
`CD0AF1A3736572FFA1C845CB8B57FBD995ABD24212FD4593509202C048C7EFB4`。该切片尚不代表 AMD/Intel 指标、P3 GPU 硬过滤评分或公网节点验收完成。

管理节点原始遥测随后补齐浏览器趋势展示：最近 100 条样本按采样时间排序，CPU、已用内存、已用磁盘、GPU 与编码器利用率共用
0–100% 坐标；未知或越界数据形成断点，不做零值或插值。Console Web 类型检查、44/44 合同测试和生产构建通过，
`scripts_build/build_console_web.bat` 已同步 `output/px_console/dev/static` 并逐文件验证 SHA-256。该阶段的浏览器侧原始趋势当时尚未替代
服务端聚合、P3 调度硬过滤或公网验收；后续切片已分别补齐前两项。

节点遥测服务端趋势切片在 PostgreSQL 内完成有界聚合。管理 API 支持 5 分钟至 7 天窗口、30 至 3600 秒桶宽且最多 288 桶，
按数据库时钟对齐桶边界；空桶和未知指标保持显式缺口，不用零值或插值掩盖缺测。结果同时返回最新样本时间、年龄、30 秒陈旧判断，
以及 CPU、内存、磁盘、GPU、编码器的已知/总样本覆盖率。节点页面同时保留原始历史用于诊断，并消费服务端聚合用于趋势展示。
SQLx PrepareQueries `pg-20260919-105428-4029c368` 已从全新 schema 生成并核对 272 条 Console 元数据；节点存储专项
`pg-20260919-105507-36c9beed` 为 10/10，Directory API 专项 `pg-20260919-105605-6740880` 为 7/7；真实 Chromium 专项
`pg-20260919-110140-6a7e7da6` 已覆盖空历史、未知覆盖率及陈旧展示，Console Web 类型检查和 47/47 合同测试通过。该切片关闭
“服务端趋势/陈旧时长”缺口，不代替断线补报、公网高频反压或公网节点验收。

切片收尾的完整开发门禁 `pg-20260919-110826-239844db` 为 419/419 PASS、0 FAIL、0 SKIP。它从全新三库执行迁移、在线及离线
SQLx、Console/Auth/Desk 存储与进程测试、四套 Web 合同、Console/Auth/Desk 真实 Chromium、数据库失联 fail-closed/恢复、三库备份
和异机恢复，并在结束时确认登记源码哈希不变且清理隔离容器与卷。`scripts_build/build_px_console_server.bat` 已完成开发发行构建，
`output/px_console/dev/px_console.exe` SHA-256 为
`A39A5881E0038727E411C83F252C1076A92C45D4F553A2686375290A4DB9A417`；Web 构建同步脚本逐文件核对静态资源摘要。
该短测基线完成本切片验收，报告 scope 仍明确保留 DB2–DB5 的公网产品、断线补报和最终统一长测出口。

P3 首批资源预约随后落地。全新 schema 的迁移 0027 不迁移任何开发数据，并在发现既有部署/实例时直接拒绝；Game Hook/WebView
部署现在必须提供显存、GPU/编码器单实例预算、安全余量和最大压力，RDP profile 必须为空。预约事务逐卡使用
`max(measured, committed-running)+pending+request` 硬过滤，并把物理 GPU stable key、库存代际及预算快照写入实例和 Start 命令；未知指标、过期库存、
超限或运行时映射不可验证均 fail-closed。节点执行前再次采样验证。多 GPU 节点可原子选择具体物理卡；WebView 在进程内把 DXGI LUID 通过
D3DKMT 反查到所选 PnP stable key 后才打开 CEF 共享纹理，Game Hook 对实际捕获首帧做同样反查，两者必须返回匹配的 Ready，否则精确启动失败。
LUID 不进入 PostgreSQL 或节点命令；同一物理卡暴露的多个逻辑适配器不会被误记为多张可预约 GPU。

SQLx 元数据报告 `pg-20260919-085203-70161370` 通过；实例预约 `pg-20260919-085304-9f18dcd4` 为 14/14，命令
`pg-20260919-085514-84c609b3` 为 16/16，节点/部署分别为 `pg-20260919-085416-a29e121f` 9/9、
`pg-20260919-085618-4365a91b` 6/6，节点 WS `pg-20260919-085715-d53b40c1` 为 1/1，目录 API
`pg-20260919-090245-02ab41c7` 为 7/7，三库恢复 `pg-20260919-090416-b1a84fe9` 为 1/1。Service 含物理 NVIDIA 硬件测试
90/90 通过；Cloud Node/Remote Service 哈希均为 `9DF69FB05B5D9146BBC18B187B8FBB260EDC15043881254B7DD7D9A673E85E97`。
Cloud Node/Remote Render 哈希分别为 `41EDD79A409D7FDF95A685EDDC8517DD62C5EA146C69B36781BF660F875E3221`、
`629CF629AC1704D9588A72BB60161129591FB1D0CF1B6F0083DB35D7447E3C09`，物理 GPU 身份硬件测试 1/1 通过。Console build/output
哈希均为 `0441DA4A0F24FB82ACCE5DF51D640C20DC1C8B44FD1C7D738BE028A3253A70A4`，Web 44/44 通过并完成静态资源哈希同步。

本批 Windows 全量短验收 `pg-20260919-092615-5418ce4c` 从三套空库执行并以 415/415 PASS、0 FAIL 完成。范围覆盖三库迁移与
权限/schema gate、268 条 Console SQLx、备份及异机恢复、Console/Auth/Desk 存储和 API、数据库断线恢复、真实 Chromium 管理流程、
Web Client 合同、最终三库恢复冒烟和测试源哈希不变；隔离容器与数据卷已清理。该报告明确不包含 Linux、正式公网 Windows/Android、Relay
数据转发或最终长测，因此不把 DB2–DB5 尚未通过的产品出口改写为完成。

P3 调度解释切片随后补齐管理员只读预览：同一份 PostgreSQL 候选快照逐部署/逐物理 GPU 计算预计显存、GPU、编码器余量、综合压力和稳定排名，
并返回结构化硬过滤原因；缺失指标保持未知，预览不创建预约，正式启动仍重新执行原子预约和节点二次准入。SQLx 270 份元数据
`pg-20260919-095659-649ec2d9`、实例 15/15 `pg-20260919-095743-5d59163d`、目录 API 7/7
`pg-20260919-095843-01f3ca7b` 均通过。Console Web 类型检查、45/45 合同测试及真实 Chromium
`pg-20260919-100752-038bfb6e` 通过；当前 Console build/output SHA-256 均为
`FF92E90F5805F1C8BF155F58FCF5AC8AFA05EDC308D1B5A0ABB0F78634078A24`。公网容量仍属后续 DB5 验收，不由该只读预览代替。

纳入该切片后的最新 Windows 全量短验收 `pg-20260919-101132-c975fede` 从三套空库完成 417/417 PASS、0 FAIL、0 SKIP；
新增项已进入实例专项、目录 API、Console Web 和真实 Chromium，最终仍验证断库 fail-closed/恢复、三库恢复、源文件哈希不变及隔离环境清理。
该报告仍不包含正式公网 Windows/Android、Relay 数据转发或最终长测，不能据此宣称整个 DB0–DB5 产品出口完成。

节点断线遥测补报切片新增迁移 0028。Windows Service 在受保护节点目录中以 LocalMachine DPAPI 保存最多 240 个样本，断线重连后先
恢复当前在线报告，再以每批最多 4 个渐进补历史。样本 UUID 和 payload SHA-256 由 PostgreSQL `(node_id,sample_id)` 回执表精确幂等
校验，响应丢失可用相同请求重试，同 ID 改采样时间或指标会拒绝；
补报只追加机器/GPU 历史，使用服务器分配的负 sequence 与在线正 sequence 隔离，不更新 latest、节点 `last_seen`、端点、可调度状态
或告警条件。趋势改用真实 `sampled_at` 分桶，去重回执比 7 天历史多保留 1 天并每批最多清理 5000 条。

SQLx PrepareQueries `pg-20260919-115336-8e5b4530` 已从全新 schema 生成并核对 276 条 Console 元数据；节点存储专项
`pg-20260919-114653-5300e840` 为 11/11，真实节点 WebSocket 专项 `pg-20260919-114758-9b9d68b7` 为 1/1。Windows Service 全套
为 90 PASS、0 FAIL、1 个需专用 NVIDIA 环境的显式 ignore；本机 WMI/NVML 一致性测试通过。原有 RDP reaper 测试因本机已占用固定端口
暴露出测试隔离缺陷，已改为在产品应用端口范围选择可用端口，单项及全套复验通过。Cloud Node 与 Remote 的 stage/dist
`px_service.exe` SHA-256 均为 `3DD5662C858CF4E2C15CC1DE910CE21D98A464CFED4B8D8AFF2C3783195136D6`；精确重试门禁加入后的
Console 开发发行构建 SHA-256 为 `09D868747D8332E9733ACD0EE531A852C0BEE5B87284798083DE351B4AA5665A`。

DB3 Auth 通知事务切片新增 migration 0005。签发、续期和吊销会与许可证事实、幂等 request、issuance 和 audit 在同一事务写入
`license_notification_outbox`；同一许可证只允许按 revision 顺序领取，30 秒租约每次使用新 UUID，迟到 ACK/重试不能确认新的领取，
失败延迟限制为 1–300 秒。签发/续期通知引用精确 wire，吊销通知只有更高 revision；撤销 outbox INSERT 权限时，测试确认前述所有事实
整体回滚。SQLx 元数据由全新三库生成并更新为 Auth 34 条；空库 PrepareQueries 报告
`pg-20260919-125355-7ab5a625` 通过，Auth 9/9 真实 PG 专项 `pg-20260919-125549-7219cbfd` 通过，严格 Clippy 通过。
本切片关闭“Auth 没有事务通知事实”的缺口；受认证投递执行器、Console/共享消费者、水位和私有离线验证仍未完成，不能据此关闭 DB3。

DB3 Console 消费者随后接入正式 PostgreSQL 产品组合根：进程监听/连接数据库前读取受保护的 `PXLIC1`、规范 Auth trust store 和库外水位。
Official 强制使用明确的 Auth HTTPS `/api/auth/licenses/verify`，先以 Auth 数据库时间确认当前 revision/未撤销，再以本地 trust root 验同一
wire；Customer 明确拒绝任何 Auth URL，仅做离线签名、绑定、有效期和水位验证。水位以 current/previous/next 原子状态绑定 Console
deployment、机器 hex64、发行、license ID、Auth deployment/recovery generation，revision 和可信时间只升不降；未知文件和中断歧义
fail-closed。readiness 与所有 API 的前后门禁继续检查到期/时钟回拨。Console runtime 13/13 单元测试和 default/pg-integration 两套严格
Clippy 已通过；真实 Customer 许可证 Console 子进程专项 `pg-20260919-132151-203c5c40` 覆盖启动、HTTP、静态资源与断库退出，
Auth API 专项 `pg-20260919-132439-aaf66b82` 为 9/9，另有真实 loopback HTTP 的 Official currentness 单元链。
日常开发构建已同步 Web 和 `output/px_console/dev/px_console.exe`，build/output SHA-256 为
`CB915423B5115A78F0B264CAE5964D1347464C386CC73C653AC2D60D809BC8D9`。完整 PostgreSQL 总回归仍须在本批后续报告补证。

DB3 后续切片把许可证字段变成产品硬门禁。设备和资源会话的最后名额在写事务内分别用 advisory lock 串行化，避免并发超发；
CloudApplications/Desktop/Rdp 在实例预约、会话创建及 descriptor 签发入口执行。真实 PG 专项为 devices 9/9
`pg-20260919-134020-93df5126`、instances 16/16 `pg-20260919-134115-17a2aa59`、sessions 11/11
`pg-20260919-134428-27e91cd8`。管理状态入口 `/api/console/managed/license` 已加入管理员门禁，directory API 7/7
`pg-20260919-135216-3c168db1` 通过。

Official 运行期每 30 秒以受保护签名 wire 和完整目标绑定重新验证，只有成功 currentness 才推进库外水位；连续失败、撤销或 revision 替换
最迟在最后成功后 40 秒使请求/readiness fail-closed 并取消进程。Auth 在这次认证接触后确认该许可证截至当前 revision 的 outbox，
不公开内部 lease；旧 wire 能确认“消费者已接触”但 currentness 仍拒绝。Auth API 9/9 `pg-20260919-135756-f4997034` 覆盖签发、续期、
吊销三次 outbox 收敛。Service 不建立第二许可证权威，已删除未使用的旧 `px_auth_mgr` 依赖；Cloud Node/Remote 聚焦发行构建均通过且
stage/dist SHA-256 同为 `0B34DBA66CF2BF680DEA61C50F93773520C8897E10C0F02F5136D53425ED7028`。
Console 聚焦开发构建随后通过，`.cache/console-dev/release` 与 `output/px_console/dev` 的 `px_console.exe` SHA-256 均为
`1654CC6DD4FD3D531B377814EA2DDD28D0570EC89F0BF9D293101586AF4E8B1D`；这不是正式发行整编或 DB5 完整制品声明。

DB3 旧组合根清理随后把非产品 `rust_server/px_console_server` crate 的 `Cargo.toml`、`build.rs` 和旧 `src/**` 连同旧根级
TOML、明文凭据交接说明、Mongo 去重工具、旧发布/迁移脚本及依赖 Mongo + `/api/v1` 的失效 RTC 测试原样归档到
`backup/postgresql_console_retirement_20260919/`。活动服务端 workspace 只保留 PostgreSQL runtime/storage/migrations；重新生成的
`Cargo.lock` 不再包含 `mongodb`、`redis`、`px_auth_mgr` 或旧 `px_console_server` 包，工作区全目标严格 Clippy 通过。归档 RTC
脚本不是删除 Direct Host 功能：DB5 必须以当前 resource-session/descriptor 流重建真实跨端用例，旧脚本不得作为通过证据。

许可证恢复/监督专项把 Console runtime 单元门禁提升为 15/15：同一 Auth recovery generation 内完成新旧 key 并存、新 key 签发、
旧 key 撤下及水位连续推进；generation 改变时旧水位明确拒绝，只有恢复审批后切换到新的私有水位根和新许可证才可启动。Official
在线监督任务现在由生产 `spawn_online_supervisor` 唯一实现并被测试直接调用；失去 40 秒新鲜度会取消 runtime，主进程以非零状态报告
`runtime authority was lost`。Linux 发行新增 `pixels-console@.service`，仅非零退出 5 秒重启，正常 SIGTERM 有界关闭；unit 已用
`systemd-analyze verify` 通过，Windows 全目标严格 Clippy 和打包脚本语法通过。WSL Unix 编译因 crates.io 索引持续低速超时未形成
通过证据，必须在后续 Linux 总门禁补跑，不能用静态 unit 校验替代。真实 Windows Console 子进程专项
`pg-20260919-142415-56c41e17` 为 1/1 PASS，覆盖空三库启动、ready/静态资源及数据库 authority 丢失后的非零退出，隔离容器和卷已清理。

Render 快速架构门禁现会先构建其注册的全部测试目标，不再遗漏 game process identity、owned process 和 display power 三项二进制。
Opus 处理器的十轮 start/encode/stop 生命周期用例改为串行运行并使用 5 秒有界调度等待，避免把开发机 CTest 进程争用误判为组件故障；
它仍逐轮验证工作线程启动、编码回调和停止完成。修复后的报告
`build_official/cloud_node/reports/render-architecture/20260919-161644-quick` 为 ownership/readability 守卫 2/2、快速用例 19/19 PASS、
0 FAIL。修复前的一次失败报告保留用于追踪，不作为通过证据；本结果只关闭日常 Render 快速门禁自身的完整性问题，不替代 DB2 公网链路或
DB5 最终统一长测。

Relay 公网数据面已完成首个独立产品纵向切片。新增 `px_relay_server` 只实现现有 Relay WebSocket/Protobuf 的认证、房间、控制、
暂停/恢复/停止及双向原始载荷转发，明确拒绝已退役的 notification/中央 WebRTC 信令通道；连接数、房间数、双向业务载荷字节和
丢弃数从 `/healthz` 输出。它不依赖 Mongo、Redis、ZLMediaKit 或 Coturn。Console 仅在
`PIXELS_RELAY_PUBLIC_HOST/PORT/APP_KEY` 三项全部显式存在时向节点下发 Relay 描述，不提供默认地址或兼容 fallback；Service 向
Render 传递规范实例 UUID，线上的 `server_` 前缀只由 Render 添加一次。Relay 单元测试 4/4、Console/Relay 严格 Clippy、Service
91/91（另 1 项物理 NVIDIA 专项按环境忽略）及严格 Clippy 均通过。

公网 Windows CloudApplication 短测分别通过 Native Direct 与 Native Relay。Relay 用例真实建立动态 4613 端点和活动房间，解码
首帧、显示工作区窗口并注入鼠标输入；本次房间内 creator→remote 为 97,207 bytes，remote→creator 为 81,277 bytes，客户端加载
Qt 模块数为 0。静态 WebView 首帧曾暴露“房间已准备但恢复消息稍晚”的竞态，Render 现以已准入的 room-prepared 为可发送权威并经
既有 resumed 事件请求新关键帧；修复后公网用例通过。Cloud Node 当前 `px_service.exe` / `px_render.exe` SHA-256 分别为
`32045AFE53C8042C546A309B92C0A3A4ACD85AA5E9A70ADD56A79F90DA94D856` /
`FB1C127CBF72C16002AD895013B54FA9ACC8EFE93F50AE496330AC271589EEF4`，公网 Relay 为
`6056D61D882228D7020FBE4146E640FE7DA268C49B803855AAD0EBDD3DBB360C`。聚焦 Render 生命周期入口同时补齐标签内遗漏目标、移除对
已归档 Console PEM 的测试依赖并修复分目录 DLL 搜索，最终 20/20 PASS。这是开发期短测，不包含 Relay 音频、文件独立 hash、
断线重连/撤销、容量排空或最终统一长测。

Relay 安全复核随后补齐逐会话 Console frontend grant：部署级 appkey 只允许建立 Relay 连接，不能替代会话授权；媒体与文件控制请求都携带
版本化 frontend credential，由 Render 经 Service 向 Console 校验 CloudApplication session/revision/token/instance/role，observer 只得到
view/audio。授权租约按 Console TTL 续签，撤销、身份漂移、续签超时或本地 logical lease 不一致都会主动关房；未完成 admission 的媒体/文件
载荷直接丢弃，拒绝请求会立即销毁 Relay 房间。桌面密码模式保持原行为，不增加旧协议 fallback。Client 配置 8/8、Render 生命周期 20/20
通过，Cloud Node Render 构建树/dist SHA-256 为 `683790DB106E9C891279E6F0C90B02F913C46C77BF5B430C4F4969AAB9F5D3C6`，
Client 为 `4397F0A35FD3EDE5D47DB077B0995C798914704663A67B373469F0EEC6325E99`。本轮新安全制品尚未完成公网部署复验：
现有公网主机 WinRM 与 SSH 都在认证阶段拒绝已登记机器凭据；此前公网 Relay/Direct 通过证据不冒充本轮安全补丁的公网证据。

Android DB5 资源会话切片已删除活动客户端中的全部旧 `/api/v1` 调用和 Console 账号/云应用 `password_hash` 连接模型。健康检查、
guest、注册、登录/退出、设备/应用目录、实例 CAS 启停、显式 desktop/CloudApplication 资源会话和 descriptor 均改用当前
`/api/console` 契约；所有请求使用 `client_type=android`，资源请求另外强制声明 user/guest subject。Native 数据面直接使用 descriptor 的
session ID、revision 和短期 frontend token，且校验返回 target、client type、controller role、状态、transport 和实际 host/port；不再伪造
`android-<nonce>` stream ID，也不回落到设备密码。Android 全模块 JVM 测试、arm64 native、lint 和从清洁输出执行的完整 Debug 构建通过。
版本 1.0.5 APK 已用 `adb install -r` 覆盖安装到 Xiaomi 22021211RC，SHA-256 为
`4033D341A7B3C5FDF4FCE2AC617D96F6887FF6D32CDC0601FABE9608F6DAC05D`。同一公网正式 Console 上完成 guest public 目录、启动 WebView、
取得显式 CloudApplication descriptor、直连 `39.71.45.66:4613`、WS frontend 准入、UDP/FEC 首帧、MediaCodec 解码、退出和实例停止清理；
最终页面恢复“可以启动”，无 AndroidRuntime/JNI fatal。本轮证明 Android guest Direct 短链路，不替代账号/ACL、Android Relay、文件、音频、
撤销/续租或统一长测。

Android 账号短链路随后在真机验收中暴露并关闭当前响应形状缺口：Console 登录只返回 `expires_at`，客户端仍错误要求从未属于当前
PostgreSQL 契约的 `absolute_expires_at`，因此正确账号会被误报“无效响应”。活动领域对象和加密 DataStore 已直接升为新 schema，删除该字段，
不读取旧 v1 会话；同时修正 `avatar_url:null` 不得变成字符串 `"null"`，并以真实登录 JSON 形状增加单元测试。当前 Android 公网脚本也已
从 `/api/v1` 整体替换为 `/api/console`，旧脚本原文归档于 `backup/postgresql_android_api_retirement_20260919/`，不参与执行。
API 短测用 `client_type=android` 分别完成 guest/user 实例、显式 CloudApplication 会话、descriptor、CAS 停止、关闭和注销。

版本 1.0.6 从清洁 `build_official/android` 完整执行 454 个 Gradle task，单元测试、lint、arm64 native、APK 和 `adb install -r` 全部通过，
APK SHA-256 为 `8118D761102E1621B6098D62EB02DBAFF0F12978C9F5922238545D93A589F706`。Xiaomi 22021211RC 随后真实登录公网测试账号，
账号目录显示 Public Web Application；启动后以账号所属 session/revision/frontend token 直连 `39.71.45.66:4613`，收到首媒体并初始化
H.264 1920×1080 MediaCodec，退出远控、停止实例后恢复“可以启动”，最后注销账号；无 AndroidRuntime/JNI fatal。
这关闭 Android 账号/目录/CloudApplication owner 的 Direct 短链路，设备 ACL、Relay、文件、音频和撤销/续租仍分别验收。

Android Relay 客户端切片现已完成本地实现和短门禁。Console descriptor 只向当前资源会话签发最长 300 秒、HMAC 防篡改并绑定
session UUID 与目标 device/instance UUID 的 `admission_ticket`；部署级 `PIXELS_RELAY_APP_KEY` 不进入本次 Android 用户 DTO、DataStore 或 APK。
Relay 在 WebSocket upgrade 前验证票据期限和目标路由，Render 随后仍以 frontend token 向 Console 做权威会话/角色准入，二者不能互相替代。
独立 Relay 准入票据 1/1、Relay 4/4（含合法票据、篡改/错目标拒绝和既有双向转发）、Console 15/15、严格 Clippy，以及真实 PostgreSQL 节点控制
报告 `pg-20260919-233340-c48eec6f` 1/1 均通过并完成隔离卷清理。Android 全模块测试、Lint、arm64 Native 和从清洁输出执行的
454/454 task 完整 Debug 构建通过；1.0.8 已用 `adb install -r` 覆盖安装至 Xiaomi 22021211RC，APK SHA-256 为
`3805209B164975B338CF4C8265E401403D9C4F80B680B9388CD67341AD2F693D`。真机已验证云应用卡片的连接偏好入口、自动/直连/Relay 三种路径及
Relay 选择持久化，无 AndroidRuntime/JNI fatal。当前公网 Console/Relay 尚未部署这批服务端代码，因此这里不宣称 Android Relay 首帧
端到端通过；`Automatic` 当前仍明确保持 Direct，不实现隐式失败回退。

Android descriptor 重签与重连切片随后完成本地实现。账号桌面和 CloudApplication 只对已有资源 session/revision 调用 descriptor 入口，
返回 revision 必须前进且 session、target、Android client type、controller role、native transport 和 user/guest owner 均保持一致；不新开
资源会话，不以新 guest 替换过期 guest。可恢复网络错误每 5 秒重试，Native 已自行恢复则停止循环；确定性拒绝 fail-closed，手动重试也先
重签。Native attempt 的独立 callback UUID 继续隔离停止后的迟到回调。请求路径/owner/过期 guest/重启和失败生命周期单测、全模块 JVM、
Lint、arm64 Native 及清洁 454/454 task 通过。1.0.10 已用 `adb install -r` 覆盖安装至 Xiaomi 22021211RC，APK SHA-256 为
`3B7F875C1C7ABE8F1667A4F85B78A346CDBB97A5404FFA20DED002238BD142BD`；版本和顶级导航真机冒烟通过，无 FATAL/JNI fatal。
该证据关闭 Android 本地续签实现门禁，不替代新 Console/Relay 公网部署后的真实 Relay 断线重连、撤销和媒体验收。

Windows Panel 的账号与资源入口已切换到当前 PostgreSQL Console 契约。注册、登录、退出、资料、头像和改密使用
`/api/console` bearer/subject 模型；设备目录、user/guest 应用目录、实例 CAS 启停及 desktop/CloudApplication 连接均使用显式资源会话和
descriptor。Panel 向 Client 传递 session ID、revision、frontend token 与可选 Relay admission ticket，不再为账号设备或云应用回落到
Render 密码；强制 Relay 但 descriptor 未提供 Relay 时明确失败。关闭 Client 或停止云应用会请求关闭对应资源会话。真实公网 API 短测以
`client_type=panel` 完成 guest、公开目录、WebView 实例启动、显式 CloudApplication descriptor、会话关闭、实例停止和 guest 注销；
Panel 聚焦测试 1/1、增量构建、零 Qt 门禁及 build/dist SHA-256 同步通过，当前 `px_panel.exe` SHA-256 为
`91F5653882686574054ACABE53AC01D61F7DB13A40053A600C9FAE4CFE939530`。

随后完成 Panel 主机配置与节点控制边界收口。Panel 现在只接受规范化 `https://host[:port]` Console 地址，健康检查使用当前
`/health/ready`；旧加密接入串、Panel 设备自注册/设备写入/自报在线、Panel 保存或转发部署级 Relay appkey、共享链接中的
`rlst/rlpt/rlak` 以及 Panel 覆盖 Render Relay 配置均已删除并完整归档。Console 在已认证节点连接中下发 Relay 部署配置，Service 验证后
仅在启动 Render 时注入且不写入持久化启动记录；节点公开访问地址同样由 Service 配置和心跳权威上报，Panel 不再自行维护。Service 与
Render 的聚焦测试、Console runtime/node protocol 测试、Rust 严格 clippy、Panel 21 项产品测试及三个产品的增量构建均通过。
`client/cloud_node/remote` 的 `px_panel.exe` build/dist SHA-256 分别为
`D4D4D8AD6D34001C643DA6D547258CBEA5EDB2AC43E3408C008F3517DC71E9A0`、
`7E344989955AFF95AC106FCBB9A219F77E996997656E40348E64E281C8F7D77C`、
`557E082735214257D0B80DE23D30C4D2FC70296FAD2E568AD5260AF5B8B34E0C`；cloud_node/remote 的 Render、RTC DLL 与 Service 也逐件
核对 build/stage 与 dist 一致。此切片关闭旧 Panel 主机接入路径，但不冒充 Official/Customer 发行隔离：签名部署发现、Official 固定官方
端点且拒绝编辑、Customer 必填私有端点且拒绝官方 deployment identity，以及按 distribution 独立输出仍是 DB5/P0；不能用域名或 IP
黑名单替代签名身份。

DB5/P0 的签名部署身份已完成服务端协议基础：独立 `px_deployment_identity` crate 定义并验证 `PXDC1` 部署证书、`PXDD1` 短期平台描述
和 `PXDP1` nonce 持有证明，Official/Private 类别、deployment UUID、公钥、证书版本、descriptor revision、trust epoch、最低客户端
build 与协议范围均进入签名和 fail-closed 校验。Console 在监听前把数据库 deployment、许可证 distribution、证书、私钥和信任水位
交叉绑定，并提供 `GET /.well-known/pixels` 与 `POST /.well-known/pixels/challenge`；证书/描述/证明合同 5/5、Console runtime 17/17
及全目标严格 Clippy 通过。随后补齐部署侧 create-new 私钥命令与独立离线 authority：根生成、规范 trust store、Official/Private 证书签发、
根轮换输入和拒绝覆盖均已形成专项 2/2；authority 不进入通用产品包。此切片没有共用商业许可证密钥，也不会在缺少材料时自行生成。
Android 客户端消费见下一段。Windows Service 随后已把同一 wire 接到实际节点连接前：同源 discovery 与 nonce proof 成功、部署/类别及
三项最低水位验证、DPAPI 持久水位全部完成后才建立 WebSocket 并发送 node token；身份请求拒绝重定向且有 64 KiB 上限。节点配置 schema
直接升为 2，不读取旧 schema；管理员显式 clear 同时清除身份水位。真实签名身份/nonce HTTP 与 DPAPI 水位专项已覆盖。Broker/Relay/更新
端点尚未全部进入认证后下发。Service 聚焦回归为 96 passed、0 failed、1 个物理 NVIDIA 测试忽略，
严格 Clippy/格式检查通过；Cloud Node 与 Remote 的 release build/stage/dist 六份 `px_service.exe` SHA-256 均为
`C9EBA9287EECDEA96731C24B00AF243659E752086D7B283DE51BCEA14B17CC1C`。

Android 已把与服务端 wire 同域分离的部署身份验证接入实际请求链：严格解析 trust store、`PXDC1/PXDD1/PXDP1`，校验
Official/Private、deployment、证书/描述/trust 水位、客户端 build、协议范围、有效期及 nonce/revision 重放边界；endpoint 测试、guest、
注册、登录必须先完成发现和 nonce 私钥持有证明，bearer 请求按最长 15 秒且不超过 descriptor 有效期的缓存重新验证。项目最低 API 31 不
假设系统 Ed25519（平台只从 API 33 保证），生产实现使用独立 Conscrypt provider。deployment ID、类别、certificate version、descriptor
revision、trust epoch 在成功证明后持久化；损坏记录、身份变化或任一水位回退 fail-closed，Customer 首次私有部署因此被固定。

构建入口现在显式区分 `official|customer`：使用不同 applicationId 和 `build_official/android/<distribution>` 沙箱；Official 固定 UUID/HTTPS
origin 且设置页不显示地址编辑，Customer 禁止编入 Official UUID/URL 并只接受 private 身份。APK/AAB 配置先校验规范 trust store、三项正整数
最低水位和 trust epoch 一致性，再允许清理/升版。验证器/请求顺序/回退及设置页专项、全应用编译和 lint 通过；approved trust store、正式
Official UUID/URL、签名 release 双制品和真机行为尚未执行，故仍不把 Android 发行身份总出口记为通过。

Android 正式构建现已收口为 `build_android_product.bat release` 双发行事务：签名、证书摘要、FFmpeg 合规材料与 Official/Customer 身份均在
清理和升版前预检；之后整个 Android 输出只清理一次、版本只递增一次，两个发行必须使用相同版本、Git revision 和独立 Gradle/native/dist
沙箱。单发行 Release 入口会失败关闭；只有两份 release manifest 均匹配才写根 `release-matrix.json`。单发行 Debug/`adb install -r`
继续用于开发短测并各自升版。临时规范测试 trust store 下的两发行 Gradle 预检均通过且版本/输出时间戳未变化，缺少正式材料的矩阵入口也验证
在清理/升版前失败；本结果是构建编排门禁，不冒充正式签名 APK/AAB 或真机双发行验收。

Windows Panel 已接入同一部署身份协议。`/.well-known/pixels` discovery 与 nonce proof 在账号密码、bearer、guest token 和资源会话请求
之前完成；严格验证 Official/Private、deployment UUID、证书/描述/trust 单调水位、客户端 build 与协议范围，并拒绝 HTTP 重定向、限制
身份响应为 64 KiB。HTTPS 现在默认启用系统证书链/主机名验证，不再因未配置私有 CA 而关闭验证。Official 使用安装策略中的规范固定 origin，
设置页只读；Customer 只接受 private 身份，候选平台通过证明后才允许保存。账号令牌和 guest 缓存同时绑定 Console origin 与 DeploymentId，
同地址更换部署身份也会清理旧会话，不能向新平台发送旧凭据。部署身份验证 2/2、Panel 产品测试 1/1 和 Client Panel 定向构建通过；
`px_panel.exe` build/dist SHA-256 均为 `0588475159CA22576C3F332FD469AEC00B74F39743552BD20186D013BE78BBB0`。本切片尚未生成或
验收 Windows Official/Customer 双发行的正式 policy/trust 资源和独立输出目录，因此只关闭运行时门禁实现，不把 Windows 双制品出口记为通过。

Windows 双发行构建边界随后已实现：Cloud Node、Client、Remote 的完整发布入口会在清理和升版之前同时预检 approved canonical trust
store、三项最低水位、Official deployment UUID 和规范 HTTPS origin；通过后只消费一次产品版本，并在
`build_official/<product>/<official|customer>/` 分别生成 deployment policy/trust、CMake/Cargo/Web/RDP、dist 与安装包。Customer policy
不会携带 Official UUID/origin；产品描述、dist 清单、远程聚焦发布和安装器均校验 distribution。安装器登记发行身份，只允许同产品同发行
覆盖/升级，跨 Official/Customer 必须先卸载。身份生成 5/5 单测、Python 编译检查、PowerShell 解析、Rust 描述符专项和 Client Panel
聚焦构建已通过；当前 development `px_panel.exe` build/dist SHA-256 均为
`6D093BFA81D18DC43771A4169642DDCB9F5EBB85F71177180FBD0AF410B8F72B`。因为仓库不包含 approved trust store、正式 Official UUID/origin
和签名材料，本轮没有伪造正式双发行或运行 release-only 全构建；Windows 正式双制品/安装升级卸载仍是待执行验收项。
策略回归随后发现并修正身份协议水位误写为 2 的问题：当前 Console、Service、Android 和 Windows 生产协议均为 1，正式 Windows policy
现明确写 1 并有单测断言；否则签名与 nonce 均正确的当前 Console 也会被 Windows 客户端确定性拒绝。

Web Client 随后完成同一发行身份门禁。Console 启动 URL fragment 现在显式携带 `console_origin`；Official/Customer Web bundle 在创建 RTC、
向 Render 发送一次性 frontend token 或使用任何凭据前，严格验证 `PXDC1/PXDD1/PXDP1`、Official/Private 类别、固定 Official origin/UUID、
客户端 build/协议范围、有效期与按 Console origin 持久化的单调水位，Customer 首次成功后固定 deployment ID，验证失败不回落到手工密码入口。
Cloud Node/Remote 矩阵构建把各发行 policy/trust/build 注入各自 Web 输出，缺少材料或发行不匹配会失败关闭；Console 只给两个公开签名身份端点
开放所需 CORS，不扩大账号 API。Web Client 62/62、Console 启动描述符 2/2、Console runtime 身份专项 2/2 与 development 生产构建通过；
缺少 policy 的 Official 构建也已确认在产物生成前失败。本切片关闭 Web 运行时和构建软件门禁，不冒充 approved 正式双发行或公网首帧验收。

原生 `px_client.exe` 不持有 Console 地址、账号令牌或服务器选择设置，因此不另建 Console 部署身份门禁。Console 资源连接由已经验签的 Panel
通过匿名管道传入与 session/revision 绑定的一次性 frontend token；用户主动输入密码的点对点连接仍是独立的本机功能，不是 Customer 配置
Official Console 的绕过路径。让 Client 再次访问 Console 会重复验证并扩大凭据和网络责任，故不列为 DB5 缺口。

Web Client 增加可重复的公网验收入口，使用 `user_web` 创建当前资源会话，并确认当前本地产物能完成 frontend grant、SDP、ICE、四条
DataChannel 和公网 UDP peer 连接，令牌也从可见 URL 移除；但公网 Render 没有发送任何 RTP，视频轨保持 muted，不能记为首帧通过。
同时确认远端 `web_client` 仍是旧版（日志仍声明游客 RTC Relay/设备密码，直接被当前 Render 以 HTTP 400 拒绝）。本地已把当前 Web 构建
同步进 Cloud Node dist，5 个文件 hash 一致，并为 SSH 聚焦部署器增加原子 Web 目录发布；公网 WinRM 与 SSH 均可达，但登记机器凭据都在
认证阶段拒绝，故无法部署当前 Render/Web 制品复验。失败实例和资源会话均已清理；该项保持 DB5 未通过，不用 peer connected 冒充视频通过。

Android 设备 ACL 已增加独立公网短测入口：用临时 Android 账号验证授权前目录/详情拒绝、显式 user ACL 后可见、撤销后重新拒绝，并在
`finally` 中恢复设备原 users/groups ACL、删除临时账号和注销管理员会话。脚本语法、参数入口通过；USB 设备 `e2b3b128` 在线，现装
`1.0.10-debug` 正常运行但处于登出状态。当前工作区没有公网测试平台管理员凭据，故本轮没有执行会改变真实 ACL 的测试，也不把入口存在
写成真机验收通过。

退役中央媒体制品审计现覆盖 Windows 和 Android：Windows 产品 dist 拒绝已知 ZLMediaKit/Coturn 文件名及组件目录；Android Debug 发布与
Release APK/AAB 发布在落盘后逐项检查 ZIP entry，同样失败关闭。相关 Python 门禁合计 11/11、两份 PowerShell 解析通过；当前三个 Windows
development dist 和已安装来源的 `Pixels-1.0.10-debug-arm64-v8a.apk` 实物扫描通过。正式双发行尚未生成，故仍保留正式安装包实物审计出口。

Relay 数据面的本地安全与计量门禁随后收紧：房间只有在远端明确接受控制并进入 prepared 状态后才允许转发 payload；同一方向的首个
`relay_msg_index` 可以从任意非负值开始，后续必须严格递增 1，并拒绝负数、重复/乱序索引及同一消息重复 room，避免重复扇出和重放。
连接被同 device 的新 generation 替换时会销毁旧房间，旧 generation 的迟到 disconnect 不能删除新连接或新房间。Relay 健康统计现在把
接收成功的合法 payload 计入 uploaded，只在目标 WebSocket sink 的 `send` 成功后计入 forwarded 和方向字节，排队成功不再冒充实际发送成功。
`px_relay_server` 单元及真实 WebSocket 回归 6/6、严格 Clippy 均通过。本轮没有部署公网制品；公网音频、文件独立 hash、真实断线重连、
在线撤销与容量排空仍保留为产品验收项，不能用这些本地门禁替代。

RDP Rust 聚焦入口也已修复：删除已不存在的 Console 包名和 0-test 旧过滤器，改为真实执行 Service Core 14 项、Service Host 30 项、
Console PostgreSQL 工作区 6 项和资源会话/RDP 占用 11 项。2026-09-20 四组均通过，数据库报告为
`pg-20260920-053731-bd9441e4`、`pg-20260920-053930-e4373942`，隔离容器与卷已清理。该结果恢复了日常 RDP 软件回归门禁，
不替代公网 Windows 的桌面画面、输入、音频、剪贴板、双工作区、故障恢复或运行制品验收。

Rust Client 依赖审计随后关闭了无效数据库依赖：`px_base` 中无人调用的 MongoDB/Redis 工具模块及依赖已删除，`px_auth_mgr` 的未使用
MongoDB 声明和 `px_sysinfo` 对未使用授权 crate 的依赖也已移除；更新后的 Client 锁文件不含 `mongodb` 或 `redis`。`px_sysinfo` 仍是
无 UI 的 `px_osinfo` 命令行采集器，活动 Rust manifest/锁文件/源码均不含 Zed、GPUI 或 gpui-component。Rust Base workspace 测试为
协议 2/2、授权 30/30、基础库 39/39（另 1 项真实 SMTP 按环境忽略），Client workspace 为 Service 96/96（另 1 项物理 NVIDIA 忽略）、
系统信息 4/4、User Proxy 99/99、Service Core 87/87（另 1 项真实 UE 样本忽略）、Service Manager 12/12；两个 workspace 的严格
Clippy 均通过。本项只清理编译依赖并恢复静态门禁，不扩大为 Windows 安装包或公网产品验收。

RDP 的 PostgreSQL 产品连接缺口已在本地实现收口。Windows Service 不再固定上报 `rdp=false`：只有安装目录中的 RDP 部署清单、代理、
策略和证书材料通过既有严格校验时，节点报告才携带 RDP 能力、Windows domain 与 proxy certificate SHA-256；任一缺件均 fail-closed，
不会伪装为可调度。Console 把这两项非秘密身份持久化到节点，变化时推进 endpoint revision 并触发重新对账。RDP descriptor 仍只允许
当前 `panel + controller + CloudApplication`；Console 仅在 descriptor 已签发且未过期、节点代际/epoch/端点仍相同、用户或 guest 授权仍有效、
实例 Running 且工作区 Ready 时解封账号密码。管理 DTO、Android、observer、已撤销登录和已关闭会话均不能取得该信封。

Panel 解析信封后以 `SecretBuffer` 持有，按 `transport=rdp` 选择现有 `--rdp-launch-stdin`，并把同一 session/revision/frontend token 一并
交给 Client；Client 因而以 Console 前端授权建立 RDP WebSocket，不再需要或伪造 Render 密码。RDP 不提供传输切换选项，模型层也会清除
该应用可能残留的本地强制 TCP/Relay 偏好，不能出现界面禁用但启动流程仍索取 Relay 路由的分叉。Console 与 Client 在完成其他字段解析前
先把源 JSON 中的工作区密码转入可清零缓冲并抹除源值；畸形信封同样不保留普通字符串密码。

节点配置 migration 0029、277 条 Console SQLx 离线元数据和严格 Clippy 已更新，PrepareQueries 报告为
`pg-20260920-064650-0b1082e1`。最终聚焦回归为 Service 节点控制 13/13、RDP Service Core 14/14、Service Host 30/30、工作区 6/6、
资源会话 11/11、Console 节点控制 1/1 和 Client 启动信封 1/1；最新 PostgreSQL 报告为
`pg-20260920-070459-213d2c8f`、`pg-20260920-070630-8149a72c`，隔离容器与卷均已清理。资源会话测试还验证登录撤销和会话关闭后立即
拒绝再次解封。Console development `px_console.exe` 构建/输出 SHA-256 为
`2857BF4AD61234210EDE16172598AC2E7C3398647492F31C64AB932FADB044FF`。Client、Cloud Node、Remote 的 development dist 均已重新收集，
分别以 41、315、77 件分发物通过逐件 SHA-256 和产品依赖边界复核；本轮使用聚焦增量构建，不执行 release-only 全量升版构建。
公网 Console 与节点制品尚未部署：`39.71.45.66` 的 WinRM 和 SSH 均可达，但登记管理员凭据分别在认证阶段返回 Access denied / Authentication
failed；因此本条只关闭本地实现和短门禁，不宣称公网 RDP 图形、输入、音频、剪贴板或会话保留验收通过。

RDP 节点执行链路随后补齐了上一增量遗漏的 Console→Service 工作区凭证边界。普通节点命令继续不携带密码；已认证节点只有持有当前
RDP Start 的精确 `command_id + lease_id` 才能显式读取一次工作区信封。Service 以可清零缓冲接收密码，按规范 `pxrdp_` 加 14 位小写
十六进制账号创建或复核标准用户，启动现有 RDP Render 后取得并严格校验本机账号 SID，再用同一命令租约回报 Console。只有 SID 确认成功
后节点才提交 Running ACK；确认失败会停止该精确 launch 并回报 Unknown，不能把未确认账号暴露给 Panel。重复投递只接受同一 launch，
不会按 PID、端口或账号名收养替代进程。RDP Start 明确不预约 GPU，也不会继承 Console Relay；Game Hook/Webview 仍保持 GPU 二次准入和
原有 Relay 行为。

本地新增协议秘密边界 2/2、Service 节点控制 15/15（含真实 WebSocket 的租约读取/确认）、严格 Client/Server Clippy 均通过；RDP 聚焦门禁
仍为 Service Core 14/14、Service Host 30/30、工作区 6/6、资源会话 11/11，最新数据库报告是
`pg-20260920-073236-bcc65364`、`pg-20260920-073330-aecd2cb3`。Console development `px_console.exe` build/output SHA-256 更新为
`68C965CF7CCEA19E80F79DD08111433F2B47C033C432C1A9DED52C0285EC1607`；Cloud Node 与 Remote 的 `px_service.exe` build/stage/dist
SHA-256 均为 `1EAE2300B43511E37F7E252B9AD265ACC6249C07F84993BE335CF353D2A1A014`。重新收集后的 development dist 分别以 315、77 件
分发物通过清单 SHA-256 与产品依赖边界复核。节点专属 RDP policy、代理密钥和证书仍只能由部署流程提供，不进入通用产品 dist；缺少时节点
继续上报 `rdp=false`。公网凭据阻塞未变化，因此本条也不冒充公网图形/输入验收。

RDP 身份与授权边界复核又发现 Client/代理策略仍接受历史 `prdp_` 前缀，会与 Console/Service 已生成的规范账号发生确定性冲突。活动实现现已
统一为严格的 `pxrdp_` 加 14 位小写十六进制，不保留旧前缀兼容；代理只对已配置账号和 Windows 域做大小写不敏感的精确身份匹配。Console
资源描述符也不再为 `transport=rdp` 签发无用途的 Relay 票据。新增 PostgreSQL 产品集成用例通过真实节点 WebSocket 完成部署准备、RDP Start、
租约工作区读取、SID 确认、Running ACK、Panel 资源会话和受保护描述符，并断言命令与描述符均无 Relay，节点控制现为 2/2，报告
`pg-20260920-075615-d51b089d`；C++ RDP 流/策略测试为 33/33，Console 严格 Clippy 通过。最新 Console development
`px_console.exe` SHA-256 为 `2756AC6D2AC8FCC7259592F040244AA1307FA4E410AA370ECF9AD4D31DAA7345`。Client、Cloud Node、Remote
三套 `px_client.exe` 的 build/dist SHA-256 分别为 `839A48D06CE1B39BF7CE00618CB5B4268CBC004D5F36780C52DE821E1551C872`、
`F3A13EDC2C010E04024889D368D6203752EF58BFF594F7F6777D6E2C9EF5EDE8`、
`D1728F7AAF90EA0CF3D1D187A5BBA91C1A9C0EB816210EC19711F081887B12D2`；Cloud Node/Remote 的 Render 与 RDP policy 也逐项通过
build/dist SHA-256 一致检查。重新收集的 development dist 为 41、315、77 件，全部通过清单和产品依赖边界验证。本条关闭本地账号前缀与
多余 Relay 授权缺口；公网节点凭据仍阻塞真实桌面验收。

RDP 通道计量现已落到实际字节提交边界：Render→Client 方向只有可靠 WebSocket 写完成成功才累计，Client→Render 方向只有 RDP payload
完整写入本机代理 TCP 后才累计；排队、写失败、超时和重复 completion 都不计入。Render 用既有资源通道上报器以 5 秒累计值和最终关闭状态经
Service 节点控制写入 Console，连接键与 RDP 前端准入使用同一个服务端生成 connection identity。桥接单测仍为 33/33，其中覆盖双向实际
payload、失败不计数和重复 completion 不重复计数；RDP 路由关闭 1/1 通过。PostgreSQL 产品集成用例新增 frontend admission、`rdp` 通道
打开、4096/2048 双向累计、关闭以及 Panel 本人历史回读，节点控制 2/2 报告为 `pg-20260920-081629-98fc8fb1`，严格 Clippy 通过。
Cloud Node/Remote Render build/dist SHA-256 分别为 `B462E932A0DD305EDBEF645DCD3A9CDB047ECB4AB83FA845ECB140C19FFCA678`、
`CC764F1B7A8F8E3E31CC379B7115C9AC7BEACEB4BDAE2132DE026BC522EA07B2`；重新收集的 315/77 件 dist 再次通过清单和产品边界验证。
本条关闭 RDP 本地真实 I/O 计量缺口，公网实机仍需验证真实桌面流量、撤销和断线终态。

RDP 关闭原因现已从网络入口贯穿到 Console 资源通道终态：远端正常关闭记为 `peer_closed`，Render 主动停止记为 `user_stopped`，授权能力
撤销记为 `policy_revoked`，WebSocket/RDP TCP/连接/超时/发送故障记为 `transport_lost`，非法包和队列溢出记为 `io_error`。原因以强原因优先
的原子状态保存，授权撤销不会被随后发生的 socket 断开覆盖；Render 停止时会在关闭适配器前为仍活动的路由产生最终状态，不再直接清空后
只依赖 `control_lost` 兜底。RDP 路由关闭/原因测试为 3/3，前端准入回归 4/4，Cloud Node/Remote Render build/dist SHA-256 分别为
`3A6769A16FA22A1111B2A1D650AB38AF15C313BB14EB613A0330E55265DDB16B`、
`FB47D5AFED82909111F1F4AF75E938C45D4143F3A876CBEFF9626CF0B90CBE21`，315/77 件 dist 清单复核通过。公网实机仍需对五类终态逐类
制造故障并核对 Console 历史。

Relay 资源通道现已从物理连接细分为稳定的逻辑媒体、音频和文件通道：媒体保持基础 connection identity，音频和复用文件分别使用
`:audio`、`:file` 后缀，独立文件路由在完成授权准入后建立文件通道。入站只统计已接收业务载荷，出站仍只在底层 WebSocket 完整写成功后
累计；队列接受、写失败和未授权载荷均不计数。首次真实载荷惰性打开对应逻辑通道，房间销毁、主动停止、授权撤销、连接替换和准入故障分别
保留 `peer_closed`、`user_stopped`、`policy_revoked`、`transport_lost`、`io_error` 终态，独立文件路由关闭仍通知文件引擎清理。
分类专项 3/3、Render 聚焦生命周期 21/21、严格 Clippy 和 PostgreSQL 节点控制 2/2 均通过，PG 报告为
`pg-20260920-092436-f389ae5b`。Cloud Node/Remote Render build/dist SHA-256 分别为
`75489A21E7FA2C01366A3DA65287ADD87BC589290AD0ACC341436E8C362A09F8`、
`E83D6C66103FA744A63BBCC052F18F7F68A968CBDB50DF775C8CCEFF3B5D1DB5`，315/77 件 development dist 清单与产品边界复核通过。
本条关闭 Relay 本地独立通道计量缺口，不替代公网音频、文件独立 hash、真实断线重连和撤销复验。

文件传输审计的短暂断线补报现已补齐 Render 活跃期闭环。开始请求在 Service/Console 暂时不可用时保留同一 transfer request UUID 按固定
1 秒间隔重试；进度或终态一旦形成待发快照，就冻结 sequence、字节数、outcome 和 digest，响应丢失或瞬时断线只能原样重发，收到匹配
transfer ID/sequence 的权威确认后才推进。终态不会再因首次报告失败而删除；若终态到达时前一进度仍待确认，先完成旧幂等快照，再以上一
sequence 的后继值提交终态。确定性拒绝仍 fail-closed，停止会取消等待，不产生无限忙循环。快照状态机 3/3、文件服务与快照 2/2、Render
聚焦生命周期 22/22 通过。Cloud Node/Remote Render build/dist SHA-256 分别为
`498FA24C3BB789E42C0254AB5F51984A39069E3207C10B013198911F7377267E`、
`1CAC0B3523AB47D219A0ED6CD4074141DE3CD456FE12E72F293303D873AD7A59`，315/77 件 development dist 复核通过。
该实现是进程内断线恢复；Render 已退出或主机重启后的 DPAPI 持久 outbox、公网断线制造与 Console 历史对账仍未完成。

文件传输终态的跨 Render 进程补报现已落到 Windows Service。开始传输仍必须在线通过 Console 会话授权并取得权威 `transfer_id`，
不会把本机排队冒充授权成功；此后每条进度或终态先写入 LocalMachine DPAPI 加密、ACL 限制且原子替换的 Service outbox，持久化成功后
才向 Render 返回 `accepted/queued`。队列最多 4096 条或 8 MiB，不满时绝不淘汰审计；满载、损坏或无法持久化均 fail-closed。
Service 在即时通知、节点重连和 15 秒报告周期排空，每批最多 32 条；Console 权威确认精确 transfer ID/sequence 后才删除。网络断开或
“Console 已提交但回包丢失”会保留完全相同的报告重试，依赖 PostgreSQL 既有 report hash 幂等；确定性拒绝会删除该 transfer 的剩余
依赖报告并记录原因，不能误接到新节点代际。Service 全量单元测试 101/101 通过（另 1 项物理 NVIDIA 测试按设计忽略），严格 Clippy
通过；其中专项覆盖 DPAPI 重开恢复、先持久化后应答、回包丢失后精确重试和确认删除。本条关闭“Render 退出或 Service 重启丢失已授权
transfer 的终态”代码缺口；主机重启实物故障注入、公网取消/重试及 Console 历史对账仍属于 DB5 短测。
Cloud Node/Remote 的聚焦 release Service 构建均通过，构建树与各自 development dist 的 `px_service.exe` SHA-256 一致为
`631823710EAC30D39DDC70B77B6A4B6149706215B873C472FC6E01D3632C82ED`；未运行 release-only 全量构建或升版。

Console 本人活动页现已接入 owner-scoped `/api/console/file-transfers`，与管理员历史保持权限分离；页面按文件名/会话、状态筛选并展示
方向、单调进度、终态原因和更新时间，中英文目录保持一致，同时明确审计历史不是可恢复任务队列。新增 API 合同 2/2，Console 全量 Vitest
49/49、类型检查、变更文件 ESLint 和生产构建通过；Vitest 也已明确排除 `e2e-public/**`，不再把 Playwright 公网用例误载为单元测试。
聚焦静态资源已发布到 `output/px_console/dev/static` 并逐文件 hash 复核，其中 JS 为
`3E540DC357700E648E423520FE7E53953C7456FB4EB1EC88409CF59A839A1C88`。这关闭客户端“无本人传输历史展示”的实现缺口，但公网真实用户
浏览器与实际传输对账仍保留在 DB5。

Console 本人文件传输历史随后进入真实产品浏览器门禁。`console-browser` 现在为隔离进程签发短期 Customer 许可证、部署证书、部署签名密钥
和规范 trust store，并让正式 Console 启动验证这些材料；没有新增产品绕过。最新报告 `pg-20260920-103212-db6b664d` 在真实 PostgreSQL、
原生 Console 与实际构建的 Chromium 前端上 13/13 通过：测试以真实 node-control WebSocket 完成节点/部署报告、实例启动、显式
CloudApplication 会话、frontend admission、文件传输开始授权和带 SHA-256 的完成终态报告；随后经历 Console 重启与数据库断开恢复，
本人活动页仍通过 owner-scoped `/api/console/file-transfers` 显示匹配的文件名、下载方向、`Completed` 和 `12.00 MB / 12.00 MB`，并保留
“审计历史不是可恢复任务队列”的边界说明。同一报告继续覆盖会话保持、断库 fail-closed/恢复和退出撤销。该证据关闭合成完成态事实的
真实浏览器对账缺口；公网两端对真实文件字节的独立 hash、取消/重试与主机重启故障注入仍属于 DB5。

Web Client 文件任务现补齐失败/取消后的显式重试。重试创建全新 job ID，重新走 Render 文件引擎与 Console 审计授权，不复用旧审计事实；
上传保留原 `File` 输入但从头启动，下载保留目标 sink，失败时仍可使用既有会话内续传缓存，显式取消保持清除续传缓存的既有语义。
清除完成任务会同时释放上传/下载状态，不再只删列表行。中英文 Retry/重试入口已接，协议测试覆盖 `receive(old) → cancel(old) → receive(new)`；
Web Client 全量 Vitest 63/63 和生产构建通过。Cloud Node/Remote development Web 构建树与 dist 的 JS SHA-256 四份一致为
`734B0686371CED03C7A7825028DA8B4CF32BEF71A6772FEE65F31AFD9BF51219`，产品清单分别 315/315、77/77 通过。本条关闭 Web 重试代码缺口，
不替代公网真实文件取消、失败后重试、两端独立 hash 与主机重启故障注入。

Windows Client 的嵌入式文件传输面板也已补齐失败任务的 Resume 入口，与原独立文件传输窗口保持一致。恢复成功仍由文件引擎生成新的
job ID，因此后续授权和审计是新事实；会话层同时修正了进度回调先于同步返回时的重复行、旧行已被 UI 清理时的越界删除，以及无回调竞态下
沿用旧速度/文件序号的问题。Cloud Node、Client、Remote 三套聚焦 Client 构建及各自 6 个测试程序均通过，构建树/dist 的 `px_client.exe`
SHA-256 分别一致为 `7B987D3416FEB268071C62C54848A1524455DCD8DF43B6BE902D3F5663CDFB2F`、
`464BD529ABADD40150FE357D4F64712FD859311D5548C1535B1561E00904DC3E`、
`6042ED86C351A95C6120451B1280407988EA748D08ED6CC6A82A4E34FDD1256E`。本条关闭 Windows 两种文件面板的重试入口一致性和已知竞态，
仍不替代公网真实失败/取消后恢复、两端独立 hash 与主机重启故障注入。

## 仍未通过的阶段出口

Console 入口前置增量：`pg-20260917-091421-1b89be5b` 的 accounts 七组 Windows 专项通过，828 个源文件 hash 复核一致。
覆盖空库初始化竞争/失败回滚、原登录绑定、退出/改密竞争、身份表最小写权限与分组分页；SQLx 已生成 235 条 Console 查询。
这是当时的存储专项；后续身份 API 与跨平台证据见本页最新完整报告，正式产品与全链路验收仍待完成。

| 阶段 | 当前未完成项 |
|---|---|
| DB0 | 已补领域/权限/恢复边界、Auth字节/固定向量，并按2026-09-19边界冻结Direct Host描述符、实际端点/代际和显式CloudApplication target；ZLM/TURN/中央RTC字段已从活动契约移除。媒体清理后的完整PostgreSQL合成基线 `pg-20260919-025221-0599733d` 为747/747 PASS，DB0本轮出口完成 |
| DB1-EXIT | 完成：Desk/Auth 产品服务与 PostgreSQL Console 正式 `px_console.exe` 均已接入；三者具有独立发行入口。Console 当前 3.2.21 发行、进程断库 fail-closed、真实浏览器和制品哈希已通过；后续能力缺口归 DB2–DB5，不再把旧 Mongo 组合根当产品入口 |
| DB2-A | 身份/管理HTTP、本人资料/头像、密码计算/限流/Origin、访客HMAC/会话/公开目录、Saved Connections、本人实例列表、更新目录、访问/通道/传输历史及录像目录HTTP、严格配置、稳定私钥加载、独立初始化CLI、静态文件服务及进程生命周期已实现；Console用户门户及管理后台的当前目录/身份/状态入口均已切新bearer/主体API，源码不再保留旧`/api/v1`，正式PostgreSQL产品二进制和发行包已切换。部署绑定录像缓存、本人/管理员授权Range下载、Render完成段session归属、Windows Service真实字节生产、本人/管理下载页面、保留/释放/驱逐 Console 副本、真实浏览器空目录及公网本人有数据下载流程已接；节点源文件/缓存/浏览器下载大小和SHA-256一致，Direct Host观察者不再默认获得输入。仍需管理员在公网真实文件上的保留/释放/驱逐浏览器动作；视频墙延期，ZLM直播和RTC/TURN管理明确退役，不再作为待实现项 |
| DB2-B/C/D | 设备/应用/节点/部署目录、user/guest资源入口、更新与历史元数据入口、Console节点WS及独立管理实时事件流已接；Windows Service已切到新节点协议并实现部署准备、调和、命令fencing、精确launch ACK、Render前端准入转发、实际媒体/RDP通道生命周期、遥测、DPAPI有界断线补报、唯一PCI身份的NVIDIA逐GPU指标、GPU预算/原子硬过滤/物理stable key运行时绑定与节点二次准入、只读调度预览/逐候选拒绝解释、数据库时钟对齐的有界服务端趋势/陈旧判断、录像session归属及通用录像字节上传。ZLM/Coturn/中央RTC signaling已归档移除，Windows/Web/Render/Service/Console的Direct Host活动代码和聚焦构建已接通；Direct Host WebRTC 已生产真实视频/数据载荷字节，并以5秒周期、单调sequence和最终终态累计上报；Relay 的媒体、音频和文件逻辑通道已独立建账，入站按已接收载荷、出站只在底层WebSocket完整写成功后累计，五类关闭结果经 Render→Service→Console 保存，本机真实套接字与重连生命周期专项通过。独立纯数据 `px_relay` 已部署公网，Windows Native Relay 的动态实例、活动房间、首帧、窗口、输入及双向字节短测通过，默认 Direct 路径回归通过。Render真实文件引擎现已把操作UUID、实际总量/进度、单文件SHA-256或确定性多文件清单摘要经Service送入Console，真实payload单元测试、本机Service WebSocket桥及PostgreSQL 8组专项通过；开始授权仍由Console在线裁决，已授权transfer的进度/终态先进入Service的DPAPI有界持久outbox，再向Render确认，并在即时通知、节点重连和周期任务中按原幂等正文补报，因此Render退出或Service重启不再丢失待报终态。本人文件传输完成态已通过真实 PostgreSQL、node-control 协议、原生 Console 与实际 Chromium 前端门禁。RDP 的 Console→Service 租约凭证、标准账号/SID 确认、无 GPU/Relay Start、精确 launch 失败回收、真实桥接 I/O 通道累计及五类关闭原因已通过本地短测。仍需 Relay 公网音频、文件独立 hash、真实断线重连/撤销，Direct Host公网Web/音频/持续续租/撤销、Android直连、AMD/Intel逐GPU指标、管理实时流的公网高频/断库专项、RDP公网执行与五类终态实机复验、文件传输公网取消/重试/主机重启故障注入与两端真实字节 hash、无人值守更新及其余产品入口 |
| DB2-EXIT / DB3 | Windows 侧功能出口完成：Desk/Auth 独立产品、Auth 事务 outbox、Official 认证接触和 30/40 秒持续 currentness、Customer 私有离线、库外水位、额度/feature 事务门禁及管理员状态均已接；keyring/恢复代际/监督取消短测通过。Service 只消费 Console control epoch。旧授权、Mongo/Redis/旧 Console 组合根和可误用入口已归档，活动锁文件不含旧后端。Linux systemd unit 静态验证通过，但 Unix binary/SIGTERM 仍须随 DB4/DB5 Linux 总门禁形成动态证据；不建设运行时双后端 |
| DB4 | 恢复集、保留、异机复制、恢复准入/执行/封印、三库写屏障/安全水位、外部见证、Auth keyring、pgBackRest/WAL/PITR、Windows SCM包及WSL2 systemd生命周期已实现。开发期仍需目标Linux发行版VM短测、Pixels外层签名/生产密钥托管、独立主机或对象仓库一次完整恢复、目标环境keyring/见证轮换及真实节点与Windows/RDP事实对账；连续7天窗口和自然周期稳定性统一放到DB5功能通过后的长测，不阻塞每个开发切片 |
| DB5 | 公网 Windows CloudApplication 的 Native Direct/Relay、Android guest 与账号 CloudApplication Native Direct 首帧/启停清理短测已通过，公网录像本人下载也已通过；Windows Panel 的账号、目录、实例与显式资源 descriptor 已切当前 API 并完成公网 API 短测，主机设置旧设备自注册/appkey/加密接入串路径也已删除，Relay 部署配置改由已认证 Console→Service 节点控制下发。Official/Customer 签名 deployment identity 的服务端证书/描述/nonce证明协议、Console 启动交叉绑定、部署私钥和离线根/trust store/证书签发工具已实现；Android、Windows Service、Windows Panel 与 Web Client 已接凭据前验签、nonce证明和持久水位，Panel/Web 还实现 Official 固定端点、Customer 私有端点及按 origin+DeploymentId 隔离凭据。Windows 与 Android 双发行的一次预检/一次升版和独立沙箱编排均已实现并通过软件门禁；Windows 另已接 policy/trust、清单、安装器、Web bundle 注入与聚焦发布边界。两平台都缺正式 approved 身份/签名材料，尚未执行 release-only 双制品；Windows 安装/升级/卸载、Cloud Node/Remote Web 正式双发行与 Android 真机双发行仍未验收。原生 Client 不持有 Console 配置或账号凭据，使用 Panel 已验证后下发的会话 token，不重复建设 Console 门禁。Android Relay 的短期路由票据、客户端选择、真机持久化及同资源会话 descriptor 重签/安全 restart 已实现，但新 Console/Relay 尚未公网部署，不能记为首帧或真实续签通过；Android 设备 ACL 仍未验收。Web Client 已通过当前授权、ICE和DataChannel但公网无RTP，且远端仍托管旧Web资产，不能记为通过；部署被失效机器凭据阻塞。仍需 Android Relay 公网首帧/真实断线续签、Relay 音频/文件/撤销实机复验、RDP/文件/更新、完整制品的自动退役媒体审计与正式安装包实物审计。全部短测通过后再统一长测 |
| DB-HA / P1–P7 | 独立主机 HA、正式发行隔离、授权/连接服务、升级、运维与真实容量/稳定性验收 |

接续依赖顺序：补完 Windows/Web/Android 的 Direct Host 与 Relay 剩余跨端矩阵 → Console录像管理员动作、文件/RDP/更新等保留工作流 →
备份/恢复短测 → 全新部署与完整制品短测 → 统一长测。
资源会话 repository 证据见[CloudApplication/桌面会话与描述符契约](postgresql_resource_session_contract.md)；
专项测试入口与独立 AES 固定向量已经通过，不再列为未开始。Service 已开始切入新节点协议，但其他客户端和真实 OS/媒体链路仍未验收，
不能将协议、存储层或合成节点通过写成产品验收通过。
所有后续代码继续遵守全新开发、无旧数据导入、无旧接口/配置适配、无 fallback；不为了让旧测试通过重建退役行为。
