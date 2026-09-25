# px_auth Linux / CN 部署记录

> 部署边界（2026-09-25）：CN 是 Pixels 官方 Auth 的正式机，不是“90”测试机。Auth 在 CN 执行 `PXLIC2` 签发，并保留其数据库/备份配套；
> Console、Relay、Desk 及其备份等其他 Server 组件可在客户自己的私有环境部署，不要求放到 CN。客户可使用自行生成或自签的 CA/HTTPS 证书；
> 不以“90”的测试 CA/SAN 作为正式私有部署的验收标准。本条仅明确部署与证书来源，不新增证书专项验证。

> 2026-09-22 现行结论：Auth 只负责签发、续期和撤销 `PXLIC2` 管理记录，不再提供在线许可证验证端点。下列 2026-09-21
> 摘要是当时部署证据；其中 `/api/auth/licenses/verify`、机器绑定、Console 在线 currentness 和库外水位已经被新契约废弃，不能作为
> 当前实现或下一次部署的验收标准。CN 下一次部署必须直接替换为新 schema 和新二进制，不迁移开发数据或保留兼容路由。

## 2026-09-22 最小许可证版本部署

CN Auth 已按全新开发约束原地重建 `pixels_auth`，只执行当前四项 migration，不迁移 2026-09-21 的开发数据。当前发行目录为
`/opt/pixels/auth/releases/3.2.11-521faf39eb94`，`px_auth`、`px_auth_admin`、`px_db` 的 SHA-256 分别为
`521faf39eb94e54719dc12a749f0e294c4f2300c19c91acbaf1fa46b6ae6032c`、
`fbcb6da4930668470cc3209305d1db3cc4a6c166dc8df4f196a978326491eed0`、
`5bca7b57137df58fae4cd4051de8d5b252970d2f04d1824da50703dafe0d51c4`，均与本次聚焦 Linux Release 构建一致。
`pixels-auth@63706848-cf7b-4a50-9652-f4fab461aa26.service` 与对应备份服务均为 enabled/active；公网 live/ready 为 204、首页为
200，管理员登录、客户创建、最小许可证签发和注销通过，已退役 `/api/auth/licenses/verify` 返回 404。

本次签发给“90”公网 Console 的最小 `PXLIC2` 只含 deployment、revision、签发/到期时间、`max_streams=4`、三个授权服务和
key ID；许可证 ID 为 `9694c8aa-f3ac-40da-9a7f-75bbaeaa1774`。Console 已安装签名许可证和 schema 2 公钥信任文件，不再配置
Auth 在线验证、机器摘要、Authority deployment、库外许可证水位或部署证书。

备份配置中残留的 Auth schema 5 已修正为当前 schema 4；修改前的私有配置与调度状态已保存在远端 `config-history`，随后重新执行当前
调度槽。恢复集 `4284840d-5e7a-4332-96ad-4a3577122a28` 已发布为 `verified`，其中 `auth.dump` 为 31,813 字节、SHA-256 为
`24bff182a709519e9049bec6864de861c8a80a1bfe17f92350e3ddcc091e73b8`，manifest 明确记录 Auth schema 4。对应的本地真实三库
备份、异地恢复和篡改拒绝专项报告为 `pg-20260922-191002-a0a00c21`。

“90”公网 Console 也已换用当前聚焦 Release 二进制和全新 29 项 schema。旧 migration 19 checksum 与当前源码不一致，因此直接重建
开发库而非增加兼容 migration；当前 deployment 为 `7c8b9e09-04c1-4b9f-a13f-07d15a4be097`。公网 live/ready 为 204、首页为
200，管理员登录、本地许可证状态、会话注销及注销后拒绝均通过；许可证、信任文件和二进制与本机 SHA-256 一致。重建会清除旧节点、
应用和用户开发数据，后续端到端云功能验收必须先让 Service/Render 按当前协议重新登记。

## 2026-09-21 PostgreSQL 新服务

CN（`49.232.190.218`）只承载官方 Auth，公网入口为 `https://auth.rgaa.vip`。Console、Relay、Render 和节点仍在既有“90”公网环境，
没有迁移到 CN。此次按全新开发产品部署，不导入 Mongo 数据，不复用旧 TOML/JWT/HMAC/API 或旧许可证签名私钥，也没有兼容回退。

- 新 Auth deployment：`63706848-cf7b-4a50-9652-f4fab461aa26`；由
  `pixels-auth@63706848-cf7b-4a50-9652-f4fab461aa26.service` 以无登录 `pixels-auth` 账号托管并开机启用。
- Auth 仅监听 `127.0.0.1:30400` 的后端 TLS；Nginx 保持 `auth.rgaa.vip:443` 与有效 Let's Encrypt 证书，不把后端端口暴露公网。
- PostgreSQL 18.6 镜像固定为
  `postgres:18.6@sha256:4ef4dbc939d61acea57712655ddb4b4ab27419c913f94cca0cd57cb3ea3c2280`，容器
  `pixels-auth-postgres` 只发布 `127.0.0.1:54329`，使用单独持久卷，数据库连接使用 verify-full 和本部署私有 CA。
- 只创建 `pixels_auth`、最小权限 `pixels_auth_owner`/`pixels_auth_runtime` 和用于三库备份对账的内部 deployment 记录；当时的五项 migration、运行角色检查、
  新 Ed25519 PKCS#8 私钥、与数据库 recovery generation 绑定的新信任根和一次性管理员初始化均完成。
- 发行目录为 `/opt/pixels/auth/releases/3.2.11-0ae2966e34b3`，稳定链接为 `/opt/pixels/auth/current`。远端
  `px_auth`/`px_auth_admin`/`px_db` 与本次聚焦 Linux 构建 SHA-256 完全一致：
  `0ae2966e34b342d38cc391394219687460e71cb066253e3233ce0c834eb44fbc`、
  `da6ab4a751f4d38196182a72767c08577970af0cecb77db36944b909f62518d4`、
  `77ecaeeee57d38d01c60d0597c4835832b76106ba5f6c6e82df1355db67a3c67`。
- 旧 Supervisor 配置已改名为 `px_auth_server.conf.disabled-postgresql`，旧目录只保留作人工回退证据，不参与启动或请求处理。
- Auth 只信任 loopback Nginx（`PIXELS_AUTH_TRUSTED_PROXY_IP=127.0.0.1`）提供的单一客户端 IP；Nginx 用 `$remote_addr`
  覆盖 `X-Forwarded-For`，不追加或继承客户端提供的地址链。非可信 peer 的转发头不参与限流，可信代理缺失、重复、链式或非法地址均拒绝。

开发期短验收通过：公网首页 200、live/ready 204；旧 `/api/v1/ping` 404；匿名管理请求 401；管理员登录、`/me`、客户列表、注销及
注销后会话拒绝全部符合契约。systemd 重启后恢复 ready；短停 PostgreSQL 时 live 保持 204、ready 为 503，数据库恢复后 ready 回到
204。伪造的公网 `X-Forwarded-For` 链被 Nginx 覆盖后登录/注销正常；直连后端缺少该头返回 400，合法单一来源进入正常认证并返回 401。
三支 Linux 制品 `ldd` 无缺失依赖。部署临时目录及其中的一次性初始化凭据已经删除。这些 Auth 运行证据不替代最终长稳、正式外层签名、
异机复制/恢复和告警验收。

## Auth 本机备份

同一 deployment 已启用 `pixels-backup@63706848-cf7b-4a50-9652-f4fab461aa26.service`。备份执行器使用独立无登录账号和只读
`pixels_auth_backup` 数据库角色；实测具有 CONNECT、schema USAGE 和表 SELECT，但没有 UPDATE。`pg_dump`/`pg_restore` 与数据库来自同一个
固定 PostgreSQL 18.6 镜像，不向备份账号开放 Docker；其摘要分别为
`66115325f4e49f7f9c79a83cfc89d2c9a1698858a1ae9786ae7595cd7a9f2de9` 和
`06115b93c3d1bf9d7c62563abb595792ea90acb9083233fd293eac1b34695840`。`px_backup` 摘要为
`d7a3b1a9b11e981a4b4df8f32fc412d48a005879cf30b2917e56a8195363293d`。

首个本地 recovery set `7a6551f3-7aea-4e73-91b2-b3e5b3a8eb6b` 已发布为 verified；`auth.dump` 为 37,367 字节，manifest 与文件
SHA-256 均为 `79c0621ce533d02a2d2bea074d0ad6d531eb927820293484ecd3a0034888ab84`，并通过固定 `pg_restore --list`。manifest 明确把
Console 和 Desk 标为“仍在 90 环境”的 not_applicable，只备份 CN 的 Auth，不跨公网读取 90 数据库。当前每小时执行，保留 24 个小时、
7 个日、4 个周、6 个月、5 个升级前恢复集，手工恢复集保留 30 天。

当前 `offsite_configured=false`，所以这只关闭 CN Auth 的本机定时备份门禁，不构成异机容灾；独立对象存储/备份机复制与异机恢复演练仍是
DB4 剩余项。

## Console 许可证消费边界

2026-09-25 短验收：使用 CN Auth 中已有、明确标为测试用途的客户 `Pixels Official public validation`
（`84368115-c76d-455d-9667-dc55dd6ce673`），为全新的隔离 Customer deployment
`7b934787-f4e0-4cc4-aa41-03222a5960c6` 签发了两小时有效的 `PXLIC2`
（license `e0837fc1-227b-4bff-bbb9-6889cf94ad0b`），仅授权 `cloud_applications` 和 1 路并发 stream。
现有 Release `px_console_admin` 使用当前 Auth 公钥信任文件离线导入成功，换成错误 deployment ID 后拒绝；未覆盖“90”的现有许可证，
也未在客户机器上正式安装。隔离 PostgreSQL 的 `sessions` 聚焦套件 14/14 通过，其中包含服务权限拦截与最后一个 stream 名额的
并发原子性检查；报告为 `test-results/server_validation/pg-20260925-021002-310c206e/report.json`。这些是签发/本地校验与
仓储层功能的组合证据，不等于真实私有部署的完整端到端验收。可重复的短测入口为 `scripts/test_cn_private_license.ps1`；
当前默认只检查仓库中固定的公开 Auth 公钥测试 fixture 的 key ID；该 fixture 曾从 90 的现有公钥信任文件提取，但运行时不访问 90，也不预检 90 的现有许可证。显式传入 `-Issue` 才会在该测试客户名下再签发一张短期许可证，并用该公钥及真实 `px_console_admin` 离线验签。公钥轮换时须由可信 Auth 运维渠道更新此 fixture；它不是客户交付的信任根，也不包含签发私钥。

本次发现 CN Auth 服务器时钟约比执行验收的 Windows 机器快 11 秒。Console 按 `issued_at` 拒绝尚未生效的许可证，因此签发后
立即导入可能暂时失败；脚本只等待签发时间到达，没有调整 CN 时钟。前两次诊断尝试也在同一测试客户名下产生了两小时有效的
测试签发记录，未交付客户，将按到期时间失效。正式运维应保持 Auth 与 Console 主机时间同步；本次不扩展为证书专项验收。

同日进一步使用 `scripts/server_validation/postgres.ps1 TestSuite -Suite cn-license` 完成运行时短闭环：脚本为隔离 PostgreSQL
deployment 显式签发新许可证，将信任文件和许可证仅交给该测试进程；真实 `ConsoleRuntime` 加载后，管理员登录并读取到
`cloud_applications`、`max_streams=1` 的许可证摘要。Android 用户启动未授权 RDP 返回 403，启动已授权 WebView 则越过
许可证门禁、因隔离环境无 Render 节点返回 503；两种请求均未留下实例。最终报告
`test-results/server_validation/pg-20260925-022437-e818efe8/report.json` 为 1/1 PASS，严格 Release Clippy 通过，
测试数据库/容器/卷与本地许可证副本已清理，“90”现有许可证及部署未改。此测试在正式 Auth 的现有测试客户名下又产生了短期
签发记录；它们只会自然到期，不作为实际客户交付。该首轮没有可用节点，尚未覆盖实际会话的额度占用与释放。

随后把同一 `cn-license` 用例扩展为受认证的节点协议短测。隔离 Customer Console 仍消费 CN 实签的 1 路云应用许可证；
模拟节点完成上报、应用准备、启动回执和停止回执。第一个 CloudApplication 资源会话创建成功，第二个实例的资源会话因额度已满
返回 403；节点确认第一个实例停止后，原会话状态为 `closed`，第二个资源会话创建成功。最终报告
`test-results/server_validation/pg-20260925-084506-1b9807ae/report.json` 为 1/1 PASS，隔离数据库/容器/卷和本地许可证
副本已清理。首次扩展运行因测试代码将 JSON 字符串中的 UUID 连同引号放入停止 URL，收到 400；修正测试 URL 后复跑通过，
未改产品代码。此次新增的 CN 测试签发记录同样只会到期，不交付客户。节点为协议级模拟，没有物理 Render、媒体画面或真实客户端，
所以这关闭的是 Console 运行时授权额度链路，不是私有部署的端到端云应用验收。

2026-09-25 又以不依赖 90 的固定公开公钥 fixture 运行同一 `cn-license` 短测，CN 实签 wire 在隔离 Console 完成离线验签、准入和 1 路额度占用/释放；`test-results/server_validation/pg-20260925-091307-b350fb85/report.json` 为 1/1 PASS。测试只增添短期 Auth 测试签发记录，未修改 90 部署。Customer Linux 的许可证文件替换工具另在隔离 systemd 环境完成错误 deployment 拒绝与正确替换后 readiness 验证，报告 `test-results/server_validation/pg-20260925-090757-134e6d21/report.json`。这仍不是客户真实凭据或正式 Customer 包的生产验收。

Console 不再请求 `auth.rgaa.vip` 验证许可证。运维从 Auth 管理面下载签名 `PXLIC2`，把许可证和 Auth 公钥信任根作为受控文件安装到
目标 Console；Console 启动和业务准入只做本地签名、deployment、到期时间、服务集合和 stream 上限检查。Auth 中的撤销会阻止续期，
但不会让已交付的离线副本瞬时失效。

“90”公网环境（`39.71.45.66`）保留为 Console、Relay、Render 和客户端功能验收节点；它不再承载 Auth。当前 Console 已删除
旧 verify URL、Authority deployment、机器指纹和许可证水位配置，并换用最小 `PXLIC2`。2026-09-21 的在线切换备份只能作为历史取证，
不得恢复成活动兼容路径。
