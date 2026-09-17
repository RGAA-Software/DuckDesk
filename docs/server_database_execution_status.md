# 服务数据库改造：实施与验收状态

> 2026-09-17。只记录实际交付范围；这不是 DB0–DB5 或整个商业化计划的完成声明。

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
Google 的 80 列替代旧 150 列；智能指针/RAII、第三方只读及不做无关全仓重排的约束继续有效。

此前完整回归 `pg-20260917-115055-ce8ce993`：570 项 PASS，Windows/Linux 各 260 个 Rust 用例，
三服务共 274 条 SQLx 查询在线/离线一致（Console 236、Auth 29、Desk 9），Auth/Desk 网页、真实浏览器、进程故障恢复及三库恢复通过。
854 个受验源文件与 Windows/Linux 各 5 个最终工具制品 SHA-256 已在源码继续修改前逐项复核一致。
本报告覆盖密码/用户名边界、每连接 schema 锁、owner bootstrap 门禁、访客来源/API、资源主体入口及此前全部 repository；
仍不代表 Console 产品切换、节点 WS、自动备份执行器或 DB0–DB5 阶段出口完成。

该完整报告之后的 Console 组合根增量已加入严格环境配置和正式私有文件装载：生产数据库固定 full TLS 校验，
显式 deployment/listen/TLS/origin/生命周期/开关，工作区密钥与访客来源密钥必须由权限检查后的文件提供；
缺失、重复、非 32 字节或活动 key 不匹配均在监听前失败，不生成临时 fallback。开发 HTTP 只允许 loopback，
监听端口明确拒绝退役的 20371。该增量的四项单元测试与全目标 Clippy 已通过，但尚未做新的完整跨平台回归。

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
- Auth 产品已切到 PG：29 条 SQLx 查询、新许可证字节契约及独立 OpenSSL 固定向量、提交后返回/精确幂等、续期 CAS/撤销审计，
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

## 仍未通过的阶段出口

Console 入口前置增量：`pg-20260917-091421-1b89be5b` 的 accounts 七组 Windows 专项通过，828 个源文件 hash 复核一致。
覆盖空库初始化竞争/失败回滚、原登录绑定、退出/改密竞争、身份表最小写权限与分组分页；SQLx 已生成 235 条 Console 查询。
这是当时的存储专项；后续身份 API 与跨平台证据见本页最新完整报告，正式产品与全链路验收仍待完成。

| 阶段 | 当前未完成项 |
|---|---|
| DB0 | 已补领域/权限/恢复边界及 Auth 字节/固定向量；其余 Console 字段 SQL 与完整合成基线尚未全部冻结 |
| DB1-EXIT | Desk/Auth 产品服务已接入；Console 尚未切到 PG，不能用 schema CLI 替代三服务验收 |
| DB2-A | 身份/管理 HTTP、密码计算/限流/Origin、访客 HMAC/会话/公开目录、严格配置与稳定私钥加载已实现；本人资料/头像、独立初始化 CLI、产品二进制切换及客户端全链路尚未接通 |
| DB2-B/C/D | 设备/应用/节点/部署目录、user/guest 资源入口与 Console 节点 WS 已接；Windows Service 已切到新节点协议并实现部署准备、调和、命令 fencing 与精确 launch ACK。真实 Console→Service→Render、GPU/RDP 执行、媒体/事件投递及其余 repository 产品入口仍未完成 |
| DB2-EXIT / DB3 | Desk/Auth 独立产品流程已验证；Console 与共享消费者仍待去 Mongo、接新签发/验证及库外水位，Auth 通知 outbox 尚未接通；不建设运行时双后端 |
| DB4 | 恢复集/保留/私有原子发布/恢复前哈希与依赖复核/固定工具/取消超时、持久计划任务、重启补跑、受限实际清理、配置化异机复制及独立 Prometheus/Alertmanager 告警送达已实现；Windows SCM 已真实验收，测试适配器已完成三库逻辑备份和全新库恢复。仍需 Linux systemd 真实宿主、生产宿主工具与最小账号、源主机下线后的异机恢复、恢复准入、权限防复活和生产 WAL/PITR；本机 Docker 专项不能替代这些故障域验收 |
| DB5 | 新环境服务端—Windows—Android 功能回归及完整制品验收 |
| DB-HA / P1–P7 | 独立主机 HA、正式发行隔离、授权/连接服务、升级、运维与真实容量/稳定性验收 |

接续依赖顺序：Console 新 API 和统一切换（包含真实媒体工作流与已实现的更新目录）→ 同步许可证消费者 → 自动备份/恢复 → 全新部署验收。
资源会话 repository 证据见[CloudApplication/桌面会话与描述符契约](postgresql_resource_session_contract.md)；
专项测试入口与独立 AES 固定向量已经通过，不再列为未开始。Service 已开始切入新节点协议，但其他客户端和真实 OS/媒体链路仍未验收，
不能将协议、存储层或合成节点通过写成产品验收通过。
所有后续代码继续遵守全新开发、无旧数据导入、无旧接口/配置适配、无 fallback；不为了让旧测试通过重建退役行为。
