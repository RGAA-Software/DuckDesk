# PostgreSQL 身份存储契约与验收映射

> 2026-09-17，DB1 纵向查询 / DB2-A 身份、管理角色与撤销事务增量。不是 Console HTTP、完整 ACL 或客户端已切换的声明。
> 前置：[数据库计划](postgresql_database_migration_plan.md)、[逐步测试门禁](server_incremental_validation_plan.md)。

## 1. 边界

`rust_server/px_console_server/storage` 是 Console 所有的 `px_console_store` crate，仅放领域 repository、类型和 SQL。
它不依赖现有全局单例、Mongo、HTTP 或 UI；用户登录密码不做可逆加密。RDP 工作区机密使用独立的[加密凭据契约](postgresql_workspace_contract.md)。
共享 `px_pg` 负责配置、连接池、错误、schema 与业务 runtime-role 验证。
将领域库独立编译用于逐步验收，不创建第二个运行产品、Mongo 适配器、运行时后端开关或双写路径。
最终 DB2-EXIT 必须把 Console 所有数据访问统一接上新存储，并去掉原 Mongo 依赖，当前尚未完成。

生产组合入口为 `ConsoleDatabase::connect`：验证部署/schema/最小 runtime 角色后建立一个池，identity/control/groups/devices/apps/
guests/nodes/deployments/instances/workspaces 都是同池句柄。关闭由组合根负责，单领域不再有生产 connect/close 入口；
独立连接助手仅在 `pg-integration` feature 下用于隔离测试。就绪状态不等于进程存活，关闭后保留的领域句柄必须失败。

## 2. 已冻结的第一批字段与行为

| 对象 | 字段 / 不变量 | 查询 / 约束 |
|---|---|---|
| 用户 | UUID id，显示用户名、规范化用户名、Argon2id hash、创建/更新时间、停用/删除状态、授权与行 revision | 主键、规范化用户名唯一、revision 为正；删除后用户名不自动释放 |
| 登录会话 | UUID id、user FK、32 字节 token SHA-256、显式 client_type、签发时授权 revision、创建/过期/绝对过期/撤销时间 | token 唯一，用户外键，到期时间关系与最长存储范围；按用户和绝对到期建索引 |
| 用户组 / 成员 | UUID group、名称/规范化名称、备注、revision、创建/更新/删除时间；group/user 双外键成员关系 | 活跃组名部分唯一；成员复合主键与反向用户索引 |
| 部署边界 | 每库独立 deployment UUID 与 service；连接时身份及完整 schema 校验 | 不匹配拒绝；不会因用户名/资源 ID 同名而换库查找 |

用户名规则：显示值和 lowercase 后的规范化值均为 2–64 个 Unicode 标量；拒绝控制字符、首尾空白、`/`、`\`；不静默 trim。
例如 64 个 `İ` 小写后超出上限，必须拒绝；64 个中文字符合法，不按 UTF-8 字节数误拒绝。
规范化仅使用 Rust Unicode lowercase，不进行 NFC/NFKC、同形字折叠或旧用户名转换。
因此 `Pixels用户` 与 `pixels用户` 同名，`éa` 与 `e + combining acute + a` 是不同名字；测试明确覆盖这一点。
接口与 UI 后续必须复用同一规则，并做好显示转义，不能各自猜测规范化方式。

密码只存 Argon2id v19 PHC：m=19456 KiB、t=2、p=1、16 字节盐、32 字节结果。
共享 `px_credentials` 与 Console/Auth repository 使用同一严格解析；数据库 CHECK 同时约束参数顺序、97 字节总长、
16/32 字节的规范无填充 base64（包含末位未用 bit 必须为零），拒绝明文、任意参数和超长输入。
Console `0022` / Auth `0003` 增加数据库约束；隔离测试逐项对照共享解析与两库 CHECK 接受/拒绝结果，不导入旧开发数据。
hash 的 Debug 脱敏，字符串用 Zeroizing 管理。以后正式版本调整算法需独立升级契约。
密码输入长度、CSPRNG 盐、有界阻塞计算、限流、dummy verify 与统一 HTTP 错误已进入新运行模块；正式产品和客户端仍待整体切换。
不保存密码密文，不恢复管理员查看原密码能力；管理员将使用明确授权的重置流程。

token 必须在业务层使用 CSPRNG 生成并先做 SHA-256，repository 只接收定长摘要；测试中的随机摘要只用于合成 fixture。
目前支持 `panel/android/user_web/admin_web` 的用户登录会话存储，client_type **不是**管理员权限或云应用目标授权。
管理 repository 每次验证 AdminWeb 会话及实时角色；admin 可写，viewer 只读，user 拒绝；Panel/Android 不因用户是 admin 而取得管理权限。
访客主体 repository 的新增实现见下文；管理员 HTTP 登录、CSRF、cookie 属性、续期/闲置规则和完整资源 ACL 尚未交付，不能把本模块单独暴露为登录服务。
CloudApplication 会话目标另属于资源会话/授权模型；不能用登录会话中的 user_id 代替目标，Android 不改装成 panel。

## 3. 事务与即时失效

- 注册直接 INSERT，唯一约束决定并发胜者；不做“先查不存在就视为注册成功”。只在提交确认后返回新 profile。
- 查凭据返回当时的授权 revision。业务层验证密码后，签发事务锁用户行，重新检查该 revision、停用和删除状态。
- 修改密码原子递增授权 revision 和行 revision，并追加撤销 outbox；旧 token 立即失效，正在执行的旧密码验证也不能获得新有效会话。
- 登录签发与改密可以先后提交，但两者完成后绝不留下有效旧 revision 会话。测试包含确定性两种次序和 100 轮竞争。
- 每次 authenticate 都联查用户状态、当前 revision、撤销时间和数据库当前时间；后台物理删除与授权是否有效无关。
- 当前会话创建接受 1 秒至 365 天的整数秒存储范围，expires_at=absolute_expires_at；这不是默认产品登录时长或续租策略。
- 单会话撤销按 `id AND user_id` 更新；重复撤销幂等成功，其他用户 ID 与不存在 ID 都拒绝且无持久影响。
- `actor` 必须来自经过验证的服务端身份上下文，不得直接信任请求体的 owner。repository 本身不是 HTTP 鉴权中间件。
- 提交异常不能推断已经回滚；没有自动重试注册/签发等可能产生重复副作用的未知结果操作。

## 4. 用户组事务

组名为 1–128 个 Unicode 标量，拒绝控制字符与首尾空白，仅 lowercase；备注最多 1024 字符。
软删除后名称可重新使用，但必须分配新 UUID，不重新认领旧成员。成员替换每次最多 1000 个 UUID；重复输入拒绝，不静默丢弃。
GroupStore 本身要求管理 token，并在每次操作事务内验证实时角色；组 UUID、成员 UUID 本身都不授予管理权限。

第一版为单活 Console。所有权限改变先取得事务级独占 advisory gate（22091401）；登录签发、管理读取和资源准入先取得共享 gate。
必须先 gate，后 actor/session，再领域对象行，最后按 UUID 顺序锁受影响 user；不能在持行锁后升级 gate。
锁在提交/回滚时释放，沿用 PG 2 秒锁等待上限；排空后可重试，不使用无界等待或内存权限缓存。
成员替换和删除组顺序：独占 gate → 校验 actor → 锁 group → 锁受影响 user → 修改成员 → 更新授权 revision/outbox/audit → 更新 group revision → 提交。
存在、停用/删除状态、输入 revision 在事务内校验；缺用户、过期 revision 或中途插入失败时全部回滚，不能先删旧成员再部分成功。
只有实际增减的用户递增授权 revision；不变成员不强制退出。成员集合不变且 revision 正确时直接返回，不递增版本。
两管理员使用同一 revision 竞争时只有一个修改成功；另一方必须重新读取，不能覆盖胜者数据。
用户角色/停用/软删除与成员变更都在同一事务追加审计与撤销事件。并发降权/删除不能消除最后一名活跃管理员。
单会话退出仅首次写出 session_revoked 事件，不递增用户授权 revision，不牵连其他会话。
outbox 原子 claim 使用 SKIP LOCKED，批量 1–100，30 秒租约，随机 lease_id；完成/失败均须匹配未过期租约。
过期工作者不能确认新租约；失败重试 1–300 秒，仅存受控错误码。权限提交成功并不依赖发送成功。
事件投递器、接收端幂等和 Broker TTL 仍待接通，不能将 outbox 已持久化等同于在线连接已被撤销。

## 5. 可执行验收

入口：`pwsh -NoProfile -File scripts/server_validation/postgres.ps1 Test`。
Windows 后追加 Linux 原生验证：同命令加 `-Linux`，使用现有 Ubuntu-20.04 WSL2 和本机已缓存 Rust 依赖。
所有测试使用本次随机独立三库/卷；Linux 不连接日常开发库，也不以 Linux 编译成功替代实际查询测试。

| 用例 | 独立预期 |
|---|---|
| registration_lookup_commits_and_collision_does_not_overwrite | 大小写查到同一用户；冲突不覆盖；另一连接看到单条提交记录；无明文密码/token 列 |
| concurrent_registration_has_exactly_one_committed_identity | 20 个并发请求只有一个提交，其他是真实唯一冲突 |
| android_session_is_distinct_and_cross_user_revoke_has_no_effect | android 不能作为 panel 使用；跨用户/猜 ID 拒绝；本人重复撤销安全 |
| expired_session_rejected_while_row_is_still_present | 到期记录仍在库内时也必须拒绝，不依赖 sleep 或清理任务 |
| password_change_invalidates_old_sessions_and_inflight_password_verification | 旧会话、旧密码验证 revision、旧 CAS 全部拒绝；新 revision 可以签发 |
| login_and_revocation_race_never_leave_a_valid_old_revision | barrier 控制的 100 轮竞争，操作完成后旧 revision 无有效会话 |
| disabled_and_deleted_users_are_rejected_without_cleanup | 停用/删除使凭据查询、会话校验和签发立即拒绝 |
| invalid_sessions_have_no_persistent_effect_and_database_failure_is_not_success | 非法时长、未知用户无新增记录；连接池关闭失败；错误 deployment 拒绝 |
| group_membership_changes_invalidate_only_affected_users | 增删成员使相应用户旧会话失效；不变成员不失效；删除组清空成员且新同名组拥有新身份 |
| invalid_group_replacement_preserves_revision_members_and_user_authorization | 未知/重复成员、过期 revision、重复组名、外键失败，无部分持久影响 |
| failed_member_insert_rolls_back_prior_delete_and_all_revisions | 在隔离库精确注入插入失败，验证先前 DELETE、成员与各 revision 全部回滚；移除故障后可提交 |
| competing_membership_replacements_have_one_cas_winner | 20 个不同成员集合竞争同一 revision，只有一个结果提交 |

身份/组/管理增量共 31 条 SQL；设备增量继续扩展查询集合，以测试运行器的精确数量为准。
`queries/*.sql` 用 SQLx 文件宏，在新 PostgreSQL schema 上编译并对比已保存 `.sqlx` 元数据；缺失、多余或 hash 不符失败。
离线构建读取同一份元数据，不需要生产凭据。SQL/schema/metadata 都固定 LF，避免不同系统换行导致 checksum 分叉。
基础套件另覆盖三服务权限、迁移锁、实际杀迁移子进程、两个独立迁移进程重试、断库恢复、三库备份还原与约束验证。

`tests/control.rs` 另外验证 8 项：最后管理员并发自降权、角色/终端拒绝、20 路 CAS、outbox 插入失败时用户或组更新全部回滚、
退出与改密事件、20 路 claim/旧租约拒绝/失败重试、共享 gate 阻塞和有界恢复。最新运行证据见[实施状态](server_database_execution_status.md)。
尚未覆盖：完整资源授权、注册/登录 HTTP、密码计算与限流、命令与事件实际投递、Windows Client/Android 真实登录、Console 全量存储切换。
当前 100 轮是同进程独立连接的身份竞争，不是“两个进程抢最后一个 Render 槽”或全部故障扰动门禁。

## 6. 独立访客主体增量

2026-09-17 接入增量：`px_console_runtime` 已新增身份/管理 HTTP 路由及单活动生命周期，
`pg-20260917-092450-742a93ef` 的 Windows 五组真实 Router + PG 专项通过，837 个源文件与 px_db hash 复核一致。
覆盖注册/登录/本人资料/改密/退出/重启、精确终端绑定、角色/组 CAS、Origin/转发头拒绝、限流、数据库持锁连接被终止。
产品 `px_console` 二进制与 Windows/Android 消费端尚未切换，故上方未完成项不被此专项整体取代。

追加身份审计与管理重置增量：分组自身创建/成员变化/删除写入 append-only `group_events`，空分组同样留痕；
用户改密、管理重置和首次会话撤销追加 `authorization_audit`。管理重置要求当前 admin_web/admin 和用户 revision，
只处理未删除且未禁用账号，权限版本提升、撤销 outbox、审计必须同事务提交，不返回原密码。
这批追加变更已在 `pg-20260917-093329-47b2eafe` 完整跨平台回归中通过；
密码数据库约束和 schema 锁是之后的增量，以[最新实施状态](server_database_execution_status.md)单独留证。

`GuestStore` 使用 guest_sessions/guest_blocks/guest_events，与 users/login_sessions 分开，UUID 不互相兜底。
client_type 仍为 panel/android/user_web，不引入 guest_android 作为 Android 冒充类型；AdminWeb 不可签发访客。
签发只接受业务层生成并 SHA-256 的 CSPRNG token，存储范围为整数秒 1–86400；这不是产品默认登录时长。
每次签发产生新 UUID；无传入旧 ID 复活、到期刷新或旧客户端存储导入。重连必须认证同一个仍有效主体。

GuestStore 的 issue 是受信业务层内部边界，不可直接暴露为无保护 HTTP：入口仍须完成来源/全局限流和请求大小限制。
每次 issue 必须传入 OriginFingerprint：业务层使用部署独立隐私密钥，对可信传输来源做带用途域的 HMAC-SHA256，
不能接收请求体/不可信转发头中的 fingerprint，不存原始 IP。repository 类型不可 Serialize，Debug 脱敏。
HMAC/可信代理边界尚待实际 HTTP 层实现，测试 fingerprint 仅为合成值，不声称已完成边缘滥用防护。
“阻止这个 guest UUID”不等于识别同一自然人；不能宣称清除 cookie/重新申请身份已被彻底防住。
来源散列只用于滥用防护，不作为 owner 或恢复凭据，也不能保证识别换 IP 的同一自然人。

新 guest_source_blocks 保存不可变的管理员、选中 guest、来源 fingerprint、原因、开始/到期时间；组合 FK 防止把不相干来源附到 guest。
管理员显式 block_source（不同于只阻止 guest）受 guest revision CAS 保护；期限为整数秒 1–86400，续加新记录保留历史。
它在独占 gate 内写来源禁令、撤销该来源所有当前有效访客、增加 revision、追加每个访客事件；任何一步失败全部回滚。
用户登录会话及其他来源不受影响。来源阻止会影响共享出口的其他访客，后续 UI 必须明确提示这个更大范围并要求管理员明确选择。
issue 在共享 gate 内检查活动来源禁令再创建；先签发者会被阻止事务撤销，先阻止则签发拒绝，不留竞态漏网会话。
读取同样检查来源禁令；禁令到期仅允许新身份，旧被撤销 token 不恢复。禁令/审计不依赖定时清理才生效或到期。
隐私密钥属于部署外部恢复材料，不能在重启/恢复时自动重生导致来源禁令失去关联。

身份校验使用 DB 当前时间，检查 token、精确 client_type、未撤销和未被阻止；不依赖到期清理。
应用 guest 目录/直接访问先在共享 gate 中认证访客，再运行同一 public 策略；不能进 acl 或管理目录。
公开 RDP 仍允许有效 guest，遵守 `(application,node)` 工作区与单前端 busy；不会因 guest 新建独立用户工作区。

本人注销仅由精确 token/client_type 定位，不接受客户端传入另一个 guest UUID；重复注销成功且不重复事件。
管理员阻止要求 AdminWeb/admin + guest revision CAS；guest_blocks 追加原因码/actor，guest 状态与撤销事件同事务提交。
已阻止记录不提供解除/刷新入口，重复当前版本 block 无新副作用；注销已阻止主体不会覆盖管理员阻止原因。
guest_events 采用与其他 outbox 一致的有界 claim/租约/重试，事件内容不可由 runtime 覆写或删除。
入库成功仍不代表实际连接已断开；投递、节点执行和宽限流程继续由后续阶段接入。

`tests/guests.rs` 有 9 项独立 PG 用例：身份/终端/公开 ACL 分离；到期/重复注销/非法时长/旧 token 不复活；
管理员 20 路 CAS 与事件插入失败回滚；20 路租约领取/晚 ack/重试/不可变审计；20 路重复签发唯一赢家与断库失败。
新增来源阻止/期限、来源阻止事件失败完整回滚、10 轮每轮 20 路签发与阻止竞争，以及 admin/viewer 的有界去密管理分页。
是否已运行通过以[最新报告](server_database_execution_status.md)为准；不代替登录 API、来源限流或 Android 实机验收。
