# CMS 用户、用户组与资源访问控制设计

> 状态：v6.4 已完成用户 Web 分页、服务端仅观看权限和浏览器安全续票实现（2026-08-21）；生产集群增强项单独标为“后续”。
> 适用范围：单个 CMS 部署（单租户）下的终端用户、CMS 管理者、设备和云端应用。
> 安全底线：appkey 只作为部署级内部凭据，不能代表终端用户或 CMS 管理者。
> 数据策略：项目尚处测试阶段，不迁移、不兼容旧身份与 ACL 数据；升级时清空相关测试数据并按新模型初始化。

## 0. 目标与非目标

### 0.1 目标

1. 将终端用户与 CMS 管理者彻底分离，避免 appkey、uid 或设备静态密码被当作登录态。
2. 用用户组统一授予设备和私有应用权限，同时保留管理员授予的个人设备绑定。
3. 公开应用允许游客发现和启动，但所有连接都必须经过短时票据。
4. Panel、浏览器用户门户和 CMS 管理后台使用同一套服务端授权事实，但使用相互隔离的会话。
5. 设备连接与应用实例连接都形成闭环：授权、启动、连接、控制、停止和审计属于同一主体。
6. 支持禁用用户、改密、组权限变更后立即阻止新的敏感操作，并可撤销既有会话。

### 0.2 非目标

- 本期不做多租户；一个 CMS 部署只服务一个组织。所有集合默认属于同一租户。
- 本期管理面只有 license 所有者一个管理主体，不新增多管理员 RBAC。若以后需要运营员、审计员等角色，新增 `c_admin`，不能把 `c_user` 提升为管理员。
- 本期 ACL 采用“授权并集”，不支持显式 deny。需要 deny 时必须新增独立规则优先级，不能通过数组顺序实现。
- Panel 本地游戏库不是 CMS 云端应用，不纳入本设计。
- 本期不迁移旧用户、旧密码、旧会话、旧用户设备绑定、旧应用授权或旧接口调用方式。

## 1. 身份、会话与资源边界

### 1.1 主体

| 主体 | 身份来源 | 可进入的界面 | 权限边界 |
|---|---|---|---|
| 游客 `guest` | CMS 签发的匿名访客会话 | `/user/**` 公共区域、公开应用 | 只能查看/启动 public 应用，受限流和配额约束 |
| 终端用户 `user` | `c_user` | Panel、`/user/**` 用户门户 | 使用自己获授权的设备、应用和实例；不能进入管理后台 |
| CMS 管理者 `admin` | license 管理账号 | `/` 登录、`/home/**` 管理后台 | 管理用户、组、设备、应用和全部实例 |
| 内部服务 `service` | CMS 与 px_service 的服务凭据 | 无人工界面 | 设备上报、调度、票据兑换；不能伪装成 user/admin |

“终端用户不能登录 CMS Web”统一解释为：终端用户不能登录 **CMS 管理后台**，但可以登录与后台同源部署的 `/user/**` **用户门户**。

### 1.2 资源

- **设备**：可远程连接的 render 主机。
- **应用**：`c_app` 中的云端应用模板。
- **应用节点**：`c_app_node`，表示应用 × 设备 × 端口，仅管理者和调度器可见。
- **应用实例**：一次实际调度运行，必须记录创建者和当前所有者。
- **连接票据**：进入设备或应用实例的短时、一次性凭据，不是会话本身。

### 1.3 重要隔离

- user_token 不能调用管理接口。
- admin_token 不能作为 `c_user` 使用资源，管理者操作必须以 admin 身份单独审计。
- service 凭据不能代表 user/admin 发起资源使用。
- guest/user/admin 三种实例所有者不能仅靠前端字段互相切换。
- appkey 不参与 ACL 判定，不允许作为 Bearer token。

## 2. 应用访问策略

每个应用始终具有明确的 `access_mode`：

| 应用策略 | 游客 | 已登录但无 ACL | 已登录且有 ACL | CMS 管理者 |
|---|---|---|---|---|
| `public` | 可见、可启动 | 可见、可启动 | 可见、可启动 | 可管理、可调度 |
| `acl` | 不可见、不可启动 | 不可见、不可启动 | 可见、可启动 | 可管理、可调度 |

规则：

1. 应用必须显式具有 `access_mode`；新建应用默认 `public`，测试环境中没有该字段的旧应用记录直接删除并重新创建。
2. 管理者可将应用改为 `acl`，但必须同时授权至少一个用户组；前端禁止保存空组列表，后端也拒绝 `acl + 空组列表`。
3. `public` 的含义仍是“允许游客使用”，但不等于无身份：CMS 会创建 guest session，并执行 IP、会话和应用三级限流。
4. 无权访问 `acl` 应用时，列表中不返回；直接猜测 app_id 启动统一返回 404，避免泄露资源存在性。
5. 应用 ACL 不叠加设备远控 ACL。用户可运行某应用，不代表可以远控承载它的节点设备。
6. 若未来需要“所有已登录用户”，新增 `authenticated`，不能改变 `public` 语义。

### 2.1 管理端操作入口

- 应用访问策略属于应用资源本身，只在“应用调度”列表中展示和编辑。
- 每个应用行直接显示 `public` / `acl` 以及已授权用户组，并通过“访问授权”一次保存策略与组分配。
- 用户管理只维护“用户属于哪些组”；用户组页面只维护组信息、成员和设备授权，不提供应用分配入口。
- 后端保留组到应用的关系查询能力，但管理端不提供双向写入口，避免两处修改同一授权关系造成冲突。

## 3. 数据模型

Mongo 中时间统一使用 UTC 毫秒时间戳；所有软删除记录在默认查询中排除。

### 3.1 `c_user`

```rust
pub struct CmsUser {
    pub uid: String,
    pub username: String,
    pub password_hash: String,      // 版本化 Argon2id；不存在旧 password 兼容分支
    pub assigned: bool,
    pub created_timestamp: i64,
    pub update_timestamp: i64,
    pub disabled: bool,             // 可逆禁用；仍在管理员列表中
    pub deleted: bool,              // 不可由普通编辑恢复的软删除；默认查询排除
    pub avatar_path: String,
    pub auth_version: i64,          // 改密/全量注销时递增
}
```

- 数据库模型不得直接作为 HTTP DTO。
- `CmsUserProfile`、`CmsUserAdminView` 均不含密码、密码哈希和内部 Mongo 字段。
- username 建立大小写归一后的唯一索引；显示名称可保留原始大小写。
- 密码只使用 Argon2id。旧 `password`/MD5 记录直接删除，不提供识别、登录或透明升级分支。
- 禁用、软删除、重置密码、用户主动“退出全部设备”时递增 `auth_version` 并撤销全部会话；禁用可重新启用，删除记录不再出现在默认列表。
- 软删除用户时同步删除其用户组成员关系和个人设备授权；用户组成员数量与成员编辑列表始终只包含未删除用户，历史残留关系不得影响授权或计数。

### 3.2 用户组与授权关系

采用独立关系集合，不在 user/group 文档中维护双向数组，避免级联更新不一致。

```text
c_user_group
  gid, name(unique), remark, deleted, created_at, updated_at

c_user_group_member
  uid, gid, created_at             unique(uid, gid)

c_group_device_grant
  gid, device_id, created_at       unique(gid, device_id)

c_group_app_grant
  gid, app_id, created_at          unique(gid, app_id)
```

- 删除组采用“先带 version 软删除，再幂等清理关系”的可恢复流程；清理中断时重复删除即可完成。
- 个人设备授权继续使用 `c_user_device`，但只能由 admin 授予，或通过受控的一次性设备认领码建立。
- 删除/禁用设备和应用时不必立即删除历史 grant，但授权查询必须剔除无效资源；后台任务可异步清理孤儿关系。

### 3.3 `c_user_session`

```rust
pub struct CmsSession {
    pub sid: String,
    pub token_hash: String,         // 原始 token 永不落库
    pub subject_type: String,       // guest | user | admin
    pub subject_id: String,         // guest_id | uid | license auth_id
    pub auth_version: i64,
    pub client_type: String,        // panel | user_web | admin_web
    pub created_at: i64,
    pub last_used_at: i64,
    pub expires_at: i64,            // 滑动过期
    pub absolute_expires_at: i64,
    pub revoked_at: Option<i64>,
    pub ip_hash: String,            // 风控使用，非硬绑定
    pub user_agent_hash: String,
}
```

- token 至少使用 256 bit CSPRNG，数据库只保存 SHA-256/HMAC 后的 token_hash。
- token_hash 唯一索引；`expires_at` 建 TTL 索引；校验、滑动续期和撤销必须原子化。
- 默认期限可配置：Panel user 30 天滑动/90 天绝对；用户门户 12 小时滑动/30 天绝对；管理后台 2 小时滑动/8 小时绝对；guest 24 小时绝对。
- user session 校验时同时比较 `auth_version`，因此改密或全量注销无需等待逐条删除完成。

### 3.4 Session 的客户端传输

| 客户端 | 传输 | 本地保存 |
|---|---|---|
| Panel | `Authorization: Bearer <user_token>` | Windows Credential Manager/DPAPI；禁止 SQLite 明文 |
| 未登录 Panel | `Authorization: Bearer <guest_token>` | 仅保存在进程内存，过期后自动重建；只能访问 public 应用 |
| 浏览器用户门户 | `__Host-px_user_session` Cookie | Secure、HttpOnly、SameSite=Lax、Path=/ |
| 浏览器管理后台 | `__Host-px_admin_session` Cookie | 与用户 Cookie 名称和校验中间件完全分离 |

- 浏览器写操作额外校验 CSRF token；服务端拒绝跨站 Origin/Referer。
- `/user/**` 不读取 admin cookie，`/admin/**` 不读取 user cookie。
- 禁止将 user/admin token、密码或 appkey 放入 URL、localStorage、日志和 WebSocket query。

### 3.5 `c_app` 与实例归属

```rust
pub struct Application {
    // 既有字段
    pub access_mode: AppAccessMode, // Public | Acl
}

pub struct AppInstanceOwner {
    pub owner_type: String,         // guest | user | admin
    pub owner_id: String,
    pub owner_session_id: String,
    pub app_id: String,
    pub instance_id: String,
    pub created_at: i64,
}
```

- 实例启动、查询、进入和停止都必须校验 owner；admin 可查看/停止全部实例，但以 admin 操作审计。
- 同一用户重复启动的复用规则由 app 调度策略决定，不能仅靠 IP 判断。
- guest 实例绑定 guest session。关闭浏览器不会立即停止实例，按应用空闲策略回收。

### 3.6 `c_connection_ticket`

```rust
pub struct ConnectionTicket {
    pub ticket_hash: String,
    pub kind: String,               // device | app_instance
    pub subject_type: String,       // guest | user | admin
    pub subject_id: String,
    pub session_id: String,
    pub device_id: String,
    pub app_id: Option<String>,
    pub instance_id: Option<String>,
    pub permissions: Vec<String>,   // view, input, clipboard, file, audio
    pub client_nonce: String,
    pub expires_at: i64,
    pub consumed_at: Option<i64>,
}
```

- 原始 ticket 至少 256 bit，只在签发响应中出现一次；数据库保存 ticket_hash。
- 默认有效期 30 秒，一次性消费。兑换时使用 `未消费 && 未过期 && 全部绑定字段匹配` 的原子条件更新。
- 重连必须重新向 CMS 申请票据；不能复用旧票据。浏览器使用只存在于 URL fragment/内存中的轮换 renewal capability 换取新票据；renewal 每次成功使用后立即轮换，仍需重新校验 session、ACL、owner、实例状态和设备在线状态。
- 票据只用于建立连接，不替代 user/admin session。

## 4. 授权计算

### 4.1 设备

```text
用户可用设备 = 组设备授权并集 ∪ 个人 c_user_device 授权
             - 已删除/禁用设备
```

- 实时查库，不做长期 ACL 缓存。
- 用户只能请求自己的设备，uid 必须从 session 提取，不能读取请求体中的 uid。
- 用户设备 DTO 只返回名称、在线状态、能力等展示字段，不返回 `desktop_link`、静态密码、内部节点地址。
- 点击连接后 CMS 再签发 device ticket 和短时 `launch_url`。

### 4.2 应用

```text
游客可用应用 = public
用户可用应用 = public ∪ 用户所在组的 acl app_id
```

- 用户应用 DTO 不返回 node_id、device_id、端口、游戏路径和调度权重。
- DTO 可返回 `grant_sources: [{ gid, name }]` 供“我的组”筛选；public 应用不返回无意义的 grant_sources。
- 从组移除授权后，新的列表、启动和连接票据请求立即收缩。已建立的媒体连接默认不中断；若业务要求强制踢出，使用单独的 revoke-active-connections 操作并审计。

### 4.3 权限快照

连接票据中的 `permissions` 是本次连接的最小权限快照：

- 设备远控通常包含 `view,input`，剪贴板、文件和语音按设备策略叠加。
- 后台监控等内部观察者使用独立 service/admin 流程，不伪装为 user，也不能继承输入权限。
- Render 必须按票据权限创建通道；前端隐藏按钮不能作为权限控制。

## 5. 注册、登录和密码策略

### 5.1 注册策略

- 用户可直接注册，不设置邀请码或注册模式开关。
- 注册接口必须通过 guest session 和 CSRF 校验，并实施账号/IP 速率限制；不能依赖 appkey 判断是否允许注册。
- 自助注册用户初始不属于任何用户组，也不具备个人设备授权；后续由管理员分组和授权。

### 5.2 登录

1. 外部登录必须通过 HTTPS/WSS；非 loopback 的明文 HTTP 登录直接拒绝。
2. 客户端提交 username + password，不提交可长期重放的 MD5 作为“密码”。
3. 服务端校验 deleted、限流状态和 Argon2id；任何 MD5 或旧 password 格式都直接拒绝。
4. 登录成功签发 session，返回脱敏 profile 和组摘要。
5. 登录失败使用统一错误，不区分用户名不存在或密码错误。

### 5.3 批量用户 CSV

批量生成初始密码是“不回传密码”规则的唯一受控例外：

- 随机明文密码只存在于生成请求内存中，并通过一次下载响应输出。
- 数据库只保存 Argon2id；不得将明文写入日志、事件、临时文件或普通用户 DTO。
- CSV 下载响应使用 `Cache-Control: no-store`；生成完成后无法再次查看相同明文密码，只能重置。
- 管理后台每次可创建 1–500 个账号，可选择初始组；用户名使用指定前缀与随机后缀，避免并发批次冲突。

## 6. 接口设计

建议把管理端和用户端物理分路，避免继续在 `/user/control` 下混合身份。

### 6.1 公共认证接口

```text
POST /api/v1/session/guest
POST /api/v1/session/user/login
POST /api/v1/session/user/logout
POST /api/v1/session/user/logout-all
POST /api/v1/session/admin/login
POST /api/v1/session/admin/logout
POST /api/v1/user/register
```

### 6.2 用户门户/Panel 接口

```text
GET  /api/v1/user/me
PATCH /api/v1/user/me                         # { username }
PUT   /api/v1/user/me/avatar                  # multipart(file)，最大 2 MiB
POST  /api/v1/user/me/password                # { current_password, new_password }；成功后撤销旧会话并换发当前 token
GET  /api/v1/user/resources/summary
GET  /api/v1/user/devices
POST /api/v1/user/devices/{device_id}/ticket
GET  /api/v1/user/apps
POST /api/v1/user/apps/{app_id}/start
GET  /api/v1/user/instances
POST /api/v1/user/instances/{instance_id}/ticket
POST /api/v1/user/instances/{instance_id}/stop
```

`GET /api/v1/user/devices` 在服务端合并个人 `c_user_device` 与用户所属组的设备授权，去重并剔除无效设备，只返回 `DeviceSummary`；不得返回 `desktop_link`、静态密码或内部地址。

- 所有 uid/owner_id 从 session 获取。
- start 返回用户可见的实例状态；成功时可同时返回一次性 `launch_url`。
- `launch_url` 会包含浏览器必须访问的 Render 地址，但不含设备密码和长期 session。资源列表 DTO 不提前暴露节点信息。

### 6.3 管理接口

```text
/api/v1/admin/users/**
/api/v1/admin/groups/**
/api/v1/admin/devices/**
/api/v1/admin/apps/**
/api/v1/admin/instances/**
/api/v1/admin/audit/**
```

- 用户 CRUD、组成员、设备/应用 grant、应用 access_mode、节点调度均要求 admin session。
- appkey 路由只允许明确列出的部署级内部服务功能，不能代理到 user/admin handler。

### 6.4 错误语义

| 情况 | HTTP |
|---|---|
| 未登录访问需要 user/admin 的接口 | 401 |
| 已登录但主体类型错误 | 403 |
| 无权的私有资源或不存在资源 | 404 |
| CSRF/Origin 校验失败 | 403 |
| 限流或配额耗尽 | 429 |
| 实例正在启动/资源占用 | 409 |

## 7. 连接票据闭环

Render 不直接访问 Mongo；一次性票据通过现有 CMS ↔ px_service 长连接兑换。

```text
Panel/浏览器
  → CMS：携带 user/admin/guest session 请求连接
  → CMS：实时 ACL + 实例 owner + 配额校验
  → CMS：创建 ticket_hash，返回一次性 ticket + launch_url
  → Web Client：从 URL fragment 读取 ticket，立即 history.replaceState 清除
  → Render：在 WebRTC 信令 body/header 中收到 ticket
  → Render → localhost px_service：请求兑换
  → px_service → CMS service WS：RedeemConnectionTicket
  → CMS：原子消费，返回 device/instance/permissions/client_nonce
  → Render：核对本机 device/instance 后建立对应权限的 RTC 会话
```

要求：

- ticket 放 URL fragment，不放 query；fragment 不发送给 HTTP 服务端，也不进入代理访问日志。
- Web Client 读取后立即清除 fragment，并只在信令 Header/Body 中发送一次。
- px_service 到 Render 的兑换接口只允许 loopback，并使用服务端随机 IPC 凭据或命名管道 ACL。
- CMS service WS 消息必须绑定已认证 device_id，不能由请求字段覆盖连接身份。
- 兑换超时、重复、错设备、错实例、错 client_nonce、已撤销 session 均拒绝。
- CMS 不可达时不降级为静态设备密码；用户门户连接明确失败。手工 ID/密码模式只保留为管理员显式开启的调试功能。

## 8. 前端设计

### 8.1 Panel

1. 左侧导航保留“远程控制”，新增独立一级“云端应用”；两页使用独立列表实例和独立资源集合，严禁把应用卡片并入远程设备列表。
2. “远程控制”只显示手工添加的远程设备和 CMS 授权设备；“云端应用”只显示 CMS 下发的 public/ACL 应用；已有“安装的游戏”仍表示 Panel 本地游戏库，三者不得混用。
3. 未登录首次进入 Panel 即自动建立仅存于内存的 `guest_panel` Bearer 会话；进入“云端应用”可看到 public 应用，不依赖登录事件或手工刷新。
4. 登录后“云端应用”显示 public + 当前用户 ACL 应用；顶部/卡片显示该主体自己的运行实例。退出、会话过期、切换账号或 ACL 收缩后，一个刷新周期内独立对账应用集合。
5. 应用卡片标识公开/专属及运行状态，只提供启动/进入/仅观看/停止，不展示节点、设备或端口；新实例的 HTTP 202 与幂等复用的 HTTP 200 都按成功处理。应用卡片不写入远程设备 SQLite。
6. 应用连接可复用成熟的 px_client 启动、全屏和控制链路，但只能复用连接控制器，不能复用远程设备页面容器或把应用伪装成设备。
7. Panel 只保存 user token 到 Windows 安全存储，不保存明文密码。
8. 设备和应用连接都从 CMS 申请 ticket；静态密码只在手工连接调试入口使用。

### 8.2 CMS 管理后台

- 使用 admin cookie，不再把 appkey 或密码存入 localStorage。
- 用户页：创建、批量 CSV、禁用、软删除、重置密码、组成员、个人设备授权、会话撤销。
- 用户组页：CRUD、成员、设备 grant，并只读显示专属应用数量。
- 应用页：public/acl 编辑、授权用户组分配、获授权组数量、无授权风险提示；应用授权只在应用调度页操作。
- 实例页：显示 owner 类型/名称、来源应用、节点、状态和管理员停止操作。

### 8.3 浏览器用户门户

用户门户继续放在 `web/px_cms`，与管理后台同源构建，但路由、布局、Cookie 和 HTTP client 分离。

```text
/                         管理者登录
/home/**                  管理后台

/user/login               终端用户登录
/user/home                我的资源
/user/devices             我的远程桌面
/user/apps                云端应用
/user/activity            我的实例与活动
/user/profile             个人中心
```

建议代码边界：

```text
web/px_cms/src/
├─ admin/                 AdminLayout、页面、adminHttp
├─ user/                  UserLayout、页面、userHttp
├─ auth/admin_session.ts
├─ auth/user_session.ts
└─ public/                guest session 与公共应用
```

- `AdminLayout` 才建立管理员 WebSocket；`/user/**` 不读取 appkey，不建立管理员 WS。
- 路由守卫只负责体验，后端中间件是唯一授权事实。
- 用户设备/应用进入现有顶层 `/web_client/`，不使用 iframe，以保留全屏、键鼠锁定、剪贴板和文件传输。

## 9. 匿名启动的限流与配额

public 应用允许匿名启动，但必须具备以下保护：

1. 单 IP 建立 guest session 和登录尝试限流。
2. 单 guest session 的启动频率、并发实例数和每日累计时长配额。
3. 单 public 应用的全局并发上限；容量不足立即返回 429，客户端退避重试，不在 CMS 内维护悬挂队列。
4. 同一 client_nonce + app_id 的短时间幂等启动，避免刷新页面重复建实例。
5. 达到阈值返回 429/409，不泄露节点容量细节。
6. 管理后台显示 guest 实例及其脱敏来源，支持封禁 IP hash/guest id。

## 10. 审计与隐私

`c_event` 至少记录：

- user/admin 登录成功、失败、退出、全量撤销；
- 用户创建、禁用、删除、改密、组成员变更；
- 设备/应用 grant 变更、应用 access_mode 变更；
- guest/user/admin 启动与停止实例；
- 设备/实例 ticket 签发、兑换成功和拒绝原因分类；
- 管理者强制停止实例或撤销活跃连接。

禁止记录：

- 明文密码、MD5、Argon2 hash；
- 原始 session token、CSRF token、ticket；
- desktop_link、设备静态密码；
- 完整客户端 IP（按部署隐私要求保存截断值或带服务端盐的 hash）。

内部后台监看观察者属于 service/admin 特殊媒体通道，不伪装成终端用户；是否进入安全审计由监看功能自身策略决定，不能污染用户使用时长和连接数统计。

## 11. 测试数据重置与初始化

本次是破坏性升级，不编写旧数据迁移器，也不保留新旧双轨接口。部署前先停止 CMS、Panel 测试客户端和所有相关应用实例，确认连接的 MongoDB 是测试库，然后执行一次性重置。

### 11.1 删除范围

删除以下旧数据及相关索引：

- `c_user`、`c_user_device`；
- 旧用户组、成员和授权数据（若测试分支已经创建）；
- user/admin/guest session 和连接 ticket；
- 归属于旧主体的应用实例、启动记录与临时调度状态；
- 缺少 `access_mode` 或仍依赖旧授权字段的测试应用及其节点记录。

设备注册数据、license、系统配置等不属于用户/ACL 的数据默认不删除。若应用与节点全部是可重建测试数据，可单独清空；删除脚本不得使用整库无条件 drop，必须显式列出集合并校验数据库名称。

### 11.2 初始化顺序

1. 启动新 CMS，创建新集合以及 group/member/grant/session/ticket 的唯一索引和 TTL 索引。
2. 建立 license 管理者会话配置；当前测试产品决策为开放直接注册，新用户默认无用户组和个人设备授权。
3. 重新创建测试用户；密码只生成 Argon2id hash。
4. 重新创建应用并显式写入 `access_mode`，再创建用户组、成员以及设备/应用授权。
5. 重新登记个人设备授权，启动新的应用实例并签发新 ticket。
6. 完成权限矩阵和双机 E2E 后才允许继续使用该测试环境。

### 11.3 代码清理要求

- 删除旧 password/MD5 校验、旧 uid 自报、旧 appkey 用户认证和旧 desktop_link 直出逻辑。
- 删除旧路由，不设置兼容截止时间，也不通过 feature flag 恢复。
- 新模型初始化失败时停止服务并报错，不回退到旧接口、静态密码或放宽 ACL。
- 测试阶段的回退方式是恢复旧程序并重新创建测试数据，不要求新数据库向旧结构反向迁移。

## 12. 实施阶段

| 阶段 | 内容 | 完成标准 |
|---|---|---|
| P0：安全止血 | 用户 DTO 脱敏、移除密码日志、Panel 不再明文保存密码 | HTTP/日志/本地存储无密码及 hash |
| P1：身份会话 | user/admin/guest session、Cookie/Bearer、CSRF、Argon2、注册策略 | appkey/uid 不能代替身份 |
| P2：ACL 模型 | group/member/grant、access_mode、资源查询和启动授权 | public 可匿名使用，acl 实时收缩 |
| P3：连接闭环 | device/app ticket、px_service 兑换、Render 权限通道、实例 owner | 复制/篡改/复用票据均失败 |
| P4：前端 | Panel 云端应用、CMS 用户组管理、`/user/**` 门户、双会话隔离 | 三端只展示各自主体资源 |
| P5：测试部署 | 精确清理旧测试数据、初始化、单元测试、接口矩阵、双机 E2E、限流和审计 | 新结构可从空数据独立启动，安全验收全部通过 |

P0–P1 必须先于用户组 UI。P3 完成前，ACL 只能限制“谁能发起启动”，不能宣称已经限制“谁能连接实例”。

### 12.1 v6.2 收尾计划与完成定义

本轮不再以“接口存在”作为完成标准，而以用户从资源发现到退出的完整可用链路作为完成标准。

| 顺序 | 工作项 | 完成定义 |
|---|---|---|
| 1 | Panel 应用资源列表 | 左侧存在独立“云端应用”一级 Tab；游客进入后看到 public 应用，登录后切换为 public + ACL；远程控制页不出现任何应用卡片 |
| 2 | 客户端资源对账 | 登录、退出、会话过期和 ACL 变化后自动刷新；网络瞬断保留现有列表并显示可重试状态，不把失败误判为空授权 |
| 3 | 应用生命周期 | 启动、进入、仅观看、停止和用户取消停止均有确定结果；取消关闭本地客户端时不得误停远端实例 |
| 4 | 权限闭环 | 登录用户按策略取得 view/input/clipboard/file/audio；游客保持最小权限；Render 对各通道执行票据能力校验 |
| 5 | Web 用户门户 | 已有 Cookie 的新标签页可恢复 CSRF；头像、改密、退出全部设备可用；设备、应用、实例状态自动更新并可分页 |
| 6 | 大规模成员管理 | 用户超过 100 个时仍可在用户组中搜索和选择全部成员 |
| 7 | 自动化与部署 | Rust、Web 和客户端关键逻辑测试通过；本机 CMS + Service + Render/应用完成游客与用户两套真实链路验收 |

### 12.2 v6.2 验收矩阵

| 场景 | 操作 | 预期结果 |
|---|---|---|
| Panel 页面分区 | 分别进入远程控制、云端应用、安装的游戏 | 远程控制只含设备；云端应用只含 CMS 应用；安装的游戏只含本地游戏库，三类资源不串页 |
| 游客 Panel | 未登录打开云端应用页 | 自动创建内存 guest session，只显示 public 应用，不落盘 guest token |
| 用户 Panel | U1 登录并属于 G1 | 云端应用页显示 public 与 G1 应用，远程控制页显示授权设备；不显示其他组资源 |
| ACL 收缩 | 管理端移除 G1 的应用授权 | 一个刷新周期内卡片消失；已有卡片发起的新启动和新票据同时被服务端拒绝 |
| 应用启动 | 点击未运行应用 | 状态从启动中变为运行中，取得一次性 ticket 并启动 px_client；界面不暴露节点、端口和静态密码 |
| 应用停止 | 选择停止后取消本地关闭确认 | 本地客户端和远端实例均保持运行；确认后两者才停止 |
| 权限 | 普通连接、仅观看、游客连接 | 普通用户按策略取得能力；仅观看无 input/clipboard/file；游客仅取得最小能力 |
| Web 新标签页 | 已登录 Cookie 存在但 sessionStorage 为空 | 自动恢复 CSRF，启动、签票和改资料不会返回 403 |
| 用户组规模 | 创建超过 100 个测试用户 | 任意用户均能被搜索、选中和保存到用户组 |
| 票据安全 | 重放、篡改、过期、跨设备使用 | 全部拒绝；并发兑换至多一次成功；日志不出现原始 ticket |
| 生命周期恢复 | CMS/Service 重启并重新对账 | running/starting/stopping 实例恢复或进入稳定失败状态，不重复启动进程 |

本轮自动化测试至少覆盖：应用列表过滤、ACL 动态收缩、CSRF 恢复、启动/票据/停止、权限缩小、停止取消语义和超过 100 用户的成员选择。真实媒体连接与进程生命周期使用本机已配置应用做最终验收。

### 12.3 v6.2 验收结果（2026-08-21）

- CMS 全量 Rust 测试 118/118 通过；px_service 测试 37/37 通过，其中包含 Windows WMI 终止后延迟消失的回归场景。
- CMS Web 类型检查与生产构建通过；Vitest 10/10、Chromium Playwright 4/4 通过。
- Panel `RelWithDebInfo/Official` 目标编译并链接通过，已部署到本机运行目录；CMS、Web 与 px_service Release 也已部署。
- 临时登录用户真实完成：创建、首次改密、public 应用发现、应用启动、完整能力票据签发、浏览器观看端加载、实例停止和账号清理。
- 临时 guest session 真实完成：只发现 public 应用、只获得 `view,input`、视频达到 `readyState=4` 且播放时间持续前进、实例成功停止。
- ACL 动态收缩真实完成：G1 用户可见并启动专属应用；授权切换到 G2 后，G1 列表立即消失，已有实例的新 ticket 返回 404，但 owner 仍可停止实例；应用策略和临时数据均已恢复/清理。
- Cookie 会话真实完成：新上下文仅凭 HttpOnly Cookie 查询 `/me`，从同源 CSRF 恢复接口取得新令牌，并成功执行资料写操作。
- 实测发现并修复 px_service 停止误判：旧逻辑把“等待进程出现”误用于 kill 后检查，WMI 的短暂旧快照会导致 409；现改为轮询等待 Render 消失，部署后真实启动/停止连续通过。

### 12.4 v6.3 Panel 资源分区修正结果（2026-08-21）

- 恢复 v5.1 已确定的信息架构：左侧新增独立一级“云端应用”，并保留“远程控制”和可选的“安装的游戏”；三者不再共用页面容器或资源集合。
- 远程列表只查询/对账设备并从本地数据库加载设备卡片；云端应用列表只查询 CMS 应用且仅保存在内存，不写入远程设备 SQLite。
- 登录、退出或切换账号时先清空上一主体的 CMS 设备/应用投影，再按新主体刷新，网络失败不会让上一用户的私有资源残留在新会话界面。
- 应用仍复用原有 px_client 启动、票据、仅观看和停止控制器，但卡片、菜单和按钮使用“启动应用/进入应用/停止应用”语义，不再显示“启动游戏”。
- Panel Official/RelWithDebInfo 增量编译链接通过，二进制和三套语言资源已部署；实机截图确认“远程控制”页没有 `car`，“云端应用”页只显示 `car`，本地“安装的游戏”仍为独立入口。
- 从“云端应用”页实际点击“启动应用”创建 guest 实例 `inst-7-a5d21880`，实例进入 running（存在 `started_at_ms`）并随后正常 stopped；验收结束后活动实例和测试 px_client 进程均为 0。

### 12.5 v6.4 用户 Web 收尾结果（2026-08-21）

- 用户设备、应用和实例新增服务端分页、搜索与状态过滤接口；旧数组接口仅保留给现有原生 Panel，避免协议突变。
- 用户和访客应用页均支持启动、进入、仅观看、停止；停止操作需要明确确认，活动页展示创建、启动、停止和稳定错误信息。
- 仅观看连接只申请 `view`，Render 最终能力来自票据 grant；Web Client 不能通过关闭本地“仅观看”开关扩大权限。
- 浏览器首次票据附带独立 renewal capability；完整重连或接管再次信令前，由跨源受限端点轮换 renewal 并签发新的一次性 ticket。续票重新校验 session、owner、ACL、实例和设备状态。
- CMS Rust 120/120、CMS Web Vitest 13/13、Chromium Playwright 常规用例 6/6 及现场 RTC 续票用例 1/1 通过；CMS Web 与 Web Client 生产构建通过。部署后真实 guest 实例完成 `view` 签票、renewal 轮换、旧 renewal 重放拒绝，并由浏览器主动关闭 PeerConnection 验证自动续票、RTC 重连、视频继续播放和实例清理。

## 13. 测试与验收

### 13.1 单元测试

- password：仅接受 Argon2 新密码、拒绝旧 MD5/password、响应和日志脱敏。
- session：签发、滑动/绝对过期、auth_version、当前退出、全部退出、主体隔离。
- ACL：多组并集、个人设备授权、孤儿资源剔除、public/acl、实时撤权。
- owner：guest/user/admin 实例查询、进入和停止边界。
- ticket：一次性、过期、错 session/device/app/instance/nonce、并发兑换仅一次成功。
- DTO：用户侧永不出现 password、desktop_link、静态密码、节点路径。
- 限流：guest、登录、启动和全局应用配额。

### 13.2 接口权限矩阵

对每个敏感接口使用 guest、U1、U2、admin、service、仅 appkey 六种身份逐格验证：

- 用户/组 CRUD；
- 我的设备、任意 uid 设备查询；
- public/acl 应用列表与启动；
- 节点调度；
- 实例查询/进入/停止；
- ticket 签发和兑换。

### 13.3 双机 E2E

准备 U1∈G1（设备 D1、ACL 应用 A），U2∈G2（ACL 应用 B），另有 public 应用 P：

1. guest 只见 P，可在限额内启动，实例归 guest session。
2. U1 见 D1、P、A，只能进入/停止自己的实例；猜测 B 返回 404。
3. U2 见 P、B，不可见 A/D1。
4. admin 从 G1 移除 A 后，U1 刷新立即消失，新启动和新 ticket 均拒绝。
5. 复制、修改、并发复用、过期、跨设备使用 ticket 全部失败。
6. user cookie 访问 admin API、admin cookie 访问 user owner API、appkey 访问二者均失败。
7. 浏览器地址、history、access log、前端错误日志不出现 token、ticket 或设备密码。
8. 新建 public 应用允许游客启动，匿名滥用受配额限制；缺少 access_mode 的记录不会被服务加载。
9. Playwright 验证 `/user/**` 不创建 admin WebSocket，退出后 Cookie 失效，返回原目标页逻辑正确。

## 14. 已定决策

1. 终端用户可登录 Panel 和 `/user/**` 门户，但不能登录 CMS 管理后台。
2. 管理者与终端用户是独立主体；不恢复 `CmsUser.administrator`。
3. 当前部署为单租户、单 license 管理者；多管理员未来独立建模。
4. 用户组授权采用独立关系集合和授权并集，不做 deny。
5. 新建应用默认 public；旧应用测试数据直接删除重建；public 游客启动必须经过 guest session、限流、配额和票据。
6. 用户应用 ACL 不叠加节点设备 ACL。
7. Panel 使用 Bearer + Windows 安全存储；浏览器使用相互隔离的 HttpOnly Cookie。
8. 设备和应用实例统一使用 CMS 一次性连接票据，Render 通过 px_service 向 CMS 原子兑换。
9. appkey 永不承担 user/admin 身份；私有连接不可在 CMS 故障时降级到静态密码。
10. P0/P1 先于用户组 UI，P3 是 ACL 安全上线阻塞项。
11. 测试阶段采用破坏性数据重置，不迁移旧身份/ACL 数据，也不保留旧认证与授权接口。
12. Panel 的远程设备、CMS 云端应用和本地游戏是三个独立资源域；底层连接能力可以复用，导航、列表模型和持久化边界不得合并。

## 15. HTTP API 实现契约

### 15.1 通用约定

- JSON 请求和响应均使用 `application/json; charset=utf-8`；CSV 下载除外。
- 成功响应统一为 `{ "code": 200, "message": "ok", "data": ... }`。
- 失败响应统一为 `{ "code": <业务码>, "message": <稳定错误文本>, "data": null, "request_id": <追踪号> }`，不得带数据库错误、路径、密钥或栈信息。
- 时间字段统一为 UTC Unix 毫秒，字段名以 `_at` 结尾；ID 均为不透明字符串，客户端不得解析。
- 新分页列表接受 `page`（从 1 开始，默认 1，最大 100000）、`page_size`（默认 20，最大 100）、`keyword` 和可选状态条件；响应为 `{ items, page, page_size, total }`。用户资源使用 `/devices/page`、`/apps/page`、`/instances/page`；旧数组接口仅供现有原生 Panel 兼容。
- 应用启动通过 `owner_session_id + app_id + client_nonce` 唯一约束实现幂等；ticket 保持一次性语义。通用 `Idempotency-Key` 存储属于后续多副本增强，不作为当前单机版本接口。
- 修改和删除接受实体 `version`；Mongo 更新条件包含当前 version，冲突返回 409，成功后 version 递增。
- 只允许显式 DTO 字段，未知字段返回 400；字符串先 trim，再校验长度和 Unicode 控制字符。

### 15.2 会话 DTO

```text
POST /api/v1/session/guest
request:  { client_nonce, client_type? }             # Panel 显式传 panel
response: { csrf_token, expires_at, access_token? }  # 仅 Panel 返回临时 guest Bearer；Web 使用 Cookie

POST /api/v1/session/user/login
request:  { username, password, client_type }
response: { profile, csrf_token, expires_at, absolute_expires_at,
            access_token? }                          # 仅 client_type=panel 返回 access_token

POST /api/v1/session/user/logout
request:  {}
response: { revoked: true }

POST /api/v1/session/user/logout-all
request:  { current_password }
response: { revoked_sessions }

POST /api/v1/session/admin/login
request:  { username, password }
response: { profile, csrf_token, expires_at, absolute_expires_at }
```

- 浏览器从 `X-CSRF-Token` 响应头或登录响应体取得 CSRF token，后续写请求使用同名请求头；服务端保存其 hash 并与 session 绑定。
- Panel 登录只接受 `client_type=panel`，在响应体取得 Bearer token；浏览器登录不在响应体返回 token。未登录 Panel 可取得独立 `guest_panel` Bearer，该 token 不写磁盘且不能调用用户或管理接口。
- 登录失败统一返回 401/`AUTH_INVALID_CREDENTIALS`；禁用用户也不泄露具体原因。限流返回 429 和 `Retry-After`。
- Set-Cookie 和清除 Cookie 必须在服务端完成；logout 即使 session 已过期也返回幂等成功。

### 15.3 用户与管理员 DTO

```text
UserProfile       { uid, username, avatar_url, groups:[{gid,name}], created_at }
UserAdminView     { uid, username, avatar_url, assigned, disabled, auth_version,
                    groups:[{gid,name}], created_at, updated_at, version }
GroupView         { gid, name, remark, member_count, device_count, app_count,
                    created_at, updated_at, version }
DeviceSummary     { device_id, name, online, capabilities, last_seen_at }
ApplicationCard   { app_id, name, access_mode, cover_url, running_instance?, version }
InstanceView      { instance_id, app_id, app_name, state, created_at, started_at?,
                    stopped_at?, error_code?, reconnectable }
TicketResponse    { ticket, renewal_token, launch_url, expires_at, permissions }
```

任何用户侧 DTO 均不得包含 `password_hash`、appkey、app_secret、node_id、device_id（应用场景）、端口、进程路径、静态连接密码或 `desktop_link`。

### 15.4 账号管理请求

```text
POST   /api/v1/admin/users                 { username, initial_password?, group_ids[], device_ids[] }
POST   /api/v1/admin/users/batch.csv       { size, username_prefix, group_ids[] }
PATCH  /api/v1/admin/users/{uid}           { version, username?, disabled?, avatar_url?, group_ids?, device_ids? }
DELETE /api/v1/admin/users/{uid}           { version }
POST   /api/v1/admin/users/{uid}/password/reset
                                               { version, generated | supplied_password }
POST   /api/v1/admin/users/{uid}/sessions/revoke-all {}
GET    /api/v1/admin/users/{uid}/sessions
GET    /api/v1/admin/guest-sessions
POST   /api/v1/admin/guest-sessions/{sid}/block
                                               { block_guest_id, block_ip_hash, reason? }

POST   /api/v1/admin/groups                { name, remark? }
PATCH  /api/v1/admin/groups/{gid}           { version, name?, remark? }
DELETE /api/v1/admin/groups/{gid}           { version }
PUT    /api/v1/admin/groups/{gid}/members   { version, user_ids[] }
PUT    /api/v1/admin/groups/{gid}/devices   { version, device_ids[] }
PATCH  /api/v1/admin/apps/{app_id}/access   { version, access_mode, group_ids[] }
```

- PUT 授权接口表达“期望的完整集合”，先校验全部目标和实体 version，再替换关系。当前测试/单机部署采用可重试的幂等全量替换；Mongo 多文档事务和 operation_id 恢复日志属于后续多副本增强。
- 管理员重置密码使 `auth_version + 1` 并撤销全部用户会话。随机初始密码只在该响应显示一次。
- 创建用户时组和个人设备 ID 会先完整校验，设置完成后才返回一次性初始密码，避免前端第二次请求失败后丢失密码。删除用户是软删除并撤销会话；删除记录保留用户名占位，当前版本不提供恢复或同名复用。

### 15.5 资源和实例请求

```text
GET  /api/v1/user/devices
GET  /api/v1/user/devices/page?page=&page_size=&keyword=
POST /api/v1/user/devices/{device_id}/ticket
     { client_nonce, requested_permissions[] }

GET  /api/v1/user/apps
GET  /api/v1/user/apps/page?page=&page_size=&keyword=
POST /api/v1/user/apps/{app_id}/start
     { client_nonce }
GET  /api/v1/user/instances
GET  /api/v1/user/instances/page?page=&page_size=&keyword=&state=
POST /api/v1/user/instances/{instance_id}/ticket
     { client_nonce, requested_permissions[] }
POST /api/v1/user/instances/{instance_id}/stop
     { reason? }

POST /api/v1/connection-tickets/renew
     { renewal_token, client_nonce }
```

- `requested_permissions` 只能缩小服务端允许集合，不能扩大；未知权限返回 400。
- start 返回 200（复用已有实例）或 202（新实例 starting），两者都返回 `InstanceView`；启动失败通过实例状态查询得到稳定 error_code。
- ticket 请求只有在设备在线或实例处于 running 且 owner/ACL 均有效时成功。
- 管理接口、服务接口和用户接口使用独立 Router 与认证中间件，不允许一个 handler 根据可选凭据猜测身份。

### 15.6 业务错误码

| HTTP | code | 含义 |
|---|---|---|
| 400 | `INVALID_ARGUMENT` | DTO、字段或权限值非法 |
| 401 | `AUTH_REQUIRED` / `AUTH_INVALID_CREDENTIALS` | 未登录或登录失败 |
| 403 | `SUBJECT_FORBIDDEN` / `CSRF_REJECTED` | 主体类型或浏览器来源不允许 |
| 404 | `RESOURCE_NOT_FOUND` | 不存在或无权私有资源，统一防枚举 |
| 409 | `VERSION_CONFLICT` / `INSTANCE_CONFLICT` | 乐观锁或实例状态冲突 |
| 410 | `TICKET_EXPIRED_OR_USED` | ticket 过期、撤销或已消费 |
| 429 | `RATE_LIMITED` / `QUOTA_EXCEEDED` | 限流或配额耗尽 |
| 503 | `DEVICE_OFFLINE` / `SCHEDULER_UNAVAILABLE` | 目标离线或调度器不可用 |

## 16. MongoDB 集合与索引契约

| 集合 | 索引 |
|---|---|
| `c_user` | unique(`username_normalized`)，unique(`uid`)，(`deleted`,`created_at`) |
| `c_user_group` | unique partial(`name_normalized`, `deleted=false`)，unique(`gid`) |
| `c_user_group_member` | unique(`uid`,`gid`)，(`gid`,`uid`) |
| `c_group_device_grant` | unique(`gid`,`device_id`)，(`device_id`,`gid`) |
| `c_group_app_grant` | unique(`gid`,`app_id`)，(`app_id`,`gid`) |
| `c_user_device` | unique(`uid`,`device_id`)，(`device_id`,`uid`) |
| `c_user_session` | unique(`token_hash`)，unique(`sid`)，TTL(`cleanup_at`, expireAfterSeconds=0)，(`subject_type`,`subject_id`,`revoked_at`) |
| `c_guest_block` | unique(`kind`,`value`)；kind 为 `guest_id` 或脱敏 `ip_hash` |
| `c_connection_ticket` | unique(`ticket_hash`)，TTL(`expires_at`, 0)，(`session_id`,`consumed_at`) |
| `c_app` | unique(`app_id`)，(`access_mode`,`name`) |
| `c_app_instance` | unique(`instance_id`)，unique(`owner_session_id`,`app_id`,`client_nonce`) partial active，(`owner_type`,`owner_id`,`state`) |

- session 的 `cleanup_at = min(expires_at, absolute_expires_at)`；每次滑动续期同时更新 cleanup_at，但不得越过 absolute_expires_at。
- 所有关系写入前验证两端资源存在且未删除；当前使用实体 version 与唯一索引防冲突。跨集合事务化审计属于后续多副本增强。
- 服务启动必须创建/校验全部关键唯一索引和 TTL 索引；任一失败时 CMS 不进入监听阶段。
- 重置工具只允许数据库名精确等于 `db_gr_cms_server` 且配置显式 `environment = "test"` 时运行；执行前打印集合和文档数量，要求命令行 `--confirm-reset-test-identity-data`，不提供 HTTP 重置接口。

## 17. Service WebSocket 与票据兑换契约

沿用现有 appkey/app_secret HMAC WebSocket 握手作为部署鉴权，但握手成功后必须将连接绑定到 query 中的 device_id；同一 device_id 的新连接替换旧连接并递增 connection_epoch。

```text
Service -> CMS  RedeemConnectionTicketRequest {
  request_id, device_id, ticket, client_nonce, instance_id?
}

CMS -> Service  RedeemConnectionTicketResponse {
  request_id, ok, code,
  grant?: { kind, device_id, app_id?, instance_id?, subject_type,
            subject_id, permissions[], expires_at }
}
```

- protocol protobuf 分配固定消息号；request_id 在单条 WS 上唯一，CMS 回包必须原样携带。
- CMS 以该 WS 已绑定的 device_id 覆盖请求字段并原子消费 ticket；设备不一致返回拒绝且不消费，防止错误节点使合法 ticket 失效。
- ticket 原文不得出现在 tracing、protobuf Debug 输出或错误响应中；只记录 ticket_hash 前 8 位和 request_id。
- Service 兑换超时后连接失败，客户端重新向 CMS 申请新 ticket；成功 ticket 由 Mongo 原子条件保证只能被消费一次。request_id 回包保持一致，跨重连响应缓存属于后续增强。
- CMS/Service 断线时停止新兑换；已建立 RTC 连接可继续，除非收到 revoke/stop。重连后先完成实例心跳对账，再开放 ticket 兑换。
- appkey/app_secret 至少每 90 天轮换，允许当前和上一把密钥重叠 10 分钟；`force_authorize=false` 只能在 loopback 自动化测试启用，非 loopback 启动必须拒绝。

## 18. 应用实例状态机

```text
starting -> running -> stopping -> stopped
   |          |           |
   +-------> failed <-----+
```

| 状态 | 可执行操作 | 超时/退出 |
|---|---|---|
| starting | 查询、admin/owner 停止 | 25 秒无启动回执 -> failed(`START_TIMEOUT`) 并发送补偿 stop |
| running | 申请 ticket、owner/admin 停止 | Service 心跳连续缺失 15 秒先标记 suspect；重连对账仍缺失 -> stopped(`PROCESS_LOST`) |
| stopping | 查询 | 10 秒无停止回执 -> failed(`STOP_TIMEOUT`)，后台继续回收 |
| stopped/failed | 只读、重新启动新实例 | 7 天后归档/清理，审计事件按审计期限保留 |

- 每次状态变更使用 `instance_id + version + current_state` 条件更新，防止迟到回执覆盖新状态。
- instance 保存 owner、client_nonce、created/started/stopped/last_heartbeat 时间以及稳定 error_code；不得只依赖内存状态。
- owner session 退出不立即停止 running 实例；进入应用空闲计时。媒体/控制客户端归零 5 秒后通知应用停止渲染，实例无人使用达到 `idle_instance_timeout_secs` 后停止进程。
- CMS 重启从 Mongo 恢复 starting/running/stopping，等待 Service 心跳对账；对账窗口内不重复启动。
- 当前版本不建立服务端启动队列：没有空闲节点或 public 全局并发达到上限时立即失败，避免浏览器退出后遗留无人认领的 queued 实例。
- 同 owner_session + app_id + client_nonce 的活跃实例唯一；重复 start 返回原实例。guest 登录为 user 后不转移旧 guest 实例归属。

## 19. 注册与密码生命周期

- 用户名：trim 后 3–64 个 Unicode 字符；大小写归一使用 Unicode lowercase，禁止控制字符、斜杠和前后空白。
- 密码：8–128 个字符；拒绝全空白；服务端 Argon2id 参数为 memory 64 MiB、iterations 3、parallelism 1、随机盐 16 bytes，hash 使用 PHC 字符串保存。
- 自助注册必须通过 guest session、CSRF 和限流；注册成功后创建无用户组、无个人设备授权的普通用户。
- 管理员可生成随机初始密码或提供满足规则的密码。随机密码使用至少 96 bit CSPRNG，不包含易混淆字符。
- 管理员重置后设置 `must_change_password=true`；该用户只能访问 `/me`、改密和 logout，首次改密成功才解除。
- 用户改密必须提交当前明文密码，经 Argon2 验证后写入新 hash，递增 auth_version 并撤销除当前请求外的会话；当前会话随响应重新签发。
- 当前对账号和 IP 分别使用固定窗口限流（默认每账号 15 分钟 20 次、每 IP 每分钟 10 次）；指数退避与管理员手动清除属于后续风控增强。
- 不提供“找回密码邮件”流程；当前版本由管理员重置。任何初始密码或 CSV 明文均禁止进入日志。

## 20. 默认配置与保留策略

```toml
environment = "test"                        # test | production
privacy_hash_salt = "replace-with-random"    # production 至少 16 bytes，仅服务端保存

[user]
panel_sliding_days = 30
panel_absolute_days = 90
web_sliding_hours = 12
web_absolute_days = 30
admin_sliding_hours = 2
admin_absolute_hours = 8
guest_absolute_hours = 24
ticket_expire_seconds = 30

[user.rate_limit]
login_per_ip_per_minute = 10
login_per_account_per_15_minutes = 20
guest_session_per_ip_per_hour = 30
start_per_subject_per_minute = 6

[user.quota]
guest_concurrent_instances = 1
user_concurrent_instances = 3
guest_daily_minutes = 60
public_app_global_concurrency = 20

```

- production 禁止空 session HMAC key、空 service secret、`force_authorize=false`、HTTP 外部登录和 test reset 开关。
- session、ticket 与 CSRF 原文使用操作系统 CSPRNG 生成，Mongo 只保存 SHA-256；无需在仓库配置可逆服务端密钥。
- 配额是安全默认值，可由管理员向下或向上调整；任何值变更记录审计。值为 0 表示禁止，不表示无限。
- session、invite 和 ticket 使用 TTL 自动清理；审计与实例历史的可配置保留任务属于后续运维增强。
