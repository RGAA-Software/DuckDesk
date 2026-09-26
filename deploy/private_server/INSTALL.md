# Pixels Customer Server（Linux ELF 候选）

本目录是 Console、Relay、Backup 三服务的手工 systemd 候选包，不是新的 Single Server 正式交付。正式 Linux Single Server 采用 Docker Compose；`px_desk` 属于官网，不在 Customer 包内。PostgreSQL 18 服务由运维独立提供，包内至多含客户端备份工具。

1. 核对归档 SHA-256，解包并运行 `python3 tools/verify_candidate.py <解包绝对路径>` 与 `sh tools/preflight_linux_host.sh`。不混用不同版本的二进制、工具和静态资源。
2. 准备一个固定的小写 deployment UUID、独立 Console 空库与 owner/runtime/backup 账号、数据库 CA、Console HTTPS 证书、同 UUID 的 PXLIC2 许可证及通过可信运维渠道核对的 Auth 公钥信任文件。自签证书可用，但 TLS 主机名和证书仍须验证。
3. 阅读 `examples/README.md`，将 Console、Relay、Backup 样板复制到包外的私有目录并替换所有示例值。Backup 仅要求 Console，Auth 和 Desk 均为 `not_applicable`。样板不是可直接运行的配置。
4. 用 `bin/px_db` 对 Console 库执行 `migrate` 和 `check`；用 `bin/px_console_admin` 生成密钥、初始化管理员及录像缓存，并在启动前验证许可证。数据库 URL 使用 `verify-full`。Relay 必须先在 Console 登记并取得一次性 node token。
5. 以 root 执行下列安装命令，使用已经核对的绝对路径。安装器不安装、不升级也不删除 PostgreSQL。

```sh
package_dir=/absolute/path/to/extracted-package
deployment_id=REPLACE_WITH_REAL_LOWERCASE_UUID
sh "$package_dir/tools/install_pg_toolchain.sh" "$package_dir"
sh "$package_dir/tools/install_linux_component.sh" console "$deployment_id" "$package_dir" /absolute/private-console.env
sh "$package_dir/tools/install_linux_component.sh" relay "$deployment_id" "$package_dir" /absolute/private-relay.env
sh "$package_dir/tools/install_linux_component.sh" backup "$deployment_id" "$package_dir" /absolute/private-backup.json
```

逐项检查 `pixels-private-console@<UUID>`、`pixels-private-relay@<UUID>`、`pixels-private-backup@<UUID>` 的服务状态、Console `/health/ready`、Relay 库存及 Backup verified 恢复点。覆盖安装复用相同部署 UUID 和配置；`tools/uninstall_linux_component.sh <console|relay|backup> <UUID>` 只停用组件，保留配置和数据。这个候选路径不代表新 Compose 正式包已经验收。
