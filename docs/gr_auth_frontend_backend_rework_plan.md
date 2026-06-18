# gr_auth 前后端安全与权限改造计划

> 范围：`rust_server/gr_auth_server` 与 `web/gr_auth`。  
> 目标：把当前“能登录、能管理授权”的原型式实现，改造成有明确认证、角色权限、稳定错误处理、可测试边界和前后端一致契约的管理系统。

## 1. 当前主要问题

### 1.1 后端问题

- `AuthorApiError` 使用 `800..812` 作为 HTTP StatusCode，并 `unwrap()`，任何业务错误都会触发 panic。
- 多个 handler 直接 `serde_json::from_str(...).unwrap()` 或 `query.get(...).unwrap()`，非法输入会 panic。
- 管理员和访客账号密码硬编码在 `author_manager.rs`。
- JWT secret 硬编码在 `author_claims.rs`。
- JWT 不携带角色，后端无法区分管理员和访客。
- `author_admin_filter` 存在但未挂载；多数管理接口只校验“有登录 token”，不校验管理员权限。
- `/api/v1/query/authors` 没有登录保护。
- 登录日志打印 `author_token`，还打印 `login_token`。
- 管理员密码存储已改为 Argon2id，字段命名已收敛为 `password_hash`。
- logout 已改为 `jti` + 进程内 blacklist，当前只失效当前 token。
- 已关闭明文 HTTP 监听，仅保留 HTTPS。

### 1.2 前端问题

- 登录成功后 `finally` 仍弹出“登录失败”。
- 登录页打印用户名、密码和 token。
- 路由没有登录守卫，直接访问 `/main/*` 会进入页面。
- 前端把输入框密码直接作为 `author_token` 发送，但后端当前校验的是 SHA256 后的 token，UI 语义和后端契约不一致。
- `http.ts` 期望 HTTP status `811` 表示登录失效，但后端不能合法返回 811。
- 前端没有角色模型，所有登录用户都被当成可管理授权的用户。
- `AdminList.vue` 中仍有硬编码 Visitor token 的旧代码。
- 授权列表分页总数使用 `tableData.length`，没有使用后端返回的 total。
- 创建授权返回 deploy 信息后，前端没有展示/下载。
- 授权详情展示 `app_secret`、`username`、`password`、deploy 信息，后续需要按角色控制。

## 2. 改造原则

- 先修稳定性，再修权限，再修密码体系。
- 后端是权限最终裁决点，前端只做体验控制。
- 所有业务错误必须返回合法 HTTP 状态码，业务 code 放 JSON body。
- 所有外部输入都必须走显式错误返回，不允许 `unwrap()` 处理请求数据。
- 敏感信息不进日志、不进 URL query、不进浏览器控制台。
- 前后端接口契约必须同步更新。

## 3. 实施阶段

### 阶段一：稳定错误处理

后端：

- 改造 `AuthorApiError::into_response()`：
  - 参数错误返回 HTTP 400。
  - 登录 token 缺失/无效返回 HTTP 401。
  - 权限不足返回 HTTP 403。
  - 数据库/服务端错误返回 HTTP 500。
  - JSON body 保留原业务 code。
- 替换 handler 中处理请求体、query 参数和数据库结果的 `unwrap()`。
- 删除登录 token、密码、app secret 的日志输出。

前端：

- `http.ts` 改为处理 HTTP 401/403，以及 body 中的业务 code。
- `LoginPanel.vue` 修复成功后仍弹失败框的问题。
- 删除敏感 `console.log`。

### 阶段二：登录态与路由守卫

- 前端新增 router guard：无 `sessionStorage.login_token` 不能访问 `/main/*`。
- 后端保留 `/api/v1/verify/author` 为公开登录接口。
- `/api/v1/log_out` 必须要求登录态。

### 阶段三：角色模型与 JWT Claims

后端：

- `Author` 增加角色枚举 `AuthorRole`。
- 新服务直接使用 `role: admin|visitor`，不兼容旧 `permission` 字段。
- JWT claims 增加 `role`。
- `AuthorClaims::generate_token()` 返回 `Result`，不再 panic。

前端：

- 登录后通过 `/api/v1/me` 获取当前用户角色。
- 菜单和按钮按角色显示。

### 阶段四：认证和管理员中间件

后端：

- 新增 `auth_middleware`：
  - 读取 `Authorization` header。
  - 验证 JWT。
  - 注入 `CurrentUser` 到 request extensions。
- 新增 `admin_middleware`：
  - 检查 `CurrentUser.role == Admin`。
- 管理接口必须挂管理员中间件：
  - `/api/v1/query/authors`
  - `/api/v1/create/new/authorization`
  - `/api/v1/create/new/deploy/authorization`
  - `/api/v1/update/authorization`
  - `/api/v1/query/authorizations`
  - `/api/v1/query/authorization/like/name`
  - 其他授权管理查询接口按业务确认。

### 阶段四补充：授权输入契约与边界校验

- 已为创建授权和更新授权增加显式输入解析层：
  - `name` 不能为空，最长 128。
  - `machine_code` 不能为空，最长 256。
  - `days` 必须是 `1..=365000` 的整数。
  - `max_streams` 必须是 `1..=10000` 的整数。
  - `role` 必须是 `1..=3`。
  - 超出 `i32` 范围的数字直接返回 `InvalidParams`。
- 创建授权和更新授权在输入校验失败时直接返回 HTTP 400，不访问数据库。
- `auth_name` query 参数已改为统一参数读取，缺失或为空返回 `InvalidParams`。
- 前端创建授权和修改授权已增加同样的边界校验，提交前先拦截明显非法输入。
- 已补充后端单元测试和 router 集成测试，覆盖缺字段、类型错误、空字符串、超长字符串、上下界、越界数字和非法角色。

### 阶段五：管理员初始化与密码哈希

- 删除硬编码 `Admin/Admin@321%!`、`Visitor/Visitor@321%!`。
- 初始管理员通过 `gr_auth_server_settings.toml` 的 `[bootstrap]` 配置层创建：
  - `bootstrap.admin_name`
  - `bootstrap.admin_password`
  - `bootstrap.jwt_secret`
- 已引入 Argon2id 存储密码 hash。
- 本服务按新服务处理，不兼容旧 hash。

### 阶段六：logout 与 token 生命周期

- 已删除全局 `TOKEN_VERSION`。
- JWT claims 已增加 `jti`。
- 已实现进程内 token blacklist：
  - logout 只失效当前 token。
  - blacklist 记录 `jti` 及过期时间。
  - 验证 token 时会清理过期 blacklist 项。

### 阶段七：HTTP/HTTPS 收口

- 已关闭 HTTP 端口，仅保留 HTTPS。
- 管理 API、登录、logout、`/me` 和静态资源都只通过 HTTPS 暴露。
- 静态资源继续由 `gr_auth_server` 托管。

## 4. 测试要求

改造后必须补充大量、面面俱到的测试，覆盖功能路径、安全路径和临界条件。测试不是可选项，是本改造的验收条件。

### 4.1 后端测试

- `AuthorApiError` 每个错误类型的 HTTP status 和 body code。
- 登录接口：
  - 正确账号成功。
  - 错误密码失败。
  - 缺字段失败。
  - 非法 JSON 失败且不 panic。
  - 空 body 失败且不 panic。
- JWT：
  - 正常 token 验证通过。
  - 过期 token 失败。
  - 篡改 token 失败。
  - role 正确写入和读取。
- 中间件：
  - 缺 token 返回 401。
  - 非法 token 返回 401。
  - visitor 访问 admin API 返回 403。
  - admin 访问 admin API 成功。
- 授权管理：
  - 创建授权成功。
  - 重复 name 返回 AlreadyExists。
  - days/max_streams/role 缺失或类型错误返回 InvalidParams。
  - name/machine_code 为空或超长返回 InvalidParams。
  - days/max_streams 的 0、负数、上界、超过上界全部覆盖。
  - role 的合法值和非法值全部覆盖。
  - 超出 i32 范围的数值返回 InvalidParams。
  - 查询分页 page/page_size 边界：0、负数、大页码、超大 page_size。
  - update 不存在 auth_id 返回 AuthorizationNotFound。
  - update 非法 body 不 panic。
- logout：
  - 只影响当前 token。
  - 多用户 token 互不影响。
- 数据模型：
  - `AuthorRole` 只能接受 `admin` / `visitor`。
  - `/api/v1/me` 返回 `role`，不返回旧 `permission`。

### 4.2 前端测试

- 已引入 Vitest + jsdom 单元测试体系，`npm run test:unit` 作为前端单元测试入口。
- 已覆盖 HTTP client：
  - 请求自动携带 `Authorization`。
  - HTTP 401 清理 token 并跳转登录页。
  - legacy 811/812 业务 code 清理 token。
  - HTTP 403 只提示无权限，不清理 token。
- 已覆盖 router guard：
  - 无 token 访问 `/main/*` 自动回登录页。
  - 有 token 可以进入 `/main/*`。
- 已覆盖授权输入校验纯函数：
  - 创建授权字段 trim 和数字标准化。
  - 缺字段、空字符串、超长字符串。
  - `days`、`max_streams` 的下界、上界、越界、小数和非数字。
  - `role` 的合法值和非法值。
  - 修改授权的非法边界。
- 登录成功跳转 `/main/auth-list`。
- 登录失败显示错误，不保存 token。
- 无 token 访问 `/main/*` 自动回登录页。
- 401 响应清理 token 并跳登录页。
- visitor 看不到管理员按钮。
- admin 能看到创建/修改按钮。
- 创建授权成功后能展示或下载 deploy 信息。
- 授权列表分页 total 正确。
- 搜索为空时恢复全量列表。
- 修改授权成功后刷新当前页。
- API 错误不会把表格数据变成字符串。

### 4.3 临界条件

- 空字符串、超长字符串、特殊字符 name/machine_code。
- `days` 为 0、负数、极大值。
- `max_streams` 为 0、负数、极大值。
- `role` 非法值。
- MongoDB 不可用。
- 配置文件缺失、字段缺失、证书缺失。
- 重复点击登录、创建、保存。
- 浏览器刷新后 token 恢复。

## 5. 验收标准

- 后端所有请求输入错误都不会 panic。
- 源码和前端控制台不再泄露密码、token、app secret。
- 管理接口必须通过 admin 权限。
- visitor 无法创建、修改、查询敏感授权信息。
- admin 能完成现有管理功能。
- `cargo check -p gr_auth_server` 通过。
- 前端 type-check/build 通过。
- 关键后端单元测试和前端交互测试覆盖上述场景。
- 后端 router 已支持直接构造并进行集成测试，覆盖基础认证、角色权限和 logout 行为。
- 授权创建/更新的输入边界已由后端单元测试、后端 router 集成测试和前端 build/type-check 覆盖。
