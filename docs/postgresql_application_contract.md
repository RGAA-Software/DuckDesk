# PostgreSQL 应用目录与权限契约

> 2026-09-17，Console DB2-B 新目录 repository。入口 `px_console_store::ApplicationStore`。
> 不是调度、真实节点运行、RDP 工作区或客户端已切换的声明。运行结果见[实施状态](server_database_execution_status.md)。

## 模式和配置

应用主键 UUID；名称 1–128 个 Unicode 标量，拒绝首尾空白和控制字符。三个模式是明确 tagged enum，没有默认模式、旧别名或旧配置字段补齐：

| 模式 | 必填配置 | 禁止混入 |
|---|---|---|
| game_hook | 部署安装根内的相对 executable 路径、原样 arguments、h264/h265 与 bitrate_kbps | WebView URL、RDP 登录/会话前提 |
| webview | 入口 URL、h264/h265 与 bitrate_kbps | 游戏 executable/arguments、RDP 登录/会话前提 |
| rdp | 无二次采集/编码/启动路径字段；观察者和接管都为 false | Render 解码再编码、自动接管、多前端共享、游戏或浏览器启动字段 |

数据库列/CHECK 同时约束模式字段组合；配置来自类型化 DTO，拒绝未知字段。不存在将整个旧文档塞进 JSONB 的存储路径。
`kind` 在该应用 UUID 生命周期内不可更改。需要改模式时新建应用，不把已有 RDP 工作区身份重新解释为 Game Hook 实例。
旧开发版 inactive 时切换模式的接口不作兼容；这不允许删除任何旧 Windows 会话/账号/应用。

Game Hook 相对路径使用 Windows `\` 分隔；保留空格、Unicode 和大小写，拒绝绝对/UNC/驱动器/ADS、`..`/`.`、空组件、
结尾点/空格、控制字符和 Windows 保留设备名，结尾要求 `.exe`（大小写不敏感）。参数最多 8192 UTF-8 字节，保留引号和 tab；
拒绝换行/NUL 等其他控制字符，不在 Console 重新拆词或 shell 展开。
这些是目录校验，不代替节点最终规范化完整路径 + 本次私有 Job 的 AND 判定，也不授权按 basename 注入/清理。

WebView URL 最多 8192 字节；HTTPS 有效主机可用，HTTP 仅 loopback/private/link-local IPv4、loopback/unique-local IPv6、localhost/`.local`。
拒绝凭据、非 HTTP(S)、控制字符及首尾空白；保留查询/fragment 原文。Console 不请求该 URL。
该校验不是网络沙箱/可信站点授权，浏览器实际加载与网络策略仍由 WebView 产品边界负责。

视频码率明确单位为 kbps，范围 128–200000。不继续使用无单位的旧字段；后续 producer/consumer 同步接新单位。
沿用当前产品“不持久化服务端 FPS 限制，由客户端会话控制调整”的决定；不在本增量恢复应用级 FPS 强制锁定。
RDP 无 VideoSpec，Game Hook/WebView 的 allow_observer/allow_takeover 必须显式填写，不静默默认 true。

## 两个版本与去密目录

- `revision`：每次真实配置或关联改变递增，CAS 防止后写覆盖；完全相同配置/组集合为无副作用成功。
- `access_revision`：访问 public/acl、停用、观察者/接管政策、组授权或删除改变时递增，不超过 revision。
- 单纯改名/视频/启动配置不增加权限版本；应用事件仍通知配置 revision 改变。
- 后续部署必须确认相应配置 revision 已准备好，准入必须保存并验证 access_revision；不能只信客户端传来的目录卡片。

管理端 AdminWeb + admin 可写，viewer 只读。用户端仅 panel/android/user_web 精确匹配会话，role 为 user/admin；viewer 不能代用户访问。
用户卡片只包含 id/name/kind/access_mode/revision/access_revision，不含路径、参数、入口 URL、Windows/节点凭据或内部配置。
目录和直接访问应用使用同一 SQL 权限：未停用、未删除，且 public 或本用户属于获授的活跃组。
公开应用仍要求当前有效主体，不把猜中的 ID 当作访问授权；访客 repository 已通过独立鉴权接到同一 public 查询，HTTP 接口尚未接入。

## 事务、事件与删除

沿用[身份契约](postgresql_identity_contract.md)的共享/独占事务 gate。先 gate，再管理主体、应用、组和受影响用户。
关联替换最多 1000 个唯一 group UUID，未知/已删除组拒绝；对 acl 应用，仅前后实际改变可访问性的成员增加用户授权版本及撤销 outbox。
所有真实修改追加 application_events，含 actor/application FK、配置/权限版本和受控事件类型；无路径、参数、口令或任意请求正文。
事件 immutable 字段兼作追加审计，runtime 无 DELETE 或修改这些字段权限；投递状态只允许必要列 UPDATE。
应用修改、用户授权变化、撤销 outbox 与应用事件一起提交；写事件失败必须回滚之前的配置或成员写入。

事件支持 1–100 条 SKIP LOCKED claim、随机 lease_id、30 秒租约、1–300 秒失败重试；过期/已换租约工作者不能确认。
真正投递与节点/连接撤销尚待接通，不能声称现有连接已实时终止。权限收缩后再开放时新的 access_revision 也不会回退，
后续 descriptor/实例续租必须拒绝旧版本，而非仅检查“现在又是 public”就使旧授权复活。

删除为软删除，保存关联、版本和审计；不会向节点发 start/stop，也不注销 RDP Windows Session 或删除 Profile/账号。
模式不可变和软删除的规则，是保护已有资源身份，不是替代后续实例/工作区状态机。

## 当前用例边界

3 个纯校验用例覆盖路径/保留名、WebView URL、RDP 互斥字段和视频边界；8 个真实 PG 用例覆盖：

1. 三种模式配置精确往返，卡片不泄露配置，重复相同更新不产生事件。
2. 目录/直接访问同策略、Android 类型严格匹配、组授权与删除组即时失效。
3. 改名与 public/acl/停用分别更新正确的两个版本。
4. 管理/资源角色分离、非法模式和 SQL 字段组合拒绝，拒绝无持久副作用。
5. 精确撤销 application_events INSERT 权限，验证应用、组、用户 revision、用户 outbox 同时回滚。
6. 20 路配置/组混合 CAS 只有一个胜者、一条新增审计。
7. 并发 claim 不重复、过期 lease/晚 ack 拒绝、延迟重试和不可变审计权限。
8. 游标分页、软删除关联保留、重建连接池仍拒绝已删除对象、断库写入失败。

访客新增用例见[身份契约 §6](postgresql_identity_contract.md)。尚未覆盖：deployment/node 表与登记、配置 ready 对账、资源预约/最后槽竞争、命令发送/回执、RDP 工作区与真实进程、
CloudApplication owner/target/descriptor、HTTP/WS 及 Windows/Android。数据库阶段不附带实现后续 P3 GPU 评分算法。
