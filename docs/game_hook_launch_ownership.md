# Game Hook：仅允许本产品启动的 App

用户决定（2026-09-09）：不考虑 Steam App；只处理本产品启动的 App，不接管用户手动运行的游戏。

## 准入规则

Hook 前必须同时满足：**本次启动的私有 Windows Job 成员 AND 完整 exe 路径匹配**。

- 根进程以 `CREATE_SUSPENDED` 创建，成功加入私有、不可继承句柄的 Job 后才恢复执行。
  Job 不允许子进程主动 breakaway；没有“把现有进程加入 Job”的产品入口。
- 普通 App 只允许根进程；配置 `game_view_path` 的启动器场景允许该 Job 中完整路径匹配的子进程。
- 发现候选、实际注入、Hook bootstrap 和看门狗恢复都重新检查归属；注入期间保留目标进程句柄，防止 PID 复用。
- 同名、同路径、用户手动重启的外部进程均不接管。关闭与恢复只操作本次拥有的 Job，不按进程名或历史 PID 杀进程。
- 不走 Steam URL / ShellExecute 启动和扫描接管分支。外部 broker 代启动但不属于该 Job 的进程也不接受。
- 原 game/webview 用户环境不增加 RDP 登录前置条件；RDP 的账号、会话保留策略不变。

## Windows 路径与参数

- 完整路径保留内部空格和 Unicode，接受一对外层双引号、`/` 与 `\`、Windows 大小写差异及普通 `.` / `..`。
- 支持常见 `\\?\C:\...`、`\\?\UNC\server\share\...` 前缀；拒绝相对路径、URL、未配对引号、NUL 和设备路径。
- `lpApplicationName` 单独传入完整 exe，命令行中的 exe 另加引号；参数尾串原样传入，不按空格拆分后重拼。
- 比较实际进程句柄查询到的完整路径，不依赖进程快照的文件名。短文件名、符号链接/目录联接别名没有自动视为同一路径；
  不确定时拒绝，配置应使用实际完整路径。参数内部需要的 Windows 转义仍由配置方正确提供。

## 验证（2026-09-09）

本机 `game_process_identity` 7 项、`game_owned_process` 5 项通过，CTest 总耗时 0.93 秒。
覆盖同路径独立实例拒绝、错误路径拒绝、根/子进程准入差异、带空格目录和 exe 名启动、停止互不影响、
子进程清理、排队 weak 回调在销毁后失效、回调内重复停止及重复启动/销毁。测试进程不注入 DLL，最长自行存活 20 秒。

```powershell
./scripts_build/build_cpp_tests.bat test_game_process_identity test_game_owned_process
ctest --test-dir build_official -R '^game_(process_identity|owned_process)$' --output-on-failure
./scripts_build/build_cpp_render.bat 8
```

实机测试使用 Console 签票后通过参数直接启动 Client，不再点击 Panel 发起连接。
测试收尾核对数据库的 exe、参数、App 类型、节点端口与实际启动配置；不通过修改数据库迁就测试。
本页的本机测试不等同于远端 Hook 图像、音频和完整生命周期均已验收。

### 90 短时实测与部署

- 增量 Render 构建成功，构建目录、`build_official/dist`、90 安装目录 SHA-256 均为
  `FC2C9DF69199505D20467A6A643D66BF3DF1F4AF813F5876FD5074A08022BE3A`。
  90 旧版可恢复备份：`C:\Program Files\PixelsRender\hook-owned-backup-20260909050742`；本轮远端仅替换 Render。
- 参数直启测试 `app-cli-20260909050844`，实例 `inst-120-47cc5f2c`，脚本总计 65.53 秒。
  13:08:50 日志记录 `game.launch` PID 24904，路径 `G:\app\2dadventure\2dAdventure.exe`；
  同 PID 的 `game.hook_gate` 明确为 `ownership=private_job path=matched`，注入成功，IPC 准入 PID 也为 24904。
  已查看 Client 截图，确认收到游戏画面；画面本身不单独作为进程归属证据。
- **用户确认 Client 是自己手动退出的。** 脚本观察到退出码 1、未强制终止，不能据此认定 Client 崩溃或异常退出，
  也不能把这次当作脚本驱动的自动关闭验证。
- Render 日志 13:09:16 收到客户端断开，13:09:35 因启动宽限期结束且无客户端退出。
  收尾确认测试 Render PID 14100、游戏 PID 24904 均不在；用户手动启动的
  `D:\software\2dadventure\2dAdventure.exe` PID 6376 仍在，Windows 会话未注销。
- Console 状态同步仍需单独核查：脚本轮询看到 `running`，停止 API 返回 409；随后只读数据库确认实例为 `failed`。
  这不是 Client 崩溃证据，但不能将本次实例状态收尾标为通过。
- 收尾再次只读核对数据库：App 为 `game-hook`，上述 G 盘 exe、空参数、60 FPS，节点设备 `001190520`、端口 32012；
  配置未修改。C++ 所有权与 150 列质量检查通过。

## 旧实现归档

移除接管分支前的完整工作区文件保存在 `backup/game_hook_owned_process_20260909/`。
manifest 记录原路径、工作区修改状态、SHA-256、基准版本 `3e38249f9332572502cd341ebed9860b81ebac82`。
归档仅供参考，不参与构建、打包或运行。

## 自然退出与重复停止修复

实测旧实例 `inst-120-47cc5f2c` 的数据库 `error` 为
`instance inst-120-47cc5f2c already stopped/failed`，不是 Client 崩溃原因。
流程为：用户关闭 Client → Render 在无客户端时退出 → Service 进程刷新记为 `Stopped` →
Console 尚处于心跳缺失确认窗口 → 测试脚本发起收尾停止 → Service 的旧 `begin_stop` 拒绝终态 →
Console 把失败回执写成 `Failed` 并返回 HTTP 409。

90 的 `pixels_service.log` 对应证据（UTC）：`05:09:38.250193Z` 记录
`reap dead app instance inst-120-47cc5f2c ... pid=Some(14100) gone`，
`05:09:52.402698Z` 才出现重复停止的 `already stopped/failed` 错误。

修复规则：

- Service 对已经 `Stopped` 的实例立即成功确认，不查询旧 PID/端口、不再次发送停止消息，也不操作替换进程。
- Console 对已经 `Stopped` 的实例直接返回当前快照，不改变版本和停止时间，不要求 Service 在线。
- 真正的 `Failed` 仍按失败处理，不通过匹配“already stopped/failed”错误字符串把所有失败掩盖为成功。
- 移除 Service 并发启动失败和停止收尾中的游戏/view 路径补杀分支。正常停止只使用记录的 Render PID，
  同时核对 Render 完整 exe、App 模式和端口；不按端口接管另一个 Render。游戏子进程由该 Render 的私有 Job 收尾。
- 不改写既有失败历史，也不修改 Windows 账号、会话或 App 启动配置。

定向回归入口：`scripts_build/test_app_lifecycle_rust.bat`。
2026-09-09 定向结果：Service Host 28 项、Console 应用调度/凭据 40 项，共 68 项通过，实际测试执行合计 3.96 秒。
测试程序增量编译分别为 1 分 10 秒与 2 分 56 秒；不包含真实 RDP 登录、游戏注入或长时间压力测试。
旧 Service/Console 完整工作区文件及原版本、修改状态、哈希保存在 `backup/app_stop_idempotency_20260909/`。

### 修复后的部署与 console 会话复测

- Service：`4331120D73376F7CE1138A35E5EDA2CDA19054934AF4DE39B9CC7DA3C140D3DC`，
  Rust 构建、dist、90 安装文件一致；远端备份 `C:\Program Files\PixelsRender\rdp-service-backup-20260909052315.exe`。
- Console：`030A79A654BB17981ED11260DAC876BA3110DD2F4275E15F4437EB45FB63BF3E`，
  Rust 构建与 `output/px_console` 一致；备份 `output/px_console/rdp/console-runtime-backup-20260909052819.exe`。
  只替换 exe，既有 workspace master key 哈希保持不变，不重新生成证书、密钥或配置。
- `inst-109-9288f783`（26.26 秒）发生断线，不能计为画面通过；同一时段 Service 收到
  `ConsoleDisconnect(3)`、`ConsoleConnect(1)`、`SessionUnlock(1)`。没有新 Render 崩溃事件，
  只能确认时间重合，不能仅凭此断言会话切换就是退出根因。
- 用户确认已到 console 后，`inst-115-5dc0a7e7`（93.80 秒）在 Session 1 持续运行，
  本次游戏 PID 30628，稳定阶段编码输出约 59.96–60.17 FPS。旧截图逻辑抓到 160×28 小窗口，
  该截图无效，不作为黑屏或出图证据。
- 测试截图脚本改为仅在目标 Client PID 中选取最大可见窗口，校验实际尺寸并拒绝过小图像。
  `inst-118-d7c2f28d`（67.55 秒）取得 1382×807、`HUNG=False` 的截图，已人工查看游戏画面；
  本次启动、Hook AND 门禁与 IPC 均对应 PID 4904。
  `inst-121-f5459d90`（68.82 秒）再次得到同尺寸正常游戏画面。
- 上述新实例重复停止均返回 `stopped`，未再发生 409 或误标 `failed`。数据库最终 PID 为 0；
  `error=PROCESS_LOST` 是现有心跳缺失确认路径的标记，**不代表已经实现退出原因精确上报**。
- 最终只读核对：测试 Client、App Render 和 G 盘测试游戏均已退出；用户原有 D 盘游戏 PID 6376、
  桌面 Render PID 1228、Windows Session 1/2 保留。App 路径、空参数、60 FPS、设备和端口均未修改。

### 自动关闭验证的限制

本轮自动关闭测试发生 15 秒等待后强制收尾，不能标为正常 UI 退出通过。
源码 `BaseWorkspace::closeEvent` 会忽略直接关闭并调用带确认框的 `ExitClientWithDialog`；
测试脚本尚未成功通过 UI Automation 找到并确认该对话框，因此这不是客户端卡死的证据。
用户先前手动退出的事实保持不变，与本轮脚本强制收尾分开记录。
退出确认成功后的既有 `BaseWorkspace::Exit` 最终会调用 `ProcessUtil::KillProcess`，
不能只把非零进程退出码当成用户退出异常的充分证据。

## 退出原因观察链路（2026-09-09 下午）

新增 `application_exit_status.h` 的稳定退出码：最后客户端离开后宽限退出为 `0x47520001`，
Game 的 45 秒启动宽限结束且没有客户端为 `0x47520002`。不调整原有定时长度、重连取消或多客户端保护。
Game 继续使用原有立即结束 Render / 私有 Job 收尾语义，RDP 保留有序关闭传输、不注销 Windows 会话的语义。
普通桌面 Render 的长期重启看门狗保持原状；WebView 原有暂停行为不改成退出。

Service 在启动进程被识别后打开并持有路径校验通过的 Windows 进程观察句柄。
句柄绑定这一代进程，即使 PID 后续被复用，仍从原内核对象读取退出码；WMI 暂时漏报但句柄仍存活时不误报退出。
观察句柄绑定启动 request_id，终态证据随既有 600 秒 finished-record TTL 保留并通过实例心跳重报。
未能取得句柄、Service 重启后丢失观察或查询失败仍按未知处理，不伪造正常退出证据。

Console 持久化 `stop_reason` / `exit_code`，终态心跳须匹配设备、实例和非空启动 request_id，原因与退出码组合也必须有效。
重复终态上报不修改版本/停止时间，已确认退出的同代实例不被迟到的活跃心跳重新激活。
显式停止沿用自己的请求/回执，已 Stopped 的重复停止保持幂等。

| 情况 | stop_reason | 状态 / error |
|---|---|---|
| 5 秒无客户端宽限退出 | `no_clients` | stopped / 空 |
| 45 秒启动宽限内已无客户端 | `startup_idle` | stopped / 空 |
| 其他退出码 0 | `clean_exit` | stopped / 空；不推断成无客户端 |
| 其他已观察到的退出码 | `abnormal_exit` | failed / `RENDER_EXIT_XXXXXXXX` |
| 没有可靠退出码 | `process_lost` | stopped / `PROCESS_LOST` |
| 显式停止成功 | `requested_stop` 或 `already_absent` | stopped |

本次定向 Rust 回归：Core 24、Windows 进程 3（含子进程测试入口）、Service Host 29、Console 41，共 97 项通过。
真实内核句柄测试使用自身测试进程，验证存活、路径拒绝、退出后原 Child 句柄释放仍可重复读取退出码，不操作产品进程。
最初使用交互 cmd 的测试夹具退出码不符合预期，已换为可控测试子进程；没有放宽退出码断言。
Core 固定端口测试串行执行避免组内并发占用，未通过杀进程释放端口。
Game 两组 CTest 再次通过（1.37 秒），C++ 所有权/150 列检查及定向 diff 空白检查通过。

自动关闭脚本改为在本次 Client PID 内按退出提示正文定位对话框，再调用唯一确认按钮；
不用窗口标题猜测，不操作其他进程窗口。`inst-124-3765d40d` 在旧运行时上首次验证确认成功、
`FORCED_EXIT=False`、Client 退出码 1，总计 57.72 秒；该轮自然终态尚未在轮询预算内到达，已用 API 幂等收尾。

原工作区实现完整归档在 `backup/app_exit_observation_20260909/`，包含基准版本、原路径、修改状态和哈希。

### 新版部署与实机证据

| 产物 | SHA-256 / 已核对位置 |
|---|---|
| Render | `53DB9EAA5D40BCB0EE7FFFA0D4C2B0FB6F76A291DFA37B9EA99559EF79426385`；构建、dist、90 |
| Service | `5C31E69CE844693D07390D2B4B1A16EBF27EEED81F631DC4CA5B4D12076D49DF`；Rust 构建、dist、90 |
| Console | `EA305D253B7A7CB02A13B06FC714F01BEC08EC0B65A986446E29B04558BB803B`；Rust 构建、output/px_console |

远端旧 Render 备份 `C:\Program Files\PixelsRender\rdp\px_render-before-20260909060849.exe`，
旧 Service 备份 `C:\Program Files\PixelsRender\rdp-service-backup-20260909061123.exe`，
旧 Console 备份 `output/px_console/rdp/console-runtime-backup-20260909061203.exe`。
仅替换本轮修改的运行文件；Client 无源代码变化，仍从 dist 使用已有版本。Console master key 哈希保持不变。
Console 刚重启后的一次探测在创建实例前因 30500 未监听失败，随后已确认服务子进程恢复监听，再开始有效测试。

`inst-109-9fcd6321`，`app-cli-20260909061338`，观察 50 秒、整轮 67.22 秒：
Client 1382×807、`HUNG=False`，已查看截图确认游戏画面；本次 G 盘游戏 PID 19376、Render PID 24656。
稳定日志采样输出约 59.97 FPS；这不是 RDP 的 60 FPS 验收，也不代表已完成音频听测。
自动确认关闭后 `FORCED_EXIT=False`，实例自然变为 stopped，重复停止返回 stopped。
只读 Mongo 确认 `pid=0, error="", stop_reason="no_clients", exit_code=1196556289 (0x47520001)`。
公共实例列表当前只返回摘要，因此退出原因以管理端持久记录/Service 日志核对，不能把摘要中未提供的字段当作空原因。

`inst-112-1892a6b4`，`app-cli-20260909061544`，观察 15 秒、整轮 49.63 秒：
再次查看 1382×807 游戏画面，`HUNG=False`；本次游戏 PID 34484 的启动和 Hook 日志均为私有 Job / 路径匹配。
确认退出后未强杀 Client，Render 在启动宽限期结束后自行退出，Service 在 UTC 06:16:36.086485 观察到
Render PID 12464 的退出码 1196556290。Mongo 最终为 `stopped, pid=0, error="", stop_reason="startup_idle"`，
重复停止成功，证明另一条自然退出分支也没有误记 PROCESS_LOST。

`inst-115-7d8ab724` 单独验证显式停止，整轮 4 秒：API 启动后直接停止，再次停止均返回 stopped；
Mongo 为 `pid=0, error="", stop_reason="requested_stop", exit_code=null`。
没有制造远端真实崩溃；异常退出、未知丢失及迟到消息的分类边界由本轮定向单元测试验证。

最终只读核对：本次 Client PID 27208/39460 均退出，90 没有 App Render 或 G 盘测试游戏残留；
用户原有 D 盘游戏 PID 6376 不变，桌面 Render 随部署重启后为 PID 5232，Service 为 Running。
Windows Session 1 console 活跃、Session 2 断开均保留，没有登录管理员 RDP、注销会话或删除账号/profile。
数据库仍为 `game-hook`、`G:\app\2dadventure\2dAdventure.exe`、空参数、60 FPS，设备 `001190520`、端口 32012。

本轮完成的是 Game 严格归属回归、自动确认退出与退出原因链路；不等同于 RDP/WebView、剪贴板、文件、
音频、输入、DPI、多会话和权限故障矩阵全部完成。相关未完成门禁继续见 RDP 实施进度，不标记整体计划完成。
