# px_auth_server 接入凭据（appkey/app_secret）身份鉴权

> 状态：已完成（灰度中，`require_app_credential = false`）
> 范围：`rust_server/px_auth_server`（服务端）+ GoPhone 仓库的 gopico-pc / box-client / goagent（三端客户端）
> 目标：给无登录体系的开放接口加"接入方身份"验证，杜绝"知道设备码即可拉取授权 / 覆写遥测"的滥用。

---

## 1. 背景与威胁模型

三个开放接口原本无任何身份验证（设备码即身份）：

- `POST /api/v1/device/pull` —— 设备自注册 / 授权拉取
- `POST /api/v1/gopico/verify/online` —— 旧版在线校验（legacy）
- `POST /api/v1/gopico/report` —— 客户端遥测上报

任何知道设备码的人都能：拉走该设备的 deploy_str、覆写该设备的遥测字段。

本方案用一套**全局接入凭据**（appkey/app_secret）验证"请求来自官方客户端"。

**边界（务必知悉）**：secret 随客户端二进制分发，可被逆向提取。该方案防蹭用 / 防爬虫 / 防随手调用，不防专业攻击者。若需更强模型（每设备独立凭据 + provisioning 流程），需另行立项。

## 2. 设计

### 2.1 凭据模型

- 全局一套：`appkey`（16 字节 hex，32 字符）+ `app_secret`（32 字节 hex，64 字符）。
- 服务端存于 `px_auth.toml` 的 `[app_credential]` 段；三端编译期内嵌同一份。
- 管理页（admin）接口不受影响——已有登录 token + 管理员 RBAC，不属于接入凭据保护范围。

### 2.2 签名算法（四方共用）

```
sign = hex(HMAC-SHA256(key = unhex(app_secret), msg))
msg  = "{appkey}\n{timestamp_ms}\n{body}"
```

请求头：

| Header | 内容 |
|---|---|
| `x-app-key` | appkey（hex 32 字符） |
| `x-app-timestamp` | epoch 毫秒 |
| `x-app-sign` | 上述 HMAC-SHA256 的小写 hex |

服务端校验：appkey 相等 → 时间戳偏差 ≤ ±5 分钟（防重放）→ HMAC 常数时间比对。secret 不上线。

实现位置：

| 端 | 文件 |
|---|---|
| 服务端算法 | `px_auth_server/src/app_credential.rs` |
| 服务端过滤器 | `px_auth_server/src/filter/app_credential_filter.rs` |
| PC | `gopico/pc/gopico-pc-core/src/license/app_credential.rs` |
| box | `gopico/box-client/app/.../license/AppCredential.java` |
| agent | `gopico/android/app/.../license/AppCredential.java` |

四方共用固定测试向量（`appkey=0123..ef`、`secret=0011..eeff`、`ts=1700000000000`、`body={"a":1}` → `4c672c58...`），Rust/Java 测试各断言相同输出，防跨语言漂移。

### 2.3 灰度开关

`px_auth.toml` 顶层键：

```toml
require_app_credential = false   # false：不校验（旧客户端兼容）；true：强制校验
```

- `false`：filter 直接放行，用于"三端尚未全部带凭据"的过渡期。
- `true`：无凭据 / 签名错误 / 时间戳超窗一律 **401**（业务码 813 `InvalidAppCredential`）。
- `require_app_credential = true` 时 `[app_credential]` 段必填，否则启动校验报错。

**切换即重启生效**。开启后旧客户端拉取会 401——按既定"拉取失败用本地授权/缓存"策略继续运行，服务不中断，仅无法更新授权。

### 2.4 生成工具 `app_credential_gen`

`px_auth_server` 的附属 bin（随 musl 交叉编译一并产出）：

```bash
app_credential_gen                       # 随机生成并打印，不改文件
app_credential_gen --write               # 写入 ./px_auth.toml
app_credential_gen --write --file /opt/px_auth_server/px_auth.toml \
    --appkey <HEX32> --secret <HEX64> --require true
```

- `--appkey/--secret` 必须成对给出（把客户端内嵌凭据写入服务端配置时用）。
- `--write` 保证 `[app_credential]` 段幂等替换；`require_app_credential` 一律规范到**顶层**（TOML 顶层键）。
- 服务器上已放一份：`/tmp/app_credential_gen`。

### 2.5 失败语义（客户端）

401 与网络错误同等对待：PC 继续使用本地授权（按本地验签 + 有效期）；box/agent 保留旧缓存。不会误降级、不中断现有功能。

## 3. 发布 / 切换流程

1. 服务端上线（filter + 开关，`require_app_credential = false`）——旧客户端不受影响。
2. 三端携带内嵌凭据的新版本发布到所有设备（PC release / box release / agent release）。
3. 服务器执行切换：
   ```bash
   /tmp/app_credential_gen --write \
     --file /opt/px_auth_server/px_auth.toml \
     --appkey <与客户端一致> --secret <与客户端一致> --require true
   sudo supervisorctl restart px_auth_server
   ```
4. 验证：无签名请求 → 401；带签名请求 → 200。

## 4. 凭据存放位置（运维索引）

| 位置 | 内容 |
|---|---|
| 服务器 `/opt/px_auth_server/px_auth.toml` `[app_credential]` | 服务端生效凭据 |
| GoPhone `gopico-pc-core/src/license/app_credential.rs` `DEFAULT_APP_KEY/SECRET` | PC 内嵌（可用环境变量 `GOPICO_APP_KEY/GOPICO_APP_SECRET` 覆盖，供测试/轮换） |
| GoPhone box-client / goagent `license/AppCredential.java` `APP_KEY/APP_SECRET` | 两端内嵌 |

**轮换**：工具生成新值 → 改三端常量发版 → 全部更新后服务端写入新值并重启。当前为单 key，结构保留扩展多 key 的空间。

## 5. 测试覆盖

- 服务端：`app_credential.rs` 单测（向量/错签/超窗/坏 hex）+ 路由集成（无头 401、错签 401、正确签名 200、require=false 放行、错 appkey 401）。
- 工具：TOML upsert（追加段/替换段/顶层 require 键/清除段内 stray 键）。
- PC/box/agent：固定向量一致性 + 请求头附加逻辑。

## 6. 明确不做（范围外）

- 每设备独立凭据 / provisioning（更强身份模型，另行立项）。
- 从 deploy_str 签发内容中剔除 CMS `appkey/app_secret/username/password` 字段（影响旧 CMS 兼容，单独评估）。
- admin 网页接口鉴权调整（已有 token + RBAC）。
