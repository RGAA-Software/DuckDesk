# PostgreSQL RDP 工作区与密钥契约

2026-09-17。Console repository 增量；不是 Windows 账号创建、RDP 登录或客户端凭据传输的验收声明。
新 schema 与密文格式从零开始，不读取旧 Mongo 工作区或旧凭据，不做双格式解密。

## 持久身份

`rdp_workspaces` 以 `(application_id,node_id)` 唯一，复合 FK 绑定 RDP 部署；不以 user/guest 作为工作区 owner。
同一个应用/节点的后续访问者复用同一 Windows 账号、SID 和文件；资源会话的访问主体另行建模。
账号名在首次创建时生成，数据库 runtime 无权改账号、应用或节点，也无权删除工作区。SID 首次确认后不能替换。
普通实例结束、重连、Stop、凭据租约到期不删除工作区；仓库接口不包含 logoff、删除 Profile 或结束工作区应用的操作。

数据库状态 provisioning 表示凭据已持久化但 SID 未确认；ready 表示受信节点已确认账号 SID，**不代表 Windows 当前登录或 Render 在线**。
真实节点必须验证自己创建的账号身份，不允许仅按账号名收养原有 Windows 用户。SID 不匹配进入显式恢复处理，不能自动创建替代账号。

## 凭据边界

`WorkspaceCredential` 不实现 Debug/Serialize/Clone，口令以 Zeroizing 保管；普通 `NodeCommand`、管理 DTO 与日志不携带凭据。
`credentials_for_start` 要求当前已认证 NodeConnection、未过期 RDP Start command/lease、精确实例版本及当前 owner 授权。
WebView/GameHook、其他节点、错租约、过期/撤销身份均不能创建或读取凭据。提交工作区/密文/审计之后，节点才可执行 OS 账号创建。
`confirm_account` 使用相同边界，重复相同 SID 幂等，不同 SID 拒绝。

这不是客户端取密接口。后续 SSPI/资源会话授权交付需另建受保护适配器；不能把整个 WorkspaceCredential 序列化给管理页或普通命令通道。
事务完成后的撤权仍依赖精确 Stop、节点授权时限与资源会话验证，不能宣称一次取密检查提供永久授权。

## 加密格式

- `workspace_secrets` 与工作区一对一；只保存 schema_version=1、key UUID、12 字节 nonce、AES-256-GCM 密文。
- CSPRNG 生成 32 字节口令随机量及随机 nonce；口令为 68 个 ASCII 字符，满足大小写/数字/符号，密文含 tag 共 84 字节。
- AAD 为固定 `Pixels-PG-RDP-Workspace-v1\0`，依次拼接安装部署、工作区、应用、应用部署、节点、key UUID 的 16 字节，
  credential_revision 的 8 字节大端值，以及固定 20 字节账号名。各字段长度固定，无字符串拼接歧义。
- AAD 不绑定访问者，不因访客变化重加密；绑定安装部署，其他部署恢复不得不经授权直接启用。
- 缺 key、错 key、nonce/密文/绑定变化均返回 RecoveryRequired，不生成新密钥、不换口令、不覆盖原记录。

`WorkspaceVault` 仅接受启动根注入的受保护密钥集合及唯一 active key，最多 32 个 key；本模块不负责读取文件或自动生成密钥。
正式产品接入时必须复用经验证的文件句柄权限检查、独立备份与配置准入；当前合成测试中的内存 key 不构成生产密钥管理实现。

## 轮换与恢复

管理员以当前 admin_web 身份和 revision CAS 执行 rewrap。旧 key 解密、active key 重新加密同一口令，随机新 nonce；
secret 更新、工作区 revision 与审计同事务。credential_revision、Windows 密码、账号、SID 与工作区身份保持不变。
viewer 只能有界分页查询去密状态。缺旧 key 不能通过“轮换”掩盖数据不可读。
旧 key 的保留期覆盖所有仍保留的备份集；不能因为当前库已 rewrap 就删除备份所需的旧 key。

三张表纳入逻辑恢复逐行/约束/索引检查；实际恢复开机还需要独立密钥与节点身份对账，不能把逻辑恢复冒烟当成 DB4/DB5 完成。

## 测试范围

加密单元测试覆盖各 AAD 字段、错/缺 key、密文篡改、nonce 与不换密码轮换；SID 单元测试覆盖规范形式和边界。
另有 Node v22.15.0 / OpenSSL 3.0.16 独立生成的 AES-256-GCM 固定向量，逐字节核对 AAD 与 Rust 解密结果，材料均为公开合成测试值。
数据库用例覆盖跨仓库重连持久身份、运行时停止后另一访客复用、20 路并发创建、CAS 轮换、越权、去密分页、缺 key 不覆盖、
创建/确认/轮换审计故障回滚。Windows OS 账号/Profile/会话保留与实际 RDP 接入尚待端到端验证。
运行结果以[实施状态](server_database_execution_status.md)中对应新报告为准。
