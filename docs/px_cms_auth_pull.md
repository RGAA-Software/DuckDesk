# GoDesk CMS 网络上报授权说明

CMS（px_cms_server）的授权模式已从"管理员手工创建授权 → 下载 deploy string → 登录页粘贴/上传 license 文件"改为与 gopico / clientbox / goagent 一致的 **设备主动上报 + 服务器授权** 模式。手工上传 license 的后端入口（`POST /api/v1/auth/control/update/authorization`）已移除。

## 工作流程

1. CMS 启动时立即向授权服务器（px_auth_server）`POST {auth_server_url}/api/v1/device/pull` 上报一次，之后每 `auth_pull_interval_secs` 秒周期上报。上报内容：product=`godesk_cms`、机器码、版本、运行状态、OS、纳管设备数。
2. 授权服务器收到未知设备时自动注册一条 **试用（trial）** 授权并下发签名 license；已知设备直接下发最新授权。
3. CMS 用内置 Ed25519 公钥验签 license（验签同时校验机器码与有效期），通过后写 KvStorage 缓存并更新内存授权。
4. 管理员在 auth server 管理后台把该设备从试用 **转为正式（licensed）**、调整流路数/天数，或 **吊销（revoke）**。CMS 下次 pull 自动生效。
5. **revoked 语义**：pull 响应 `revoked=true` 时，CMS 立即清空本地授权（内存置空 + 删除 KvStorage 缓存），服务回到未授权状态。
6. **失败语义**：任何网络/HTTP/验签失败只记日志，本地已有授权保持不变（沿用 KvStorage 缓存，服务不中断）。离线重启时从 KvStorage 缓存恢复授权。

## 机器码（xxxx-xxxx）

CMS 的机器码与 gopico-pc 同款算法（`rust_base/px_base/src/machine_code.rs` 移植自 `gopico-pc-core/src/license/machine_code.rs`）：

- 采集物理网卡 MAC（Windows 用 PowerShell `Get-NetAdapter -Physical`，CREATE_NO_WINDOW；fallback sysinfo 网卡）、物理磁盘序列号（`Win32_DiskDrive`；fallback lsblk/sysinfo 磁盘名+容量）、CPU 描述（vendor-brand-核数）。
- 规范化（去分隔符、统一大小写、排序去重）后拼成 `v3|mac=...|disk=...|cpu=...`，取 MD5 前 8 字节转 u64，对 1e8 取模格式化为 `xxxx-xxxx` 8 位数字码。
- 只绑定物理硬件：重装 OS 不影响，换网卡/换硬盘会变。网络/IP 变化不影响。

机器码显示在 CMS 桌面面板（可复制）和 web 登录页。管理员在 auth server 后台的设备列表（GoDesk CMS tab）里按机器码找到设备并转正。

## CMS 配置（px_cms.toml）

```toml
# 授权服务器地址（px_auth_server）
auth_server_url = "https://127.0.0.1:30400"

# 授权拉取周期（秒）
auth_pull_interval_secs = 3600

# 接入凭据（与 auth server 的 [app_credential] 段一致）
# 为空则 pull 请求不携带签名头（兼容 auth server require_app_credential=false 的灰度期）
[app_credential]
appkey = ""
app_secret = ""
```

- 三个配置项都有 serde 默认值（`https://127.0.0.1:30400` / `3600` / 空凭据），旧配置文件不新增这些字段也能正常启动。
- 凭据非空时，pull 请求携带 `x-app-key` / `x-app-timestamp` / `x-app-sign` HMAC-SHA256 签名头（算法在共享 crate `px_auth_mgr::app_credential`，与 auth server 端一致）。用 auth server 侧的 `app_credential_gen --write` 生成后，把同一对 appkey/app_secret 抄到 CMS 配置。
- CMS 调 auth server 使用 `danger_accept_invalid_certs(true)`（auth server 是自签名证书，与内部服务间调用现状一致）。

## 手动触发拉取

- Web 前端：`POST /api/v1/auth/control/pull/authorization`，返回安全状态（`AuthStatus`，见下）；服务器已吊销时返回 `authorized=false` 的状态，拉取失败返回 500。该路径在 appkey filter 白名单中（未授权时没有 appkey 可用，否则死锁）。
- 桌面面板：授权状态行的"刷新"按钮触发一次 pull 并刷新显示（授权状态行现在会显示模式：试用/正式）。

## 未授权前可用的白名单接口（安全视图，不含凭据）

未授权时 CMS 没有任何 appkey，以下接口在 appkey filter 白名单内，供登录页使用，**均不返回 appkey/app_secret/username/password**：

- `GET /api/v1/auth/control/get/auth/status`：授权状态安全视图 `{authorized, mode, days, max_streams, end_timestamp_ms, used_time_ms, valid, machine_code}`。

## 已使用时间的口径

已使用时间**由授权服务器计算**并通过 `device/pull` 响应的 `used_time_ms` 返回（服务器侧：`days - 剩余`，锚定在服务器时钟）。CMS 直接采用该值展示，**本地不计时、不做时钟纠正**；周期 pull 自动刷新，网络失败时沿用上一次拉到的值继续运行。只有 license 本身明确过期（`end_timestamp_ms`）才失效。
- `POST /api/v1/auth/control/pull/authorization`：手动拉取，返回同样的安全视图。

登录凭据的本地下发：`AuthStatus` 额外带 `username`/`password` 字段，但**仅当请求来自服务器本机**（loopback 或本机任一网卡 IP，见 `is_local_request`）时非空——登录页据此自动填充登录表单；从其他机器访问时这两个字段为空串，凭据不外泄。egui 桌面面板（本机）也直接显示登录账号。
- `POST /api/v1/auth/control/verify/auth/account`：登录校验（license 里的 username/password 本身就是凭据），**成功后返回 appkey**，前端保存到 localStorage，用于之后调用受 appkey filter 保护的接口（如 `get/authorization`、`update/password`）。

注意：`pull/authorization` 绝不返回完整授权（含凭据的 `SanitizedAuthorization` 只能由 appkey 保护下的 `get/authorization` 下发），否则任何人都能白拿 appkey 穿透 filter。

## 已知行为（与旧模式语义一致）

- `update/password` 只改内存中的登录密码；周期 pull 会用服务器 license 里携带的密码覆盖回来（等同旧模式"重启后恢复 license 密码"）。
- 登录体系（`update/password`、`auth/valid`、`get/authorization`、`get/used/time`）不受影响，用户名/密码仍由签名 license 携带；`verify/auth/account` 见上节（成功后返回 appkey）。

## 相关代码

- CMS pull 逻辑：`rust_server/px_cms_server/src/auth/spvr_auth_pull.rs`
- 授权服务器 device/pull：`rust_server/px_auth_server/src/authorization_handler.rs`（`handle_device_pull`，product=`godesk_cms`）
- 共享产品常量/签名算法：`rust_base/px_auth_mgr/src/authorization.rs`、`rust_base/px_auth_mgr/src/app_credential.rs`
- 机器码：`rust_base/px_base/src/machine_code.rs`
