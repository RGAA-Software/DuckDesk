# Single Server：Console 备份恢复命令

Console 管理网页只负责查看已验证恢复集和只读预检。以下命令只处理 Console 数据库；Auth 和 Desk 不在 Single Server 安装或恢复范围内。

恢复始终创建新库 `pixels_console_restore_<恢复集UUID去掉横线>`。命令不会覆盖 `pixels_console`、修改 Console 连接配置或自动切换业务。先在网页复制恢复集 UUID；请使用与当前安装包一起发布的命令脚本和 PostgreSQL 工具，不要从开发目录手动拼装文件。

## Windows Setup

以管理员 PowerShell 运行，只读校验：

```powershell
$pixelsServer = Get-ItemProperty 'HKLM:\Software\Pixels\SingleServer'
& "$($pixelsServer.InstallLocation)\current\restore_console.ps1" `
    -InstallRoot $pixelsServer.InstallLocation -ConfigRoot $pixelsServer.ConfigRoot `
    -RecoverySetId '<recovery-set-uuid>'
```

实际恢复时执行：

```powershell
& "$($pixelsServer.InstallLocation)\current\restore_console.ps1" `
    -InstallRoot $pixelsServer.InstallLocation -ConfigRoot $pixelsServer.ConfigRoot `
    -RecoverySetId '<recovery-set-uuid>' -Execute `
    -PgUser '<postgresql-admin-role>' -PgPasswordFile '<private-pgpass-file>'
```

`pgpass` 文件按 PostgreSQL 客户端格式保存目标主机、端口、数据库通配符、管理员角色和密码，并只允许本机管理员读取；不要把密码写进命令行、仓库或安装目录。脚本在恢复时要求 Backup 空闲，暂停对应 Backup 服务，验证归档与包内工具 SHA-256，创建并校验新库，最后恢复原本运行的 Backup 服务。失败不删除已创建的新库。

## Linux Compose

在当前解压的发行包目录运行：

```bash
./restore_console.sh <recovery-set-uuid>
```

脚本先只读校验，随后明确要求输入 `RESTORE`、PostgreSQL 管理员角色与密码；密码仅通过标准输入交给一次性容器，并写入容器内权限为 `0600` 的临时 `pgpass` 文件。脚本暂停 Backup，使用包内 PostgreSQL 工具恢复到新库，核对部署身份，最后重新启动 Backup。Console 和 Relay 不会因这次隔离恢复而停止。

## 恢复后的边界

看到“Restored and verified”仅表示归档已进入新库且 Console 部署身份相符，不表示业务已切换。当前日常备份属于独立恢复集，现有恢复准入规则不会自动批准它上线。切换前仍需维护窗口、权限与外部事实对账，并明确处理备份之后发生的会话、授权和节点变更；本命令故意不自动执行切换或清理。任何步骤失败都保留原库和新库现场，由运维调查，不要反复换库名试图掩盖失败。

## 2026-09-28 短验收

正式 Customer Server 1.0.10 的 Windows Setup 与 Linux Compose 包已在一次性 PostgreSQL 18.6 上分别执行包内恢复命令。测试先备份 `before-backup`，再把原库改为 `after-backup`；两平台恢复后均确认隔离新库为 `before-backup`、原库仍为 `after-backup`、部署身份一致，且原本运行的 Backup SCM 测试服务或 Compose 测试容器已重新运行。测试密码包含冒号和反斜杠，覆盖 `pgpass` 转义。Windows 以 PowerShell 5.1 执行；包和归档 SHA-256 另行核对。测试服务/容器与数据库已清理，没有接入 90 或操作客户业务库。此短测验证恢复命令和停启编排，不冒充真实客户环境的业务切换或长期备份验收；1.0.8 和 1.0.9 不作为此恢复功能的最终交付包。
