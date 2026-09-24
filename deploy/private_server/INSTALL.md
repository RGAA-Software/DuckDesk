# Pixels Customer Server（Linux）安装

本说明随下一次正常构建的 Customer Server 包交付。目标为 Ubuntu 24.04 x86_64 + systemd；包内含 Console、Relay、Desk、Backup 和 PostgreSQL 18.6 **客户端工具**，不安装 PostgreSQL 服务，也不含 Auth 签发器、真实证书、许可证或口令。当前已发布的 1.0.1 包不可变，不含本说明和 `examples/`。

## 准备

1. 从可信渠道取得同批次的版本归档及其 `.sha256` 文件，在归档目录执行 `sha256sum -c VERSION.tar.gz.sha256`（将 `VERSION` 换成实际版本）。解包后设定其**绝对路径**为 `package_dir`，再执行 `python3 "$package_dir/tools/verify_candidate.py" "$package_dir"` 和 `sh "$package_dir/tools/preflight_linux_host.sh"`。不要混用 development 候选与正式包。
2. 为本次部署生成一个固定的小写 UUID。准备 PostgreSQL 18 的 Console/Desk 空库及 owner、runtime、只读 backup 角色，以及客户自己的数据库 CA 和服务 HTTPS 证书；CA 和证书可自行生成或自签，不要求公有 CA。客户负责终端的信任配置和实际域名或 IP。将绑定该部署 UUID 的 `PXLIC2` 许可证及 Auth 公钥信任文件安全放入客户私有目录。私有 Console 本地验签，不依赖 Auth 在线服务；本阶段不逐张验证客户证书的签发来源。
3. 阅读 [`examples/README.md`](examples/README.md)，复制适用的 `console.env.example`、`relay.env.example`、`desk.env.example`、`backup.json.example` 到包外的私有路径。替换所有 `REPLACE`、示例 UUID/域名、Backup 的零 SHA-256/零 schema 版本；用 `chmod 0600` 或更严保护输入文件。样板不是真实配置，不能直接安装。
4. 用包内 `bin/px_db` 分别对 Console、Desk 空库执行 `migrate`，再以 runtime 角色执行 `check`。该工具读取 `PIXELS_DATABASE_URL` 和 `PIXELS_DEPLOYMENT_ID`；生产 PostgreSQL 连接必须使用 `verify-full`，不得打开本机开发豁免。用包内 `bin/px_console_admin generate-secrets` 生成 Console 密钥：提供 `PIXELS_CONSOLE_GUEST_SOURCE_KEY`、新文件路径 `PIXELS_CONSOLE_WORKSPACE_KEY` 和新 UUID `PIXELS_CONSOLE_WORKSPACE_KEY_ID`，再把该 UUID/路径填入样板的 `PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY`/`PIXELS_CONSOLE_WORKSPACE_KEYS`。用 `bootstrap` 初始化首个管理员时提供 owner `PIXELS_DATABASE_URL`、`PIXELS_DEPLOYMENT_ID`、`PIXELS_CONSOLE_INITIAL_USERNAME` 和私有 `PIXELS_CONSOLE_INITIAL_PASSWORD_FILE`；用 `initialize-recording-cache` 初始化空缓存时提供 `PIXELS_DEPLOYMENT_ID`、`PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY`。完成后撤下 owner 与初始口令。Relay 必须先在 Console 登记并取得一次性 node token。

## 安装

下面在目标主机上以 root 执行；先把示例路径改为已核验的包及私有配置路径。不要把真实密钥写入命令行。未部署 Desk 时省略其安装，并在 Backup 计划中把 Desk 明确标记为 `not_applicable`。

```sh
package_dir=/absolute/path/to/EXTRACTED_VERSION
deployment_id=REPLACE_WITH_REAL_LOWERCASE_UUID

sh "$package_dir/tools/install_pg_toolchain.sh" "$package_dir"
sh "$package_dir/tools/install_linux_component.sh" console "$deployment_id" "$package_dir" /absolute/private-console.env
sh "$package_dir/tools/install_linux_component.sh" relay "$deployment_id" "$package_dir" /absolute/private-relay.env
sh "$package_dir/tools/install_linux_component.sh" desk "$deployment_id" "$package_dir" /absolute/private-desk.env
sh "$package_dir/tools/install_linux_component.sh" backup "$deployment_id" "$package_dir" /absolute/private-backup.json
```

Backup 安装前还须在 `/etc/pixels/<部署 UUID>/backup/` 准备 `postgres-ca.crt` 与 required 数据库的私有凭据文件；配置里的 `pg_dump`/`pg_restore` 路径和摘要必须与本包安装到 `/opt/pixels/postgresql/18` 的工具链一致。Backup 样板只配置本机仓库，**不是异机灾难恢复**。

## 检查与覆盖

- `systemctl status "pixels-private-console@$deployment_id.service"`，并分别检查 `relay`、`desk`（如使用）和 `backup`。异常时查对应 `journalctl -u` 和 readiness；`active` 不等于业务就绪。管理员登录后核对许可证服务、到期时间和 `max_streams`；Relay 应出现在 Console 库存，Backup 应产生实际 verified 恢复点。
- 升级先备份，再按 Server → 节点/客户端的运维顺序逐组件覆盖：使用**新正式包**重新运行相同的 `install_linux_component.sh`，不要先卸载。启动失败时安装器恢复该组件此前的配置、release 指向及运行状态；数据库数据恢复仍由运维根据备份处理，不存在跨节点自动回滚。
- `tools/uninstall_linux_component.sh <console|relay|desk|backup> <部署 UUID>` 只停用指定组件，保留配置、release 和数据；它不是升级前置步骤。

客户生产凭据/TLS、实际断公网拓扑及四服务在目标主机同时运行尚未完成商业交付验收；本说明是操作入口，不是已通过的生产验收声明。
