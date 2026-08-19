# CMS 客户端用户 / 用户组 / 资源访问控制设计

> 状态：待评审 v4（2026-08-19，应用公开/ACL 策略）
> 前置调查：本文 §1 全部基于当前代码现状。

## 0. 名词与边界

- **客户端用户**：在 px_panel 登录的终端用户，存于 `c_user`；仅用于使用设备和应用，**不能**登录 CMS Web。
- **CMS 管理者**：CMS Web 控制台使用者，使用现有 license 授权中的 `username/password` 登录；与 `c_user` 是两套完全独立的账号体系。
- **管理员会话**：CMS 管理者登录后签发的 `admin_token`，只代表管理面权限；不是 `c_user` 的角色，也不使用已删除的 `CmsUser.administrator` 字段。
- **资源**：
  - 设备：可远程连接的 render 主机。
  - 应用：CMS `c_app` 中登记的应用模板，经 `c_app_node`（应用 × 设备 × 端口）调度运行。
  - Panel 本地“游戏”Tab 的 SQLite 游戏库不属于 CMS 资源。
- **应用访问策略**：应用级 `access_mode`，取值为 `public` 或 `acl`（见 §2）。

## 1. 现状与问题

### 1.1 已有地基

| 资产 | 位置 | 状态 |
|---|---|---|
| `c_user` + 登录/注册/改密接口 | `rust_server/px_cms_server/src/user/` | Panel 有完整 UI 闭环在用 |
| `c_user_device` 用户↔设备绑定 | `user_device/` | 登录后拉取并灌入本地列表 |
| 批量生成用户（CSV） | `cms_user_handler.rs` | 后端接口已存在，CMS Web 尚无入口 |
| 应用三层体系 | `app_schedule/manager.rs` | 调度链完整（选节点/预占/启停） |
| 管理者登录 | `auth/verify/auth/account` | 校验 license 账号，当前返回 appkey |
| Argon2 参考实现 | `px_auth_server/author*.rs` | 可复用密码迁移模式 |

### 1.2 必须修复的问题

1. 应用路由及大部分管理路由仅依赖 appkey；appkey 会存在于 Panel、浏览器 localStorage 和旧 launch 链接中，不能作为用户或管理员身份。
2. `c_user_device` 接口由客户端传入 uid，只靠 appkey 可跨用户读写；`desktop_link` 含静态设备密码。
3. `CmsUser` 当前会序列化 password，Panel 也保存用户明文密码；必须改为会话 token 与脱敏 DTO。
4. 现有应用默认可匿名启动，升级时不能突然中断该行为；但新建的私有应用必须能严格按用户组控制。
5. 应用“成功启动”不等于“只能由获授权用户接入”；实例 URL 必须有连接侧的票据校验。

## 2. 核心策略：应用始终启用公开 / ACL 分类

不引入 `force_user_login`、`allow_anonymous_launch` 等运行时模式开关。用户系统和应用访问策略始终启用，由每个应用自己的 `access_mode` 决定是否需要登录。

| 应用策略 | 匿名用户 | 已登录但无 ACL | 已登录且有 ACL | CMS 管理者 |
|---|---|---|---|---|
| `public` | 可见、可启动 | 可见、可启动 | 可见、可启动 | 可管理 |
| `acl` | 不可见、不可启动 | 不可见、启动返回 403 | 可见、可启动 | 可管理 |

- 所有**既有** `c_app` 在迁移时统一写为 `access_mode = "public"`，保证升级后的默认行为与现状一致。
- 新建应用默认 `public`；管理员主动改为 `acl` 并授予至少一个用户组后，才成为私有应用。
- `public` 表示“任何人可访问”；未来若需要“所有已登录用户可访问”，应新增第三种明确策略，而不是改变 `public` 语义。
- 应用策略不改变设备远控的语义：设备和应用仍是独立授权维度。已知静态设备密码的直连风险见 §8。

## 3. 数据模型

### 3.1 `c_user`（扩展）

```rust
pub struct CmsUser {
    // 既有：uid / username / password / assigned / timestamps / deleted / avatar_path
    // deleted=false 为可登录；deleted=true 为禁用或软删除，不再新增 active 字段。
    pub group_ids: Vec<String>,
    // password 使用版本化哈希：存量 MD5 登录后迁移为 Argon2。
}
```

- `CmsUser` 是数据库模型，不能直接作为 HTTP 响应。
- 新增 `CmsUserProfile`、`CmsUserAdminView` 等 DTO，响应中一律不含 password 或 password hash。
- 禁用、删除、重置密码均撤销该用户全部 session。

### 3.2 `c_user_group`（新建）

```rust
pub struct CmsUserGroup {
    pub gid: String,
    pub name: String,              // 唯一
    pub remark: String,
    pub device_ids: Vec<String>,   // 桌面远控授权
    pub app_ids: Vec<String>,      // 私有应用授权（c_app.app_id）
    pub created_timestamp: i64,
    pub update_timestamp: i64,
    pub deleted: bool,
}
```

用户与组为多对多。删除组时必须从所有 `c_user.group_ids` 中级联移除该 gid；组名建立唯一索引。

### 3.3 `c_user_session`（新建）

```rust
pub struct CmsUserSession {
    pub token_hash: String,       // 原始 token 不落库
    pub subject_type: String,     // "user" | "admin"
    pub subject_id: String,       // uid 或 license auth_id
    pub expires_at: i64,
    pub absolute_expires_at: i64,
    pub revoked_at: Option<i64>,
    pub last_used_at: i64,
}
```

- 原始 token 使用不少于 256 bit 的随机值，仅经 `Authorization: Bearer <token>` 传输。
- user/admin 都使用可撤销的不透明 session；不使用 JWT。
- token_hash 唯一索引，expires_at TTL 索引；校验、滑动续期和撤销应原子执行。
- CMS Web 管理者 token 与 `c_user` token 仅共用存储结构，权限及主体严格隔离。

### 3.4 `c_app`（扩展与迁移）

```rust
pub struct Application {
    // 既有字段…
    #[serde(default = "default_public_access_mode")]
    pub access_mode: AppAccessMode, // Public | Acl
}
```

- 启动时执行一次有版本记录的、幂等的 Mongo 迁移：为缺失字段的旧应用写入 `public`。
- 管理端修改为 `acl` 时，若没有任何用户组授予该 app，必须二次确认并显示“该应用将无终端用户可访问”。

### 3.5 授权判定

```text
可见/可连设备 = ∪(组 device_ids) ∪ (个人 c_user_device)，剔除禁用设备
可见/可启动应用 = 所有 public 应用 ∪ ∪(登录用户各组的 app_ids)
```

- 未登录请求仅可看到/启动 public 应用。
- `acl` 应用只按 app_id 判断用户组授权，不叠加节点设备授权；游戏画面流不应因无桌面远控权而被拒绝。
- 用户侧应用 DTO 不返回 node_id、device_id、游戏路径等调度或管理面信息；每项带 `access_mode` 与 `grant_groups: [{ gid, name }]`，供客户端展示授权来源与筛选。
- 实时读库，不使用授权缓存；从组移除资源后，下一次请求立即收缩。既有会话不强制踢出，除非后续另行定义。

## 4. 身份与接口权限

| 主体 | 可用接口 |
|---|---|
| 匿名 | public 应用列表、public 应用启动/launch、登录和既有注册入口 |
| user_token | 自己的 profile/session、自己的设备列表、private 应用列表和启动 |
| admin_token | 用户/用户组/设备/应用全部管理面、任意应用调度 |
| 内部服务凭据 | 设备及服务上报；不能代表用户或管理员 |

### 4.1 登录与会话

- `POST /user/control/login` 仅校验 `c_user`，确认 `deleted=false`，返回 user profile、group 摘要与 user_token。
- 用户 logout 仅撤销当前 user_token；禁用、删除、重置密码撤销该 uid 的全部 token。
- CMS Web 的 `verify/auth/account` 仅校验 license 管理者账号，成功后签发 admin_token；管理面不再以 appkey 作为登录态。
- appkey 可以继续作为内部兼容凭据，但不能授予 user/admin 权限，也不能决定 `acl` 应用是否放行。

### 4.2 用户与设备接口

- 用户接口从 token 提取 uid；不得信任请求体/查询参数中的 uid。
- `query/user/devices` 必须有 user_token，返回组授权 ∪ 个人授权的脱敏设备视图；仅该受控通道下发 desktop_link。
- 个人绑定保留为组外补充，但用户不能凭任意 device_id 自助绑定；需管理员授予或一次性设备认领码。
- 旧 `logged_in_user_id` 在 logout 时清理；它只用于显示状态，不能作为授权依据。

### 4.3 应用列表与启动

新增用户侧接口：

```text
GET  /user/control/query/user/apps
POST /user/control/apps/{app_id}/start
```

- 匿名列表只返回 public 应用；登录用户返回 public 应用与自己 ACL 授权应用。
- `acl` 应用无有效 user_token 或用户不在授权组时统一拒绝，不泄露应用存在性。
- 用户只能以 app_id 自动选节点启动；`/app/node/start/{node_id}`、应用节点管理和实例停止均为 admin_token 专用。
- 旧 `app/launch/*` 链接对 public 应用兼容；可保留其中的 appkey 参数，但它只作为历史兼容数据，不能被当作授权条件。

### 4.4 连接票据（私有应用上线阻塞项）

启动 `acl` 应用成功后，CMS 签发短时、一次性连接票据，绑定：

```text
user session + app_id + instance_id + expire_at
```

Web Client 携带该票据连接 Render；Render 必须验证票据并拒绝缺失、过期、错用户、错实例或已使用的票据。只有完成该闭环，`acl` 才能称为真正的访问控制，而非仅限制“谁能创建实例”。

## 5. 前端与管理面

### 5.1 Panel

1. 左侧导航新增一级 **云端应用** Tab；既有 **远程桌面** Tab 保持现有本地连接列表、添加设备和设备右键操作，不把两种资源混在同一列表中。
2. 云端应用页由独立组件实现（建议 `CloudAppContent`、`RunningAppStrip`、`CloudAppGrid`、`CloudAppCard`），不复用 `AppStreamList`：后者会写本地 stream DB，且含锁定/重启/编辑设备等不适用于应用的操作。
3. 页面布局：顶部固定“正在运行”横条，显示当前用户自己的应用实例及“进入”按钮；下方为横向筛选条和应用卡片网格。

```text
[全部] [公开] [我的组 A] [我的组 B] ...

[图标 应用 P 公开     启动] [图标 应用 A 我的专属 启动] ...
```

4. 未登录时云端应用 Tab 仍可访问，但只显示 public 应用，并提示“登录以查看专属应用”；登录后显示 public 应用和当前用户获授权的 ACL 应用。
5. 一个应用属于多个授权组时卡片只显示一次，但能命中多个组筛选项；卡片可标记“公开”或“我的专属”，不展示设备、节点、端口等调度信息。
6. ACL 用户组仅在其名称本身可作为业务分类时才直接用于横向筛选。若用户组仅服务权限管理，另加 `display_category` 等展示分类字段，不能用 ACL 组替代展示分类。
7. 卡片状态仅由服务端返回：可启动、正在启动、运行中（进入）、当前繁忙、无权限。点击启动后显示启动中，成功后使用启动响应中的短时票据进入 Web Client。
8. 不再保存用户明文密码，只保存 user_token（Windows 侧使用现有安全存储能力）。public/acl 由服务端结果决定，客户端只做展示，不能自行推导或绕过。
9. 应用目录在进入 Tab、登录成功、手动刷新时拉取，可按 30–60 秒刷新；当前用户运行状态可按约 5 秒刷新。

### 5.2 CMS Web

1. CMS 管理者使用 admin_token；不再将 appkey 存作浏览器管理会话。
2. 用户管理页：新增、禁用、删除、重置密码、指派用户组、批量生成 CSV。
3. 用户组页：CRUD、设备与应用勾选、成员查看。
4. 应用页：显示并可编辑 `public` / `acl`；切至 `acl` 时展示已授权组数量和风险提示。

### 5.3 浏览器终端用户门户

用户门户直接实现于现有 `web/px_cms` 项目并随 CMS 同源部署，不新建独立前端项目或服务。它与管理员后台共用构建产物，但必须是相互隔离的路由、布局、会话和 HTTP 客户端。

```text
/                 → 既有 CMS 管理者登录
/home/...         → 既有管理员后台

/user/login       → 终端用户登录
/user/home        → 我的资源（登录后的默认页）
/user/devices     → 我的远程桌面
/user/apps        → 云端应用
/user/activity    → 我的运行实例与近期活动
/user/profile     → 个人中心
```

建议的代码边界：

```text
web/px_cms/src/
├─ admin/                 # 管理员布局与页面
├─ user/                  # 用户门户布局与页面
├─ auth/admin_session.ts  # admin_token 生命周期
├─ auth/user_session.ts   # user_token 生命周期
├─ adminHttp.ts           # 管理端请求
└─ userHttp.ts            # 用户端请求
```

- `AdminLayout` 才可建立管理员 WebSocket；`/user/**` 路由不得读取 appkey、不得建立管理员 WebSocket。现有根 `App.vue` 的无条件 appkey/WebSocket 初始化应迁入管理员布局。
- 管理员路由仅接受 admin_token，用户路由仅接受 user_token；两种 token 的存储 key、HTTP 客户端和路由守卫完全分开。路由守卫仅改善体验，后端仍是唯一的授权判断点。
- 浏览器用户会话使用 `Secure`、`HttpOnly`、`SameSite` cookie，并对写操作做 CSRF 防护；不在 localStorage 存 user_token。
- 用户登录后的默认首页为“我的资源”：汇总当前用户自己的运行实例、获授权设备、public 应用与 ACL 应用；未登录访问门户时仅展示 public 应用，访问私有资源后登录应返回原目标页面。
- 用户门户可调用的核心接口为：登录/退出、`me`、资源汇总、我的设备、设备连接、我的应用、应用启动、我的运行实例；接口不返回 desktop_link、静态密码、节点或端口。

#### 浏览器连接流程

用户门户不重复实现 WebRTC 播放器。设备连接或应用启动后，CMS 校验 ACL 并签发一次性短时票据，浏览器以顶层新页进入目标 Render 的既有 `/web_client/`：

```text
CMS 用户门户 → CMS ACL 校验 / 签发连接票据
             → Render /web_client/#ticket=<one-time-ticket>
             → Render 校验票据 → WebRTC 信令 / 画面与控制
```

- 使用顶层页而不是 iframe，以保证全屏、键盘、鼠标锁定、剪贴板与文件传输能力。
- `px_web_client` 新增票据模式：从 URL fragment 读取 ticket 后立即用 `history.replaceState` 清除，再经信令请求 Header/Body 传给 Render；不再携带设备密码或长期 user_token。
- 现有手工设备 ID/密码连接模式仅作为兼容或调试入口；用户门户全程使用连接票据。

## 6. 安全处理与遗留

| 项目 | 本期处理 |
|---|---|
| MD5 密码 | 版本化透明迁移到 Argon2；新协议完成后停止接受 MD5 重放值 |
| 用户密码外泄 | DTO 脱敏；不回传、不打印、不在 Panel 保存明文 |
| launch 链接 appkey | public 兼容但 appkey 不参与授权；管理与私有权限改用 token |
| 私有应用实例 URL | 连接票据由 Render 验证（§4.4） |
| 静态设备密码直连 | 后续以 CMS 一次性连接票据替代 Render 静态密码校验 |
| 审计 | `c_event` 记录用户登录、private 应用启动、设备连接及管理员授权变更 |

## 7. 实施计划

| 阶段 | 内容 |
|---|---|
| P1：模型与迁移 | `group_ids`、`c_user_group`、`c_user_session`、`c_app.access_mode`、Mongo 索引、旧 app 全量迁为 public |
| P2：认证与 ACL | user/admin session 中间件、脱敏 DTO、用户/设备接口收紧、public/acl 列表与启动判定 |
| P3：连接闭环 | 私有应用连接票据签发、Render 验证、Web Client 携带票据 |
| P4：前端 | Panel 云端应用 Tab；CMS Web 用户组与应用策略管理；同一 `web/px_cms` 内的 `/user/**` 用户门户、双会话隔离与票据跳转 |
| P5：测试与上线 | 自动化、双机 E2E、迁移校验、回归与审计验证 |

P1–P2 完成后 public 应用应与现网完全兼容；`acl` 应用在 P3 完成前不得在生产环境宣称为安全隔离。

## 8. 测试与验收

### 8.1 Rust 单元测试

| 用例 | 断言 |
|---|---|
| 旧应用迁移 | 缺失 access_mode 的记录被一次性写为 public；重复执行无副作用 |
| 授权并集 | 用户属两个组并有个人绑定时，设备集合去重正确 |
| 应用策略 | 匿名仅见 public；登录用户见 public + 自己 ACL；无权 ACL 不泄露 |
| 禁用与会话 | disabled 用户登录 401；禁用/改密后旧 token 401 |
| session 生命周期 | 签发、滑动续期、绝对过期、单 token logout、全量撤销均正确 |
| 主体隔离 | user_token 不能调管理面；admin_token 不能伪装为 c_user；appkey 不可替代两者 |
| IDOR 防护 | 用户传入其他 uid 查询/改设备/改资料均 403 |
| 密码迁移 | MD5 存量成功登录后落库为 Argon2；响应与日志不含密码/哈希 |
| 启动判定 | public 匿名放行；acl 匿名 401、无权 403、有权放行、管理员放行 |
| 连接票据 | 缺失、过期、错 session/instance、重复使用均被 Render 拒绝 |

### 8.2 接口集成测试

使用隔离 Mongo 实例调用真实 CMS HTTP 接口。对每个敏感接口用匿名、U1、U2、管理员、仅 appkey 五种身份逐格验证 HTTP 状态码和返回 DTO，覆盖：用户查询/修改、用户设备、应用列表、应用启动、节点启动及组 CRUD。

### 8.3 双机 E2E

准备：U1 属 G1（设备 D1、private 应用 A），U2 属 G2（private 应用 B），另有 public 应用 P。

1. 未登录 Panel：左侧云端应用 Tab 中 P 可见、可启动；A/B 均不可见，并展示“登录以查看专属应用”；既有远程桌面列表和本地功能不回归。
2. U1 登录：云端应用页可见并启动 P/A，不可见或启动 B 返回 403；顶部仅出现 U1 自己启动的运行实例；设备列表仅有 D1 和个人授权设备。
3. U2 登录：可见并启动 P/B，不可见或启动 A 返回 403。
4. 管理员将 A 从 G1 移除：U1 下次刷新后 A 立即消失，新的启动请求被拒绝。
5. 将旧应用迁移后验证均为 public，旧 launch 链接仍能启动；确认 appkey 不会让任何 private 应用通过。
6. 对 private 应用复制实例 URL、篡改/复用/过期连接票据，Render 均拒绝接入。
7. 回归既有 CMS Web 冒烟、录像、设备连接和 public 应用启动流程。
8. Playwright 覆盖 `/user/**`：游客仅见 public 应用；U1 登录后默认进入“我的资源”、只见自己的设备/实例与 P/A；U2 不可见 A；用户路由不产生管理员 WebSocket，也不能调用管理员接口。

## 9. 已定决策（2026-08-19）

1. CMS 管理者 license 账号与 `c_user` 终端用户严格分离；`CmsUser.administrator` 已移除。
2. 不提供用户访问控制模式开关；应用策略始终生效。
3. 所有存量应用迁移为 `public`，保持现网默认行为；私有化由管理员逐应用选择 `acl`。
4. 用户-组为多对多；个人设备绑定保留，但必须改为受控授予。
5. 应用 ACL 不叠加设备维度；节点调度只在服务端进行。
6. appkey 不再承担浏览器管理会话或用户 ACL 身份；私有应用的连接票据是上线阻塞项。
7. Panel 以左侧“云端应用”一级 Tab 展示应用；远程桌面连接列表与应用目录/实例状态保持独立。
8. 浏览器用户门户直接写入 `web/px_cms`，使用 `/user/**` 独立路由树；它与管理员后台同源部署、同构建产物，但会话与代码边界严格隔离。
