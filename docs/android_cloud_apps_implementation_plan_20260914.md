# Android 云应用模块实施计划

> 状态：已切换 PostgreSQL `/api/console` 资源会话模型；2026-09-19 已完成公网 guest 与账号目录、实例启停、显式 CloudApplication 描述符、Direct 首帧与清理短测
> 日期：2026-09-15
> 范围：`src/px_android`，复用现有 `px_console` 用户、游客和应用调度接口
> 首轮环境：公网测试 `px_console`（`https://39.71.45.66:4600`）与 Pixels Android 真机

## 0. 已确认的产品决定

- Android 新增独立一级 Tab，中文固定为“云应用”，英文为“Cloud Apps”。
- 旧固定 Render 端口已完全退役，不是默认值、回退值、探测目标或验收端口。Android 必须使用 Console/节点配置返回的实际端点；
  当前统一默认中桌面 Render 为 4601，应用 Render 从 4613–4998 动态分配，同一实际端口承载 TCP/WS 与 UDP。
  端口与节点连接基线以 [节点连接配置](node_connection_configuration.md) 为准。
- “设备”和“云应用”是两个独立资源域。云应用不再作为设备卡片动作，也不写入设备数据库。
- 设置页提供 PX Console 地址配置、连接检查、账号注册、登录和退出。
- 未登录用户进入“云应用”时使用仅驻留内存的 Android guest Bearer，只显示 public 应用。
- 登录后显示 public 与当前账号 ACL 授权应用；切换账号、退出、会话过期或切换 Console 后立即重新对账资源。
- 注册成功后自动登录；若注册成功但自动登录失败，必须明确提示“账号已创建”，不得重复注册。
- 首版复用现有 Android Native 串流工作区。`game-hook`、`webview` 进入现有原生串流链路；Android 尚无 FreeRDP 客户端，
  `rdp` 应用显示“当前 Android 版本暂不支持”，不得按普通 Native 串流误启动。
- 首版操作为刷新、启动并连接、连接现有实例和停止。观察/接管、个人资料编辑和密码修改不扩入本批。
- 不保留任何旧版兼容行为：不迁移旧应用页、旧账号 session、旧 endpoint 存储或旧默认端口，不建立 facade、别名、双入口、
  feature flag 或运行时 fallback。开发与候选验收直接使用最新模型和清洁安装。

本决定取代 `android_pixels_ui_design.md` 中“三个一级入口”和“远程应用属于设备详情”的旧信息架构，但不改变设备直连、
文件传输和远控工作区现有产品边界。

## 1. 当前代码基线与缺口

已经存在、应复用的能力：

- `ConsoleApiClient` 已实现用户登录、用户应用列表、启动、停止和 Native 连接描述。
- 旧 `ApplicationLibraryViewModel` 中启动后轮询、连接现有实例和停止的算法可作为实现参考，但旧类型和页面不保留。
- `RemoteSessionService` 与 `RemoteWorkspaceScreen` 已能消费 `RemoteSessionRequest`，云应用无需建立第二套播放器。
- 用户 Bearer 已由 Android Keystore 加密后写入 DataStore，明文密码未保存。
- Console 已实现游客 Bearer、注册、public/ACL 应用、游客/用户实例以及两类 Native 连接描述。

必须补齐或修正的缺口：

1. 一级导航只有设备、传输、设置，`Applications` 仍映射到设备 Tab。
2. 应用页面位于 `feature-devices`，资源与页面职责没有独立模块边界。
3. Console 地址只存在于登录表单或已登录 session 中；未登录地址不能独立保存，退出后会被清空。
4. 身份状态只有 SignedOut/SignedIn，没有内存 guest session，无法浏览和启动 public 应用。
5. Android 没有注册接口与注册 UI。
6. 应用 DTO 丢弃了 `app_type`、`access_mode` 和 `version`，连接 DTO 也丢弃了 `app_type`/`rdp`，存在错误启动 RDP 的风险。
7. public 应用列表不携带当前游客实例；客户端需要把 `/public/apps` 与当前 guest 的 `/public/instances` 合并。
8. 现有错误模型过于粗糙，注册冲突、实例冲突、配额、调度不可用和 request ID 无法稳定展示。
9. 应用列表尚未完成公网 Console 与 Android 真机的公网链路闭环。
10. Android 活动源码和测试曾残留旧固定 Render 端口及独立 UDP 端口。这是迁移遗漏，不能当作兼容行为；
    当前实现已删除，UDP 端口取本次连接的实际 Render 端口。
11. Android 登录和 guest 请求当前使用 `client_type=panel`，会把 Android 会话记成 Panel；Console 也只向 Panel 返回 Bearer。
    最新实现必须增加独立 `android` client type 和 Android Bearer 会话，不能继续借用 Panel 身份。
12. 旧应用连接复用 `RemoteSessionTarget.Account`、空 `fallbackRemoteDeviceId` 和合成 preference key，没有表达应用、实例和返回页面。
13. 旧导航在远控结束后固定返回设备页；从云应用进入的会话必须返回云应用页。
14. 退役端口不只残留在 Android：共享 Render、Service/UserProxy、Web Client 和开发脚本仍有旧默认值。
    节点配置对它们的运行时覆盖不等于迁移完成；云应用 E2E 前必须将活动默认、测试和脚本同步到最新端口模型。
15. Debug 网络安全配置当前只为 `localhost` 信任打包的旧测试证书；直接配置测试 Console IP 时不会自动使用该信任。

## 2. 目标信息架构与页面状态

手机底部导航调整为：

```text
设备        云应用        传输        设置
```

平板使用同样四个资源入口的 Navigation Rail。进入远控工作区后继续隐藏一级导航。

“云应用”页面只展示 Console 应用，不显示节点、设备、端口或连接密码：

```text
云应用                                      刷新
公开应用 · 游客使用          [登录 / 注册（进入设置）]

┌ 应用名称 ──────────── 公开 · 可以启动 ┐
│ 应用类型/兼容性                       │
│                            [启动并连接] │
└───────────────────────────────────────┘
```

登录后标题区改为账号名与“公开 + 专属”；应用卡片显示公开/专属、类型和实例状态。页面需要覆盖以下显式状态：

- 尚未配置 Console：解释用途并提供“前往设置”。
- 正在建立游客/用户会话、加载列表。
- 游客 public 列表、登录用户 public + ACL 列表。
- 空列表、Console 不可达、认证过期、节点离线、配额/占用、实例启动失败。
- Starting、Running、Stopping；同一应用有操作在途时禁止重复提交。
- RDP 或以后未知类型：卡片保留可见，但按钮禁用并显示不支持原因。

服务端当前 `cover_url` 可能为空。首版使用按应用类型区分的本地矢量图标作为稳定回退；远程封面加载可以在确认 URL 安全、
缓存与认证策略后单独加入，不能为本批引入宽松 TLS 或任意重定向。

## 3. 模块与依赖边界

新增 `:feature-cloud-apps`，直接建立新领域边界；现有 Application Library 只用于对照启停/轮询算法，不迁移旧类型和页面：

```text
app
  └─ PixelsApp / PixelsAppGraph（组合根、一级导航、进入 RemoteSessionService）
       ├─ feature-devices
       ├─ feature-cloud-apps
       ├─ feature-transfer
       └─ feature-settings

core-domain
  ├─ console endpoint/session/registration 类型与接口
  └─ cloud app/application instance/compatibility 类型与接口

core-data
  ├─ ConsoleEndpointStore（非秘密，DataStore）
  └─ AndroidConsoleSessionStore（全新 schema，Keystore 加密 user token）

core-network
  ├─ ConsoleSessionCoordinator
  ├─ ConsoleAccountRepository
  └─ ConsoleCloudAppsRepository
```

关键所有权约束：

- `ConsoleSessionCoordinator` 是 endpoint、Android guest token 和 Android user token 的唯一会话协调者。
- guest token 只在内存存在；user token 继续加密持久化；密码和注册确认密码从不落盘。
- endpoint 是独立非秘密设置。切换 endpoint 必须取消旧请求、清除旧 endpoint 对应的 user session 和 guest session，再发布新状态。
- Render 主机和端口只来自经过校验的 Console resource-session descriptor。不得猜测、回落或探测已退役端口。
- 云应用 ViewModel 只依赖领域 repository，不直接拼 URL、读 token 或操作 Android Service。
- 组合根把领域层产生的 `RemoteSessionRequest` 交给已有 `RemoteSessionService`，不复制会话生命周期。
- 领域层新增明确的 `RemoteSessionTarget.CloudApplication`，携带应用 ID、实例 ID、连接描述和返回目的地；删除
  `fallbackRemoteDeviceId`，设备账号连接和云应用连接不再共用含糊的 `Account` 目标。
- 全新 endpoint/session schema 不读取、转换或回退到现有 `account_session_v1`。旧实现从活动源码删除，清洁安装是验收基线。

建议的身份状态：

```text
NoEndpoint
  -> GuestStarting -> GuestReady
  -> UserSigningIn -> UserReady
  -> Registering -> UserSigningIn -> UserReady

UserReady --退出/401--> GuestStarting
任意状态 --切换 Console--> NoEndpoint/GuestStarting
```

并发调用共享同一个 guest 创建任务，避免页面刷新、注册和启动同时签发多个 guest session。401 最多自动重建 guest 并重试一次；
用户 session 的 401 清除持久 token、切回 guest，不静默使用旧身份继续请求。

## 4. Console API 对接矩阵

现有资源路由足够完成首版，不新增兼容路由；但身份契约必须原生增加 Android client type：

| 场景 | API | Android 行为 |
|---|---|---|
| 健康检查 | `GET /health/ready` | 只接受 HTTPS 就绪响应，不用目录接口冒充探针 |
| 建立游客身份 | `POST /api/console/guest-sessions` | `X-Pixels-Client-Type: android`；guest Bearer 仅驻内存 |
| 注册 | `POST /api/console/accounts` | 无 guest 依赖；成功后用同一当前接口自动登录 |
| 登录/退出 | `POST /api/console/sessions`、`DELETE /api/console/session` | Android user Bearer 加密保存 |
| 游客/用户目录 | `/api/console/guest/applications`、`/api/console/applications` | 分别使用 guest/user Bearer，并与本人实例列表合并 |
| 本人实例 | `GET/POST /api/console/instances`、`POST /instances/{id}/stop` | 强制显式 `X-Pixels-Subject-Kind: guest|user` 和 revision CAS |
| 建立资源会话 | `POST /api/console/resource-sessions` | 云应用 target 必须同时带 application_id 和 instance_id；桌面 target 只带 device_id |
| 获取描述符 | `POST /api/console/resource-sessions/{id}/descriptor` | 校验 session、target、Android client type、controller role、revision、native transport 与实际 host/port |

列表 DTO 完整解析 `app_id`、`app_type`、`name`、`access_mode`、`cover_url`、`running_instance` 和 `version`。
Native 连接描述必须解析 `app_type`，发现 `rdp` 或未知类型时在创建 `RemoteSessionRequest` 前拒绝。

Console 身份实现同步调整：

- 为 client type 建立受控枚举，合法值至少包含 `panel`、`android`、`user_web` 和 `admin_web`，不继续散落字符串判断。
- user 登录对 `android` 签发 Bearer，session 审计记录真实 client type；Panel 和 Web 的既有合法身份保持各自行为。
- guest session 对 `android` 签发独立 Android guest Bearer；Bearer middleware 校验它是允许的非浏览器 guest，不能伪装成 cookie Web session。
- 将仅以 Panel 命名的 Bearer 签发/认证内部函数改成传输语义明确的实现；不是给 Android增加 Panel 别名。
- Console 增加 Android user/guest 登录、注册、资源访问、错误身份拒绝和审计字段测试，部署最新测试 Console 后再做真机 E2E。

错误响应统一解析 HTTP 状态、`code`、`error`、`message`、`request_id` 与 `Retry-After`。UI 使用本地化稳定文案，诊断中只记录
request ID 和错误分类，不记录 token、密码或完整连接描述。注册表单本地先执行与服务端一致的基本约束：用户名 2–64 个字符、
无首尾空白/控制字符/斜杠，密码 8–128 个字符且不能全为空白；服务端仍是最终判定者。

## 5. 设置页设计

设置页拆成两个连续但独立的区块：

### 5.1 PX Console

- 地址输入，例如 `https://console.example.com:4600`。
- “测试连接”和“保存”操作；只接受 HTTPS、有效主机和端口，不接受 user-info、query、fragment 或非空路径。
- “测试连接”访问 `GET /health/ready` 并要求业务就绪，不尝试旧地址或目录兼容接口。
- 保存成功后云应用与账号功能立即使用新 endpoint。
- 已登录时修改 endpoint，必须明确确认将退出当前账号；取消时不改 endpoint。
- Release 仅信任系统/企业受信 CA。Debug 可使用明确打包的测试公钥证书，但不得关闭主机名或证书校验。

### 5.2 账号

- 未登录：登录/注册切换，用户名、密码；注册额外要求确认密码与规则提示。
- 注册成功：自动登录，并保留在设置页显示账号与 Console 地址。
- 已登录：显示用户名、Console 地址和退出按钮；`must_change_password` 继续明确提示，不在本批实现改密。
- 页面旋转和后台恢复不保留明文密码；在途登录/注册有单一进度状态，禁止重复提交。

## 6. 分阶段实施

### P0：Console 设置与身份底座

- 先完成仓库活动路径的端口基线收口：桌面 Render 默认 4601、Service 4603、应用池 4613–4998、Panel 4999；
  修正 Render、Service/UserProxy、Web Client、Android、测试与开发脚本的旧默认。历史报告可保留原始证据，但不能被当作当前命令或验收指南。
- Native SDK 参数的 WS/TCP 与 UDP 均使用连接描述返回的同一实际 Render 端口，不再内建另一个 UDP 端口。
- Console 增加类型化 `android` client type、Android guest/user Bearer 的签发与认证；Android 不再发送 `panel`。
- 新增独立 endpoint store 和全新 Android session schema；不读取或迁移旧 account/session/endpoint 数据。
- 建立 session coordinator 与内存 Android guest session。
- 补全注册 API、错误 DTO、失效/切换/并发规则及单元测试。
- 改造设置页，使 Console 配置与账号表单解耦。
- 替换 Debug 包内旧测试证书，为当前测试 Console 的 SAN 域名/IP 配置明确的信任边界；不同时信任新旧两套证书。

验收：仓库活动代码、配置、测试和可执行开发脚本不再包含已退役的端口默认；Android 不再包含 `client_type=panel`；Console 审计中的 Android 会话身份正确；
重启 App 后新 endpoint 保留；新 user token 可恢复；guest token 不落盘；切换 endpoint 清理旧身份；注册后自动登录。

### P1：独立云应用模块与四 Tab 导航

- 新建 `feature-cloud-apps`，直接实现新页面和 ViewModel；删除 `feature-devices` 中旧 Application Library 页面、ViewModel 和测试，
  不留转发类或类型别名。
- 新增“云应用”一级 Tab、手机 Navigation Bar 与平板 Navigation Rail 项。
- 删除设备卡片中的“应用”入口和相应 action；设备页不再触发应用目录请求。
- 完成中英文目录、空状态、登录引导、公开/专属标记和类型图标。

验收：设备与云应用在导航、状态、数据源和持久化上完全分离；所有新文案中英文 key 数量一致。

### P2：游客/账号目录与应用生命周期

- 实现 guest public 目录与 guest instances 合并，以及 user public + ACL 目录。
- 实现启动幂等、轮询、进入已有实例、停止、401 单次恢复和前后台刷新。
- Direct连接映射直接消费Console返回的`device_id`、`instance_id`、实际Render host和动态port。2026-09-19后当前描述符不再为WebRTC
  保留`signal_device_id`或Relay信令目标；Relay既有非WebRTC数据能力是独立路径，不得成为Direct Host失败后的隐式fallback。
- 通过新的 `RemoteSessionTarget.CloudApplication` 串入现有 RemoteSessionService；会话结束返回云应用 Tab，而不是设备首页。
- 添加 app type 能力门控；首版允许 `game-hook`、`webview`，拒绝 `rdp` 和未知类型。

验收：游客和用户资源不串身份；快速重复点击只创建一个实例；退出/切换账号后一个刷新周期内完成资源对账。

### P3：自动化、真机与交付

- 单元测试、Compose 导航测试、网络契约测试和进程内假 API 集成测试。
- 用目标级日常构建产出本批 Console、Service/Render 与 Android 产物，不运行 release-only 全量构建。
- 将本批改变的 Windows 运行时产物同步到 `build_official/<product>/dist` 和公网测试部署，逐文件核对 SHA-256；确认 Console、节点和 Android 的 API/身份契约来自同一批次。
- 当前公网测试 Console 与 Android 真机完成端到端矩阵。
- 执行 Android 日常构建与 lint；覆盖安装，不卸载、不清数据；记录 APK SHA-256、Console/Render 版本和测试实例 ID。

## 7. 测试矩阵

### 7.1 自动化门禁

- Endpoint：规范化、持久化、错误地址、切换确认、切换时取消旧请求。
- Session：guest 去重、仅内存、过期重建、user 加密恢复、401 降级、退出幂等。
- 身份：Android 请求只发送 `client_type=android`；Console 拒绝错误 client type，审计不出现伪造的 Panel 身份。
- 注册：本地校验、用户名冲突、限流、注册成功自动登录、注册成功但登录失败。
- 目录：guest 仅 public；用户 public + ACL；退出、换号和 ACL 收缩后无旧卡片残留。
- 生命周期：启动 200/202、重复点击、轮询超时、停止中、回调晚到、ViewModel 销毁和前后台恢复。
- 类型：game-hook/webview 可进入 Native；rdp/未知类型无法创建 RemoteSessionRequest。
- 连接描述：实例`device_id`、`instance_id`、实际Render host和动态port原样进入Direct边界；类型化CloudApplication target、资源会话、
  role、lease和generation必须保持，描述符不携带中央WebRTC信令目标。
- 清理：旧 Application Library、`OpenApplications`、`RemoteSessionTarget.Account` fallback、旧 session schema 和旧端口不在活动产物中。
- UI：四 Tab、系统返回、页面旋转、深浅主题、英文/简中目录 parity、较大字体基本可用。

日常命令：

```powershell
cd src/px_android
./gradlew.bat testDebugUnitTest :app:assembleDebug :app:lintDebug
```

### 7.2 当前公网 Console 真机矩阵

1. 在 Android 设置中配置当前 Console，测试 HTTPS 连接；不直接填写或探测任何固定 Render 端口。
2. 未登录进入云应用，看到 public 应用；通过 Console 启动公网节点上的 game-hook 和 webview 测试应用，验证首帧、输入、返回和停止。
3. 注册唯一测试账号并自动登录；管理员授予测试组 ACL，刷新后看到 public + 专属，未授权应用不可见。
4. 启动、断开、连接现有实例、停止；确认 Console 中 owner、instance 和审计主体正确。
5. 退出后只剩 public；重新登录恢复账号目录；Console 重启或 token 过期后无身份串用。
6. RDP 测试卡可见但明确禁用；不得创建 RDP 实例来冒充 Android 支持。
7. 收尾停止本轮实例，删除一次性测试账号/授权，确认公网节点无本轮残留 Render；不影响已有长期测试应用配置。

每个真机会话按现有 Android 约定控制在 5 分钟内。应用列表、实例状态与 Console 审计截图/日志只记录非秘密标识。

## 8. 当前环境检查（2026-09-14）

- 测试 Console 公网地址 `39.71.45.66:4600` 当前 TCP 可达，配置为 HTTPS/test 环境。
- 远端 Console 当前证书 SAN 包含公网地址 `39.71.45.66`；Android Debug 包内现有测试证书不是该远端证书，
  真机直连前必须只同步远端公钥证书到 debug trust 配置并保留正常主机名校验。Release 不打包测试证书。
- USB 设备 `e2b3b128` 已授权，Debug APK 已采用覆盖安装方式部署；后续真机测试不得卸载应用或清除数据。
- 不再探测任何已废弃的内网节点地址或固定 Render 端口。先确认公网节点已向 Console 上报在线状态，再启动应用并以该实例
  resource-session descriptor 返回的实际主机和 4613–4998 动态端口验证 TCP/WS 与 UDP 数据面。
- Android 真机直接使用公网 Console；应用 Native 连接使用 Console 返回的公网节点地址和实际动态端口，不以 ADB reverse 代替数据面网络。

这些是 E2E 开始前的环境门禁，不阻塞 P0/P1 的代码和自动化测试。

## 9. 完成定义

只有同时满足以下条件才把 Android 云应用标记为完成：

- 独立“云应用”Tab 已交付，设备页无旧应用入口。
- Console 配置、游客、注册、登录、退出和账号恢复全部通过自动化与真机验证。
- public/ACL 隔离、实例 owner、启动/连接/停止和错误反馈符合 Console 契约。
- Android 的运行时代码、默认配置和测试不包含已退役固定端口，连接全过程只消费权威实际端点。
- Android/Console 活动代码中不存在 Android 冒充 `panel`、旧 session 读取、旧应用页转发、双入口或 fallback 标识符。
- game-hook 与 webview 在公网节点分别取得真实画面和输入证据；RDP 明确能力禁用。
- Debug APK 覆盖安装通过，自动化/lint 通过并记录 APK SHA-256；未把测试证书、token、密码或测试账号带入 Release。
- 测试实例和临时账号完成可审计收尾，公网节点无本轮遗留运行实例。

## 10. 2026-09-15 交付与公网验收记录

- 公网 Console 已部署当前身份与云应用实现；Android guest、user 登录/退出、临时账号注册/登录/退出/删除均通过正式 HTTPS API 验收。
- 真机 `e2b3b128` 使用覆盖安装，既有 endpoint 和应用数据保留；设置页连接检查、四个一级 Tab、游客目录和 RDP 禁用状态均已验证。
- 修复 Android Native 直连媒体与文件传输地址遗漏设备密码哈希的问题，并对所有查询参数执行 URL 编码。修复后 game-hook 与 webview
  均能从 Console 返回的公网节点地址和动态 Render 端口进入远控，UDP 视频稳定解码至约 60 FPS，心跳正常。
- WebView 真机验证包含远程点击输入：页面交互状态发生变化；演示页面中的旧节点称呼已替换为“公网节点连接测试”并同步到公网测试环境。
- 启动、返回、连接已有实例和主动停止均完成真机闭环；收尾时云应用卡片恢复“可以启动”，无本轮活动实例残留。
- Android 全量 `testDebugUnitTest`、`lintDebug` 与 Debug APK 构建通过；最终 APK SHA-256 为
  `51856DFEFA84FEDBA6FAC2DCB0D12B4912CCC78E9E61D7D63A75BCB662318FF1`，使用既有 Debug 签名覆盖安装，未卸载或清除应用数据。

## 11. 2026-09-19 当前资源会话与账号补充验收

- 登录响应按当前 Console 形状只读取 `expires_at`；已删除客户端领域对象、加密会话存储和测试中的 `absolute_expires_at` 假字段。
  DataStore key 与 Keystore alias 直接升为 v2，不导入 v1；`avatar_url:null` 保持真正的空值。
- 活动公网脚本已整体切到 `/api/console`，以 `client_type=android` 分别验证 guest/user 目录、实例、显式 CloudApplication target、
  descriptor、CAS 停止、资源会话关闭和注销。旧 `/api/v1` 脚本原文只保存在 `backup/`，不参与执行。
- 1.0.6 Debug APK 从清洁输出完成 454 个 Gradle task，并以 `adb install -r` 覆盖安装；SHA-256 为
  `8118D761102E1621B6098D62EB02DBAFF0F12978C9F5922238545D93A589F706`。
- Xiaomi 22021211RC 成功登录公网测试账号、读取账号目录、启动 Public Web Application、取得明确 CloudApplication descriptor、
  直连公网动态端口 4613、收到 H.264 1920×1080 首媒体并初始化 MediaCodec；随后结束远控、停止实例、确认恢复“可以启动”并注销账号。
- 本记录关闭账号 CloudApplication Direct 短链路，不冒充设备 ACL、Android Relay、文件、音频、撤销/续租或最终统一长测。

设备 ACL 的可重复公网短测入口为 `scripts/test_android_device_acl_public.py`。它使用临时 Android 账号依次证明授权前不可见、显式 user ACL
授权后目录和详情可见、撤销后重新隐藏；结束时恢复设备原始 users/groups ACL 并删除临时账号。该脚本必须使用现有公网测试平台的管理员凭据，
不能生成或硬编码管理员身份；脚本存在不等于真机/公网验收已经通过。

## 12. 2026-09-19 Android Relay 路由切片

- 云应用卡片复用同一连接偏好编辑器，增加“自动/直连/Relay”显式路径。选择按 `cloud-app:<application_id>` 保存；`Automatic` 当前等同
  已验收的 Direct 路径，不做静默 Relay fallback。
- Console descriptor 在配置 Relay 时返回 host、port 和逐资源会话 `admission_ticket`。票据最长 300 秒并绑定 session 与目标
  device/instance；部署 appkey 不返回客户端。Relay 在 upgrade 前验票，Render 仍独立校验 frontend grant 和租约。
- 独立 Relay 准入票据、Relay、Console 单测和严格 Clippy 通过；真实 PG node-control 报告为
  `test-results/server_validation/pg-20260919-233340-c48eec6f`，1/1 PASS，源哈希不变且隔离环境已清理。
- 1.0.8 Debug APK 从清洁输出完成 454/454 Gradle task，单测、Lint、arm64 Native、打包和 `adb install -r` 全部通过；SHA-256 为
  `3805209B164975B338CF4C8265E401403D9C4F80B680B9388CD67341AD2F693D`。Xiaomi 22021211RC 已验证入口、三种路径及 Relay 选择重开持久化，
  无 AndroidRuntime/JNI fatal。
- 公网仍运行本切片之前的 Console/Relay，故尚无 Android Relay 首帧证据。必须先部署当前服务端，再验证媒体、音频、输入、撤销和
  descriptor 到期后以新 descriptor 重连；不得用 UI/单测冒充端到端通过。

## 13. 2026-09-20 Android descriptor 续签与重连切片

- Android 对账号桌面和 CloudApplication 断线不创建新的资源会话，只向
  `POST /api/console/resource-sessions/{existing_session_id}/descriptor` 提交当前 revision；返回 revision 必须严格前进，session、目标、
  `client_type=android`、controller role、状态、native transport 和 owner kind 必须保持一致。
- `ResourceConnection` 明确记录 `user|guest` owner。登录用户只能以当前有效 user session 续签；guest 只能复用创建原资源会话的内存 guest
  身份。guest 到期、登录状态切换或 owner 不一致均 fail-closed，不静默创建新 guest、新资源会话或换用另一主体。
- 同一 UI session 使用显式 restart 停止旧 Native attempt 并装载新 descriptor。Native callback 每次 attempt 使用独立 callback UUID，
  已取消 attempt 的排队/迟到回调不能污染新连接；用户停止、换会话和续签并发时，旧结果不能覆盖新的 prepared request。
- 可恢复断线期间若 Console 暂时不可达，续签以 5 秒间隔继续尝试；Native 已用旧授权自行恢复时立即停止续签循环。认证、协议、资源结束等
  确定性拒绝进入类型化失败并停止旧 transport；用户手动重试资源会话前也必须先取得新 descriptor。Direct 本机连接保持原重连行为。
- 单元测试覆盖同会话 descriptor 的唯一请求路径/revision、owner/target 拒绝、user/guest 身份选择、过期 guest 不换 owner、工作流重启、
  start 拒绝、停止后的迟到 restart 和显式失败。全模块 JVM 测试、Lint、arm64 Native 与清洁 454/454 task 构建通过。
- 1.0.10 Debug APK 已用 `adb install -r` 覆盖安装至 Xiaomi 22021211RC，未卸载、未清数据；SHA-256 为
  `3B7F875C1C7ABE8F1667A4F85B78A346CDBB97A5404FFA20DED002238BD142BD`。设备报告 versionCode 10010/versionName 1.0.10-debug，
  冷启动及“设备/云应用/传输/设置”顶级导航可见，无 FATAL EXCEPTION/JNI fatal。
- 本切片关闭 Android 本地续签实现和短门禁，不关闭公网端到端：当前新 Console/Relay 仍未部署，Android Relay 首帧、真实断线后新票据
  重连、音频、输入与在线撤销仍须在同批公网服务端上短测。
