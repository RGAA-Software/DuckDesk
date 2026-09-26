# Pixels Customer Single Server · Windows

运行版本化 `PixelsServer_<version>_Setup.exe`，选择安装目录、私有配置目录和持久数据目录。Windows 会显示“未知发布者”；本产品按决定不做 Authenticode 签名。Setup 内含 Console、Relay、Backup、Console Web、PostgreSQL 18 **客户端**工具以及初始化程序，不含 PostgreSQL 服务、Auth、Desk 或许可证私钥。

首次安装无需预制 `backup.json`、`.env`、证书或 Relay token。Setup 创建私有目录并启动 `Pixels.Setup`；在本机打开 `http://127.0.0.1:4700/`，填写外部 PostgreSQL 18 的主机、端口、超级用户密码、CA PEM，以及 Console 公网主机和初始管理员。系统自动创建 Console 库/角色、证书、Backup 配置、管理员和 Relay 登记。数据库管理员密码仅在此过程中使用，不写入常驻配置。随后访问 `https://<公网主机>:4600/`，登录 Console，上传 Auth 签发的 PXLIC2 许可证；未授权前业务会话保持关闭。首次生成的 Console HTTPS 证书由本部署自签名 CA 签发，私有部署运维可按自己的信任策略管理。

同一部署升级直接再次运行新版 Setup；安装器按 deployment ID、产品身份和包内逐文件 SHA-256 验证后覆盖程序并重启服务，不先卸载。配置、许可证、录像缓存与 Backup 仓库留在安装目录之外。升级时应先升级 Server，再按接口兼容窗口覆盖其他节点/客户端。

在“应用和功能”卸载，或运行 `Uninstall.exe`；卸载仅移除 Pixels 服务和程序，保留私有配置、持久数据及外部 PostgreSQL。若要销毁这些数据，须另行明确操作。`Pixels.Setup` 是首次初始化的本机服务；初始化结束后停止，不承担业务流量。

开发候选使用聚焦快速 Release 构建，不运行正式全量发行脚本：

```powershell
pwsh scripts_build/build_single_server_windows_candidate.ps1 -PostgreSqlClientRoot C:\absolute\verified-postgresql-client -OutputDirectory C:\absolute\new-candidate -SuiteVersion 1.0.4
python setup/make_single_server.py --package C:\absolute\new-candidate --output C:\absolute\PixelsServer_1.0.4_Setup.exe
```

正式优化发行仍由 `scripts_build/build_single_server_windows_release.ps1` 使用与 Linux 相同的预留 suite 版本构建。候选包即使通过短测，也不能冒充正式发行制品。
