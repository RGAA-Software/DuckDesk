# Auth 许可证契约（DB0 / DB3）

> 2026-09-22。共享库：`rust_server/px_auth_server/license`，crate：`px_license`。
> 本文只描述全新开发的当前契约，不迁移、不兼容早期开发格式，也不提供运行时 fallback。

## 唯一授权模型

系统只保留服务端许可证 `PXLIC2`：Auth 签发，Console 验签并执行授权。许可证只回答三个问题：

- 授权给哪个 Console deployment；
- 可以使用哪些服务；
- 整个 deployment 最多允许多少条并发 stream，以及授权何时到期。

Service、Render、Panel、Windows Client、Web Client 和 Android 不解析、不保存、不执行许可证。它们只消费 Console 已经准入的业务结果。
许可证不承担产品发行身份、OEM 身份、机器身份、升级身份或客户端认证职责。

## 字节与字段

唯一接受格式：`PXLIC2.<payload base64url-no-pad>.<Ed25519 signature base64url-no-pad>`，总长最多 8192 字节。
签名输入是 UTF-8/ASCII 的 `Pixels-License-v2`、单字节 NUL 和原始 payload bytes。下载响应或许可证本身携带的公钥不能成为可信根。

payload 是紧凑 UTF-8 JSON，严格按以下字段顺序编码，不允许空白、重复、未知、缺失或重排字段：

```text
schema, license_id, deployment_id, revision, issued_at, expires_at, max_streams, services, key_id
```

- `schema=2`；`license_id` 和 `deployment_id` 是非 nil、标准小写带连字符 UUID。
- `revision` 是正 i64；`issued_at` 和 `expires_at` 是整数 Unix 秒，并满足
  `0 <= issued_at < expires_at <= 253402300799`。
- `max_streams` 是正 u32，表示整个 Console deployment 同时存在的未关闭资源流总上限。
- `services` 是非空、有序、无重复数组，当前闭集为 `cloud_applications`、`desktop`、`rdp`。
- `key_id` 是签发公钥原始 32 字节 SHA-256 的小写 hex64。

载荷不包含 product、distribution、release namespace、OEM ID、机器指纹、`not_before`、trial/licensed 模式、管理员凭据、数据库凭据或客户端升级信息。
签名有效后仍要求原始 payload bytes 与标准编码完全相同；验证器不能先修复或重排 JSON 再接受。

## 签发、续期与撤销

Auth 使用显式提供的 Ed25519 PKCS#8 v2 私钥签发，不因文件缺失生成替代密钥，也不把私钥写入数据库、安装包或 API 响应。
签发和续期通过 `request_id` 幂等；续期保持原 `license_id`、customer 和 deployment，提升 revision 并生成新的签名 wire。

撤销是 Auth 管理面的事实：它阻止该记录继续续期或被当作活动授权发放。已经交付的签名副本不与 Auth 在线通信，因此仍可使用到自身
`expires_at`。需要缩短撤销收敛时间时，应签发较短有效期并由客户运维替换许可证；系统不得声称离线副本可被瞬时撤销。

不再存在 `/api/auth/licenses/verify`、许可证通知 outbox、在线 currentness 轮询、40 秒退出、revision/可信时间水位或机器绑定。

## Console 验证与执行

Console 启动时从受控文件读取 Auth 许可证公钥信任根和唯一 `PXLIC2` 文件，并验证：

1. wire、payload 和签名均为规范编码；
2. `key_id` 精确命中信任根中的公钥；
3. `deployment_id` 与当前 Console deployment 完全一致；
4. 当前时间早于 `expires_at`；
5. stream 上限和服务集合满足上述闭集约束。

Console 在业务准入时继续检查到期时间。`max_streams` 通过 PostgreSQL 事务和 advisory lock 竞争最后名额；
`cloud_applications`、`desktop`、`rdp` 分别门控云应用、桌面和 RDP 会话。设备登记不占 stream。

许可证信任根仅用于 Auth 签名密钥轮换，可以在有界集合中暂时保留新旧公钥；只有活动私钥用于签发。它不是部署证书或客户端 trust store，
不参与 TLS、平台发现、客户端登录和升级路由。连接安全使用正常 HTTPS/TLS。

## 固定向量与短验收

`tests/vector.json` 使用 RFC 8032 公开测试 seed，由独立 Node crypto/OpenSSL 生成。公开 seed 只用于测试，不是部署凭据。
合同测试覆盖固定向量、deployment/时间绑定、规范编码、篡改、错误可信根、额度/服务约束以及信任根轮换。开发期使用快速 Release：

```powershell
$env:CARGO_PROFILE_RELEASE_OPT_LEVEL='1'
$env:CARGO_PROFILE_RELEASE_INCREMENTAL='true'
cargo test --release --locked --manifest-path rust_server/Cargo.toml -p px_license --test contract
```

Auth 存储与 API 测试还必须覆盖签发、同 request 精确重试、续期、撤销、权限和数据库故障；Console 测试必须覆盖正确 deployment、错误
deployment、过期许可证、未知 key、服务门控和并发 stream 上限。最终发布再使用完整优化 Release，开发阶段不生成 Debug/RelWithDebInfo 产物。
