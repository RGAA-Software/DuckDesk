# RDP 模式实现与验收进度（2026-09-08）

本记录是实施检查点，不是功能全部通过或发布验收报告。
产品约束以 [实施计划](rdp_application_mode_implementation_plan.md) 为准；测试每批含收尾不超过 10 分钟。

## 2026-09-09 上午继续实施（最新，持续补充）

### 晚间收尾：WebView 修复与后续入口

光标经用户手动验收正常；CEF 标准光标接收与宿主光标来源隔离均已修复，逐条诊断日志收尾移除。
文本复制粘贴已补 WebView 路由、CEF 编辑操作及 Console control 票据的 clipboard 权限，
最终实测英文复制与中文双向回读通过，100.60 秒含精确实例收尾。
详见 [光标修复](webview_cursor_fix_20260909.md)、[文本剪贴板修复](webview_clipboard_fix_20260909.md)。
下方早期交互失败是历史检查点，不代表当前部署结论。
Qt/Web 统一本机输入法面板尚未实施，入口为
[跨客户端文本输入计划](application_text_input_design_20260909.md)。

### 17:21 用户操作干扰提示后的 WebView 复测

用户说明此前也在操作本机，旧样本不能单独归因产品。请求短暂不操作后，
`inst-195-60bcedcf` 70.99 秒重测仍出现复制/中文粘贴断言 False，未触发前台丢失检查。
客户端已关闭并通知用户恢复操作，实例最终 stopped / error 空；具体根因仍需区分模拟输入与产品链路。
见 [WebView 复测补充](webview_acceptance_20260909.md)。

### 17:06 WebView 回归发现交互阻断

既有百度 WebView 页面、点击与普通文字输入成功，但 Ctrl+C / Ctrl+V 双向文本断言失败；
更换为显式虚拟键事件仍未通过，Enter 也没有达到预期页面行为。尚未完成精确根因定位。
滚动没有可验证结果，同实例重连和动态 WebGL 暂未扩展，不能标记 WebView 全面通过。
四批最长 102 秒，均已显式停止及重复停止验证，数据库原 URL/30 FPS 保持，Windows 会话保留。
详细失败样本、测试助手限制和下一步日志入口见 [WebView 回归记录](webview_acceptance_20260909.md)。

### 16:53 RDP 文件传输补测

双向文件/目录、中文与空格路径、空文件和 SHA-256 校验通过；同名目录合并提示取消后原测试内容保留。
32 MiB 测试文件在接收 64 KiB 后替换剪贴板取消，staging 自动移除且新文本不被迟到结果覆盖。
首次取消批次因测试助手恢复剪贴板错误未算完整通过；修正收尾后 33 秒复验通过。
两个相关 CTest 0.98 秒通过；测试数据已精确清理，会话保留。网络中断文件传输仍未实测。
详见 [文件验收记录](rdp_file_acceptance_20260909.md)。

### 16:33 本地配置恢复后的 RDP 交互验收

键盘/远端身份、中文 emoji 文本双向剪贴板、HTML/图片双向剪贴板、合成音输出、
1024×640 与 1920×1080 远端分辨率、全屏/最小化恢复通过。专用动态窗口确认鼠标点击、滚轮、按键各 1 次；
色块/中文/运动画面已目视检查。活跃阶段 presentation 约 44–49 FPS，**没有 60 FPS 达标结论**。
成功批次分别 40、102、49 秒，已知解码错误 0；初始两个焦点脚本失败样本未计入通过。
测试实例及三个远端 fixture 已收尾，会话保留；完整索引与未测范围见
[交互验收记录](rdp_interaction_acceptance_20260909.md)。文件传输、WebView 和 P6 外设仍需后续分项。

### 15:51 A 恢复，双账号本地配置隔离通过

A 原容器保全后，仅在另建工作副本上正常挂载/卸载，再只读迁回 1591 个普通文件，逐文件哈希通过。
原容器和第一份备份不变。A 登录为 Regular / Session 3；B 原 Session 2 不变，两者 Status=0。
`inst-147-5a4de07b` 的 GraceReconnect 28 秒通过，解码错误 0、最终 stopped。
另补 A/B 双向文件 ACL 和代理私钥隔离，约 8 秒全部通过，临时文件已精确清理。
A/B 数据库节点端口仍为 32014/32016，未修改启动参数；完整细节见 [恢复记录](rdp_profile_fslogix_diagnosis_20260909.md)。
下方“A 尚未迁移”是此前检查点，不再代表最新状态；全量交互/外设验收仍未完成。

### 15:42 用户重启后：B 本地配置恢复通过

FSLogix 服务/驱动已移除。只读挂载 B 原 VHDX 的备份，将 1650 个普通文件迁回原本地目录，
逐文件哈希通过，保留原 SID 并修复其 ProfileList 映射；原容器与副本哈希不变、均已卸载。
Windows 15:42:18 登录日志明确为 Regular，不再 TEMP。
`inst-145-235f504b` 的 GraceReconnect 31 秒通过：出帧、正常关闭、宽限重连、Render 自然退出，解码错误 0；
数据库 stopped / error 空 / exit_code 1196556289，会话保留。
重启前 TEMP 已被 Windows 清除，本次不声称恢复其新增内容；A 尚未迁移，暂不启动 A。
完整保全/迁移及验收边界见 [恢复记录](rdp_profile_fslogix_diagnosis_20260909.md)。

### 15:13 用户要求卸载 FSLogix

90 已通过微软安装器成功卸载 FSLogix Apps / Cloud Caching，禁止自动重启。
安装项、服务与驱动注册项验证均为 0；日志仍明确要求重启，**未执行重启**。
A/B 原 VHDX 与 metadata 哈希不变，现有会话与手动游戏保留；B 的 TEMP 尚未迁移修复。
已保留安装程序和相关注册表备份，详细范围与后续数据保全要求见
[卸载记录](rdp_profile_fslogix_diagnosis_20260909.md#用户授权卸载2026-09-09-1513)。

### 下午只读诊断：B 的 TEMP 与 FSLogix 停服

已确认 B 原 profile 来自 FSLogix VHDX：10:51 挂载成功，11:23:32 `frxsvc.exe` 崩溃停服，
11:38 Windows 改用 TEMP。原 260 MiB 容器仍存在但未挂载，**不能将空用户目录认定为数据已删除**。
本轮未注销、挂载、修改注册表或启动服务；恢复涉及机器级 FSLogix，需单独授权。
完整时间线、证据和先保全再恢复方案见 [FSLogix 只读诊断](rdp_profile_fslogix_diagnosis_20260909.md)。

### 下午续测：WebView 出图、RDP 退出崩溃定位

- WebView `rtc-accept-webview-90` 使用原百度 URL、30 FPS、端口 32010，参数直接启动 dist Client。
  `inst-119-a65c1218`，57.33 秒，已查看 1382×807 网页截图；自动确认关闭、无强杀 Client。
  WebView 断开后保持运行符合当前暂停语义，本轮使用显式停止和重复停止收尾，数据库错误为空、`requested_stop`。
  未将这个样本计作键盘/鼠标、剪贴板或动态 WebGL 验收。
- B 工作区 `app-105-797a8ee1` 重连原 Session 2。`GraceReconnect` 的 `inst-123-08f2949a`（65 秒）
  两次均出帧、解码错误 0；首次已查看桌面，但“无法登录到你的帐户”的历史临时 profile 提示仍在。
  该结果仅证明连接恢复，**其退出阶段随后发现异常，不算完整生命周期通过**。
- `GraceExit` 的 `inst-126-77ccd1d0`（63 秒）客户端正常关闭，Render 却以 `0xC0000409` 退出；
  新退出上报链路正确持久化 `failed / abnormal_exit / RENDER_EXIT_C0000409`。
  `RevokeSession` 的 `inst-128-086a74f6`（37 秒）在撤销本次 guest 后 2.79 秒断开，解码错误 0；
  仅证明实时授权撤销，不掩盖同样存在的 Render 退出问题。
- 暂停扩展测试，对精确匹配实例/PID/完整路径的测试 Render 挂接调试器，没有挂接桌面 Render 或用户游戏。
  调试使用独立暂存目录，不配置系统全局崩溃策略。`inst-136-36045c8c` 取得小型异常转储，
  SHA-256 `26793D8F7FE3ED86A9C424BDEE0B623977A2A0C037CBFC78F8B6A5C71B87AABB`，
  仅在忽略的本机私有测试目录保留，不提交含内存的 dump。
- 使用同版 Render PDB 解析后，调用栈为 `asio2 timer handler → timer destructor → iopool destructor →
  vector<thread> destructor → std::terminate → abort`，FAST_FAIL 子码为 7。
  **不是仅凭 0xC0000409 认定栈越界**：此处明确是可 join 线程在自身线程中析构导致的主动终止。
- `RdContext` 原先仅 `stop_all_timers()` 后释放共享 timer；取消是异步的，最后一个引用可能在 timer 线程释放。
  改为保有 owner 时调用 `stop()`，取消、排空并 join 线程池后才 reset。AppTimer 已有 stop/join，不改变其逻辑。
  旧工作区源文件归档于 `backup/rdp_exit_timer_join_20260909/`。新增直接使用真实 RdContext 的销毁测试。
- 截图工具原先使用屏幕矩形复制，发现本机其他窗口遮挡可污染截图；该无效图片已删除，未计入结果。
  现改为 `PrintWindow` 仅捕获目标 Client HWND，没有桌面截屏回退；已取得可见 RDP 内容。
  截图本身仍不能替代账号身份、会话持久性或交互断言。

#### 14:49 修复后复验与收尾

真实 `RdContext` 新增两项测试，各重复 20 次：挂起长延时任务后销毁、UI 回调释放最后一个外部 context owner。
两项通过；连同 RDP 协议/显示/解码/剪贴板/路由关闭/UI 队列共 11 组 CTest 全部通过，9.12 秒。
C++ 所有权与 150 列检查、新增测试 clang-format、PowerShell 语法检查和定向 diff 检查通过。

仅增量构建 Render，构建树、`build_official/dist`、90 安装副本 SHA-256 一致：
`25FE6C089ECA74E1EFFEC1B10602E4752373C36C97B1560AB5575A179D2EDED7`。
90 旧版备份 `C:\Program Files\PixelsRender\rdp\px_render-before-20260909064628.exe`。
本轮没有修改 Client、Service、Console 或 FreeRDP 运行文件、证书、凭证密钥或数据库启动参数。

| 修复后实机样本 | 时长 | 结果 |
|---|---|---|
| `inst-138-23a778ca` GraceExit | 23 秒 | 出帧，Client 正常关闭，Render 自然退出，解码错误 0 |
| `inst-140-fb24965d` GraceReconnect | 40 秒 | 同一实例内重连成功，最后关闭后自然退出，解码错误 0 |
| `inst-142-a917863c` WrongCertificate | 4 秒 | 错误代理 pin 被拒绝、无首帧，正常显式收尾 |

前两实例的 Mongo 最终均为 `stopped, pid=0, error="", stop_reason="no_clients", exit_code=1196556289`。
Service 分别观察到 Render PID 34336、35004 的正常原因码，没有再出现 `RENDER_EXIT_C0000409`。
重连样本在 14:48:01.347 第一次断开后恢复，最后 14:48:17.160 断开，14:48:22.160 才触发原 5 秒宽限退出。
WrongCertificate 最终为 stopped / requested_stop / 空错误，服务器实际证书未修改。
测试输出中的 `ERRCONNECT_CONNECT_CANCELLED` 出现在主动关闭后的 stderr，不是本轮认证失败证据。

验收脚本现在输出非敏感实例 ID / Client PID，并在观察到 failed 时立即停止空等。
`GraceReconnect` 的通过条件也加强为最后一个客户端关闭后必须观察到自然 stopped，
不能靠显式收尾 API 掩盖退出异常。协议回调仍不自动升级为画面、输入或音频听测通过。

最终：本轮 Client、App Render、代理和调试器均已退出；用户游戏 PID 6376 不变。
桌面 Render 随发布重启后为 PID 35272，Service Running；Administrator Session 1 console、B Session 2 Disconnected，
原登录时间保留。远端临时调试工具及 dump 已清理（工具可从本机 SDK 重新复制），本机私有目录保留小型诊断 dump。
最近 8 分钟未查询到新增 DWM Event 1000，只代表该观察窗口，不证明历史 DWM 问题根治。

**剩余限制没有消失**：B 的原 profile Status 8、未加载，TEMP profile Status 1、已加载；
本轮未注销、删改 profile 或注册表，因此工作区持久化仍不能通过。完整 WebView 输入/动态页面、
RDP 音频听测/外围功能/多工作区隔离矩阵仍需继续，不将整个开发计划标记完成。

### 14:18：正常退出原因上报与自动关闭验证

Render 区分 5 秒无客户端宽限退出和 Game 45 秒启动宽限退出；Service 通过持有的 Windows 进程句柄读取真实退出码，
Console 按设备/实例/启动请求校验终态并持久化原因。显式停止、已观察异常退出、无法确认的 PROCESS_LOST 分开处理。
未改宽限长度、Game 私有 Job AND 路径准入、WebView 暂停行为或 RDP 保留 Windows 会话的原则。

97 项定向 Rust 回归及两组 Game CTest 通过，C++ 所有权/格式检查通过。Render/Service 已同步 dist 和 90，
Console 已同步运行目录，相关 SHA-256 一致并保存旧运行文件备份，证书、密钥及数据库启动配置未修改。
Client 仍从 dist 参数启动；退出时在本次 PID 内识别提示正文并确认，无需强制结束。
新版实机三批分别 67.22、49.63、4 秒，数据库确认 `no_clients`、`startup_idle`、`requested_stop`，
均 stopped、错误为空、重复停止成功；两次游戏画面已查看，稳定阶段编码约 60 FPS。
测试 App 进程已收尾，原有游戏 PID 6376、Windows Session 1/2 保留。

详细实例、证据、哈希和限制见 [Game Hook 退出原因验证](game_hook_launch_ownership.md#退出原因观察链路2026-09-09-下午)。
本轮不冒充 RDP 全功能验收；RDP/WebView 的剩余功能与故障矩阵仍需继续完成。

### 13:40：console 会话复测与重复停止修复

Service/Console 已修复“自然退出后再次停止误标 failed”：已 Stopped 的实例幂等返回成功，真正 Failed 不被掩盖。
Service 同时移除游戏/view 按完整路径补杀和停止时按端口接管 Render 的兜底。68 项定向 Rust 回归通过，
Service 的构建/dist/90 与 Console 的构建/运行目录分别完成 SHA-256 校验，已有可恢复备份，未改证书、密钥和启动配置。

用户切回 console 后，短测编码输出约 60 FPS；修正截图窗口选择后，两次查看到正常游戏画面。
新实例重复停止均成功返回 stopped，测试 App 进程全部清理，用户原有游戏 PID 6376 及 Windows 会话保留。
自动 UI 退出仍不能计为通过：Client 有退出确认框，脚本未成功确认，最终采用强制收尾；这不是已证实的 Client 卡死。
完整时间线、运行哈希、实际测试时长及限制见 [Game Hook 验证记录](game_hook_launch_ownership.md)。

### 13:08：仅本产品启动的 App 才允许 Hook

用户明确不考虑 Steam App，Hook 改为“本次私有 Job 归属 AND 完整 exe 路径匹配”，不接管外部同路径进程。
本机 12 项测试通过，Render 增量构建、dist 发布及 90 同步哈希验证完成；实机 PID 24904 的启动、Hook 和 IPC 日志一致，
Client 已出图，测试游戏退出后用户原有 PID 6376 保留。详见 [实现、路径规则及验收证据](game_hook_launch_ownership.md)。

本轮 Client 由用户手动退出，退出码 1 不能作为崩溃或异常退出结论。当时的独立待查项为 Console 停止 API 409 及实例最终 `failed`（现已定位并修复，见上），
不是 Hook 归属失败，也未将整轮生命周期验收标为通过。没有注销 Windows 会话、修改启动配置或修复 profile。

### 11:35 起：90 恢复后部署、Panel 续票恢复及 game 回归

- 90 记录的启动时间为 11:23:05。恢复检查时仅 Administrator Session 1 活动，重启前的
  A/B 会话不在；本轮没有执行重启、注销或 profile 修复。原测试用户 SID 仍保留。
- 将已完成的 Render 改动部署到 90；逐项验证 exe、DLL、FreeRDP SDK/补丁与运行文件哈希。
  可恢复备份：`C:\Program Files\PixelsRender\rdp-upgrade-backup-20260909033555`。
- Panel 恢复能力已实现：同一 Panel 内保留原 app/instance/logical session/nonce/stream 绑定，
  仅在 Client 关闭后的 5 秒内允许一次续票尝试，不自动重连、不放宽工作区独占。
  续票不返回 Windows 密码，复用该身份绑定的内存 SecretBuffer；关闭超时、登录身份变化或
  恢复消费后清除缓存，不写磁盘。旧进程回调不能删除替换进程或延长旧恢复期限。
- Console 应用目录增加 `app_type`，Panel 对 RDP 无可用恢复凭据的再次访问走既有幂等启动，
  不继续使用卡片中已经结束的实例 ID。game-hook/webview 不增加 RDP 或创建用户的前置条件。
- 实机 `panel-launch-20260909033841`：从 Panel 启动 B，观察 15 秒后关闭，再次点击，
  0.97 秒创建替换 Client；日志明确进入 `rdp.panel.recovery outcome=renewing`。
  Render 的两次准入使用同一脱敏 stream `37215bbf`，11:39:01.480 断开，
  11:39:02.974 重新准入；最终 11:39:18.577 断开，11:39:23.578 按原 5 秒宽限退出。
  两张截图均已查看，确实出现桌面；B 的 Session 2 最终保留为 Disconnected。
- **不能据此将工作区持久化验收标为通过**：桌面提示“无法登录到你的帐户”。只读检查确认
  B 原 profile 状态 8、未加载，SID `.bak` 仍指向原目录；当前加载的
  `C:\Users\TEMP.WIN-RASS8RC6V3H.000` 状态 1。原目录存在，但 `NTUSER.DAT` 不存在。
  11:38:47 的 User Profiles Service 1515/1511 记录备份及临时 profile 登录。
  其他账号也有历史 TEMP 目录，不能单凭此认定本次代码造成损坏。未改注册表、删除目录或注销 B，
  profile 修复必须经单独授权。此次短观察窗口未发现新增 DWM 1000，不代表旧 DWM 根因已修复。
- game 完整路径匹配回归 `panel-launch-20260909034030`：配置的
  `G:\app\2dadventure\2dAdventure.exe` PID 30988 被正确注入；
  `D:\software\2dadventure\2dAdventure.exe` PID 30444 保持不动，测试结束后仍在。
  但 Client 尚无游戏画面：Hook IPC 的 `display_name_` 为空，帧处理器初始化拒绝，随后
  `PROCESSOR_CARRIER_NOT_FOUND`。继续修复为在 game IPC 接入点赋予稳定虚拟显示 ID，
  保留已有非空 ID，不放开通用帧处理器的空 ID 检查。
  `panel-launch-20260909034707` 仍未出画面：实际 `ConfigureIpcMediaIngress` 直接调用
  `OnCapturedVideoFrame`，第一次改动所在的旧 IPC 方法没有被调用。随后将真实 typed weak
  IPC 回调接入 `OnIpcVideoFrame(const CaptureVideoFrame&)`，再进入既有采集/缓存/编码链路；
  最终实机结果待补充，不能将前两次仅启动 Client 的样本计为游戏画面通过。
- Panel 恢复/取消/绑定与缓存生命周期测试、game 路径比较测试已通过；
  真实 UI 探针修正高 DPI 坐标转换，点击失败不计作产品连接失败。
  RDP 相关 13 组 CTest 全部通过（15.74 秒）；game 路径/显示源 ID 的独立测试组通过。
  C++ 所有权与 150 列检查、`git diff --check` 通过。
- 本机 Panel 哈希 `7CF2B0C1DB17698E5536B0AAC5A332FAD39226BBD52E520158B8FCDAE5A792C8`，
  构建目录与 `build_official/dist` 一致。Console 哈希
  `872C2DA8E70AD17F79651D257E9B54337D3D7D4513D6EFDC8CA969D03CC0B848`，构建与运行目录一致，
  原凭证主密钥及私有 CA 保留。代码未提交/push。

### 10:51 起：用户明确授权单个测试会话注销，B 恢复桌面

用户要求仅注销一个测试用户、避免影响其他人，以验证能否恢复。本次是明确授权的故障恢复操作，
**不是产品普通断连/超时的注销策略**；不把自动注销加入 Render、Service 或重连流程。

- 操作前核对 B 账号 `grdp_75b2ed95a7304d3`，SID
  `S-1-5-21-651462275-30253513-3142702281-1092`，已断开的 Session 4；没有活动 RDP 测试运行时。
- 10:51:01 仅执行该 Session 4 的 `logoff`。该会话程序随 Windows 注销结束；账号、SID、profile 保留，
  会话未保存内容不可恢复。未操作 Administrator Session 1、usbtest2 Session 2 或工作区 A Session 3。
- 随后重新启动 B，经产品 Client/WS/Render/proxy 进入 **Session 8**，登录时间为 10:51，原 SID 不变。
  `run-20260909025115` 整批 51 秒；首张截图为登录过渡黑帧，稍后 `desktop-later.png` 已查看确认真实
  Windows 桌面和既有启动程序窗口。不能单凭首帧回调或窗口标题断言桌面恢复。
- 从这次注销开始到恢复观察点，90 Application Event 1000 没有新增 DWM 崩溃；原有游戏进程
  PID 31716 及其 2026-09-08 17:06:53 启动时间未变。未重启 90/RDS、未修改显示驱动或 Windows 图形策略。
- 随后再做 B 的普通重连与 `GraceExit`，44 秒通过：出帧、解码错误 0、自然宽限退出，仍为 Session 8，
  没有第二次注销。但 10:52:48 记录到一次同异常码的 DWM 崩溃，10:52:52 RDS 确认重新连接成功；
  所以只能说桌面使用恢复，不能说系统崩溃消失。测试收尾后 B 为 Disconnected，原账号/会话继续保留。

这证明单独注销 B 后 **B 可恢复桌面**，不证明 DWM 根因已修复，也不代表仍保留旧会话的 A 已恢复或
双工作区并行已通过。其他会话保留原身份和登录时间；未为排障扩大注销范围。

### 10:34–10:46：故障记录，90 系统 DWM 崩溃

下面已通过结果是故障发生前的历史证据，**不能据此宣称当前 90 的桌面连接或双工作区验收通过**。

- 首次双工作区样本各运行约 50 秒、均出帧，但日志核对仅有约 8 秒的同时连接区间。
  第二次同步启动时，A 出帧，B 约 5 秒后断开；第三次两者均未出帧，约 31/32 秒结束，
  `ERRCONNECT_CONNECT_CANCELLED` 为首帧超时后的主动取消结果，不是账号认证错误。
- 10:40 单独重试 A（`run-20260909024033`）仍无首帧；Client 记录 GDI/音频/动态通道已初始化，
  `published=0`，WebSocket 心跳正常，解码错误 0。90 的 RDS Event 1149 确认目标标准账号认证成功。
- 90 Application Event 1000 从 **10:34:35** 开始出现 `dwm.exe` 崩溃；故障模块 `dwmcore.dll`，
  异常 `0xe0464645`，偏移 `0x00000000000c70f8`。截至一次查询的 25 分钟窗口共 27 条，
  连接等待期间约每 3 秒重复。`dwm.exe` 为 10.0.20348.2849，模块为 10.0.20348.3451。
- 10:45 使用原生 `wfreerdp.exe` 直接连接 `10.0.0.90:3389`，绕过 GammaRay Client/WS/代理，
  仍在该观察窗口产生同一 DWM 崩溃。记录为 `direct-20260909024504`；仅在 stdin 传测试账号，
  保持 NLA 与后端证书指纹校验。此对照定位到远端 RDS/图形环境方向，尚不能认定某一驱动就是根因。
- `qwinsta` 显示原 Session 3/4 仍保留，另有无用户名的临时 Session 5/6 处于 Closing。
  未 reset/logoff 这些会话，也未重启 RDS、停 DWM、删除用户/profile 或终止工作区应用。
- 90 有 RTX 4090（驱动 32.0.15.9579）以及 Parsec、Oray、GameViewer、ToDesk 虚拟显示适配器；
  只记录环境，不把“有多个适配器”当作驱动冲突的证明。可用内存约 49 GiB，提交约 31/73 GiB。
- WER 存档含 `Report.wer`，所列临时 `.dmp` 已不在原路径；当前拿不到调用栈。
  下一步需要单独确认临时开启 DWM 崩溃转储采集；驱动/系统图形策略变更及重启不在本轮自动处理范围。

验收脚本同时修复了报告问题：已自然停止的测试实例不再重复 stop 掩盖原始失败；
输出非敏感 FreeRDP 错误码；观察期内进程提前退出不能计为正常连接通过。
本轮测试实例全部已收尾，Console 与本机/90 Service 保持运行。先保留故障证据，不循环启动长时间测试。

### 10:22 起：已建立连接的授权撤销

修复前 `run-20260909015147` 确认真实缺口：首次票据校验通过后，撤销原 Console 游客会话，
30 秒观察窗口结束时 RDP 仍可保持连接；不能用“新票据无法签发”替代运行中撤销。

现在 Service 通过原 Console WebSocket 发起独立的运行时授权核对，复用兑换消息路由，新增
`rdp_logical_session_id`（proto tag 6，与 ticket/nonce 互斥）。Console 使用已认证的节点身份，
检查实例/逻辑会话/原登录 session 的绑定、撤销/到期、用户禁用与 auth_version、游客封禁和当前应用 ACL。
成功结果为 `rdp_runtime`，不是新准入票据，不返回 Windows 密码，也不改变席位或抢占策略。

- Service 每 3 秒检查有逻辑 owner 的 RDP 实例，最多 8 个核对请求并行；排队与响应共用 3 秒期限。
  拒绝、超时或可信 Console 通道不可用均停止对应运行时；停止失败时保留预留并重试。
  网络短暂中断也可能断开桌面连接，这是当前 fail-closed 行为，不承诺 Console 离线时保持操作。
- 冷启动尚无逻辑 owner 的实例仍沿用原启动期限；最后客户端断开仍沿用原 5 秒宽限。
  旧核对结果应用前重新比较完整实例记录和 logical owner，不能停止换代后的实例。
- 当前检测依赖 Render 的逻辑会话快照，不把它描述成独立于心跳的新长连接租约；运行核对也没有延长
  原 Console 登录会话/票据记录的存活期。长期运行和大规模节点的延迟尚未验收，不保证所有规模下 6 秒内关闭。
- 更新顺序必须为 Console → Service；旧 Console 不认识新运行核对字段，不能先部署新 Service。
  本机配置、私有 CA、workspace master key 均保留；只备份/替换可执行文件。

实测记录（本机 Client/Console，90 Service/Render；每项含收尾均小于 10 分钟）：

| 项目 | 本轮结果 |
|---|---|
| 首次撤销回归 `run-20260909022318` | 先出帧，再撤销本次游客，连接自动关闭；整批 13 秒 |
| 仓库脚本 `RevokeSession` | 再次通过；撤销后 1.93 秒断开，整批 22 秒；管理员收尾在断开观察之后，不能掩盖结果 |
| 正常 Connect | 50 秒，通过，解码错误 0，没有周期授权误断开 |
| GraceReconnect | 35 秒，同逻辑 owner 的两次连接均出帧，解码错误 0 |
| GraceExit | 43 秒，先观察自然退出再收尾，通过 |
| 错误证书指纹 | 6 秒拒绝，无远端首帧；服务器证书未修改 |
| C++ 聚焦回归 | 13 组通过，18.24 秒，含协议路由关闭、私有 CA、解码、显示、剪贴板和异步生命周期 |
| Rust 聚焦回归 | 88 项通过（14 Core + 25 Host + 4 live-auth + 38 Console app + 7 ticket），Mongo 专项未计入 |

`scripts/test_rdp_acceptance.ps1` 新增 `RevokeSession`，通过 `-CleanupAdminCredentialFile` 指向本机
DPAPI 加密的 PSCredential 文件，仅用于撤销测试自身 guest 后的精确实例收尾。不得将明文凭据写入命令行。
本轮单独执行 Mongo ignored 用例仅得到“running 1 test”并提前退出，未取得测试完成摘要，不能记作通过。

部署 SHA-256：

| 产物 | 构建树与部署副本一致的 SHA-256 |
|---|---|
| Client（dist） | `1398560EBA4B90C7895B4601DCD57A3FB9180F7E4CD25253EAE6E671B4A826FC` |
| Render（dist/90） | `2FD919E88677A8EDEDBAB90922B1E0AD753541626A2F0DDC79EDB5BDB6F59086` |
| Service（dist/90） | `E958C35C447F65D35E14AB6DD3044958B99AE39D28FE39BB368F65951C279D01` |
| Console（output/px_console） | `1B8E6FA71DF19F948F1AB4C6E246DED7428FA8B5ACDA0AF033C9B74229D0697B` |

90 Service 备份为 `C:/Program Files/PixelsRender/rdp-service-backup-20260909022250.exe`；
Console 最终备份为 `output/px_console/rdp/console-runtime-backup-20260909023016.exe`。
撤销后 Windows Session 3/4 及原登录时间均保留；未登录 Administrator、未注销、未删除账号/profile。
以上仍不代表 Panel 自动续票恢复、全部外围硬件或 P0–P7 完整验收完成。

### 09:24–09:49 重启后继续：快速恢复修复

本机重启后重新启动既有 Console，未重建密钥。90 的两工作区仍为原 Session 3/4、原登录时间。

- 先前 `GraceReconnect` 脚本重新签发新 ticket，创建了不同逻辑会话；宽限期拒绝不同 owner 符合设计，
  不能据此放开抢占。脚本现通过 `/api/v1/connection-tickets/renew` 轮换原恢复能力，保持 nonce、
  logical_session_id，Windows 配置只在内存复用，不将其写入 URL/命令行/日志。
- 更正脚本后仍复现占用，确认另一处实现问题：RDP 桥接 TCP 已关闭，但路由释放只依赖 WebSocket
  close/disconnect 或析构；传输层清理可能晚于用户的快速恢复。新增协议关闭通知，返回对应 session
  executor 后原子 `RemoveIf` 核对路由身份，再关闭逻辑 binding、释放带 generation 的席位并通知断开。
  迟到回调不能移除复用了 socket 值的新路由；未降低 NLA/票据或独占要求。
- 新版 `GraceReconnect` 36 秒通过，前后两 Client 均出帧，拒绝 false、解码错误 0。
  90 时间线：09:43:03.539 协议关闭/开始宽限 → 09:43:04.845 同 stream 再次 connected；未重启 Render。
- `GraceExit` 42 秒通过：09:44:07.658 最后客户端断开 → 09:44:12.658 宽限到期 →
  09:44:12.672 Render 有序退出；Console 缺进程对账另计，不注销 Windows 会话。
- 新增 `rdp_route_close` 测试使用真实本地 HTTP/WebSocket 与 RDP TCP 桥接，四次完整创建/关闭，
  验证协议关闭不依赖 WS close 回调、席位仅释放一次；首轮 CTest 0.68 秒通过。
- 节点部署修复 CEF 子进程先退出的竞态：枚举后已不存在视作已停止，不把它误判为部署失败。
  首次失败发生于复制前，已恢复原 Service 后重试成功。最新完整备份为
  `C:/Program Files/PixelsRender/rdp-upgrade-backup-20260909014150`。
  本轮 Render 构建树/dist/90 SHA-256：`2FD919E88677A8EDEDBAB90922B1E0AD753541626A2F0DDC79EDB5BDB6F59086`。

这一检查点仅证明恢复接口与产品通道可恢复，不代表 Panel 关闭后自动保存/续票恢复能力已完成。
授权撤销仍继续核查，不将票据签发/重放拒绝单测当作已建立通道的实时撤销证据。

用户确认继续后，本机交互桌面可用。经批准维护固定 FreeRDP 最小补丁，原 demo 与干净上游均未修改。
详见 [补丁说明](../patches/freerdp/README.md)。以下结果取代下文凌晨“等待补丁授权/桌面解锁”的阻塞状态。

### 已复验

- 修复 MF 格式变化后未取出待处理输出、pending 被误报为帧的问题，补齐失败释放和输出长度校验。
  同一 `test_client_rdp_decoder`：未打补丁的 3.31.0 两项均复现 `0x0/-1015`；补丁版两项通过，
  分别覆盖首帧及八次重置、仅 SPS/PPS 无画面及后续 IDR、八次创建/销毁。色块按 RDP full-range BT.709 生成。
- 产品代理 Connect：首帧、正常退出通过，解码错误从 2 降为 0（36 秒）。
- `run-20260908234912`（89 秒）：真实可读桌面；协商尺寸由 1280×720 到 1024×640、1920×1080，
  全屏及最小化恢复通过，解码错误 0。RDS 本次约 30 秒才开放 DISP，因此先前只改窗口大小的短测试不能证明远端 resize。
- `run-20260908235353`（71 秒）：双向中文/emoji 文本、HTML、DIB 图片通过。
- `run-20260908235520`（134 秒）：双向文件/目录校验通过，含中文目录、空文件；不是仅检查通道 ready。
- `run-20260908235822`（70 秒）：客户端专属 CoreAudio 会话从基线 0 到峰值 0.2690，35 个有效采样；
  未录音、未打开麦克风，不把峰值测试称作人工听音或设备切换验收。
- Panel 私有 CA：原 Schannel `revocation status is unknown` 导致应用列表为空。现在仅显式私有 CA 使用
  `CURLSSLOPT_REVOKE_BEST_EFFORT`，不关闭证书链/主机名验证，也不退回系统 CA；不使用 `NO_REVOKE`。
  真实 HTTPS 的五种请求方法均验证正确 CA 成功、错误 CA/错误主机名拒绝。参见 [curl 官方定义](https://curl.se/libcurl/c/CURLOPT_SSL_OPTIONS.html)。
- `panel-launch-20260909000054`：现有登录用户从 Panel 云端应用直接启动 RDP，私有 stdin 交接，
  无额外授权/Windows 登录弹窗，进入同一个 Session 3 和原桌面状态。
- 最新 CTest 11 组全部通过（14.69 秒），包含新增 MF、Render UI 队列及私有 CA 测试；
  C++ ownership/150 列检查、`git diff --check`、退役 Native CLI 参数拒绝检查通过。

### 本轮发现并修复的退出及兑换延迟

Panel 启动实例关闭 Client 后，旧 Render 已记录“5 秒退出”，但进程没有消失。
原因是 `RdContext::PostDelayTask` 只把任务塞入 UI 队列，未唤醒无窗口 RDP 的 `GetMessage`。
原 Connect 脚本 finally 主动停止实例，掩盖了这个问题，故原有主动停止证据不算自然退出通过。

新增 `render::UiTaskQueue` 在入队时可靠投递 `WM_NULL`，拥有明确关闭状态；
测试覆盖工作线程唤醒空闲循环、回调内关闭取消余项、拒绝关闭后入队、重复销毁释放捕获。
90 日志已确认 08:21:54.860 断开 → 08:21:59.861 宽限到期 → 08:22:08.903 有序退出，
Windows Session 未注销。5 秒是宽限，后续网络清理及 Console 15 秒缺进程对账另计。
新增 `GraceExit` 验收在任何 stop API 之前观察自然停止。新版 Service 下整批 57 秒通过：
首帧、正常关闭、解码错误 0、自然停止全部满足；短于首帧准备时间的早期失败采样不算通过。

Service 还存在全局运行时锁内同步 WMI 查询：应用心跳 reaper 每个历史实例重复枚举，
Render/Panel 普通心跳每条又枚举，monitor 每 3 秒枚举。实际客户端在恰好 3 秒被拒绝，
此前 Render 记录兑换 TIMEOUT 后才收到成功回包，不是凭证错误。
现在应用 reaper 一次快照处理所有候选、普通心跳使用监控缓存、monitor 的常态枚举放到
`spawn_blocking` 且不持运行时锁；重新取锁后核对实例记录/桌面启动身份，丢弃过时快照。
启动/停止等显式管理操作保留原来的同步进程确认，不将它们错误描述为全部异步化。
新增测试覆盖一次枚举处理两实例、慢查询中及时 stop/start、旧快照不覆盖新状态、20 条心跳零枚举。
最新 Rust 聚焦 83 项通过（14 Core + 25 Host + 38 Console app + 6 ticket）；Mongo 专项不计入该数字。

### 08:55 前后的短批次检查点

- 跨工作区 ACL 实测：两个标准账号各自可读取自己的专用文件；均不能读写另一个 profile 的文件，
  均不能读取宿主 proxy 私钥。只打开测试文件句柄验证 ACL，未输出私钥或邻居文件内容。
- `panel-launch-20260909004609`：原有 webview/baidu 从 Panel 启动，实际网页画面已查看确认；
  只关闭本批 Client 和 `inst-202-38b674c3`。此项不是全部 Native 输入/文件功能验收。
- `run-20260909005500`：同工作区第二个 Client 明确显示“已被占用，未抢占”，第一个继续正常显示，32 秒。
- `run-20260909004053`：动态色条、棋盘和移动图形实际显示，解码错误 0；稳定 5 秒窗口呈现约 50–53 FPS。
  必须限定为远端 1280×720、本地 1920×1080 放大，不是 1080p/60 FPS 成绩；也不是输入到画面的时延。
  测试脚本经 Windows PowerShell 5 读取无 BOM UTF-8 导致生成内容中的中文乱码，已改为 C# Unicode 转义；
  不将这个测试内容编码问题归因于 RDP 中文输入或剪贴板。
- 动态测试暴露早期全屏问题：DISP ChannelConnected 在服务器 CAPS 前触发，提前的布局被忽略。
  Client 增加协议线程内 `DisplayChannel`，等 CAPS 后发送合并后的最新尺寸，断开/重新附着可重发。
  回调不保存 owner 裸指针，也不建立全局回调表。两项聚焦测试通过；
  `run-20260909005548` 真实早期全屏通过，远端日志与画面内容同时确认 1920×1080，中文正确。
  本批 127 秒，动态稳定窗口约 24–31 FPS，解码错误 0；尚未达到 1080p/60 FPS。

### 部署及仍待验收

本机 Client/Panel/Render 与 90 已按部署脚本逐项校验 SHA-256；SDK manifest 现在绑定上游版本、补丁摘要及全部 DLL。
部署入口拒绝代理构建树和 SDK 混用。90 最近完整备份为 `rdp-upgrade-backup-20260909001824`；
最新 Service 独立备份为 `rdp-service-backup-20260909005430.exe`，源码构建树/dist/90 的 Service SHA-256 均为
`E9CBE3E85569B001C53F1DB525EB42F3D46C627EE08CDB8459A88DBDBC9718E7`。
本机 Service 在发布停止阶段有一次状态竞态，确认 Stopped 后完成同步，并已恢复 Running。

仍不能声明整份计划完成：Unity/game 实机回归尚未通过。配置为 `G:/app/2dadventure/2dAdventure.exe`，
但现有 game-hook 按同名进程接管了 90 上 `D:/software/2dadventure/2dAdventure.exe` 的旧 PID 31716，
后者创建于 9 月 8 日 17:06，不是本轮测试创建的进程，未擅自终止。控制/UDP 关联成功不等于有采集画面。
还需宽限内重连、跨工作区并行、权限撤销、DPI/输入细项、真正远端 1080p 动态画面等证据。
P6 外设保持未验收边界。未提交/push，未注销或删除 Windows 用户/profile，也未终止其工作区应用。

## 已落地的代码路径

- Console：新增 `rdp` 应用类型；按应用+节点持久保存加密凭证，AES-256-GCM 的附加认证数据绑定工作区、节点、设备和凭证版本。
  缺失密钥或数据库时失败，不自动生成新密钥覆盖旧账号关系。签票只读取既有凭证，不创建工作区。
- Service：受信任 Console TLS 才报告 RDP 能力；保证标准本地账号、SID 与版本一致，拒绝管理员身份和工作区改绑。
  受保护目录保存非秘密身份记录，DPAPI 加密一次性启动配置；密码不进 Render 命令行。
- Render：独立 RDP 装配分支，不创建桌面采集/视频编码/原生 UDP 媒体模块。独占工作区内核文件锁，Job 监管代理子进程；只清理运行时，不注销会话、不删除账号或 profile。
- WebSocket：仍使用产品 `/media` 与 `px::Message`；RDP OPEN 下发连接绑定，DATA/CLOSE 携带代次，32 KiB 分块，有界接收队列和实际发送完成反馈。
  RDP 路由不提供宿主 HTTP、文件管理、UserProxy 或输入注入入口。
- 代理安全策略：GammaRay 自有必加载模块检查固定回环目标、准确工作区身份、后端证书 SHA-256 与有效期，限制虚拟通道。
  上游代理的默认证书回调不足以作为产品证书校验，因此不能省掉此模块。
- Client：迁入 Qt 6 内部 RDP 显示/输入和协议工作区，FreeRDP 直接解码、合成原生 RDP 图像，不增加二次编码。
  本地适配器只监听回环并校验 TCP 对端归属当前 Client 进程。
- Panel：RDP 票据跳过原生宿主配置探测；使用同一个 `px_client.exe --rdp-launch-stdin`，一次性私有 stdin 管道交接启动材料，不把密码放入参数、环境、启动 URL 或配置文件。
- 凭证交付：只有明确声明 `windows-rdp-v1` 的客户端取得 RDP 配置，响应带 `Cache-Control: no-store, private`，秘密的调试输出脱敏。
  Windows Panel 安装 `rdp/console-ca.pem` 后验证 Console TLS；Service 使用 `rdp/console-ca.der`。
  未部署信任材料不启用凭证下发能力，不借测试忽略证书选项进入产品路径。

## 已完成的聚焦验证

- Service 生命周期 21 项通过；RDP 账号/工作区/启动参数等 13 项通过（不等同于真实 Windows 账号创建测试）。
- Console 先前应用调度 36 项通过；新增签票/隐私测试持续补充中。
- `rdp_stream` 通过，包括绑定、队列、关闭、可靠发送、工作区独占锁，以及客户端回环通道、超时、重复停止和排队销毁。
- Qt 6 显示模块和 FreeRDP 协议模块独立编译通过；图像边界和缩放/DPI 坐标测试通过。
- 修正 demo 的缩放坐标分母；RDP 负滚轮按有符号 9 位编码，失焦释放已发送的键鼠状态。
- Render 曾完成增量构建和 dist 哈希核对；后续共享库及协议变化需再次构建/核对，不能沿用早期哈希声称最终交付。

## 22:40 产品链路实测检查点

- 本机 Console/Client（10.0.0.16）与 90 上的 Service/Render/proxy 已部署；Console 私有 CA、proxy 证书 pin、RDS 证书 pin 均实际验证成功。
  没有使用忽略证书、认证降级或 RDP Administrator。
- 专用测试应用 `RDP Acceptance 90`，节点端口 32014；Console 自动生成并加密保存凭证，Service 自动创建标准账号。
  产品客户端已通过 `/media`、原生 NLA 和 proxy 策略取得实际桌面，多次启动/关闭/重连成功；不是独立探针画面。
- 已查看客户端截图，使用转发的 Win+R 和键盘启动 `cmd /k whoami`，确认远端身份为受管 `grdp_` 用户，profile 独立。
  Windows Session 3 的初始登录时间 22:29 在多次重连后保持不变；退出客户端后状态为断开，未注销。
  原 Administrator console Session 1 和原 usbtest2 Session 2 未被接管或注销。会话中有该机器原有的全用户启动软件，未擅自删除或禁用。
- 修复已由实机复现的问题：Render 使用非法文件路径作为内核互斥体名；NetUserAdd 后缺少 BUILTIN\\Users 导致重试误判 Guest；
  固定 FreeRDP 的 INI 音频键读反；其新版 CredSSP 将委派凭证写入前端 settings 而旧 peer.identity 为空；Qt 6 扩展扫描码标志位置变化。
  不修改第三方源码：两项 FreeRDP 差异由固定版本适配边界处理，仍要求 NLA、指定账号及密码匹配、后端证书校验、禁止麦克风。
- 新增策略事件日志仅写固定事件名，不写账号、密码或协议正文；存放受保护工作区目录，单文件上限 64 KiB，达到上限不继续写入。
- Client 已实现文本/HTML/DIB/文件目录剪贴板；数据格式与文件安全测试通过。实际双向剪贴板尚在测试，不能把单测视为通道验收。
- 增加 Ctrl+Alt+Enter 全屏、Tab 转发；Qt 原生窗口消息扫描码及中文 IME/按键释放两项测试通过，仍需完整实机交互覆盖。
- 本批所有实机连接测试分别约 6～55 秒，均有客户端/实例收尾；未进行长时间压力测试。

## 尚未验收，必须继续

### 23:18 后续检查点

- 实机双向文本剪贴板通过，内容包含中文和 emoji；文件目录剪贴板尚未通过。诊断已确认能力协商及 4 项文件描述解析成功，
  Windows 对内容请求返回失败，正在补充大小查询和通道回归；不能将“已实现”写成“实测通过”。
- 修复 drdynvc 独立工作线程与帧交付竞争：固定 SDK 必须同时设置 `SynchronousDynamicChannels=TRUE`，仅设置静态通道线程标志无效。
  修复后连续三次启动/显示/关闭通过，未再出现帧队列违约；AVC444 YUV 合成警告仍需单独处理。
- 真实错误 proxy pin 测试被 TLS 校验拒绝，未出图。产品路径没有忽略证书。
- 停止 90 Service 后，Render 实测约 6 秒退出；客户端提示连接断开、Windows 会话保留。恢复 Service 后重新授权启动成功，
  仍是初次 22:29 登录的 Session 3。该机制是失联运行时退出后重建，不是旧 IPC token 的孤儿进程接管。
- 五个 CTest 目标（RDP 传输、显示、剪贴板、帧、Panel 启动流程）全部通过，总耗时 8.33 秒。
  新增 C++ 所有权/150 列门禁通过。真实文件批次 87～93 秒，Service 失联批次 14 秒，均低于 10 分钟。
- 固定 SDK 构建脚本已执行成功；Client/90 同步 core DLL、SDK 完整 revision/hash 清单及五项许可证。
  90 部署备份为 `C:\Program Files\PixelsRender\rdp-upgrade-backup-20260908150917`，没有删除旧版或 Windows 工作区。

以下为总验收清单，已通过的子项以上述最新检查点为准：

### 23:54 后续检查点

- 双向文件/目录剪贴板实测通过（137 秒批次）：远端 Explorer 复制中文目录到本机，再从本机粘贴到受管账号临时目录，
  核对中文内容和空文件。根因是声明 `CB_CAN_LOCK_CLIPDATA` 后未锁定读取快照；现已配对 Lock、带 clipDataId 的大小/内容请求及 Unlock。
  新增完整传输、取消解锁与迟到响应丢弃测试通过；按 Microsoft MS-RDPECLIP 的 SIZE 请求规定使用 8 字节。
- 第二个客户端得到 `session-occupied` 拒绝，原客户端连接及画面不受影响。已补客户端提示，实测显示“工作区已被占用，未抢占现有会话”。
- 真实键盘事件下 Ctrl+Alt+Enter 全屏通过，已查看 1920×1080 截图；协议日志确认 1280×720→1024×640→1920×1080。
  最小化/恢复没有断开或帧队列违约。早期 PostMessage 脚本未改变系统修饰键状态，不能作为全屏失败的产品证据。
- HTML/图片本机→远端实测通过。反向图片暴露 Windows 常用 `BI_BITFIELDS` BGRX 布局，已按精确 RGB mask 扩展解析并补畸形输入测试；
  HTML 反向测试夹具改为显式 UTF-8 字节流，避免旧 .NET 字符串剪贴板编码影响，待复测。
- Console 调度及凭证测试 37 项、签票 6 项通过；显式运行真实 MongoDB 的原子兑换/续期/绑定测试通过（独立测试库，未改产品库）。
- Panel 启动失败与完成回调现会延后、按 QProcess 身份清理记录及 stream 映射，避免旧回调删除替换实例或未启动进程保留秘密。
- 新 Render 会在 proxy 监听就绪后立即删除已解析的明文 INI，不等会话结束；最新本机 Render 已构建，90 仍需同步此最后变更。

1. 最终变更再次构建、发布和哈希核对（此前已分别完成各模块构建，不代表后续修改自动交付）。
2. Panel UI 启动闭环、真实双向文本/HTML/图片/文件目录剪贴板、音频听测和全屏/resize/DPI。
3. 真实代理策略模块：有效/错误证书、准确/错误账号、通道与停止测试；Rust DPAPI 到 C++ 解封的跨语言正反例。
4. 处理实测中的 AVC444 YUV 合成警告，以及一次启动时的帧交付校验失败；补充定位日志和回归测试，不以偶尔出图代替稳定性验收。
5. 快速重连、Service 重启恢复、权限撤销、不同工作区隔离，以及 Windows 会话和应用状态保留。
6. 音频听测、输入/全屏/DPI、短时动态画面统计、既有模式回归与最终文档。

本次已经取得产品链路画面，但上述剩余项未完成，不能宣布首版全部验收通过。
外围设备、多屏和其他 P6 扩展继续按计划单列，不以通道建立代替真实功能验收。

### 2026-09-09 00:35 后续检查点

- HTML 和图片剪贴板双向实机均通过，`run-20260908155554`，41 秒。反向 DIB 使用 32-bit BI_BITFIELDS 的精确 BGRX masks；
  UTF-8 解码按无状态完整消息校验，截断尾字节拒绝。已有双向文件目录、中文文本/emoji 测试仍保留。
- 系统音频已实测通过：`run-20260908162634`、`run-20260908162918`，本机 Client 进程音频峰值分别 0.244、0.272，
  有效采样 37、35 次；远端标准用户播放有间隔的合成测试音，未录制麦克风或其他应用内容。首批收尾被构建发布器终止，
  第二批正常关闭，107 秒，不能将第一批的进程退出码作为正常退出证据。
- 音频根因不是等待时间：Windows 未看到 `rdpdr` 时不向 `rdpsnd` 发数据，见
  [微软协议说明](https://winprotocoldoc.z19.web.core.windows.net/MS-RDPEA/%5BMS-RDPEA%5D-240423-diff.pdf)。
  现在同时维护配置与策略白名单，动态音频播放通道允许；RDPDR 仅允许有界单包握手、能力协商、零设备列表，
  非零设备声明、设备读写、打印机命令、分片/压缩/长度不符及未知包一律拒绝。`DeviceRedirection=true` 仅为上游通道开关，
  不代表产品开放设备重定向；强制加载 GammaRay 策略仍是启动门禁。负向协议单测通过，90 审计为受限握手通过。
- UI 队列抽为可测试内部组件，关闭取消未派发动作；新增 5 组销毁/回调内关闭/弱引用失效/容量恢复/工作线程并发测试。
  7 个相关 CTest 目标全部通过，7.74 秒；新增 RDP 节点不可迁移且禁止观察/抢占的 Console 测试也通过。
- 90 最新完整部署备份：`C:\Program Files\PixelsRender\rdp-upgrade-backup-20260908162550`；全部文件 SHA-256 核对通过。
  当前实例的明文 INI 在 Ready 后已消失；早期测试版本留下的临时 INI 还需限定范围清理，不删除工作区账号/profile。
- CDB 已确认 MF AVC444 合成告警来自 CHROMAv2 的三个空输入平面：`result=ffffffff type=2 srcNull=1/1/1 dstNull=0/0/0`。
  此问题不再视作可忽略日志。保持第三方源码只读，改用构建开关选择 OpenH264 客户端解码，RDP 原生 AVC444 转发不变；
  新 SDK 正在构建/复测，尚未确认图形修复通过，不承诺硬件加速或 60fps。
- 可移植性检查发现 OpenSSL provider 曾取自开发机绝对路径，正在将 `legacy.dll` 纳入 Client 发布并在网络初始化前设置运行目录；
  需以实际加载模块路径核验，不能仅靠环境变量设置成功判定。

### 2026-09-09 01:04 后续检查点

- 固定依赖迁到官方 FreeRDP 3.31.0 `aa8650b300aa4cabd85d9c72b431301509b9043f`，第三方源码和原 demo 未修改。
  [官方发布记录](https://github.com/FreeRDP/FreeRDP/releases/tag/3.31.0) 包含图形与安全修复，但升级本身不是产品验收证据。
  代理现在只使用公开 `rdpContext` 与 `VerifyX509Certificate` 回调，不读取已私有化的 proxy context 布局；
  固定目标/NLA/证书 pin/工作区身份门禁保留。音频 INI 适配新版本正常命名 `AudioInput=false, AudioOutput=true`。
- 新版本 OpenH264 链路 `run-20260908165703` 42 秒首帧通过、正常收尾，但仍有 `DecodeFrame2 0x22` / AVC444 忽略更新，
  不能把旧 MF 空平面告警消失当成图形修复。正在以同一官方版本的 MF 构建作对照，不屏蔽日志、不改 Render 为重新编码。
- 实际加载模块证明 `freerdp3.dll`、`openh264-6.dll`、`legacy.dll` 均来自 `build_official/dist`，
  不再依赖开发机 `C:/source/vcpkg/packages`。环境在 Client 网络初始化前设置，Panel 子进程也显式传递。
- 第二个持久验收应用 `app-105-797a8ee1` / 节点 `node-106-7d3e91ec` / 32016 首次启动成功，27 秒批次：
  工作区 `6f396f5e-b75d-4572-9373-93da88b11022`、账号 `grdp_75b2ed95a7304d3`、SID 尾号 1092、Session 4，首次登录 00:58。
  原应用仍为 SID 尾号 1091 / Session 3 / 首次登录 22:29。两个标准账号及 profile 保留；尚不等于跨账号文件 ACL 负向实测完成。
- 3.31.0 错误 proxy pin 测试 `run-20260908170134` 7 秒被拒绝、未出图；后端真实审计为 `backend.certificate.accept`。
  7 组 CTest 全通过（8.30 秒），Service Core 13 项及 Service Host 21 项通过；新所有权/150 列检查与 diff 检查通过。
- 已清理旧版本在原验收工作区遗留的 33 个临时 `.proxy.ini`，删除前核对精确目录、命名、受管账号及 mandatory policy，
  无活跃 RDP Render。只删除这些临时凭证文件，无恢复副本；Console 持久凭证、账号、profile、会话和升级备份未删除。
  Service 另增加每次启动的 RAII 清理，Ready、失败或取消均清理自身 bootstrap/INI；双进程在初始化窗口同时硬杀的残留仍需独立测试。
- 90 完整升级备份为 `C:\Program Files\PixelsRender\rdp-upgrade-backup-20260908165614`，部署逐文件 SHA-256 相同。
  后续 SDK 对照构建尚不能当成最终运行物料，最终发布需再次核对。
- 本机交互桌面探测 `OpenInputDesktop` 返回 error=5、foreground PID=0，已请求解锁。
  此期间只做后台连接/协议日志测试，不把窗口标题当成真实可见画面、输入、音频听测或 Panel 点击验收。

### 2026-09-09 01:17：工程构建交付，图形验收仍阻塞

- 官方 3.31.0 的 MF 对照构建仍在初始化阶段出现两次 `bounding frame 0x0` / `avc444_decompress failure`。
  `direct-20260908170948` 使用同版本官方 `wfreerdp.exe` 直接访问 90:3389，证书 SHA-256 严格固定、凭证仅 stdin，
  同一标准账号 Session 3 复现相同两次错误，25 秒后正常断开。因此此现象不依赖 GammaRay WS/proxy 或 Qt 显示代码。
  固定源码的 `h264_mf.c` 在输出类型变更/需要更多输入时尚无有效输出，`h264.c::log_decompress` 仍校验 0×0 解码区域；
  这是下一步补丁审查的具体入口，不代表已经修复或已证明所有后续画面正确。
- 按 AGENTS 的第三方只读规则，没有修改 FreeRDP 源码，`git status --porcelain` 仍为空。
  若继续维护该解码路径的最小补丁，需要用户明确允许将固定 FreeRDP 构建副本纳入项目维护；
  原 `D:/dolit/rdp`、其 FreeRDP 引用以及官方 checkout 保持不变。替代解码器/图形配置也不能未经实测标为稳定。
- 修复项目自己的 Ready 提前触发：PostConnect 新分配的空 GDI surface、resize 空 surface 和 Refresh 请求均不再自行标记 dirty；
  只有远端 EndPaint 更新才提交画面。此回调仍不是人工可见画质验收，不能掩盖上游被忽略的图形更新。
- 新增受版本控制的 `scripts/test_rdp_acceptance.ps1`：公共验收应用的匿名授权、可信 CA/主机名、秘密 stdin、
  限时进程/实例收尾及正常退出检查；支持 Connect / WrongCertificate / WrongAccount / WrongPassword。
  不保存 Windows 密码、ticket 或完整协议日志。Connect 会因任何检测到的解码失败返回失败，而非仅按窗口标题放行。
  本次 Connect 结果为 `FirstFrameCallback=True, NormalExit=True, CodecErrorCount=2, Passed=False`（26 秒）。
  错误账号和错误密码分别 6、7 秒真实被拒绝、无首帧、正常收尾，均通过负向测试。
- 后端错误证书 pin 也经过真实验证：`run-20260908171531`（7 秒）前端工作区身份通过，但代理记录
  `backend.certificate.reject`，客户端未出图；测试 finally 已逐字恢复原部署 manifest 并验证恢复成功。
  不修改 Windows RDS 证书或账号密码，不使用忽略证书，也不接管管理员会话。
- 最新 CTest 7 组全部通过（7.19 秒）；Rust Core 14、Host 21、Console 应用/凭证 38、签票 6，共 79 项通过。
  新增每次启动清理测试，验证重复 Drop 安全，仅删除自己的 bootstrap/INI，邻居文件及持久 identity 不受影响。
  Mongo 原子兑换集成用例在本轮默认命令中是显式 ignored，不计入 79 项；此前单独运行通过的证据仍保留。
- 最新工程构建已同步 `build_official/dist`，90 完整备份 `C:\Program Files\PixelsRender\rdp-upgrade-backup-20260908171318`，
  全部修改运行文件逐个 SHA-256 相同。核心哈希如下；这是可复现的待验收工程版本，不是正式发布/图形全部通过的声明。

| 产物 | SHA-256 |
|---|---|
| Client | `81F377F4615A4266C99911D71A3A733A38EA5E9BD9C0CCA3D471590A9F439CC7` |
| Panel | `2AE9B259834F9A33CE66117743A72823EBAD0A07A8B6D6D9480C04F412681859` |
| Render | `93B302B6E97A0803279CAA080C39F3EE9FCAFC4DE6B8A5721F3C89637FD9FDBA` |
| Service | `256E6743484E23D5CED0FDCDD5009C2AF57149B3AD0CEC229C53065A8C5A1037` |
| FreeRDP core | `6CEDE3D41E5D61F34E2DA68DA4C00D9231D44D3812D8B8FB1545733D862C8BDC` |
| Proxy policy | `0336664E2005EA4FC432489EB6ED395C4DA895C661392A9FAA876153821E315A` |

复验命令（PowerShell 7，使用已经创建的公共测试应用；不是创建额外 Windows 用户）：

```powershell
./scripts/test_rdp_acceptance.ps1 -ConsoleUrl https://localhost:30500 `
  -ConsoleCaPem output/px_console/rdp/console-ca.pem -AppId app-24-8cf70180 -Scenario Connect
./scripts_build/test_rdp_rust.bat
ctest --test-dir build_official -R '^(client_rdp_.*|rdp_stream|panel_stream_launch_auth_workflow)$' --output-on-failure
```

未完成门禁仍包括：上述图形问题、解锁后的 Panel UI/新 SDK 全通道与既有 game/webview 实机回归、
跨工作区文件 ACL 负向验证、权限撤销/宽限内重连、DPI/音频听测和短时动态画面统计。
P6 外围硬件扩展仍单列；不要因已完成编译与部分实测而把整份开发计划改为完成。

同版运行时恢复复验：`run-20260908171553`（15 秒）停止 Service 后 Render 7 秒退出，
客户端提示“连接已断开，Windows 会话保留”，Session 3/4 及 Administrator 原会话均保留；Service 已恢复运行。
`run-20260908171726`（18 秒）重新授权并连接原 Session 3 后正常收尾。这两批只证明失联清理与恢复，
不覆盖已知 AVC444 初始化问题，也不是宽限内不断 Render 重连的替代证据。
