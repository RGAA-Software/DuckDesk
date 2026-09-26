# Customer Server 配置样板

这些文件只示范当前配置结构，不含可用的账号、口令、许可证、签名根或私钥。**不能直接安装或运行。** 正式包中的 `examples/` 目录只读；先复制到目标主机的私有配置目录，替换所有 `REPLACE`、示例域名、示例 UUID 和 Backup 的零摘要/零 schema 版本，再将输入文件权限设为 `0600` 或更严。

- 包内三个样板的 deployment UUID 必须是同一个真实客户部署 UUID，并与 PostgreSQL 初始化身份、PXLIC2 许可证一致。不要复用本页的 `11111111-1111-4111-8111-111111111111`。
- `console.env.example`：数据库 URL 使用 `verify-full` 和客户 CA；许可证及公钥文件由运维另外安全放置。`PIXELS_RELAY_APP_KEY` 必须与 Relay 配置相同。Console 密钥和首个管理员按包内 [`INSTALL.md`](../INSTALL.md) 的准备步骤生成；不要把初始管理员密码放进环境文件。
- `relay.env.example`：先在 Console 登记 Relay，取得只返回一次的 node token，再填写控制端点和证书 CA。Relay 的公开地址/端口由 Console 库存配置，不靠本文件推断。
- `backup.json.example`：Console 是唯一 required 数据库；官网 Desk 和官方 Auth 均为 not_applicable。由数据库运维单独准备只读 Backup 角色，并在私有配置目录安全放置凭据和 `postgres-ca.crt`。`pg_dump`/`pg_restore` 的 SHA-256 取自**当前包匹配**的 PostgreSQL 客户端工具清单；Console `schema_version` 按当前正式数据库 schema 填写，不能保留示例的零值。

备份样板只配置本机仓库，不等于异机灾难恢复。安装顺序和恢复边界见包内 [`INSTALL.md`](../INSTALL.md)。样板不是配置向导，也不会自动生成密钥、创建数据库或替代程序启动校验。
