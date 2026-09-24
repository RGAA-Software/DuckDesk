# Customer Server 配置样板

这些文件只示范当前配置结构，不含可用的账号、口令、许可证、签名根或私钥。**不能直接安装或运行。** 正式包中的 `examples/` 目录只读；先复制到目标主机的私有配置目录，替换所有 `REPLACE`、示例域名、示例 UUID 和 Backup 的零摘要/零 schema 版本，再将输入文件权限设为 `0600` 或更严。

- 四个文件里的 deployment UUID 必须是同一个真实客户部署 UUID，并与 PostgreSQL 初始化身份、PXLIC2 许可证一致。不要复用本页的 `11111111-1111-4111-8111-111111111111`。
- `console.env.example`：数据库 URL 使用 `verify-full` 和客户 CA；许可证及公钥文件由运维另外安全放置。`PIXELS_RELAY_APP_KEY` 必须与 Relay 配置相同。Console 密钥和首个管理员按仓库 `docs/px_console_server_runtime_config.md` 的全新部署流程生成；不要把初始管理员密码放进环境文件。
- `relay.env.example`：先在 Console 登记 Relay，取得只返回一次的 node token，再填写控制端点和证书 CA。Relay 的公开地址/端口由 Console 库存配置，不靠本文件推断。
- `desk.env.example`：管理凭据是独立随机口令的 UTF-8 文本 SHA-256，不是口令明文，也不是对 hex 解码后的字节求 hash。未部署 Desk 时无需使用此文件。
- `backup.json.example`：按实际数据库修改 required/not_applicable 目标。私有 Customer Server 不安装 Auth 签发器，示例因此标记 Auth 为 not_applicable。为 required 数据库先使用包内 `px_db provision-backup-role` 建立只读角色，并在 `/etc/pixels/<deployment UUID>/backup/` 安全放置凭据和 `postgres-ca.crt`。`pg_dump`/`pg_restore` 的 SHA-256 取自**已安装且与当前包匹配**的 PostgreSQL 工具链清单；目标 `schema_version` 按当前正式数据库 schema 填写，不能保留示例的零值。

备份样板只配置本机仓库，不等于异机灾难恢复。安装顺序和恢复边界见仓库 `docs/private_server_install_guide.md`。样板不是配置向导，也不会自动生成密钥、创建数据库或替代程序启动校验。
