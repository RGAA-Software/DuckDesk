# px_auth Linux / CN 部署记录

## 2026-09-21 PostgreSQL 新服务

CN（`49.232.190.218`）只承载官方 Auth，公网入口为 `https://auth.rgaa.vip`。Console、Relay、Render 和节点仍在既有“90”公网环境，
没有迁移到 CN。此次按全新开发产品部署，不导入 Mongo 数据，不复用旧 TOML/JWT/HMAC/API 或旧许可证签名私钥，也没有兼容回退。

- 新 Auth deployment：`63706848-cf7b-4a50-9652-f4fab461aa26`；由
  `pixels-auth@63706848-cf7b-4a50-9652-f4fab461aa26.service` 以无登录 `pixels-auth` 账号托管并开机启用。
- Auth 仅监听 `127.0.0.1:30400` 的后端 TLS；Nginx 保持 `auth.rgaa.vip:443` 与有效 Let's Encrypt 证书，不把后端端口暴露公网。
- PostgreSQL 18.6 镜像固定为
  `postgres:18.6@sha256:4ef4dbc939d61acea57712655ddb4b4ab27419c913f94cca0cd57cb3ea3c2280`，容器
  `pixels-auth-postgres` 只发布 `127.0.0.1:54329`，使用单独持久卷，数据库连接使用 verify-full 和本部署私有 CA。
- 只创建 `pixels_auth`、最小权限 `pixels_auth_owner`/`pixels_auth_runtime` 和新 deployment identity；五项 migration、运行角色检查、
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

## “90”Console 消费者切换

“90”公网环境（`39.71.45.66`）的 Official Console 已原子切换到
`https://auth.rgaa.vip/api/auth/licenses/verify`，Authority deployment 固定为
`63706848-cf7b-4a50-9652-f4fab461aa26`。新 trust store 和许可证在本机、远端暂存和安装后均核对 SHA-256；许可证明确绑定现有
Console deployment、machine、`pixels_console` 和 `official`，没有旧 Authority、旧路径或本机 Auth fallback。旧 launcher、trust store、
许可证、库外水位和计划任务 XML 保留在“90”的
`D:\PixelsServer\backups\auth-cn-switch-20260921081906`，只作显式人工回退证据。

切换后 Console 从精确正式路径启动且 4600 正常监听；公网 `/health/live` 和 `/health/ready` 均为 204。随后停用“90”的
`Pixels-Auth` 计划任务、终止其进程并确认 4602 不再监听；在本机 Auth 完全停止的条件下，每 5 秒探测一次 Console readiness，连续
70.7 秒共 15 次全部返回 204，跨过两次 30 秒在线刷新和 40 秒 freshness fail-closed 边界。这证明“90”Console 已实际依赖 CN Auth，
不是仅修改配置或继续命中旧本机服务。切换后的 Console 用户登录、本人资料、注销及已注销 token 拒绝也已短测通过。90 上不再承载
运行中的 Auth，但未删除旧文件和可恢复备份。
