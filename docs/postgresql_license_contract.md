# Auth 新许可证字节契约（DB0 / DB3）

> 2026-09-17。共享库 `rust_server/px_auth_server/license`，crate `px_license`。
> Auth 产品入口已接入新 PG 存储及协议；Console/其他消费者尚未切换，不代表 DB3 整体已验收。

## 字节与字段

唯一接受格式：`PXLIC1.<payload base64url-no-pad>.<Ed25519 signature base64url-no-pad>`，总长最多 8192 字节。
签名输入是 UTF-8/ASCII 的 `Pixels-License-v1` + 单字节 NUL + 原始 payload bytes；
不是仅签解码后的某些字段，也不能把下载响应提供的公钥作为可信根。

payload 为紧凑 UTF-8 JSON，严格按以下字段顺序，不允许空白、重复/未知/缺失字段或字段重排：

```text
schema, license_id, deployment_id, product, distribution, machine_sha256,
revision, mode, issued_at, not_before, expires_at, max_devices, max_sessions,
features, key_id
```

- schema=1；UUID 必须非 nil、标准小写带连字符编码。
- product：pixels_console/gopico/clientbox/goagent；后三项是 Auth 已有独立产品，不是 Pixels 的旧名兼容。
  不接受 console/cms/Pixels_cms 等旧别名。其他产品消费者未接新契约前不能声称它们已验收。
- distribution：official/customer；mode：trial/licensed。没有默认值，也不依赖省略字段推断发行。
- machine_sha256：当前机器身份契约提供的 32 字节指纹，小写 hex64；不能拿旧 MD5 字符串补齐或转换。
- revision 为正 i64；UTC 时间为整数 Unix 秒，0 <= issued_at <= not_before < expires_at <= 253402300799。
- max_devices/max_sessions 为正 u32；撤销不是发放零容量许可证。
- features 为非空、有序且无重复数组，当前顺序为 cloud_applications、desktop、rdp；未知功能拒绝。
- key_id 为配置公钥原始 32 字节 SHA-256 的小写 hex64。签发私钥显式提供 PKCS#8 v2，
  不因文件缺失生成替代 key，不把私钥写入数据库/包/响应。

所有字段为 ASCII 范围内的闭集枚举/整数/UUID/hex，避免跨语言 Unicode、浮点或 map 顺序差异。
签名有效后仍要求原始 bytes 等于本契约标准编码；不“修复”后再验签。
载荷不包含管理员账号、password、app_secret、token 或数据库凭据。

## 验证与信任状态

验证方必须同时提供受控信任根中的公钥集合、deployment、product、distribution、机器指纹、当前时间、
minimum_revision 和 last_trusted_time。签名正确但任一绑定不符、not_before 未到、
expires_at 已到、revision 过旧或时间回拨均拒绝。
minimum_revision >=1，last_trusted_time >=0；不能用缺失状态绕过回滚检查。

信任根以 `key_id` 精确选择公钥；不得逐 key 猜测，也不得信任 wire/下载响应携带的 key。Auth 信任根还绑定
服务 deployment 与数据库 `recovery_generation`，活动私钥必须与 `active_key_id` 一致。轮换时可在有界集合内同时保留
新旧公钥，但只有活动私钥签发；从下一份信任根删除旧 key 后，以旧 key 签发的 wire 必须失败。
信任根严格规范编码、拒绝未知字段/重复 key/key-id 替换和宽松权限文件；灾难恢复后的新代际不得继续信任旧 key。

这两个水位必须独立保留或在恢复准入对账中重建；普通进程内数字不能证明备份恢复后防复活。
在线 Auth 还须查 license revoked_at/当前 revision；离线 Customer 不联系官方也可验签，
但不能承诺获知未导入的官方撤销。最终有效期/更新水位由运维明确管理。

## 固定向量与测试

`tests/vector.json` 使用 RFC 8032 的公开测试 seed，由独立 Node crypto/OpenSSL 生成；
Rust ring 签发必须逐字节生成相同 wire，验证器必须接受该向量。
公开测试 seed 不是部署凭据，服务不自动加载测试材料。

七组测试：固定向量；部署/产品/发行/机器/时间/版本/回拨边界；有效签名下的非规范/未知字段；
损坏 wire/篡改/错误可信根；签发额度/功能/密钥输入拒绝；轮换期新旧 key 验签及撤回；
重复/替换/非规范信任根拒绝。零单元测试不算通过，执行入口明确选择 `--test contract`，要求 7 个用例实际通过。

```powershell
cargo test --locked --manifest-path rust_server/Cargo.toml -p px_license --test contract --target-dir .cache/pg-cargo
```

Auth 已实现 PG 签发事实先提交、request_id 幂等、撤销事务与审计、新接口/管理网页；
使用及精确验收范围见 [Auth 配置](px_auth_server_runtime_config.md)与[状态](server_database_execution_status.md)。
仍需 Console 新验证/本地水位与私有离线验证、全部消费者删除旧签名解析路径、恢复防回滚和通知 outbox。
这些未完成前不得把此库或 Auth 独立验收计为 DB3 完成。
