# gr_auth_server 管理员权限改造方案

> 状态：待实施  
> 目标：用基于 JWT Claims 的角色鉴权（RBAC）替换当前硬编码管理员/访客账号、URL 传密码等不安全的实现。

---

## 1. 现状问题

当前 `gr_auth_server` 的管理员能力存在以下明显缺陷：

| 问题 | 影响 | 所在文件 |
|------|------|----------|
| 硬编码 `Admin/Admin@321%!` 账号 | 源码级后门，任何人都能登录 | `author_manager.rs` |
| 硬编码 `Visitor/Visitor@321%!` 账号 | 同上，且自动插入数据库 | `author_manager.rs` |
| 管理员校验从 URL Query 取密码 | Token/密码进入日志、浏览器历史、代理日志 | `filter/author_admin_filter.rs` |
| 管理员过滤层未挂载到任何路由 | 目前只是死代码 | `author_server.rs` |
| 登录校验中间件不注入用户信息 | 接口不知道谁在操作，无法做权限控制 | `filter/author_login_token_filter.rs` |
| JWT Secret 硬编码 | 可伪造 Token | `author_claims.rs` |
| 密码使用 `MD5(SHA256(password) + salt)` | 自制密码学方案，MD5 已不安全 | `author_manager.rs` |
| 同一份 router 同时暴露 HTTP/HTTPS | 敏感接口可明文访问 | `author_server.rs` |
| 退出登录全局递增 Token Version | 一人退出全员失效；重启后旧 Token 复活 | `author_claims.rs` |

> 注：业务错误码直接当 HTTP StatusCode 返回导致的 panic 问题（`author_api_error.rs`）不在本方案范围内，按现有逻辑继续返回给调用方报错即可。

---

## 2. 改造目标

1. 管理员账号可配置，不再写死在源码里。
2. 登录后 JWT 携带角色信息。
3. 中间件负责鉴权并把当前用户注入请求上下文。
4. 管理员接口通过独立中间件保护。
5. 密码使用标准哈希算法（Argon2 / bcrypt）。
6. 移除 URL Query 传密码的方式。
7. 删除空壳 `AuthorContext`。

---

## 3. 数据模型改造

### 3.1 Author 增加角色

文件：`src/author.rs`

```rust
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone, Default, PartialEq)]
#[serde(rename_all = "snake_case")]
pub enum AuthorRole {
    #[default]
    Visitor,
    Admin,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct Author {
    pub name: String,
    pub password_hash: String, // 明确是哈希，不是明文
    pub role: AuthorRole,
}
```

说明：
- 旧字段 `permission: String` 用 `AuthorRole` 枚举替换，避免字符串比较和拼写错误。
- 数据库已有文档需要迁移：
  - `"perm_all"` -> `"admin"`
  - `"perm_visitor"` -> `"visitor"`

---

## 4. JWT Claims 改造

文件：`src/author_claims.rs`

```rust
use serde::{Serialize, Deserialize};
use jsonwebtoken::{encode, decode, Header, Validation, EncodingKey, DecodingKey, TokenData, errors::Result as JwtResult};
use jsonwebtoken::errors::{Error as JwtError, ErrorKind};
use std::time::{SystemTime, UNIX_EPOCH, Duration};
use crate::author::AuthorRole;
use crate::author_api_error::AuthorApiError;

// TODO: 从 gr_auth_server_settings.toml 的 [bootstrap] 配置读取，不要硬编码
const SECRET_KEY: &[u8] = b"author_secret_key";

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct AuthorClaims {
    pub sub: String,      // 用户名
    pub exp: usize,       // 过期时间
    pub role: AuthorRole, // 角色
}

impl AuthorClaims {
    pub fn new(user_id: String, role: AuthorRole, expire_seconds: u64) -> Self {
        let expiration = SystemTime::now()
            .checked_add(Duration::from_secs(expire_seconds))
            .unwrap()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs() as usize;

        Self {
            sub: user_id,
            exp: expiration,
            role,
        }
    }

    pub fn generate_token(&self) -> Result<String, JwtError> {
        encode(&Header::default(), self, &EncodingKey::from_secret(SECRET_KEY))
    }

    pub fn verify(token: &str) -> JwtResult<TokenData<Self>> {
        decode::<Self>(
            token,
            &DecodingKey::from_secret(SECRET_KEY),
            &Validation::default(),
        )
    }
}
```

关键改动：
- Claims 增加 `role`。
- `generate_token` 返回 `Result`，不再 `unwrap()`。
- 删除全局 `TOKEN_VERSION` 机制，改由标准 JWT `exp` 过期 + 服务端黑名单/缓存实现退出登录（见第 9 节）。
- **Secret 仍需外部化**：开发阶段可保留常量，但正式发布前必须从 `gr_auth_server_settings.toml` 的 `[bootstrap]` 配置层注入。

---

## 5. 认证中间件：注入当前用户

新增文件：`src/auth_middleware.rs`

```rust
use axum::body::Body;
use axum::extract::Request;
use axum::middleware::Next;
use axum::response::Response;
use crate::author_api_error::AuthorApiError;
use crate::author_claims::AuthorClaims;
use crate::author::AuthorRole;

#[derive(Clone, Debug)]
pub struct CurrentUser {
    pub name: String,
    pub role: AuthorRole,
}

pub async fn auth_middleware(
    mut req: Request<Body>,
    next: Next,
) -> Result<Response, AuthorApiError> {
    let token = req
        .headers()
        .get("Authorization")
        .and_then(|v| v.to_str().ok())
        .ok_or(AuthorApiError::MissLoginToken)?;

    let claims = AuthorClaims::verify(token)
        .map_err(|_| AuthorApiError::InvalidLoginToken)?;

    req.extensions_mut().insert(CurrentUser {
        name: claims.claims.sub,
        role: claims.claims.role,
    });

    Ok(next.run(req).await)
}
```

说明：
- 只负责“你是谁”，不负责“你能做什么”。
- 后续 handler 可通过 `Extension<CurrentUser>` 直接拿到当前操作人。

---

## 6. 管理员权限中间件

新增文件：`src/admin_middleware.rs`

```rust
use axum::body::Body;
use axum::extract::Request;
use axum::middleware::Next;
use axum::response::Response;
use crate::auth_middleware::CurrentUser;
use crate::author::AuthorRole;
use crate::author_api_error::AuthorApiError;

pub async fn admin_middleware(
    req: Request<Body>,
    next: Next,
) -> Result<Response, AuthorApiError> {
    let user = req
        .extensions()
        .get::<CurrentUser>()
        .ok_or(AuthorApiError::InvalidLoginToken)?;

    if user.role != AuthorRole::Admin {
        return Err(AuthorApiError::MustBeAdministrator);
    }

    Ok(next.run(req).await)
}
```

挂载方式（Axum 中间件从下到上执行）：

```rust
// author_server.rs
use crate::admin_middleware::admin_middleware;
use crate::auth_middleware::auth_middleware;

.route("/api/v1/create/new/authorization", post(handle_create_new_authorization))
    .layer(middleware::from_fn(admin_middleware))
    .layer(middleware::from_fn(auth_middleware))
```

注意：
- `auth_middleware` 在最外层（最后 `layer`），`admin_middleware` 在最内层。
- 普通登录接口只需要 `auth_middleware`。

---

## 7. 初始管理员账号的安全创建

### 7.1 推荐方案：配置文件 bootstrap 层

文件：`src/author_manager.rs`

```rust
use argon2::{self, Config, ThreadMode, Variant, Version};
use rand::Rng;
use crate::gAuthorSettings;

pub struct AuthorManager;

impl AuthorManager {
    pub async fn init(&self) -> bool {
        if !self.has_admin().await {
            let settings = gAuthorSettings.lock().await;
            let admin_name = settings.bootstrap.admin_name.clone();
            let admin_pass = settings.bootstrap.admin_password.clone()
                .expect("bootstrap.admin_password must be set when no admin exists");

            if let Err(e) = self.insert_admin(&admin_name, &admin_pass).await {
                tracing::error!("insert admin failed: {}", e);
                return false;
            }
        }

        // 访客账号如果业务确实需要，也应可配置；否则删除
        if !self.has_visitor().await {
            // 建议：如果不再需要硬编码访客，则删除此逻辑
        }

        true
    }

    async fn insert_admin(&self, name: &str, password: &str) -> Result<(), String> {
        let hash = Self::hash_password(password)?;
        self.insert_author(Author {
            name: name.to_string(),
            password_hash: hash,
            role: AuthorRole::Admin,
        }).await
    }

    fn hash_password(password: &str) -> Result<String, String> {
        let salt: [u8; 16] = rand::rng().random();
        let config = Config {
            variant: Variant::Argon2id,
            version: Version::Version13,
            mem_cost: 65536,
            time_cost: 3,
            lanes: 4,
            thread_mode: ThreadMode::Parallel,
            secret: &[],
            ad: &[],
            hash_length: 32,
        };
        argon2::hash_encoded(password.as_bytes(), &salt, &config)
            .map_err(|e| e.to_string())
    }

    fn verify_password(password: &str, hash: &str) -> Result<bool, String> {
        argon2::verify_encoded(hash, password.as_bytes())
            .map_err(|e| e.to_string())
    }
}
```

配置示例（`gr_auth_server_settings.toml`）：

```toml
[bootstrap]
jwt_secret = "random-secret-with-at-least-32-characters"
admin_name = "admin"
admin_password = "<首次启动时生成的强密码>"
visitor_name = "Visitor"
visitor_password = ""
```

### 7.2 备选方案：首次启动生成一次性密码

如果必须“开箱即用”，可以在首次启动时：
1. 生成 16~20 位随机密码。
2. 写入 `logs/gr_auth_server/.bootstrap_admin` 或安全打印到控制台。
3. 首次登录后强制修改密码。

该方案需要额外实现“强制修改密码”接口，优先级低于配置文件 bootstrap 方案。

---

## 8. Handler 改造示例

### 8.1 登录接口

文件：`src/author_handler.rs`

```rust
use axum::extract::Extension;
use crate::auth_middleware::CurrentUser;

pub async fn handle_verify_author(
    body: Body,
) -> Result<Json<RespMessage<AuthorLoginResp>>, AuthorApiError> {
    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(&body)
        .map_err(|_| AuthorApiError::InvalidParams)?; // 不再 unwrap

    let author_name = get_body_str(&r, KEY_AUTHOR_NAME)?;
    let author_token = get_body_str(&r, KEY_AUTHOR_TOKEN)?;

    let author = gAuthorManager
        .verify_author(&author_name, &author_token)
        .await
        .ok_or(AuthorApiError::InvalidPassword)?;

    let claims = AuthorClaims::new(author.name, author.role, 3600);
    let login_token = claims.generate_token()
        .map_err(|_| AuthorApiError::DatabaseError)?;

    Ok(Json(ok_resp(AuthorLoginResp { token: login_token })))
}
```

说明：
- 登录成功后返回的 Token 包含角色。
- **不要 `println!` 打印 Token**。

### 8.2 管理员接口

```rust
pub async fn handle_create_new_authorization(
    Extension(user): Extension<CurrentUser>,
    body: Body,
) -> Result<Json<RespMessage<Authorization>>, AuthorApiError> {
    tracing::info!("operator: {}, role: {:?}", user.name, user.role);

    let body = get_body(body).await?;
    let r: Value = serde_json::from_str(&body)
        .map_err(|_| AuthorApiError::InvalidParams)?;

    let name = get_body_str(&r, KEY_CREATE_AUTHORIZATION_USER_NAME)?;
    // ... 其余参数

    let auth = gAuthorizationManager
        .gen_new_authorization(name, machine_code, days, max_streams, customer_role)
        .await
        .map_err(|e| match e {
            AuthorizationError::AlreadyExist => AuthorApiError::AlreadyExists,
            AuthorizationError::DatabaseError => AuthorApiError::DatabaseError,
            _ => AuthorApiError::DatabaseError,
        })?;

    Ok(Json(ok_resp(auth)))
}
```

说明：
- 管理员权限已在中间件层校验，handler 里可直接使用 `CurrentUser` 做审计日志。
- 不需要再从 body/query 里读 `author_name`/`author_token`。

---

## 9. 退出登录机制

当前“全局 Token Version”方案有严重副作用，建议改为以下两种之一：

### 方案 A：服务端 Token 黑名单（推荐）

- 登录成功后把 Token 存入 Redis/MongoDB，记录 `exp`。
- 退出登录时把 Token 加入黑名单。
- 中间件校验 JWT 后，再查黑名单。
- Token 自然过期后从黑名单清理。

### 方案 B：短有效期 + 刷新 Token

- Access Token 有效期设为 15 分钟。
- Refresh Token 单独管理，可撤销。
- 退出时撤销 Refresh Token。

对于 `gr_auth_server` 这种后台管理场景，**方案 A 更简单直接**。

---

## 10. 路由与中间件挂载表

| 路由 | 是否需要登录 | 是否需要管理员 | 备注 |
|------|-------------|---------------|------|
| `/api/v1/ping` | 否 | 否 | 健康检查 |
| `/api/v1/verify/author` | 否 | 否 | 登录接口 |
| `/api/v1/log_out` | 是 | 否 | 需要当前用户身份 |
| `/api/v1/query/authors` | 是 | 是 | 管理员接口 |
| `/api/v1/create/new/authorization` | 是 | 是 | 管理员接口 |
| `/api/v1/create/new/deploy/authorization` | 是 | 是 | 管理员接口 |
| `/api/v1/update/authorization` | 是 | 是 | 管理员接口 |
| `/api/v1/query/authorization/*` | 是 | 是/否 | 按业务需要分配 |
| `/api/v1/verify/appkey/secret` | 视业务而定 | 否 | 可能是客户端校验接口 |

---

## 11. 待删除/废弃项

实施时应清理以下内容：

- [ ] `filter/author_admin_filter.rs`（URL Query 传密码方案）
- [ ] `filter/author_login_token_filter.rs`（替换为新的 `auth_middleware.rs`）
- [ ] `author_manager.rs` 中的硬编码 `AUTHOR_ADMIN_PASSWORD`、`AUTHOR_VISITOR_PASSWORD`
- [ ] `author_manager.rs` 中的 `gen_token` / `gen_password_by_token` / `gen_password` 自制哈希
- [ ] `author_claims.rs` 中的全局 `TOKEN_VERSION`
- [ ] `author_context.rs`（空壳结构体）
- [ ] 所有 `println!` 敏感信息/调试输出

---

## 12. 实施顺序建议

1. **数据模型**：改 `Author` + `AuthorRole`，写 MongoDB 迁移脚本。
2. **密码哈希**：引入 `argon2`，替换 `verify_author`。
3. **JWT**：给 Claims 加 `role`，外部化 Secret。
4. **中间件**：新增 `auth_middleware.rs` + `admin_middleware.rs`。
5. **路由**：按第 10 节表重新挂载中间件。
6. **初始管理员**：改为 `gr_auth_server_settings.toml` 的 `[bootstrap]` 配置层创建。
7. **退出登录**：改为黑名单机制。
8. **清理**：删除废弃文件、移除 `println!`、补单元测试。

---

## 13. 依赖变更

`Cargo.toml` 需要新增：

```toml
argon2 = "0.5"
```

> `rand` 已存在，可直接用于生成 salt。

---

## 14. 风险与回滚

| 风险 | 缓解措施 |
|------|----------|
| 数据库已有 `Author` 文档格式不兼容 | 上线前执行迁移脚本，把 `permission` 字段转为 `role` |
| 旧密码哈希无法登录 | 首次登录时检测哈希格式，触发强制重设密码；或批量重置管理员密码 |
| JWT Secret 外部化后配置遗漏 | 启动时校验 Secret 长度，过短直接报错退出 |
| 中间件顺序挂错导致未授权访问 | 增加集成测试覆盖管理员接口 |

---

## 15. 验收标准

- [ ] 源码中不再出现硬编码管理员/访客账号密码。
- [ ] 管理员接口必须通过 `admin_middleware` 保护。
- [ ] 登录 Token 携带角色信息。
- [ ] 密码使用 Argon2 哈希存储。
- [ ] 没有从 URL Query 读取密码/Token 的代码。
- [ ] 退出登录只影响当前 Token，不影响其他用户。
- [ ] 所有 panic 风险点（`unwrap`）被替换为错误返回。
