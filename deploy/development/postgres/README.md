# PostgreSQL 本机开发环境

这是全新 PostgreSQL 开发底座。没有 Mongo 数据导入、旧配置兼容或 fallback。
当前提供三库初始化和独立 schema 工具，Auth/Desk 产品入口已接入新存储，Console 仍在逐领域实现。
工具输出 READY 只表示数据库身份和 schema 检查通过，不代替服务 API/客户端验收。

## 启动与使用

仓库根目录执行，要求 PowerShell 7、Docker Linux 引擎和 Windows MSVC 或 Linux 原生链接工具链。本轮实际验证的 Rust 为 Windows 1.95.0、WSL Linux 1.91.1；这不是正式发行工具链已统一锁定的声明：

```powershell
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 Up
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 Status
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 TestSuite -Suite unit
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 TestSuite -Suite database
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 TestSuite -Suite workspaces
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 TestSuite -Suite sessions
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 TestSuite -Suite transfers
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 TestSuite -Suite recordings
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 TestSuite -Suite preferences
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 TestSuite -Suite files
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 TestSuite -Suite backup
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 TestSuite -Suite backup-pg
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 Test
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 Test -Linux
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 PrepareQueries
pwsh -NoProfile -File scripts/server_validation/postgres.ps1 Down
```

`Up` 首次默认选择操作系统允许绑定的空闲 loopback 端口，并保存到本地配置；也可加 `-Port` 显式选择端口（启动前检查可绑定性）。后续读取已生成配置，不会偷偷改已有卷的端口、身份或密码。
使用固定 Compose project `pixels-pg-dev`；数据保存在其专用 named volume，Down 只停止并移除容器/网络，保留数据和本地凭据。
首次生成的随机凭据在 `.env/pixels-pg-dev/postgres.env`，文件受目录 ACL/权限保护且被 Git 忽略。不要提交、粘贴到日志或复制到另一部署。
初始化只对新数据卷生效；修改 env 密码不会自动修改数据库账号，密码轮换不属于本轮入口。

`Test` 每次建立随机 project、loopback 端口、部署 ID、三库及独立数据卷，不使用开发卷。
结束时删除本轮测试容器、网络和数据卷；它们只有合成数据，可重新生成。凭据文件与脱敏报告保留在本地忽略目录。
失败返回非零，不因缺环境跳过；报告位于 `test-results/server_validation/<run_id>/report.json`，逐命令日志在同目录。
报告记录代码文件 hash、Git revision、二进制/镜像摘要、工具版本及实际测试结果。

`TestSuite -Suite <名称>` 用同样的独立新库，只执行选定 Console 原生套件：unit、identity、control、devices、applications、
guests、nodes、deployments、instances、commands、workspaces、database、sessions、transfers、recordings、preferences、files、backup、backup-pg。
files 是共享私有文件/缓存实际 IO 测试，不需要业务数据库，但沿用隔离运行器与报告。
backup 是 DB4 恢复集、保留、私有原子发布、工具身份、取消与超时，以及持久计划任务、重启对账和受限清理内核；
不把内核测试冒充已经安装并运行的 SCM/systemd 服务或生产恢复演练。
backup-pg 通过测试专用 Docker 适配器，实际调用隔离 PostgreSQL 18.6 内的 `pg_dump`/`pg_restore`，将 Console/Auth/Desk 三库发布为同一恢复集，
分别恢复到三个全新数据库并核对部署身份，同时验证发布后归档篡改会被拒绝。该适配器只属于测试，不是生产备份执行器；
它不代替固定宿主工具、专用备份账号、定时服务、异机复制、权限回退对账或 WAL/PITR 验收。
专项保留逐用例状态/数量和源码 hash 门禁，报告明确标记 FOCUSED-ONLY。
该模式不执行浏览器或全量跨平台验收；只有 backup-pg 专项执行自身定义的三库恢复检查。专项模式不接受 `-Linux`；

备份告警链可独立执行：

```powershell
pwsh -NoProfile -File scripts/server_validation/backup_alert_delivery.ps1
```

该脚本使用唯一命名的临时 Docker 网络和容器，验证 Prometheus → Alertmanager → webhook 的真实 firing 通知并在结束后精确清理；
结果保存在 `test-results/backup-alert-<run-id>/`。它不修改开发 PostgreSQL 容器，也不代表独立主机故障域已经验收。
修复先用它定位，再运行 `Test -Linux` 完成阶段回归。
未显式指定 Suite、把 Suite 传给其他 Action 均拒绝，避免误以为执行了所选范围。

`PrepareQueries` 只用于有意修改 SQL/schema 后，针对独立全新三库生成各 crate 的 SQLx 离线元数据；不计为验收。
它只更新登记的 `.sqlx/query-<hash>.json`，之后必须重新运行 `Test`，不能用生成元数据替代在线/离线一致性检查。
开发阶段新建 schema 已改变时，旧开发卷会按 checksum 门禁拒绝；不会自动篡改 ledger 或转成兼容模式。
正式版本升级另用受控新 migration；开发卷重建须明确限定对应数据卷，不触及其他 Docker 数据。

## 数据库与角色

| 数据库 | 建表/升级账号 | 运行账号 | 首批表 |
|---|---|---|---|
| pixels_console | pixels_console_owner | pixels_console_runtime | users/login_sessions、groups/members、授权 outbox/audit、devices/关联/授权/audit |
| pixels_auth | pixels_auth_owner | pixels_auth_runtime | authors/author_sessions、customers、licenses/issuances/requests/audit |
| pixels_desk | pixels_desk_owner | pixels_desk_runtime | feedback、versions、admin_sessions |

每库都有独立 `pixels` schema 和只读部署身份。运行角色只能访问本服务明确授权的表；不能跨库、DDL、提权或改部署身份/schema 版本账本。
业务启动/ready 额外检查最小 runtime 角色，拒绝 owner/超级用户或被额外授予建表能力的 runtime。schema CLI 的 owner 检查与业务就绪不同。
容器内 `pixels_admin` 仅用于本地初始化及合成数据备份恢复验收，不作为产品运行账号。
首批表不是完整业务模型；不宣称 DB2/DB3 已完成。新用户采用 UUID、UTC 时间、密码 hash 和授权 revision，没有旧开发版字段适配。

初始化脚本在 `init.sh`，必须保持 LF。PostgreSQL 18 的数据卷挂载在 `/var/lib/postgresql`，由固定镜像管理实际版本子目录。
镜像锁定为 `postgres:18.6@sha256:4ef4dbc939d61acea57712655ddb4b4ab27419c913f94cca0cd57cb3ea3c2280`；SQLx 锁定 0.8.6，并提交 Rust workspace 的 Cargo.lock。
PG 目录依据见 [Docker PostgreSQL 官方镜像](https://hub.docker.com/_/postgres)，schema 执行机制见 [SQLx 0.8.6 Migrator](https://docs.rs/sqlx/0.8.6/sqlx/migrate/struct.Migrator.html)。

## Rust 基础模块

`rust_server/px_pg` 的库提供脱敏配置、连接池、错误分类、部署/schema 检查。默认 TLS verify-full；明确本机开发模式只接受 loopback 主机，拒绝 URL 参数覆盖 host、schema、TLS 和超时。
SQL 日志关闭，公开错误不透传服务端消息、查询、DSN 或约束细节。失败的写请求不能仅凭连接中断判定已回滚，业务阶段需按幂等键对账。
共享库通过参数接收 schema 描述，不依赖领域 SQL；独立工具 `px_db` 组合三个服务各自目录的版本化建表脚本。

```powershell
cargo check --locked --manifest-path rust_server/Cargo.toml -p px_pg --target-dir .cache/pg-cargo
cargo test --locked --manifest-path rust_server/Cargo.toml -p px_pg --lib --target-dir .cache/pg-cargo
cargo clippy --locked --manifest-path rust_server/Cargo.toml -p px_pg --all-targets --features pg-integration --target-dir .cache/pg-cargo -- -D warnings
```

真实 PG 测试通过上面的 `postgres.ps1 Test` 执行；直接启用 pg-integration 却缺测试环境会失败。
工具命令为 `px_db migrate <console|auth|desk>` 与 `px_db check <console|auth|desk>`。
前者名称沿用 SQLx 的 schema migration 术语，只做新库建表/未来正式 schema 升级；无旧数据搬迁入口。运行时健康检查不执行 DDL。
配置通过环境变量 `PIXELS_DATABASE_URL`、`PIXELS_DEPLOYMENT_ID` 和本机模式 `PIXELS_PG_LOCAL_DEVELOPMENT=1` 传入，密码不进入命令行参数。

## 当前验收范围

录像缓存专项：`pwsh -NoProfile -File scripts/server_validation/postgres.ps1 TestSuite -Suite cache`。
该专项验证真实 PG 与本机私有文件的发布、原登录撤销、读取短租约、保留 CAS 和两阶段清理；
`-Suite files` 验证独立文件/跨进程锁。两者不替代媒体 HTTP/Range、播放器或 Console 产品端到端验收。
`-Suite activity` 验证资源会话下的连接观察、访问历史、二十路通道名额竞争、原生产者/代际和权限边界。

已实现的自动用例：配置拒绝/脱敏、三服务身份、权限隔离、事务、唯一约束竞争、连接池耗尽恢复、错误密码、建表重复/锁超时、schema 篡改/缺失/未来版本拒绝。
运行器还验证停库与恢复、重启数据保留、恢复冒烟，以及 DB4 恢复集执行链的实际三库 pg_dump/pg_restore、发布后逐档哈希复核、
全新目标库内容核对与篡改拒绝，并精确清理本轮测试资源。
已新增 Console 身份/用户组/管理/设备 repository，SQLx 查询在线检查/离线元数据比较、真实杀迁移子进程和两进程重试、100 轮登录/改密竞争、
用户/组/设备变更中途失败回滚、20 路 CAS 与 outbox 租约竞争、三库数据/约束/索引/关系恢复冒烟。准确查询/用例数以运行器及最新报告为准。
`-Linux` 在 Windows 用例后使用 Ubuntu-20.04 WSL 原生 Rust 测试程序访问同一轮隔离测试库；默认不联网取依赖，缺依赖失败而非跳过。
各子命令有 600 秒硬超时，超时终止的仅是当前创建的进程树；预期失败用例也不接受超时冒充正确拒绝。
Desk 新服务已加入：9 条 SQLx 查询在线/离线核对、6 个真实 PG/API/原生进程用例，Windows 浏览器表单/管理操作及实际服务重启、PG 停机恢复。
前端提交幂等 ID 单元测试覆盖不确定结果重试与成功后的新提交。开发 PG 使用 terse 错误输出且不记录错误绑定参数，避免约束错误打印完整个人信息行。
Auth 新服务已加入：29 条 SQLx、许可证固定向量与签发/撤销事务、管理员 bootstrap/显式密钥工具、文件 ACL、原生进程与管理网页，
包括提交响应丢失后的精确重试、续期/撤销、会话注销和真实 PG 停机恢复。
这些仍是 DB1 / DB2 部分 / DB3 Desk/Auth 独立子集。完整 Console API/ACL、许可证消费者、全部两进程业务竞争/故障扰动、自动备份和 Windows/Android E2E 仍待实现与验收。
Desk 的[配置与开发说明](../../../docs/px_desk_web_overview.md)、[字段/安全契约](../../../docs/postgresql_desk_contract.md)是新入口，不再使用旧固定端口/密码文件/远程覆盖脚本。
身份新规则及用例边界见[身份存储契约](../../../docs/postgresql_identity_contract.md)。
设备规则见[设备契约](../../../docs/postgresql_device_contract.md)，Auth 见[配置/开发说明](../../../docs/px_auth_server_runtime_config.md)。
完整阶段规则见[逐步测试计划](../../../docs/server_incremental_validation_plan.md)。
