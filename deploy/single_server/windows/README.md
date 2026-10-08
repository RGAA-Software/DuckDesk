# Pixels Official Single Server · Windows

Official 是安装包发行身份，不是固定服务器地址。官方自用与私有部署使用同一包；服务器访问地址、数据库和证书在首次初始化时配置，访问地址可以是局域网 IP 或内网域名，不要求公网 IP。

运行版本化 `PixelsServer_<version>_Setup.exe`；安装器使用固定默认安装、配置和数据目录，不要求逐项填写。Windows 会显示“未知发布者”；本产品按决定不做 Authenticode 签名。Setup 内含 Console、Relay、Backup、Console Web、PostgreSQL 18 **客户端**工具以及初始化程序，不含 PostgreSQL 服务、Auth、Desk 或许可证私钥。

首次安装无需预制 `backup.json`、`.env`、证书或 Relay token。只需预先单独安装 PostgreSQL 18，并创建可登录的超级用户 `Pixels`、密码 `Pixels@123`，监听标准端口 `5432`。安装器自动使用本机 PostgreSQL，选取本机网络地址，创建 Console 库/角色、证书、Backup 配置、初始管理员 `Pixels` / `Pixels@123`，登记 Relay 并启动三个服务。数据库管理员密码仅在初始化时使用，不写入常驻配置。数据库连接优先使用 TLS，数据库未开启 TLS 时也能连接；不校验 CA，无需填写 CA PEM。Console 和 Relay 分别监听 `0.0.0.0:4600`、`0.0.0.0:4605`；不再运行初始化网页或 4700 服务。交互安装成功后自动打开 Console 网页（静默安装不打开浏览器），并启动用户会话中的 Rust 托盘程序。托盘双击或右键“打开管理网页”可再次打开 Console；登录时通过 Windows Run 项自启。退出托盘不停止三个服务。包内有 Auth 签名的离线 PXLIC2 入门许可证：远程桌面、游戏、WebView、RDP 各同时最多 1 路，总共最多 4 路。管理员可在 Console 网页导入新 PXLIC2 替换，不需要重装。首次生成的 Console HTTPS 证书由本部署自签名 CA 签发，私有部署运维可按自己的信任策略管理。

同一部署升级直接再次运行新版 Setup，不先卸载。安装器按 deployment ID、产品身份和逐文件 SHA-256 验证，停服务前检查数据库迁移历史与升级专用凭据；需要升级数据库时先备份，再以受限 owner 账号执行 SQLx 迁移，最后覆盖程序并重启服务。首次初始化会把这个 owner 的连接凭据保存到配置目录 `database-upgrade/console-owner.url`，仅本机管理员/System 可读，业务服务没有读取权限；日常覆盖不需要数据库超级用户密码。历史安装缺少此凭据时必须经管理员明确批准恢复，不自动重置密码或降低数据库认证。具体流程、快照和失败边界见 `docs/single_server_windows_database_upgrade_20261001.md`。

覆盖时沿用已登记的安装/配置/数据目录。若同一部署尚无 Console↔Backup 控制配置，安装器生成一次专用令牌并补入两个私有配置；以后覆盖保留该令牌，配置冲突则拒绝升级。未尝试数据库迁移的安装失败恢复原程序和配置；一旦开始迁移，失败时保留备份及新旧程序现场并停止服务，不盲目启动旧程序或覆盖数据库。配置、许可证、录像缓存与 Backup 仓库留在安装目录之外。升级时应先升级 Server，再按接口兼容窗口覆盖其他节点/客户端。

在“应用和功能”卸载，或运行 `Uninstall.exe`；卸载仅移除 Pixels 服务和程序，保留私有配置、持久数据及外部 PostgreSQL。若要销毁这些数据，须另行明确操作。

开发候选使用聚焦快速 Release 构建，不运行正式全量发行脚本：

```powershell
pwsh scripts_build/build_single_server_windows_candidate.ps1 -PostgreSqlClientRoot C:\absolute\verified-postgresql-client
```

候选安装包固定输出到 `build_official/private_server/candidates/windows-single-server/PixelsServer_<next_version>_Setup.exe`，
同目录的 `package/` 是与安装包一致的逐文件清单。重复构建会在成功后替换这个固定目录，不再为每次打包另建一个输出目录；
`.cache/single-server-windows` 仅保存 Cargo 增量编译中间文件，不是交付目录。

正式优化发行仍由 `scripts_build/build_single_server_windows_release.ps1` 使用与 Linux 相同的预留 suite 版本构建。候选包即使通过短测，也不能冒充正式发行制品。
