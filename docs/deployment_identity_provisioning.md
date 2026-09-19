# 部署身份离线签发与安装

> 2026-09-20。部署身份只证明当前平台属于 Official 或某个 Customer 私有部署，不承担商业额度、用户登录或软件发布签名。

## 1. 工具与隔离边界

- `px_console_admin generate-deployment-key` 在目标部署的受限私有目录中生成 Ed25519 PKCS#8 部署私钥，只向标准输出返回公钥。
- `px_deployment_authority` 是 Pixels 离线发行基础设施工具，只能在隔离签发环境使用，不进入 Server、Console、Client、Node、Android
  或 Customer 安装包。其源码/二进制本身不含根密钥，但生产根密钥仍不得复制到联网构建机或客户主机。
- 所有写入均使用 create-new：目标存在、目录权限过宽、reparse/symlink 或写后复核失败时拒绝，不覆盖旧材料。
- Console 运行期只读取已签发材料；缺失或不匹配时在监听前退出，绝不临时生成替代身份。

正式环境应先建立离线根密钥的双人审批、离线备份、介质登记和签发审计。本页命令定义程序行为，不替代组织密钥仪式。

## 2. 初始化 Pixels 离线根

只在新建根或经批准的根轮换时执行一次。先创建仅当前签发身份、SYSTEM/Administrators 可访问的空目录，然后设置输出路径：

```powershell
$env:PIXELS_DEPLOYMENT_VENDOR_SIGNING_KEY = 'D:\PixelsOffline\private\deployment-root.pk8'
.\px_deployment_authority.exe generate-vendor-key
```

记录输出的 `vendor_key_id` 与公钥，通过第二通道复核。随后建立规范 trust store；轮换时把仍在过渡期内的旧/新公钥按逗号传入附加列表，
并单调提升 `PIXELS_DEPLOYMENT_TRUST_EPOCH`：

```powershell
$env:PIXELS_DEPLOYMENT_TRUST_STORE_OUTPUT = 'D:\PixelsOffline\out\deployment-trust.json'
$env:PIXELS_DEPLOYMENT_TRUST_EPOCH = '1'
$env:PIXELS_DEPLOYMENT_ADDITIONAL_VENDOR_PUBLIC_KEYS = ''
.\px_deployment_authority.exe create-trust-store
```

相同公钥重复出现会被拒绝。根轮换必须先让已有客户端通过已信任的软件/维护包接受新 trust store，再用新根签证书；不能让远端发现响应
自行增加根信任。

## 3. 生成部署私钥

在 Console 目标主机的受限私有目录中执行：

```powershell
$env:PIXELS_CONSOLE_DEPLOYMENT_SIGNING_KEY = 'D:\Pixels\private\deployment.pk8'
.\px_console_admin.exe generate-deployment-key
```

把输出的 `deployment_public_key_hex`、目标 `PIXELS_DEPLOYMENT_ID`、申请类别、证书版本和有效期送入受控审批；部署私钥不得离开目标部署，
不得提交到仓库、数据库、日志或签发工单。Official 类别只由 Pixels 自营部署审批；客户私有部署只签 `private`。

## 4. 离线签发证书

在离线签发环境设置已审批字段。时间为非负 Unix 秒，结束时间必须晚于开始时间；证书版本对同一部署单调增加：

```powershell
$env:PIXELS_DEPLOYMENT_VENDOR_SIGNING_KEY = 'D:\PixelsOffline\private\deployment-root.pk8'
$env:PIXELS_DEPLOYMENT_CERTIFICATE_OUTPUT = 'D:\PixelsOffline\out\customer-a.cert'
$env:PIXELS_DEPLOYMENT_ID = '<deployment-uuid>'
$env:PIXELS_DEPLOYMENT_KIND = 'private'
$env:PIXELS_DEPLOYMENT_PUBLIC_KEY_HEX = '<deployment-public-key-hex>'
$env:PIXELS_DEPLOYMENT_CERTIFICATE_VERSION = '1'
$env:PIXELS_DEPLOYMENT_NOT_BEFORE = '<unix-seconds>'
$env:PIXELS_DEPLOYMENT_EXPIRES_AT = '<unix-seconds>'
.\px_deployment_authority.exe sign-certificate
```

审批人核对输出的 deployment、类别、版本和 issuer key ID。向目标部署传递证书和对应 trust store；它们虽不含私钥，Console 当前仍按私有
文件 ACL 读取，以统一拒绝被低权限账号替换。不得传递厂商根私钥。

## 5. Console 安装与启动门禁

目标主机设置：

```text
PIXELS_CONSOLE_DEPLOYMENT_CERTIFICATE=<certificate path>
PIXELS_CONSOLE_DEPLOYMENT_SIGNING_KEY=<deployment PKCS#8 path>
PIXELS_CONSOLE_DEPLOYMENT_TRUST_STORE=<trust store path>
PIXELS_CONSOLE_DEPLOYMENT_CERTIFICATE_VERSION=<minimum accepted certificate version>
PIXELS_CONSOLE_DESCRIPTOR_REVISION=<monotonic descriptor revision>
PIXELS_CONSOLE_DEPLOYMENT_TRUST_EPOCH=<exact installed trust epoch>
PIXELS_CONSOLE_MINIMUM_CLIENT_BUILD=<minimum accepted client build>
```

启动会交叉检查数据库 deployment UUID、许可证 distribution、证书类别、证书版本/有效期、trust epoch、签发根和部署公私钥。
Official 许可证只能搭配 `official` 证书，Customer 许可证只能搭配 `private` 证书。任一不一致都 fail-closed，不能通过修改域名、IP、
User-Agent 或配置字符串绕过。

证书轮换采用“先分发 trust store（如需要）→ 安装新证书 → 提高最低证书版本 → 提高 descriptor revision → 重启并验证”的顺序。
旧证书、旧 trust epoch 或旧 descriptor revision 不得在失败回滚中降水位；真正的灾难恢复必须走经批准的恢复代际流程。

## 6. 最小验收

- 重复运行三个生成/签发命令均拒绝覆盖，原文件摘要不变。
- `official`/`private` 交叉组合、错误 deployment UUID、错误部署私钥、未知根、旧证书版本和错误 trust epoch 均无法启动 Console。
- `GET /.well-known/pixels` 的证书和短期描述可由预置信任根验证；篡改任一字节失败。
- `POST /.well-known/pixels/challenge` 只接受当前 descriptor revision，证明绑定 32 字节随机 nonce 且过期/重放失败。
- 通用 Server/客户端安装包清单中不存在 `px_deployment_authority`、厂商根私钥或部署私钥。

客户端验签、水位持久化以及 Official/Customer 独立发行仍按 DB5/P0 验收；完成前，服务端协议通过不等于发行隔离通过。
