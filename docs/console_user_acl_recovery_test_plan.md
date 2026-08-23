# Console 用户、ACL 与实例恢复完整测试方案

版本：1.0

适用范围：当前测试阶段的 Console、Console Web、px_service、px_render 与 Web Client

关联设计：[console_user_group_acl_design.md](console_user_group_acl_design.md)

## 1. 目标

本方案用于确认以下能力可以作为一个完整链路交付，而不只是单个接口可用：

1. 管理员、注册用户和游客的身份、会话、CSRF 与权限边界正确。
2. 任意有效登录用户无需个人/用户组设备授权即可连接全部 Console 设备；应用 public/ACL 策略在目录、实例启动和连接票据入口保持一致。
3. 启动、运行、停止以及 Console 或 Service 异常后的实例状态能够收敛，不产生错误补偿停止或遗留进程。
4. Console Web 在会话过期、暂时断网和 ACL 动态收缩时给出正确结果，不泄露旧权限或凭据。
5. Web Client 能以 view-only 权限连接、续票并从一次 RTC 连接失败中恢复。
6. 自动化测试、真实进程测试和远端双机测试都有明确的准入条件、证据与清理方式。

本轮不验收尚未调通的 `net_rtc`。媒体测试使用当前已可工作的 RTC/Web Client 链路；`net_rtc` 后续单独建立专项方案。

## 2. 测试对象与信任边界

| 对象 | 本方案关注点 |
| --- | --- |
| Console | 身份、ACL、应用目录、实例生命周期、票据、状态恢复、审计 |
| Console Web | 用户门户、公共应用、管理端授权、会话失效、错误恢复 |
| px_service | 节点心跳、启动/停止命令、启动回执、Console 重连 |
| px_render / 应用进程 | 实际启动、采集、编码、连接与退出清理 |
| Web Client | 一次性票据兑换、view-only、续票、RTC 重连、视频播放 |
| MongoDB | 用户、组、ACL、实例和审计记录的持久化一致性 |
| Redis | 会话、CSRF、票据及短期状态；重启后安全失效 |
| px_media | 直播推拉流专项的依赖；不作为用户 ACL 的授权源 |

权限判断必须在服务端完成。前端隐藏按钮只属于交互优化，不属于安全边界。应用是否可见、是否允许启动、是否允许为实例签票，必须复用同一套策略。

## 3. 环境分层

测试按由快到慢、由纯逻辑到真实链路的顺序执行：

| 层级 | 环境 | 用途 | 是否阻断提交 |
| --- | --- | --- | --- |
| L0 | Rust/TypeScript 纯测试 | 权限矩阵、状态机、序列化、请求错误处理 | 是 |
| L1 | Vitest + Playwright Mock | 页面流程、分页、会话过期、ACL 刷新 | 是 |
| L2 | 本机完整栈 | Console、Service、Render、应用、RTC、进程恢复 | 是 |
| L3 | 两台机器/NAT | 真实网络、证书、端口映射、重连与稳定性 | 发布前是 |
| L4 | 5 分钟稳定性与故障注入 | 延迟漂移、资源泄漏、组件重启 | 发布前是 |

本机基线使用 `https://127.0.0.1:30500` 或当前局域网 Console 地址。远端地址、账号和密钥只放在部署机的私有配置或密码管理器中，不写入仓库、测试报告、命令历史和截图。

## 4. 前置条件

### 4.1 软件和服务

- Windows 测试机已安装项目约定版本的 Visual Studio C++ 工具链、CMake/Ninja、Rust 和 Node.js。
- Node.js 满足 `web/px_console/package.json` 的版本要求，已安装当前 Chrome。
- MongoDB、Redis、px_media、Console、px_service 能按部署配置启动。
- 本机至少有一个已配置、能够正常启动的 public 测试应用。
- HTTPS 证书在测试浏览器中已明确接受；远端发布验收必须使用主机名匹配的可信证书。
- 执行进程重启测试的终端具有查询和终止目标进程所需权限。

### 4.2 安全约束

- 禁止在脚本、文档、截图或 Playwright trace 中保存真实密码、app secret、session cookie、CSRF token、ticket 和 renewal token。
- 自动化数据统一使用 `e2e-<日期>-<随机串>` 前缀，清理时只允许匹配本次前缀。
- 禁止无条件 drop 整个数据库；每次删除必须显式限定集合、实体 ID 或测试前缀。
- 重启脚本只能操作工作区 `output/px_console/px_console.exe` 的精确绝对路径。
- 远端脚本禁止按进程名批量结束其他目录中的 Console 或 Render。

## 5. 标准测试数据

每轮集成测试创建独立数据，避免依赖人工遗留状态：

| 标识 | 数据 | 权限关系 |
| --- | --- | --- |
| Admin1 | Console 管理员 | 可管理用户、组、设备和应用 ACL |
| User1 | 普通用户 | 属于 Group1，个人授权 Device1 |
| User2 | 普通用户 | 属于 Group2，无 Device1 授权 |
| Guest1 | 独立游客会话 | 只能访问 public 应用 |
| Group1 | 用户组 | 包含 User1，授权 Device1 |
| Group2 | 用户组 | 包含 User2，不授权 Device1 |
| AppPublic | public 应用 | Guest1、User1、User2 均可见并可启动 |
| AppG1 | ACL 应用 | 只授权 Group1 |
| AppG2 | ACL 应用 | 只授权 Group2 |
| Device1 | 在线设备 | 承载三个测试应用 |

另创建一个离线节点、一个停止状态实例和一个伪造实例 ID，用于验证错误路径。密码使用测试专用随机值，不得复用管理员或部署账号密码。

## 6. 自动化执行顺序

从仓库根目录执行。任何一步失败都停止进入更慢的层级，并保留失败证据。

### 6.1 Rust 测试

先进入 Visual Studio 开发者环境，再执行：

```powershell
cd rust_server
cargo test -p px_console_server
cargo test -p px_service
```

重点确认：

- public、ACL、guest、user 和实例 owner 的纯权限矩阵。
- `Starting`、`Running`、`Stopping` 在心跳、断线和重启后的状态收敛。
- 丢失启动回执时，若心跳已确认 Running，不发送补偿停止。
- 未解决的 Starting 超时后才进入 Failed；丢失进程进入 `PROCESS_LOST`。
- 一次性票据、续票轮换、重放和过期处理。

### 6.2 Console Web 静态检查和单元测试

```powershell
cd web/px_console
npm ci
npm run type-check
npm run test:unit -- --run
npm run build
```

### 6.3 Chromium 页面测试

```powershell
cd web/px_console
npm run test:e2e -- --project=chromium
```

普通 E2E 使用 mock 数据，不启动破坏性进程。它必须覆盖：

- public 应用和登录用户应用分区。
- 列表轮询发生暂时网络错误时保留最后一次可信投影。
- 会话返回 401 时清除 CSRF 并跳转登录，保存原页面 redirect。
- ACL 被撤销后，下一次成功刷新删除失去权限的应用。
- 启动、view-only、停止和取消停止的页面语义。
- 超过 100 个用户时仍可分页、搜索并保存成员。

### 6.4 本机真实 RTC 续票

先启动完整本机栈及至少一个 public 应用，然后执行：

```powershell
cd web/px_console
$env:PX_LIVE_USER_RENEWAL = '1'
$env:PX_LIVE_CONSOLE_URL = 'https://127.0.0.1:30500'
npx playwright test e2e/user-live-renewal.spec.ts --project=chromium
Remove-Item Env:PX_LIVE_USER_RENEWAL -ErrorAction SilentlyContinue
Remove-Item Env:PX_LIVE_CONSOLE_URL -ErrorAction SilentlyContinue
```

该用例必须实际看到视频 `currentTime` 增长，主动关闭一次 PeerConnection，确认旧票据不复用、续票成功且新连接继续播放，最后停止测试实例。

### 6.5 Console 重启恢复

该脚本会强制重启 Console，只能在专用测试环境执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_console_restart_recovery.ps1 `
  -ConsoleUrl https://127.0.0.1:30500 `
  -AllowRestart
```

验收点：实例在 Console 重启前已经 Running；Console 恢复后仍由 Service 心跳确认 Running；能够为原实例重新签发仅观看票据；清理后没有本轮 game-hook Render 或 Client 遗留。

## 7. 身份、会话与 CSRF 用例

| 编号 | 场景 | 预期 |
| --- | --- | --- |
| ID-01 | 新用户直接注册并登录 | 注册成功；密码只存 Argon2id hash；无默认组和设备权限 |
| ID-02 | 错误密码连续登录 | 返回统一错误；触发配置的限流；不泄露用户是否存在 |
| ID-03 | 普通用户调用管理员 API | 返回 401/403；无数据变化；记录必要审计 |
| ID-04 | 缺少、错误或跨会话 CSRF 执行写操作 | 请求拒绝；实体版本不变化 |
| ID-05 | 注销当前会话 | 当前 cookie 和 CSRF 立即失效，其他会话按产品语义保留 |
| ID-06 | 注销全部会话 | 该用户已有会话全部失效 |
| ID-07 | Web 轮询期间会话过期 | 清理本地 CSRF，跳转登录，登录后可回原地址 |
| ID-08 | 两个浏览器上下文分别登录不同用户 | cookie、CSRF、目录和实例完全隔离 |
| ID-09 | 游客会话过期 | 自动建立新 guest session；旧 ticket/renewal 不恢复 |

所有接口都检查 HTTP status、业务 code 和数据库副作用，不能只看页面提示。

## 8. 用户组、设备与应用 ACL 用例

### 8.1 可见性矩阵

| 主体 | AppPublic | AppG1 | AppG2 |
| --- | --- | --- | --- |
| Guest1 | 可见/可启动 | 不可见/不可启动 | 不可见/不可启动 |
| User1 / Group1 | 可见/可启动 | 可见/可启动 | 不可见/不可启动 |
| User2 / Group2 | 可见/可启动 | 不可见/不可启动 | 可见/可启动 |
| Admin1 | 管理端可见 | 管理端可见 | 管理端可见 |

对每个“不允许”组合同时调用目录、启动和票据接口。目录不得泄露实体；直接猜 app ID 或 instance ID 时返回 403/404，且不得靠返回差异泄露资源是否存在。

### 8.2 动态变化

1. 将 User1 从 Group1 移除，下一次成功刷新后 AppG1 消失，新启动和新票据均被拒绝。
2. User1 已有实例不因一次前端刷新直接销毁，但不得续签超出新权限的票据；最终生命周期按服务端策略收敛。
3. 再把 User1 加回 Group1，目录恢复；旧 ticket 和 renewal token 不能复活，只能签发新票据。
4. 将 AppG1 改为 public 后游客可见；改回 ACL 后游客下一次刷新不可见。
5. ACL 模式保存时必须至少选择一个有效用户组；不存在、已删除或 version 冲突的组使整个更新失败。
6. 删除用户后，组成员计数和成员列表同步减少，不保留悬空关系。
7. 删除用户组前检查关联应用和设备；执行确认删除后关系清理，应用不得意外转为 public。
8. 创建超过 100 个用户和多个同名片段，验证分页总数、搜索、选择、反选与保存一致。

## 9. 实例、票据与状态机用例

### 9.1 正常生命周期

1. 对同一用户、应用和节点快速重复点击启动，只产生一个有效实例或返回明确的幂等结果。
2. 状态按 `Starting -> Running -> Stopping -> Stopped` 转换，时间戳单调且原因字段正确。
3. 只有 Running 实例可签发连接票据；Stopped、Failed、其他 owner 的实例拒绝。
4. user 实例只接受同一 user owner；guest 实例只接受同一 guest owner，主体类型和 ID 都必须匹配。
5. view-only 票据不包含键鼠、剪贴板、文件、声音控制等交互权限。
6. 停止确认才发命令；取消停止不修改状态、不发停止命令。

### 9.2 票据安全

| 场景 | 预期 |
| --- | --- |
| 正常兑换一次性票据 | 首次成功，建立声明范围内的连接 |
| 同一票据第二次兑换 | 拒绝重放 |
| 篡改主体、设备、实例或权限 | 签名/声明校验失败 |
| 过期票据 | 拒绝，且不延长实例 |
| 使用 User1 票据登录 User2 会话 | 拒绝 |
| 正常 renewal | 返回旋转后的新 ticket 和 renewal |
| 重放旧 renewal | 拒绝 |
| ACL 撤销后 renewal | 拒绝，不恢复旧权限 |

日志和 URL query 中不得出现明文 ticket/renewal；浏览器启动信息只可放在 URL fragment 或安全内存中。

### 9.3 状态恢复与竞态

| 编号 | 故障注入 | 预期 |
| --- | --- | --- |
| ST-01 | 实例 Starting 时重启 Console，Service 实际仍在启动 | 恢复 Starting，后续心跳 Running 后进入 Running |
| ST-02 | 实例 Running 时重启 Console | 恢复 Running，可重新签票，不重复启动 |
| ST-03 | 实例 Stopping 时重启 Console | 保留 Stopping；收到停止心跳后进入 Stopped |
| ST-04 | 启动回执丢失，但心跳先报告 Running | 启动视为成功，不发送补偿 stop |
| ST-05 | 启动回执和 Running 心跳都缺失 | 超时后 Failed，允许安全补偿停止 |
| ST-06 | Service 短暂断线，小于恢复窗口 | 实例保持可恢复状态；重连心跳修正状态 |
| ST-07 | Service 断线且进程缺失超过 15 秒 | Starting -> Failed 或 Running/Stopping -> Stopped，原因 `PROCESS_LOST` |
| ST-08 | 收到过期 request 的迟到回执 | 不覆盖更新后的实例状态，不触发重复补偿 |
| ST-09 | malformed heartbeat 或未知 instance | 拒绝/忽略并记录，不能污染合法实例 |
| ST-10 | stopped heartbeat 重复上报 | 幂等，不重新标为 Running，也不无限续活 |

每个用例同时检查内存状态、API 投影、数据库状态和目标机器进程，四者最终必须一致。

## 10. Web 与真实媒体验收

### 10.1 用户入口

- 左侧“云端应用”作为独立 tab，不与远程设备列表混合。
- 未登录可进入公共应用；登录后按 public 与授权应用的并集展示，不重复。
- 应用卡显示名称、public/专属标识、节点可用性和运行状态。
- 启动失败显示服务端可操作原因，不使用笼统的“Start failed”掩盖离线、无权限或超时。
- 页面刷新、浏览器后退和多个 tab 不产生重复实例。

### 10.2 RTC/Web Client

- 使用当前高版本 Chrome，不为旧浏览器增加降级复杂度。
- Console 用户入口默认签发 view-only；观察者不参与编码器码率、分辨率、帧率等控制反馈。
- 首次画面、本机正常连接、一次 PeerConnection 关闭后的续票与重连均自动验证。
- 测试键鼠、剪贴板、文件和音频控制通道均不可用；视频仍正常播放。
- 连接断开后按配置的 5 秒缓冲窗口处理，短暂切页/重连不得立即结束采集进程。

### 10.3 H.264/H.265

- H.264：Chrome 真实播放必须通过，记录首帧时间、帧率和 5 分钟延迟漂移。
- H.265：验证客户端切换编码后推流链路、ZLMediaKit 流信息和解码端均识别 H.265；若当前 Chrome/系统解码能力不支持该组合，使用项目支持的专用 H.265 客户端验收，不把“浏览器无解码能力”误判为 Console 推流失败。
- 编码切换后不得复用旧 codec 声明；新连接协商、流元数据和实际码流必须一致。
- H.264 与 H.265 都只推主流；无观看者时不采集编码属于预期行为。

## 11. 性能、稳定性与资源泄漏

### 11.1 五分钟媒体基线

分别在本机和远端执行 60 fps、H.264 主流的 5 分钟播放：

- 每秒记录源端时间标记、观看端画面时间、`video.currentTime`、连接状态和缓冲事件。
- 首帧目标为实例 Running 后 5 秒内；一次 RTC 断开后的自动恢复目标为 15 秒内。
- 本机 RTC 稳态端到端延迟以 2 秒内为初始门槛；若机器负载或编码设置使门槛不可达，先形成可重复基线并定位采集、编码、网络、jitter buffer 或解码阶段，禁止用无限缓存掩盖问题。
- 5 分钟内延迟不能单调增长，不应出现无网络丢包却持续转圈；发生卡顿必须关联浏览器统计、Service/Render 日志和 CPU/GPU/网络采样。
- 音频默认静音时，不应因音频轨缺失阻止视频播放。

### 11.2 生命周期压力

- 连续执行 100 次启动、签票、兑换、停止，确认没有活动实例、请求索引或目标进程线性增长。
- 10 个浏览器上下文并发读取 public 目录和签发各自票据，不跨会话串票。
- 对同一实例并发发起 20 次续票，只允许符合旋转规则的一条链成功，不产生重复有效 renewal。
- 记录 Console、Service、Render 的私有工作集、句柄、线程和连接数；结束后回落到基线允许范围。

## 12. 数据库、审计和可观测性

- 用户、组、ACL 和实例写操作带操作者、目标、结果、reason/request ID 与时间。
- 登录失败、权限拒绝、CSRF 拒绝、ticket 重放和异常状态转换可被定位，但不记录秘密值。
- 同一逻辑事件的重试不能制造误导性的多条成功记录；事件开始、刷新、恢复和结束语义一致。
- 实例最终停止后，活动列表消失，历史记录保留必要诊断字段。
- MongoDB 暂时不可用时写操作明确失败，不返回伪成功；恢复后不重复执行未知结果的非幂等操作。
- Redis 清空或重启后已有会话和票据安全失效，用户可重新登录/建立 guest session，旧凭据不可恢复。
- 日志时间统一可关联，远端机器时钟通过 NTP 保持同步。

## 13. 远端双机部署与 NAT 验收

### 13.1 执行前部署清单

远端公网端口范围不能直接等同于内部服务端口。执行前必须维护一张经路由器和 Windows 防火墙实际验证的映射表：

| 用途 | 协议 | 外部端口 | 内部主机/端口 | 验证方式 |
| --- | --- | --- | --- | --- |
| SSH 管理 | TCP | 部署时填写 | 部署时填写 | 公网 SSH 登录 |
| Console HTTPS | TCP | 5200-5210 中已分配端口 | Console HTTPS 端口 | `/health` 200 |
| Relay/Service | TCP | 5200-5210 中已分配端口 | 配置端口 | Service 在线 |
| RTC/媒体 | TCP/UDP | 按实际组件配置 | 按实际组件配置 | candidate/媒体收发 |
| px_media | TCP/UDP | 按实际播放协议配置 | px_media 配置 | API 与实际拉流 |

如果 RTC 需要的 UDP 端口数量超过现有公网范围，必须先调整路由器映射或部署 TURN；不能只开放 Console HTTPS 就宣称双机链路通过。

### 13.2 执行步骤

1. 本机 L0-L2 全绿后生成 Console、Console Web、px_service、px_media 及依赖包。
2. 记录 Git commit、构建时间和每个可执行文件 SHA-256，不直接从开发目录复制未知旧文件。
3. 通过 SSH 上传到新的版本目录，保留上一版目录；敏感配置单独注入。
4. 校验证书、MongoDB/Redis 地址、对外 base URL、Service/Relay 和媒体端口。
5. 启动服务并从公网客户端验证 `/health`、Web 登录和 Service 在线。
6. 运行 Guest1/User1/User2 可见性矩阵，再执行真实启动、view-only 连接、续票和停止。
7. 分别重启 Console、Service 和 px_media，记录恢复时间和实例/媒体影响。
8. 执行 5 分钟 60 fps 稳定性测试，并保留浏览器 WebRTC 统计与两端资源采样。
9. 清理本轮数据和进程，确认上一版仍可通过版本目录回滚。

## 14. 故障取证与结果保存

每次真实链路测试建立独立结果目录，建议命名 `test-results/<日期>-<commit>-<case>`，只保存脱敏材料：

- 测试环境、Git commit、二进制 SHA-256 和配置模板版本。
- 用例开始/结束时间、执行命令、通过/失败及失败步骤。
- Console、Service、Render、px_media 的相关时间窗日志。
- 精确进程路径、PID、命令行和测试前后资源快照。
- Playwright HTML report、失败截图、trace 和浏览器 WebRTC 统计。
- API status、业务 code、request ID；响应中的凭据字段必须删除或掩码。
- NAT 映射验证结果、网络丢包/时延和两台机器时钟偏差。

失败时先保留证据，再清理测试实体。不得为了让用例变绿而延长所有超时；应先区分确定失败、网络波动和状态尚未收敛。

## 15. 清理与回滚

1. 停止本轮创建的全部实例，等待 API 与 Service 心跳均确认最终状态。
2. 仅结束命令行含本轮 instance ID/test prefix 且路径属于测试部署目录的 Render/Client 进程。
3. 按依赖顺序删除本轮应用 ACL 关系、应用、组和用户；旧设备 grant 若参与兼容性测试则单独清理。
4. 验证活动实例、临时 ticket、renewal、guest session 和测试进程数量为 0。
5. 清理终端环境变量和含秘密的临时文件；测试报告仅保留脱敏版本。
6. 部署回滚使用上一版完整目录及其匹配配置，不能混用不同版本 DLL。

## 16. 发布验收门槛

以下条件全部满足才算该功能完成：

- L0 Rust、TypeScript 与 Web 单元测试全部通过。
- Chromium 普通 E2E 全部通过，真实 RTC 续票和 Console 重启恢复各至少连续通过 3 次。
- Guest/User/Group/App 的可见性、启动和签票矩阵无一处策略漂移；无设备 grant 的有效用户可列出全部设备并签发控制/文件票据。
- Starting/Running/Stopping 三种重启场景与启动回执竞态均收敛到正确状态。
- 本机和远端 5 分钟播放无单调延迟增长；关键性能指标有记录。
- 测试结束无活动测试实例、无测试 Render/Client 进程、无悬空组成员或 ACL。
- 日志、URL、报告和 Git 历史中没有密码、secret、cookie、ticket 或 renewal token。
- 远端端口映射、证书和回滚步骤经过实际验证，而不是只完成配置。

## 17. 本轮已建立的回归基线

截至本方案建立时，本轮代码已完成以下本机验证，可作为下一轮测试的起点：

- Console Rust 全量测试：126 个通过。
- px_service 测试：37 个通过。
- Console Web Vitest：13 个通过。
- Chromium 常规 E2E：8 个通过；真实链路用例在普通运行中按设计跳过。
- 真实 Web Client 续票与 PeerConnection 重连：通过。
- Console 重启后运行实例恢复、重新签发 view-only 票据和最终进程清理：通过。
- Console Web 类型检查、构建，以及 Console release 构建部署：通过。

这些结果只证明对应 commit 的本机基线，不替代第 13 节远端双机/NAT 验收。后续每次发布必须在测试记录中重新写入实际结果，不能只引用本节。

## 18. 测试记录模板

```text
测试日期：
测试人员：
Git commit：
Console / Service / Render / px_media SHA-256：
环境：L0 / L1 / L2 / L3 / L4
用例范围：
结果：PASS / FAIL / BLOCKED
首帧时间：
5 分钟延迟范围：
重连恢复时间：
遗留实例/进程：
证据目录：
失败摘要与 request ID：
清理确认：
```
