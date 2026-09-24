# Linux Customer Server 私有部署操作入口

适用范围：当前正式套件 `1.0.2`，目标 Ubuntu 24.04 x86_64、systemd。本文是现有安装入口的操作顺序，不宣称真实客户生产凭据、断公网拓扑或四服务同机生产部署已经验收。后续正常发版时替换套件版本；不为跨版本测试专门发布新包。

## 1. 交付物与部署前准备

- 取得同一发行批次的 `1.0.2.tar.gz` 和 `1.0.2.tar.gz.sha256`，先在归档所在目录执行 `sha256sum -c 1.0.2.tar.gz.sha256`。解包后目录名为 `1.0.2`；执行 `python3 <解包绝对路径>/tools/verify_candidate.py <解包绝对路径>` 和 `sh <解包绝对路径>/tools/preflight_linux_host.sh`。不要使用 development 候选目录代替正式 Customer 包。
- 为客户部署固定一个小写 deployment UUID；准备独立 PostgreSQL 18 数据库、owner/runtime/备份角色、服务可验证的 PostgreSQL CA、HTTPS 证书、域名与 DNS。套件附带 PostgreSQL 客户端工具，**不安装 PostgreSQL 服务**。
- 从 Auth 正式签发流程取得绑定该 deployment UUID 的 `PXLIC2` 许可证和受信签发公钥文件。签发私钥、测试许可证和真实数据库口令均不在套件内；私有 Console 本地验签，正常会话不依赖 Auth 在线服务。
- 1.0.2 套件内 `examples/` 提供四项无密钥配置样板；1.0.1 不含这些样板，不得修改其不可变归档。源码样板位于 [`deploy/private_server/examples/`](../deploy/private_server/examples/README.md)，请先读其占位值说明。
- 1.0.2 套件根目录已包含独立可读的 [`INSTALL.md`](../deploy/private_server/INSTALL.md)；包内样板只引用该包内指南，客户离线取得完整包即可读取安装步骤。
- 在安装前准备 Console、Relay、Desk（如使用）各自的私有环境文件，以及 Backup 的 schema 2 JSON 和数据库凭据文件。输入配置须为绝对普通文件、权限不宽于 `0600`，不得用测试环境的 CA、口令或许可证。Console 的完整变量及首次初始化顺序见[Console 运行配置](px_console_server_runtime_config.md#全新部署)，Relay 控制/端点配置见[P3 连接实施计划](p3_connection_scheduling_execution_plan.md)，Desk 配置见[Desk 运行配置](px_desk_web_overview.md)，备份保留规则见[PostgreSQL 方案](postgresql_database_migration_plan.md)。

## 2. 全新安装顺序

以下命令在目标主机上以 root 执行；`package_dir` 是已核验的解包绝对路径，`deployment_id` 是上一步固定的 UUID。配置文件路径必须指向事先准备好的私有文件，不要把密钥直接写入命令行或本文。

1. 依[Console 运行配置](px_console_server_runtime_config.md#全新部署)和[Desk 运行配置](px_desk_web_overview.md)创建空库及角色，使用包内 `bin/px_db migrate console`、`migrate desk`，再以运行时角色分别 `check`。`px_db` 读取 `PIXELS_DATABASE_URL` 和 `PIXELS_DEPLOYMENT_ID`；生产环境须使用 `verify-full`，不能设置本机开发 TLS 豁免。按同一 Console 文档用包内 `bin/px_console_admin` 生成密钥、初始化首个管理员和录像缓存。完成后撤下 owner 凭据及初始化密码。
2. 把 Console 环境文件中的 `PIXELS_CONSOLE_DISTRIBUTION=customer`、`PIXELS_CONSOLE_RELEASE_NAMESPACE=pixels.customer`、`PIXELS_DEPLOYMENT_ID`、许可证/公钥、TLS/Origin、运行时数据库 URL 设为实际值。静态路径必须精确为 `/opt/pixels/private/<deployment UUID>/current-console/static/console`。Desk 如安装，其静态路径为 `/opt/pixels/private/<deployment UUID>/current-desk/static/desk`。Relay 的公开端点须使用当前配置，不得使用退役端口。
3. 使用下面的现有入口安装。Backup 必须先安装包内 PostgreSQL 18.6 客户端工具；它的配置还要求 `/etc/pixels/<deployment UUID>/backup/postgres-ca.crt` 和该目录内的私有数据库凭据，并把 `pg_dump`/`pg_restore` 路径及 SHA-256 绑定到已安装工具。Console、Relay、Desk、Backup 可分别覆盖安装，不需要先卸载。

```sh
package_dir=/absolute/path/to/1.0.2
deployment_id=00000000-0000-0000-0000-000000000001

sh "$package_dir/tools/install_pg_toolchain.sh" "$package_dir"
sh "$package_dir/tools/install_linux_component.sh" console "$deployment_id" "$package_dir" /absolute/private-console.env
sh "$package_dir/tools/install_linux_component.sh" relay "$deployment_id" "$package_dir" /absolute/private-relay.env
sh "$package_dir/tools/install_linux_component.sh" desk "$deployment_id" "$package_dir" /absolute/private-desk.env
sh "$package_dir/tools/install_linux_component.sh" backup "$deployment_id" "$package_dir" /absolute/private-backup.json
```

`deployment_id` 上方只是格式示意，不能把示例 UUID 用于实际部署。未使用 Desk 时不安装它；Backup 的 `plan.targets` 应与实际安装的数据库集合一致，不得把未部署的服务伪装成已备份。

## 3. 安装后与后续覆盖

- 使用 `systemctl status "pixels-private-console@$deployment_id.service"`，并分别查看 `relay`、`desk`（如使用）、`backup`。异常时查看相应 `journalctl -u` 日志和组件 readiness；不要把 systemd `active` 单独当作业务就绪。Console 管理员应能登录并查看当前许可证的服务、期限和 `max_streams`；Relay 应出现在 Console 当前库存中。Backup 应有实际 verified 恢复点，不能仅凭服务启动判定数据可恢复。
- 正常升级先备份并按 Server → 各节点/客户端顺序安排维护；每个组件用新正式包再次运行对应 `install_linux_component.sh` 即为覆盖安装。脚本在启动失败时恢复该组件此前的配置、release 指向和运行状态；数据库 schema 或业务数据恢复仍需运维依据备份处理，不存在跨节点自动回滚。
- 停用单个组件使用包内 `tools/uninstall_linux_component.sh <console|relay|desk|backup> <deployment UUID>`。该入口只停用注册，保留私有配置、release 和数据；**不要为了升级先卸载**。Linux Server 1.0.1 到 1.0.2 的 Relay 相邻版本覆盖、活动状态和降级拒绝已完成隔离短测；1.0.2 四服务共部署、登录及备份恢复也已完成隔离短测。

当前尚未完成的商业交付验证是客户真实生产凭据/TLS、实际断公网拓扑和目标主机四服务同时共部署；这些不阻塞本轮开发，也不得在交付说明中写成已通过。实际部署时遇到具体缺口，再按该缺口修复，不新增自动升级编排。
