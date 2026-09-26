# Customer Single Server：Linux 与 Windows 实施计划

> 2026-09-26 更新：1.0.3 的预制配置安装流程已由 1.0.4 一键安装流程替代。Windows Setup 单 EXE 与 Linux 版本镜像/Compose 共用 Rust 首次初始化逻辑；不要求预制 JSON、env、证书或许可证。下文 1.0.3 数据和前置清单仅为历史记录，不能当作当前安装说明。当前操作见 [Windows Setup](../deploy/single_server/windows/README.md) 和 [Linux Compose](../deploy/single_server/linux/README.md)。

## 0. 一键安装目标与最小改造

- Windows 交付一个自包含的 Setup EXE，内含 Console、Relay、Backup、Web 页面、初始化工具、PostgreSQL **客户端**工具和配置生成逻辑。首次运行不要求用户预先创建 JSON、env、证书、许可证或 Relay token；安装器自行建立受限私有目录和持久数据目录。同部署升级直接覆盖安装，保留身份与数据。
- Linux 交付一个可直接执行 `docker compose up -d` 的 Compose 文件及版本固定的服务镜像；不要求先运行另一段配置生成脚本。Compose 首次启动进入同一网页初始化流程，升级只替换版本镜像并重新执行 Compose。PostgreSQL 服务仍按既定边界由客户另外安装，Pixels 不捆绑或卸载它；初始连接信息通过网页输入，不通过预制 `database.env`、`console.env`、`relay.env` 或 `backup.json` 交给用户。
- 初次安装仅开放受限初始化入口，未配置完成时不开放业务 API、Relay 会话或流服务。网页引导管理员填写已安装 PostgreSQL 的连接信息、具备创建本产品数据库/角色权限的一次性管理凭据，以及初始 Pixels 管理员凭据；安装流程创建本产品专用库与角色，复用现有 `px_db`/`px_console_admin` 完成 schema、密钥、证书、配置和 Relay 注册，并生成 Backup 的 Console-only schema 2 计划。PostgreSQL 服务安装本身不属于 Pixels 包；数据库管理凭据只用于初始化，不持久保存。连接失败、权限不足时在网页明确提示，允许修正重试，不留下半授权业务服务。
- 授权在安装后的 Console 管理网页导入由 Auth 签发的 `PXLIC2`，Console 使用随包固定的可信**公钥**验证 deployment、期限、服务和 stream 限额；私钥和客户许可证不打入安装包。未授权或过期时管理员仍可登录、查看状态及更换许可证，所有会话与流业务保持关闭。Relay、Backup 进程可为初始化及数据保护而运行，但 Relay 不得获得可用业务会话；不得生成测试许可证或绕过验证。
- 网页初始化与授权是两个明确状态。配置、证书、数据库、管理员凭据和许可证保存在包外受限持久目录；Windows 卸载、Linux `compose down` 不删除它们。首次安装、网页初始化、授权、同部署覆盖升级、卸载重装分别短测；至少断言未授权业务被拒绝、授权后 Console/Relay/Backup 可用。
- 只保留一套初始化业务逻辑，由安装器/Compose 调用；NSIS、Shell 与网页不得各自维护一份数据库/Relay/Backup 编排。现有 1.0.3 包可作为实现基线，但必须重新构建并验证新包后，才能交给用户做图形安装验收。

实施状态（2026-09-26）：Windows 1.0.4 快速 Release 候选已完成空 PostgreSQL 18 上的网页初始化短测：自动创建库/角色、Console TLS/管理员、Relay 登记和 Backup 配置，三个 Windows 服务运行；同部署 Setup.exe 覆盖安装成功，卸载后服务和程序移除而配置/数据保留。从空目录静默运行单个 Setup.exe，以及未初始化时重复运行 Setup 恢复本机网页，也已验收。Linux 1.0.4 候选镜像/离线包已在 Docker Desktop 的 Ubuntu 24.04 容器环境短测：`./deploy.sh` 一条命令启动、网页初始化、三项业务进程运行、管理员登录、`compose down/up` 后身份保持、卸载仅删容器并保留命名卷。正式 1.0.4 优化发行的 Windows Setup 与 Linux Compose 归档均已生成并通过逐文件 SHA-256 核验；正式 Windows 包另以隔离 PostgreSQL 18 完成管理员登录、Relay ready/fresh、Backup verified/restore 及覆盖保留数据短测，正式 Linux 镜像已在 Docker 装载并验证三支服务程序及 PostgreSQL 18.6 客户端工具。随包公钥已用 CN Auth 实签短期 PXLIC2 和正式包内 Console 验签，错误 deployment 被拒绝；这张测试许可证只用于隔离验证，不交付客户。尚未在原生 Ubuntu 主机、客户真实环境或实际 Render 媒体会话上验收，不据此宣称这些场景通过。`check-setup-database` 与 `initialize-setup-database` 仍保留为管理员诊断工具，不是安装前置步骤。首次初始化要求 PostgreSQL 18 超级用户，完成后不保存其密码；若在数据库创建中途失败，残留资源须人工核对，不自动破坏已有库。

> 2026-09-25。本文记录 Single Server 的范围、实现与剩余验收边界。原有 1.0.2 Linux systemd 包不等于新的 Compose 包。目标是一个客户部署、一台主机运行三项 Pixels 服务；Linux 使用 Docker Compose 一键部署，Windows 使用原生 Setup。PostgreSQL 等基础环境由运维单独安装和管理，不属于本部署包。不建设集群编排或自动升级系统。

以下记录保留 1.0.3 的历史验收背景：该版 Linux/Windows 优化 Release 制品通过清单核验及预制配置环境短测，但其安装交互已被 1.0.4 候选替代。它不再定义当前安装前置条件，也不能用来声称 1.0.4 的图形界面、原生 Ubuntu 或客户环境已验收。

## 1. 产品边界

| 项目 | 决定 |
|---|---|
| 运行服务 | Console、Relay、Backup 三项；Broker 仍是 Console 内部连接编排能力，不另发进程。 |
| 随包工具 | `px_console_admin`、`px_db`、Console Web、PostgreSQL 18 客户端备份工具，以及按平台需要的安装/校验脚本。工具不是常驻服务。 |
| 不入包 | `px_desk`、Desk Web/数据库/安装项、官方 `px_auth` 与签发私钥、许可证、客户证书和密码。Desk 是官网服务，源码及其独立发行不受本计划影响。 |
| 基础环境 | PostgreSQL 18 可在同机或外部主机，由客户/运维单独安装、备份其系统配置并管理生命周期；Pixels 不打包 PG 服务或 PG 容器，也不安装、卸载或升级它。将来若实际引入 Redis，同样只在部署文档规定版本、可达性和配置，由运维单独安装；当前产品不要求 Redis，首版预检也不得把 Redis 当成前置条件。 |
| 发行 | Customer Server 一个独立产品版本；Linux 交付 Docker 镜像与 Compose 一键部署文件，Windows 交付 x86_64 Setup。两平台运行相同三项服务、各自校验制品；不是 Cloud Node、Client、Remote 的安装包。 |
| 网络和授权 | 对外入口、Relay endpoint、HTTPS 证书/私钥、PostgreSQL CA、绑定 deployment UUID 的 `PXLIC2` 与 Auth 公钥信任文件由运维提供。自签 TLS 证书允许使用，但仍校验证书、主机名和有效期；不跳过 TLS 验证。私有 Console 离线验许可证，不请求官方 Auth。 |

首版不承诺异机容灾、PostgreSQL HA、双 Render 或跨主机 Relay 迁移。本机 Backup 可恢复数据库误操作，不能防止同机磁盘丢失；异机副本属于商业发布前另行配置的备份目的地。Windows 不做 Authenticode 签名，Android 规则不受影响。

## 2. 安装输入与最终状态

首次安装只要求另行部署的 PostgreSQL 18，且它可通过 `verify-full` TLS 从 Server 访问。Windows 运行一个 Setup.exe；Linux 解压包后运行 `./deploy.sh`（或预先加载镜像后 `docker compose up -d`）。本机 `127.0.0.1:4700` 网页填写 PostgreSQL 主机、端口、超级用户密码、CA PEM、Console 公网主机和初始管理员。Linux 容器内的 `127.0.0.1` 不是宿主机，数据库地址须从容器网络可达。

初始化器生成 deployment UUID、专用数据库角色、Console/Relay/Backup 配置、Console HTTPS CA/证书和密钥；一次性数据库超级用户密码不写入运行配置。随包只有 Auth 的许可证验证公钥，没有签发私钥。管理员随后在 Console 网页导入与这个 deployment 匹配的 PXLIC2。未授权时允许管理员完成授权管理，但不开放业务会话。Windows 的 `Pixels.Setup` 和 Linux 的 `setup` 容器只用于本机初始化；完成后不承担业务流量。配置和数据持久化在程序/镜像之外。

## 3. 共用的单机安装流程

1. 校验安装包清单/哈希、Customer/平台身份和目标目录。首次安装创建受限配置/数据目录；同 deployment 覆盖只替换程序，不卸载基础环境或清除数据。
2. 本机网页核验 PostgreSQL 18 超级用户与 CA/TLS，创建新的 Console 数据库和 owner/runtime/Backup 角色，迁移 schema，生成管理员、密钥、证书、录像缓存及 Console-only Backup 计划。Auth/Desk 在 Backup 计划中为 `not_applicable`。不接管已有同名库，也不自动删除失败后已创建的资源。
3. Console 在未授权状态启动并允许管理员登录、导入 PXLIC2；登记本部署 Relay 并把一次性 node token 存入受限文件后启动 Relay。初始化中断时同一 Setup/Compose 可恢复；授权前 Relay 不获得业务会话。Backup 独立启动，其 `pg_dump`/`pg_restore` 受精确摘要约束。
4. 运维从 Console 网页导入正式许可证后再验收授权后的业务能力、Relay ready/fresh 和 Backup verified 恢复点；无许可证的安装短测不声称这些结果。

单机入口复用现有业务 API 和初始化工具，不复制另一套 Console/Relay/Backup 业务生命周期。安装失败不自动抹除数据库、许可证、备份或旧 release/镜像；schema/数据恢复仍需运维依据备份处理。卸载默认停用三个 Pixels 服务或容器，保留数据库、配置、许可证与备份；清除数据必须另有显式操作。

升级和卸载保持简单：同一 deployment 直接再次安装新包，Linux 由 Compose 替换三容器，Windows 停三项 SCM 服务、替换程序并重启；不先卸载，也不建立跨节点升级事务。卸载只移除本机 Pixels 运行项及程序，数据库、配置、许可证、备份保留。Windows 替换失败只尝试恢复本机上一版程序和运行状态，不替运维回滚数据库 schema。

## 4. Linux：Docker Compose 一键部署

目标为 Ubuntu 24.04 x86_64 + Docker Engine/Compose。离线包包含版本化镜像归档、`compose.yaml`、`deploy.sh` 和清单；没有预制配置样板。Compose 首次启动含一个本机初始化容器，完成后退出；常驻的只有 Console、Relay、Backup 三个容器。PostgreSQL、Redis、Desk、Auth 不在 Compose 中。

- 新 Linux 镜像只包含三个服务、Console Web、初始化工具和 PostgreSQL **客户端**工具。正式构建不再编译或复制 Desk 可执行文件、Web、样板、systemd unit、版本字段；镜像清单和负向扫描拒绝 Desk/Auth/签发私钥。`px_desk_server` 仍由官网独立发行。
- `./deploy.sh` 是离线包的一条命令入口：先验包、`docker load`，再执行 `docker compose up -d`。也可预载镜像后直接运行 Compose。初始化器与三项业务服务共享命名卷；它自动创建私有配置和数据，不要求手工准备 `env`、JSON 或证书。Console/Relay 对外分别使用 4600/4605，Backup 不开放端口。
- 数据库地址必须从容器网络可达；同机 PostgreSQL 也不能填容器内 `127.0.0.1`。文档说明 PostgreSQL 的监听、网络准入、TLS 主机名和账户前置要求，但 Compose 不启动或修改数据库。配置目录、录像缓存和备份仓库使用明确的持久挂载；不把数据库数据目录挂入 Pixels 容器。`docker compose down` 不删除这些挂载或基础环境。
- 同 deployment 重跑复用命名卷内的身份和 Relay token；新镜像用版本标签及摘要，不使用 `latest`。不自动迁移旧 systemd 部署，不静默删服务或数据库。许可证导入/更换由 Console 管理网页完成，不依赖 Auth 在线服务。

## 5. Windows：新增正式 Single Server 包

目标为 Windows x86_64 原生运行，不依赖 WSL、Docker 或 90 的测试部署目录。输出一个 Customer `PixelsServer_<version>_Setup.exe`、安装包清单和逐文件 SHA-256；使用仓库已固定的 NSIS 工具链，明确标注 unsigned。安装身份独立于三个桌面产品，安装目录与私有配置/数据目录分离。

- 新发行入口统一构建 `px_console.exe`、`px_relay.exe`、`px_backup.exe`、`px_console_admin.exe`、`px_db.exe`、Console Web 与已审核的 Windows PG18 客户端工具；禁止 Desk/Auth、测试凭据、日志及旧构建产物混入。现有 Console、Backup 独立组包脚本可复用校验/收集逻辑，不把它们的旧输出直接拼接成正式包。
- Backup 已有原生 SCM 服务入口和安装脚本。Console、Relay 当前公网部署使用计划任务/launcher，不能把测试任务当作正式客户服务；本批为两者接入 Windows SCM 生命周期，并让安装器使用稳定的服务名、受限身份、启动/停止和失败恢复。无需通用服务包装器或第二套监督进程。
- 安装逻辑先实现可独立调用、可短测的 PowerShell 单机入口，再由 NSIS 做薄封装；不要在脚本和 NSIS 中各写一份 Console schema 初始化、Relay 登记或回滚逻辑。覆盖安装停服务、校验新包、替换软件、保留私有配置/数据并检查三服务；卸载不卸载 PostgreSQL、不清除数据库和备份。

## 6. 版本、短测与完成门槛

日常开发使用聚焦 Release 构建及隔离候选，不运行正式全量入口。2026-09-25 已为本次正式优化构建预留 Server suite 1.0.3，Linux 与 Windows 制品均已生成并通过清单核验；下一版本水位为 1.0.4。制品构建成功不等于全部业务验收完成，也不把 development 候选冒充正式包。

| 切片 | 完成条件 |
|---|---|
| S1 包边界 | Linux/Windows 清单只含三服务与必要工具；Desk/Auth/私钥/测试字节负向检查；Backup 的 Console required、Auth/Desk not_applicable 计划通过。 |
| S2 Linux Compose 入口 | 已通过开发短测：离线包一条命令启动、本机网页从全新 PostgreSQL 18 创建部署、三个业务容器运行、管理员登录、`compose down/up` 后身份保持、卸载保留命名卷。授权后的 Relay ready 与 Backup verified 恢复点留待签发正式许可证后验收。 |
| S3 Windows 服务与入口 | 已通过开发短测：单 EXE 空目录首装、网页从全新 PostgreSQL 18 初始化、三项真实 SCM 服务运行、管理员登录、同包覆盖、未完成时重复安装恢复初始化页、卸载保留配置/数据。授权后的业务验收同上。 |
| S4 正式制品 | 1.0.4 快速 Release 候选双平台包与哈希已生成，不冒充优化 Release 正式制品。正式发行、图形手工点击路径、原生 Ubuntu 24.04 主机和客户环境尚未验收；不要求长测或第二台 Render。 |

验收只用隔离 deployment 和短测数据。运维另行准备 Docker Engine/Compose（Linux）、PostgreSQL 18、可达网络及数据库 TLS CA；数据库角色、Console 证书和 Backup 配置由首次网页创建，许可证在 Console 网页导入。未来实际需要 Redis 时再增加要求，不预装、不预检未使用的 Redis。自签证书允许；统一长测、独立故障域灾难恢复和 HA 不被本计划伪称完成。
