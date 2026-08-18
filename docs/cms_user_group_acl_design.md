# CMS 客户端用户 / 用户组 / 资源访问控制设计

> 状态：待评审 v3(2026-08-18，双模式修正)
> 前置调查：本文 §1(全部基于代码现状)

## 0. 名词约定

- **客户端用户**：在 px_panel 上登录的终端用户（`c_user` 表）,CMS 产生（管理员批量生成 / 设备端自助注册）。
- **管理员**:CMS web 控制台使用者，维持 license 账号体系，不在本期改造范围。
- **资源 = 设备 + 应用（主）**:
  - 设备：可远程连接的 render 主机。
  - 应用：CMS `/apps` 登记的 `c_app` 应用模板（hook 游戏即其唯一用途）,经 `c_app_node`（应用×设备×端口）调度启动。
  - panel 本地"游戏"Tab(SQLite 游戏库）与 CMS 无关联，不是本期资源。

## 1. 现状与问题

### 1.1 已有地基

| 资产 | 位置 | 状态 |
|---|---|---|
| `c_user` + 登录/注册/改密接口 | `rust_server/px_cms_server/src/user/` | panel 有完整 UI 闭环在用 |
| `c_user_device` 用户↔设备绑定 | `user_device/` | 登录后 `RequestBindDevices` 拉取灌本地列表 |
| 批量生成用户（CSV) | `cms_user_handler.rs:430` | 接口在，CMS web 无入口 |
| 应用三层体系 | `app_schedule/manager.rs` | 调度链完整（选节点/预占/启停） |
| `/servers/config` 公开配置接口 | appkey filter 白名单内 | **登录前可读**，正好用来下发"是否强制登录" |
| Argon2 + bootstrap 参考 | `px_auth_server/author*.rs` | 可抄 |

### 1.2 问题

1. 应用三层 schema 无 owner/uid;16 个 `/app/*` 路由只认 appkey;launch 链接明文带 appkey，是绕权洞。
2. `c_user_device` 只用于"登录后自动填列表",`query/user/devices` 返回含明文密码的 `desktop_link`,无"未授权"对立面。
3. 无组概念；自助注册无审核；logout 不清 `logged_in_user_id`。
4. **但同时：匿名（不登录）使用是现状默认形态，不能为做 ACL 把匿名路径掐死**——必须双模式。

## 2. 核心设计：双模式

| | 宽松模式（默认，=现状） | 强制登录模式 |
|---|---|---|
| 开关 | `px_cms.toml` `force_user_login=false`（缺省） | `force_user_login=true` |
| 匿名使用 | 一切如旧：本机手动添加设备、本地游戏库、直连密码直连 | panel 远控/应用功能锁定，先登录 |
| 登录后 | 多一份云端授权的设备/应用列表 | 一切云端资源只来自组授权 |
| 凭据分发 | `query/user/devices` 需登录（登录才拉） | 同左，且 panel 未登录不展示任何远程入口 |
| 应用启动 | CMS web 管理面（appkey）如旧；launch 链接如旧 | 启动接口必须带 user_token;launch 链接需登录 |

- 模式下发：panel 启动时调 `/servers/config`（公开接口）拿 `force_user_login`；变化在 panel 下次启动/重连 CMS 时生效（不做实时踢）。
- 两种模式都管不住的残余：**知道设备明文密码的人可以绕开 panel 用 web client 直连 render**——静态设备密码的固有属性，根治靠连接票据（§8 遗留）。

## 3. 数据模型

### 3.1 `c_user`（扩展）

```rust
pub struct CmsUser {
    // 既有: uid/username/password/assigned/timestamps/deleted/avatar_path/administrator(哑)
    pub group_ids: Vec<String>,    // 新增
    pub active: bool,              // 新增: 禁用即无法登录
    // password: md5 → Argon2 透明迁移
}
```

### 3.2 `c_user_group`（新建）

```rust
pub struct CmsUserGroup {
    pub gid: String,
    pub name: String,              // 唯一
    pub remark: String,
    pub device_ids: Vec<String>,   // 授权设备
    pub app_ids: Vec<String>,      // 授权应用(c_app.app_id)
    pub created_timestamp: i64, pub update_timestamp: i64, pub deleted: bool,
}
```

### 3.3 `c_user_session`（新建）

opaque token(UUID)+ uid + 过期时间（12h 滑动）。login 签发，启停应用/拉取资源时校验。不引 JWT（可撤销、踢人立即生效）。

### 3.4 授权判定（登录态）

```
可见/可连设备 = ∪(组 device_ids) ∪ (个人 c_user_device),剔除 active=false
可见/可启动应用 = ∪(组 app_ids)
```
**设备与应用是两个独立维度，不交叉校验**:game-hook 给用户的是游戏画面流而非机器桌面，启动应用不泄露 `desktop_link` 凭据，因此启动应用不要求节点设备也在用户设备集合内（否则会逼迫管理员为"玩游戏"发放桌面远控权，语义颠倒）。节点选择留在调度器内部，用户不感知。
实时读库不缓存，组里移除资源用户下次拉取立即收缩。

## 4. 核心流程改造

### 4.1 登录（panel → CMS，既有接口增强）

`POST /user/control/login`：校验 `active`；返回 `user_token` + `group_ids` + 组名；logout 清 `logged_in_user_id`（修现存 bug)。

### 4.2 设备凭据分发收紧

`query/user/devices`：需 user_token；返回 组授权 ∪ 个人绑定（剔除禁用设备）。设备密码只经此通道分发。

### 4.3 应用列表下发（新增，双模式兼容）

`GET /user/control/query/user/apps`(token 可选）：返回应用 + 节点摘要（node_id/device_id/节点名/忙闲）,**不下发** `game_path` 等管理面字段。行为按模式 × 登录态区分：

| | 匿名（无 token) | 登录（有效 user_token) |
|---|---|---|
| 宽松模式 | 返回**全部**应用（launch 链接本来全民可启动，列表不做硬过滤才有意义） | 返回全部应用 + 标注"我的组授权"（前端分组展示，不硬过滤） |
| 强制模式 | **401** | 只返回组授权集合（硬过滤） |

设备列表接口（`query/user/devices`）两个模式都需登录（现状如此，匿名本来就无 uid 可调），不做全量下放。

### 4.4 应用启动带身份（双模式兼容）

**关键前提：appkey 不是管理员专属**——panel 设置里就存着 appkey，强制模式下若启动接口认 appkey,panel 可绕过用户鉴权直接调。因此引入第三种票：**admin_token**(CMS web 控制台 license 账号登录 `verify/auth/account` 时签发，与 user_token 同存 `c_user_session`、以角色字段区分）。

`app/instance/start`、`app/node/start/{node_id}` 判定矩阵：

| | 匿名 / 仅 appkey | user_token | admin_token |
|---|---|---|---|
| 宽松模式 | 放行（= 现状，launch 链接兼容靠这个） | 放行 + 记录启动者（审计） | 放行 |
| 强制模式 | **403**（认了 appkey 就是洞） | ACL 校验（app ∈ 组授权，不叠加设备维度，见 §3.4) | 放行 |

- CMS web AppsView 两种模式下统一改用 admin_token 调启动。
- launch 链接：宽松模式不变（appkey GET 可启动）；强制模式要求引导页先登录换 user_token。

### 4.5 panel 侧

1. 启动时读 `/servers/config` → `force_user_login=true` 则先弹登录框，远控 Tab/应用入口锁定（本地游戏库不受影响）。
2. 应用列表拉取按模式兼容（§4.3 矩阵）：宽松模式匿名也拉全量、可直接启动；登录后拉取并标注组授权；强制模式未登录不调（UI 已锁定）、登录后只拉授权集合。应用显示在"游戏"Tab 的"云端应用"分区，可一键启动（`app/node/start` 带 user_token/匿名时带 appkey → 复用 game-hook web client 落地）。
3. 登录态显示用户名 + 组名。

## 5. CMS web 改造（管理面）

1. **用户管理页**（现状只读）：新增/禁用/删除/重置密码/指派组 + 批量生成（CSV）入口。
2. **新增用户组页**：组 CRUD + 设备勾选 + 应用勾选 + 成员查看。
3. 应用调度页不动。

## 6. 强制点汇总

| 路径 | 改造 | 模式敏感 |
|---|---|---|
| `query/user/devices` | user_token + 组∪个人过滤 | 两者皆需登录（现状如此） |
| `query/user/apps`（新） | 模式矩阵：宽松=匿名全量/登录标注；强制=匿名 401/登录硬过滤 | 是 |
| `app/(node/)instance/start` | 模式矩阵（§4.4)：宽松放行；强制要 user_token(ACL）或 admin_token，仅 appkey 403 | 是 |
| `app/launch/*` | 匿名开关 / 强制模式要登录 | 是 |
| `user/control/login` | active 校验、签发 token、返回组 | 否 |
| `user/control/register` | `allow_self_register=false` 时 403 | 否 |
| panel UI | 强制模式未登录锁定远程功能 | 是 |

## 7. 配置项（全部写在 `px_cms.toml`)

三个开关均为 CMS 配置文件项（`CmsSettings` 加字段，serde 缺省），不硬编码、不进 leveldb:

```toml
# px_cms.toml 新增(缺省值 = 完全兼容现状,升级无感)
force_user_login = false        # true = 强制登录模式: panel 未登录锁定远程/应用功能,
                                #        资源拉取与应用启动必须带 user_token/admin_token
allow_self_register = true      # false = 关闭 panel 自助注册, 只能管理员在 CMS web 创建用户
allow_anonymous_launch = true   # false = /app/launch/* 链接要求先登录换 user_token
```

下发路径:`force_user_login` 经 `/servers/config`（公开接口，登录前可读）下发给 panel;`srv_ssl_enable` 同款广播链路的 access info 也带一份冗余（panel 断网重启时以本地缓存为准，连上 CMS 后以 `/servers/config` 为准）。

## 8. 安全债一并处理

| 债 | 处理 |
|---|---|
| 密码 md5 存储 | 迁 Argon2，登录透明重哈希 |
| `desktop_link` 明文密码 | 格式不动，靠 §4.2 收紧分发面 |
| launch 链接明文 appkey | 双模式 + `allow_anonymous_launch` 开关 |
| 自助注册无审核 | `allow_self_register`（默认 true 兼容） |

## 9. 遗留（后续可选）

- **信令层用户票据**:render `/verify/security/password`、`/alloc/local/rtc` 只认静态设备密码，拿到密码即可绕开 panel 直连（两种模式都管不住）。根治 = CMS 签发一次性连接票据，render 验票，另立项。
- 审计：`c_event` 记录"用户 X 登录/启动应用 Y/连接设备 Z",P4 顺手加登录与启动事件。
- 强制模式的实时生效（当前为启动时读取，不做在线踢人）。

## 10. 实施计划（预估）

| 阶段 | 内容 | 预估 |
|---|---|---|
| P1 后端 | c_user/c_user_group/c_user_session、`force_user_login` 下发、登录增强、query/user/apps、§6 各强制点 | 2.5d |
| P2 CMS web | 用户管理补全 + 用户组页 | 1.5d |
| P3 panel | 强制登录门 + 云端应用分区与启动 + 登录态组信息 | 1d |
| P4 测试 | 见 §11 | 1d |

## 11. 测试方案

### 11.1 单元测试（rust,`cargo test -p px_cms_server`)

| 用例 | 断言 |
|---|---|
| 授权并集 | 用户属 2 组 + 1 个个人绑定 → 设备集合 = 三者并集去重 |
| 组移除收缩 | 组里删掉设备 D 后，同用户再查，D 消失 |
| 禁用账号 | `active=false` 登录 → 401 |
| 禁用设备剔除 | `active=false` 设备即使在组里也不出现在下发列表 |
| session 生命周期 | login 签发 → whoami 通过 → 删 session → 再调 401；过期 token 401 |
| Argon2 迁移 | 存量 md5 记录登录成功，落库后变为 Argon2 格式，再次登录仍成功 |
| 列表矩阵 | 宽松×匿名=全量；宽松×登录=全量+组标注；强制×匿名=401；强制×登录=仅授权 |
| 启动矩阵 | 宽松×appkey=放行；强制×appkey=403；强制×user_token 无权 app=403；强制×user_token 有权=放行；强制×admin_token=放行 |
| 注册开关 | `allow_self_register=false` → /register 403 |
| 组 CRUD | 重名组拒绝；删组后用户组授权失效 |

### 11.2 集成测试（curl 直打 CMS 本机）

登录→拿 token→带/不带 token 调 `query/user/apps`、`query/user/devices`、`app/node/start`，按 §4.3/§4.4 矩阵逐格验证 HTTP 状态码与返回体。

### 11.3 双机 E2E(10.0.0.16 CMS + 10.0.0.90 设备，CDP 无头 Chrome)

仿 `scripts/cdp_records_e2e.mjs` 新增 `scripts/cdp_user_acl_e2e.mjs`:

1. **准备**:CMS web 上批量生成 2 个用户（U1/U2)、建 2 个组（G1 绑设备 90+应用 A;G2 绑应用 B),U1 入 G1、U2 入 G2。
2. **宽松模式**（默认配置）:panel 不登录 → 本地功能正常、应用列表全量、可启动应用；U1 登录 → 列表含 G1 资源并带组标注。
3. **强制模式**(toml 改 `force_user_login=true` 重启 CMS,90 侧 panel 重启）:
   - panel 启动 → 弹登录框、远程入口锁定（截图断言）
   - U1 登录 → 只见 G1 设备与应用；U2 登录 → 只见应用 B、不见设备 90
   - U1 启动应用 B（无权）→ 403；启动应用 A → 成功，game-hook web client 可开
   - 管理员把设备 90 移出 G1 → U1 重新拉取 → 设备消失（收缩实时性）
   - U1 禁用（active=false)→ 再登录 401
4. **回归**:force_user_login 回 false 后，录像页 E2E(`cdp_records_e2e.mjs`）与 web 冒烟（`cdp_cms_smoke.mjs`）重跑全绿。

### 11.4 配置项验证

三个开关各自翻转一次：`allow_self_register=false` 时 panel 注册 403;`allow_anonymous_launch=false` 时裸 launch 链接跳转登录页。

## 12. 设计决策（2026-08-18 已定）

1. **用户-组：多对多**(`group_ids: Vec<String>`)。一人属多组是常态，一对一只会逼出重复组。
2. **个人绑定（`c_user_device`)：保留**为组外补充。它是现有行为（panel"添加远端设备"在写），废弃会破坏现网用户列表。
3. **应用启动不叠加设备维度**:game-hook 给用户的是游戏画面流而非机器桌面，启动应用不泄露 `desktop_link` 凭据；叠加会逼迫管理员为"玩游戏"发放桌面远控权，语义颠倒。设备授权管桌面远控，应用授权管游戏启动，互不相干。
4. **开关默认值**:`force_user_login=false`、`allow_self_register=true`、`allow_anonymous_launch=true`——升级即无感，完全兼容现状。正式商用部署建议显式 `force_user_login=true` + `allow_anonymous_launch=false`。
