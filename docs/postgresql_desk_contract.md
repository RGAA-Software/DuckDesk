# Desk PostgreSQL 纵向改造契约

> DB0/DB3 的 Desk 子集；新环境直接初始化，不导入 Mongo，也不保留旧路由、配置读取、管理密码或版本校验串。

## 1. 数据和权限

Desk 只负责咨询、问题反馈、产品版本元数据，不调度云桌面，不直接查询 Console/Auth 数据库。
独立 pixels_desk + owner/runtime；UUID 主键、UTC timestamptz；SQL 参数绑定；启动校验部署/schema 与 runtime 角色，建表由 px_db 执行。
业务启动拒绝 owner/超级用户及获得建表/建角色等额外权限的 runtime；就绪检查同样检查该边界，不把建表凭据当普通服务凭据。

| 表 | 主要字段 / 约束 | 访问 |
|---|---|---|
| feedback | id=request_id UUID、kind consult/issue、title、your_name、description、email/wechat/qq、consult_type/version/os、body_sha256、created_at/updated_at、processed、revision | 匿名仅创建并取 receipt；管理会话按 kind 查询、CAS 标记。咨询与问题字段组合由 CHECK 约束，非 JSONB 业务袋 |
| versions | id UUID、product/distribution/channel/os/architecture、build_number、version、artifact_url/sha256/size_bytes、metadata_url/metadata_sha256、created_at | 管理会话发布，公开按五个明确维度查询最新 build_number；元数据不代替更新包签名验收 |
| admin_sessions | id UUID、token_hash 32 bytes、credential_fingerprint 32 bytes、expires_at、revoked_at | 8 小时会话，每次请求查过期、撤销和当前配置指纹；只存摘要 |

文本长度由接口和 SQL 双重约束。分页 page 1–10000、page_size 1–100，稳定按 created_at DESC,id DESC 排序；管理更新要求 revision CAS。
CAS 未命中（ID/kind 不存在或 revision 冲突）统一 409；不以第二次查询制造存在性/并发歧义。runtime 只能更新 processed/revision/updated_at，不能修改正文或删除提交。
公开创建请求必须提供 UUID request_id；重复相同 ID/正文返回同一 receipt，不同正文为 409，不返回旧提交内容。
公开表单按请求限长；联系人信息和请求正文不写日志。查询、处理与版本发布全部要求管理会话。
版本 product=cloud_node/client/remote/android/server，distribution=official/customer，channel=stable/preview；不猜默认产品或发行。
build_number 是 1..i64::MAX 的整数；同产品/发行/渠道/OS/architecture/build 唯一，较旧发布记录不覆盖新 build 的查询结果。
当前受支持组合：Cloud Node/Client/Remote 为 windows+x86_64，Android 为 android+aarch64，Server 为 windows 或 linux+x86_64。
不猜缺失平台，不把 ARM Android 包当作 x86 包；后续支持新平台需显式扩充契约与 SQL CHECK。
内容大小为 1 字节至 1 TiB，SHA-256 为小写 64 位 hex；制品及签名元数据引用均为明确 HTTPS URL，不允许用户信息、查询令牌或片段。
签名元数据引用只是待验证的不可变内容，不代表已验签或可安装；实际更新信任链仍由部署计划规定的更新框架验证。

## 2. 管理与配置

部署提供独立随机 32 字节 hex 管理凭据的 SHA-256（PIXELS_DESK_ADMIN_TOKEN_SHA256）；服务器不生成或读取旧明文密码文件。
`POST /api/desk/admin/sessions` 校验凭据后签发独立随机 bearer 会话；网页只保留短期会话，不把管理原始凭据当请求头发送。
会话摘要和管理凭据指纹写 PG；配置凭据轮换后旧会话即时不匹配，重启不复活撤销会话。
本版是单管理凭据入口，不冒充多管理员 RBAC；Console 统一运维身份属于后续范围。

必须显式配置 DATABASE_URL/deployment、监听地址、静态资源目录；生产监听需 TLS 证书/私钥。
仅明确 LocalDevelopment 且 loopback 监听时允许 HTTP 和本地无 TLS PG；没有监听所有网卡的 HTTP fallback。
数据库缺失、跨服务/部署或 schema 不匹配时启动失败且非零退出。运行中 `/health/live` 只代表进程，`/health/ready` 检查 PG/schema。
请求中 DB 故障返回 503，不读取进程内“最新版本”缓存冒充已落库结果；错误用标准 HTTP 状态和稳定 code，不再使用 600 系列状态。

## 3. 新接口与消费者

- `/api/desk/admin/sessions`：POST 登录；`/api/desk/admin/session`：DELETE 撤销当前会话。
- `/api/desk/consults`、`/api/desk/issues`：POST 匿名提交、GET 管理查询。
- `/api/desk/consults/{id}`、`/api/desk/issues/{id}`：PATCH 管理标记 processed，提交 expected_revision。
- `/api/desk/versions`：POST 管理发布；正文是 `{target:{product,distribution,channel,os,architecture},build_number,version,artifact_url,sha256,size_bytes,metadata_url,metadata_sha256}`。
  GET 查询参数须包含 target 的五个字段，返回该精确平台的最新 build；无默认平台或旧正文兼容。

JSON 请求拒绝未知字段；结构错误 400/422、无会话 401、找不到 404、版本/幂等冲突 409、过大 413、数据库故障 503。
咨询/反馈网页、管理登录/列表同步使用新路由、UUID、ISO 时间和 revision，不建设旧接口转发。
客户端不处理管理权限或数据库账号；发布工具必须使用管理会话，删除写死的版本发布校验串。

## 4. 必测

真实 PG + 实际路由：登录、非法/撤销/到期/轮换凭据、咨询/反馈提交与幂等冲突、管理查询和 CAS、分页、未知字段/缺字段/超限。
拒绝前后核查持久行数；测试服务重启后数据/会话状态保留，断库 503，恢复后成功；禁止日志泄露口令/正文。
版本测试六个产品/平台组合 × 两发行 × 两渠道隔离、旧 build 晚到、唯一冲突、二十个并发发布仅一成功及 DB 写失败不改变公开结果。
Windows 与 Linux 原生服务进程访问 PG；网页类型检查、真实浏览器表单/登录/处理/退出。源码和依赖树不含 Mongo，旧路由应返回 404。
这些通过才声明 Desk 纵向小步完成；Auth、Console 和 DB4/DB5 的未完成项继续单独记录。
