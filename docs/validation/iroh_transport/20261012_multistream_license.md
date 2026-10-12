# 多路验证授权更新（2026-10-12）

用户要求从现有 Auth 服务取得新授权，支持每种应用类型至少20路。使用仓库 `.env/auth_rgaa_vip.json` 中的既有账号，
通过 `https://auth.rgaa.vip/api/auth/licenses/issue` 为当前 Console 部署签发正式 PXLIC2，未修改随包入门授权或验证代码。

- 原授权：入门模式，总4路，各类别1路。
- 新授权：总80路，启用 cloud_applications、desktop、rdp；GameHook与WebView属于cloud_applications。
- 正式许可证没有逐类别配额，80路允许桌面、GameHook、WebView、RDP各20路同时占用授权额度，不是每类单独保留20个名额。
- 许可证ID：`02b71f36-09f4-48ab-a9cd-0a0d0d45f338`，revision1，绑定部署`2f5b1d67-6dba-47bd-a985-f5925a0b1662`。
- 到期：2027-10-12 22:26:27（北京时间）。原入门授权与新授权不是同一有效期。
- PXLIC2 SHA-256：`90882aa859f0bc84aa5358548674d01770e5e5f3d36bee1bede2e52d9d61457e`。

通过现有Console管理员接口PUT导入，GET回读同一许可证、80路和starter_license=false；Console ready204，
远端持久文件SHA与Auth签发文件相同，服务Running。Ed25519签名使用原安装信任公钥验证通过，未替换信任库。
没有重启服务、编译、传输安装包或启动多路媒体测试。Auth和Console本次管理员会话均已退出。

原入门授权已保存在90的`D:\112233\multistream-license-20261012\starter-before.pxlic2`，
SHA为`718e645666f34377722226af29c180a83d00b3c35ae74541a0f0b9a384ca8d41`，备份仅SYSTEM/管理员可访问。
新许可证及去重签发请求保存在本机`.env/multistream-license-20261012`；签发wire、密码和会话凭据不进入Git证据。

单独以MCP的SYSTEM身份调用`px_console_admin validate-license`失败。已核对项目私有文件检查：
它只信任当前用户、SYSTEM和Administrators，而新文件属于Pixels.Console服务SID，DACL也包含该SID；
因此SYSTEM运行的独立校验器不能读取这种服务私有文件。未放宽ACL，也未把失败算成签名成功证据；
实际服务身份的导入验证、持久SHA及管理状态回读作为本轮激活依据。

授权与运行容量分别管理。目前90节点max_instances=4、XR=1，已启用部署容量为5、12、1、1，
未将这些容量扩大，也未改变禁用测试应用。不能将80路授权激活称为已跑通80路或每类20个实例。
下一步先按计划测试并发调整节点与应用部署容量，再执行多Relay正常负载验证；卡帧主动调查仍暂缓，Android排除。

完整无凭据元数据、容量快照和持久检查见[证据索引](20261012_multistream_license.json)。
