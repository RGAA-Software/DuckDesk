# 独立会话应用工作区：详细设计

日期：2026-09-10。状态：最小 DuiLib UI demo 已实现并完成本机验证；完整工作区仍为设计提案。
尚未修改产品应用模式、系统策略或 90 环境。协议、字段、工作区模块名均为拟定。

### 最新实施检查点：最小 UI demo 已完成本机验证

后续用户已授权开始开发，本节覆盖上文及下文保留的“尚未安装/开发”历史描述。
已拉取最新代码 `93c5cca09`，通过现有 vcpkg 安装经典 DuiLib，未升级其他包。
新增独立 `src/px_workspace/demo` 和常规增量构建入口，未修改 Render/Service 或 90。
构建与复现见 [demo README](../src/px_workspace/demo/README.md)。

- 已完成普通窗口、背景、底部文本按钮、中文状态和退出；这不是系统任务栏，也尚未接入真实应用图标。
- 3 组 CTest 通过，每组创建/关闭 8 个窗口，验证通知点击、关闭回调和默认字体尺寸，共 0.77 秒。
- 100%/150%/200% 布局缩放截图已查看，中文和按钮可见；150%/200% 使用 demo 参数覆盖布局，
  不是修改 Windows DPI 或跨显示器实测。实际 DPI 切换、鼠标命中和 Server 会话仍待验收。
- 修复原型两项问题：GDI+ 与 C++23 `std::byte` 名称冲突通过头文件顺序解决；
  XML Font 缺少 id 导致缩放后字体不变，已补 id 并加入字体尺寸断言。第三方源码未修改。
- 首次截图脚本使用隐藏窗口启动导致取不到 MainWindowHandle，已改为普通可见 demo 窗口；
  失败脚本只结束其自身启动的测试进程，后续三种布局截图均正常退出，不计为产品崩溃。
- 原生所有权检查和 clang-format 检查通过；未新增 UI 框架到 Render。
- EXE 约 971 KiB，静态 UI/CRT；dumpbin 直接导入仅有 Windows 系统 DLL：
  gdiplus、COMCTL32、ole32、GDI32、USER32、KERNEL32、OLEAUT32、IMM32。
  这不是干净系统完整安装验收，但不需要另装 Qt/.NET/DuiLib UI 运行时。
- 已发布到 `build_official/dist/workspace_ui_demo/`，EXE、XML、DuiLib 许可证与构建输出逐项 SHA-256 一致。

| 文件 | SHA-256 |
|---|---|
| workspace_ui_demo.exe | `7B257535EF1F6852C693192FF73474225D182DE24C077947BD04566ADFB09B77` |
| workspace.xml | `223B79DF54EA0C36E14EE5292E77556BA6A329FFD751E327FA404A99EFDC0A3F` |
| duilib-copyright.txt | `8A3659E4164839A7A3C78EB092FD6CF1D285C8980F714DCC13E991B9C636614F` |

下一步先查看 demo 交互，再验证实际鼠标/DPI 和最小恢复原型；完整工作区尚未实现，
本轮没有创建账号、隐藏 Explorer、改任务栏、注册服务或连接远端会话。

## 0. 最新实施优先级：功能优先

### UI 选型已确认：经典 DuiLib 最小核心

用户要求使用最精简版本：选择经典 [duilib/duilib](https://github.com/duilib/duilib)，
不采用网易 NIM 框架或基于 Skia/SDL 的扩展版。`px_workspace` 使用 C++、Win32 和 DuiLib，
不引入 Qt、.NET/WPF、CEF、Electron、Skia 或 SDL；Render 不增加 UI 框架依赖。

- DuiLib 仅用于背景、基础任务栏按钮、标签、简单布局及必要提示界面。
- 不构建或打包上游示例、浏览器控件、设计器及无关功能；资源只包含本产品用到的 XML、图标和样式。
- 优先静态链接 UI 核心，复用项目现有 C++ 运行库部署策略，不要求部署端安装独立 UI 运行时。
  静态链接 UI 不等于整个程序没有 DLL 依赖，最终依赖清单和体积以构建检查为准。
- 不为缩减少量体积修改第三方源树；通过我们自己的构建目标和资源清单控制范围。
- 引入前固定上游提交并记录许可证，验证当前 MSVC/CMake、DPI、中文显示及销毁回调行为；
  尚未拉取依赖、构建原型，也没有 Windows Server 实测通过结论。
- Win32 管理层负责窗口、AppBar、Explorer 显隐和恢复；DuiLib 不承担进程/会话生命周期。
- `--recover` 模式不初始化 DuiLib 控件树、皮肤或完整 UI，继续使用最小 Win32 恢复路径。
- 第三方内部所有权保持只读；项目适配层遵守仓库 RAII/智能指针规则，不照抄裸指针异步回调。

用户最新决定：不用过于考虑安全问题，优先实现功能。本节覆盖后文中可能被理解为首版前置条件的
安全加固、复杂策略和全面兼容要求；后文未纳入以下首版范围的项目属于后续扩展，不阻塞核心功能开发。

### 最高优先级：先做 vcpkg + DuiLib 最小 demo

用户进一步要求：后续先通过 vcpkg 尝试引入经典 DuiLib，写一个最小 demo 看实际效果，
这项验证优先于账号、Session、Service 监督和完整工作区开发。本轮仅记录任务，尚未安装或开发。
上游安装说明包含 `vcpkg install duilib`，但项目实际接入前仍需核对所用 registry 中的 port、
版本、构建参数和 CMake 导出方式，不假定 README 命令已经在本仓库验证通过。

执行顺序：

1. 检查仓库现有 vcpkg/manifest/triplet，优先复用；如需独立验证，使用隔离的 demo 构建目录，
   不改全机集成设置，不运行 `vcpkg integrate install`，不顺带升级产品其他依赖。
2. 核对并固定 vcpkg baseline、duilib port 版本/port-version 和源码来源；
   选 Windows x64、静态 UI 库，并使 CRT 与实际构建策略一致，不混用不兼容运行库。
   若 port 无法满足最小构建，先记录具体原因，不自动换成重型分支或修改第三方源码。
3. 拟建独立 `workspace_ui_demo` CMake 目标，通过项目常规增量 C++ 构建入口调用；
   不使用 release-only 全量构建脚本，不让 demo 成为 Render 的链接依赖。
4. demo 展示普通窗口中的任务栏样式区域、几个应用图标按钮、标签、简单状态提示和关闭按钮；
   用最少的 XML/图标资源验证布局、点击和中文显示。不是实际系统任务栏，不接管桌面。
5. 检查 100%/150%/200% DPI 下的布局、文字和命中区域，窗口缩放、反复开关及正常退出；
   用图示按钮表示应用即可，不启动或接管用户的业务程序。
6. 检查构建及发布产物的依赖清单、文件体积和基础运行占用；确认不需要 Qt/.NET/浏览器运行时，
   明确区分 Windows 系统 DLL、既有 VC 运行库与新增依赖，不提前承诺单 EXE 零依赖。
7. demo 的 EXE、必要 DLL 和资源发布到 `build_official/dist/workspace_ui_demo/` 独立目录，
   与构建输出逐项核对 SHA-256 后提供可直接启动的路径，供用户查看界面。
8. 记录 vcpkg 版本/baseline、源码版本、构建命令、截图、依赖清单、发布哈希和已知问题。
   本机交互每批含收尾不超过 10 分钟；90 验证按需后续安排，不以本机通过冒充 Server 已通过。

demo 不创建账号、不连接 RDP、不隐藏任务栏/桌面、不安装服务、不更改业务应用配置。
通过标准为：最小依赖可重复构建、界面和中文/DPI 基础功能正常、关闭无崩溃、发布可直接运行。
若 UI 库或项目所有权适配成本超出预期，先总结问题再定下一步；不在验证过程中扩展为完整工作区。

### 首版要交付的功能

1. Console 新增独立 `session-app` 类型，配置 EXE、参数和工作目录；创建/复用专用账号与会话，保留管理员能力。
2. 一个常驻 `px_workspace.exe`，使用目标 Session 默认 Desktop，不杀 Explorer；提供背景和基础底部任务栏。
3. 启动业务应用，支持普通弹窗、外部浏览器、窗口切换和最小化恢复；Office 等按实际安装情况验证。
4. Rust Service 监督 workspace；正常退出恢复原生界面，强杀/崩溃后按需启动 `--recover`，UI 卡死能够检测。
5. 客户端断开、Render 超时退出不结束工作区；再次连接优先恢复原工作区，不重复启动业务应用。
6. 以单显示器、少量代表应用完成端到端验收和发布，不等待全应用、全故障矩阵完成后才交付。

### 首版不做，也不作为阻塞条件

- AppLocker、程序执行白名单、文件沙箱、管理员降权、限制所有系统入口或防止恶意用户逃逸。
- 新建复杂的凭证体系、重复鉴权框架、抗管理员篡改日志、专门的安全审计平台。
- 完整 Explorer/托盘替代、多显示器自定义任务栏、全平台客户端适配和全部 Office/浏览器版本兼容。
- 为限制外部辅助应用而注入拦截所有进程创建、ShellExecute 或 COM 激活。

优先复用既有账号凭证存储和管理通道的实现能力，新模式保留独立配置与记录；
不要为“配置独立”重复造一套加密系统。账号名/类型标识等最小区分仍需保留，以免误用旧模式数据。
托盘初版可以提供“临时显示系统任务栏/托盘”入口，接受同时出现系统入口；不为了限制它们破坏业务功能。

### 仍必须保留的可靠性底线

- 不误操作其他用户 Session，不按同名进程扫杀，不因 Render 退出杀掉业务应用。
- 修改界面前保存恢复状态，恢复操作可重复，异常时能回到普通桌面；不要求防恶意篡改。
- 原始账号密码不进入普通日志或命令行；沿用现有产品认证，不新增无约束的提权执行接口。
- 保持仓库的 C++ 所有权、异步生命周期、Job AND 路径准入要求及旧模式行为不变。

这些是功能正确性和避免损坏用户环境的要求，不是新增安全产品范围。
恢复日志首版可采用一个版本化 JSON 快照，加每项修改的 prepared/applied/restored 标记，
原子替换和逐项补偿即可；不另造通用事务引擎或复杂事件存储系统。

### 实际开发顺序

首先完成上面的 vcpkg + DuiLib 最小 demo 并查看效果；通过后再做
“专用会话启动应用＋一个 workspace 基础 UI＋最小恢复”，再接入 Console 类型与 Service 状态，
然后完成传输接入和断连重连；基础窗口功能与恢复一起开发，不要求先完成整套框架。
第一轮验收至少覆盖：正常启动、弹窗/外部浏览器、最小化恢复、正常退出、强杀恢复、UI 卡死、
Render 退出后重连、其他会话不受影响。复杂故障、拓扑和全应用矩阵逐步补充。
传输方式仍是第 2 节列出的单独技术决策，不能用“功能优先”默认等同于复用旧 RDP 应用配置。

## 1. 产品决定与目标

### 2026-09-10 开发启动与产品对照

已获用户授权开始实现，首先执行第 0 节 DuiLib demo，不部署完整工作区。
本机代码已从 `ceecb52dc` fast-forward 至 `93c5cca09`，原有未跟踪文件保持不动。
现有 vcpkg 位于 `C:/source/vcpkg`，基线 `b216ddff25a1f432870e6c340ce79357049ef86e`；
计划使用 `duilib 2024-12-23#1`、`x64-windows-static-release`，port 固定上游
`502ac62be82c2bc33cf0e8635782fb370c68b1e7`。安装 dry-run 仅新增 duilib，不升级其他依赖。

产品交互参考：[TSplus 应用面板](https://docs.tsplus.net/tsplus/floating-and-application-panels/)、
[Citrix 应用发布](https://docs.citrix.com/en-us/citrix-virtual-apps-desktops/seamless.html)、
[Microsoft RDS 集合与 RemoteApp](https://learn.microsoft.com/en-us/windows-server/remote/remote-desktop-services/rds-create-collection)。
TSplus 的任务栏/应用面板最接近本轮交互目标，但不推断其内部实现，不引入这些商业产品作为依赖。
demo 后仍需明确：传输路线、无人连接后的图形环境、托盘/外部应用兼容、文件持久性和更新行为、
Windows Server 支持范围及部署容量。上述内容不阻塞独立 UI demo。

新增 `session-app` 应用类型，与 `game-hook`、`webview`、`rdp` 并列。
独立配置、账号映射及运行状态，不要求创建旧 RDP 应用，也不读取它的配置。
可以复用经过抽离和测试的底层能力，但不能用 `rdp` 别名冒充新类型。

- 专用 Windows 用户、独立 Session，使用该会话的 `WinSta0\\Default`。
- 不使用 SEB 的额外 Desktop；避免浏览器、Office、COM/外壳辅助窗口跨 Desktop。
- 不杀 Explorer、不替换全机 Winlogon Shell、不关闭 UAC、不改变其他用户桌面。
- 隐藏目标会话的原生桌面入口与任务栏，提供应用背景、窗口切换及状态界面。
- 保留管理员能力，不强制标准用户或 AppLocker；不修改宿主内置 Administrator。
  新账号权限拟定为显式 `account_role`，支持 `administrator`；不是偷偷提升原 RDP 账号。
- 正常运行每工作区只有一个新增常驻 `px_workspace.exe`，包含管理与 UI；
  崩溃后由既有 Rust Service 按需启动同一 EXE 的 `--recover` 模式。
- 客户端离开后 Render 仍按既有适用宽限期退出；工作区、账号和业务应用保留。
- 故障优先恢复普通桌面操作能力，而非锁死用户；这会暴露该专用用户原本拥有的系统入口。

本方案是兼容性优先的业务工作区，不是安全沙箱。管理员资格、文件对话框、外部浏览器和
任意业务扩展意味着不能承诺防止恶意用户运行其他程序或访问主机。账号分离不等于管理员之间的数据安全隔离。

## 2. 范围与尚未冻结的边界

第一阶段支持 Windows Server 专用会话、Windows 工作区界面和 Console 配置管理。
普通外部应用窗口允许出现，故“单应用”表示一个业务入口，不是只允许一个 EXE 或一个 HWND。
密码/UAC/安全桌面不拦截、不模拟同意，也不因为桌面变化而强制切回。

**画面与输入载体尚未冻结。** 用户确认了应用类型独立，但没有最终指定新模式采用会话采集还是原生 RDP 图形。
工作区原型先独立于远程显示完成；传输实现不能借此设计直接选择旧 RDP 配置或偷偷恢复被退休的链路。
若选会话采集，必须验证 DDA/WGC 等在 RDP 断开、显示设备变化、重连后的可用性和输入 Session 绑定；
若选原生 RDP，则要单独验证新类型的 Qt/Web 能力与账号传递，不把旧模式的验收直接移植。
双方都应复用产品已有认证控制消息通道，不为工作区控制再建一条网络连接。
会话创建可能内部使用 RDP 登录，但“建立会话”与“客户端画面协议”是两个不同选择。
无客户端时不为本方案预设新的常驻 RDP 保活连接；若图形应用确实依赖它，先报告实测结果再调整决定。

## 3. 架构及所有权

```text
Console：新应用配置 / 准入 / 凭证权威 / 持久工作区映射
    ↓ 既有管理通道
Rust Service（Session 0）
    ├─ WorkspaceSupervisor → px_workspace.exe（目标用户 Session）
    │                           ├─ 管理、状态记录、默认 Desktop 适配
    │                           └─ 背景 / 任务栏 / 业务窗口呈现
    ├─ ApplicationLifetime → 业务根进程与受控后代的 Job / 身份记录
    └─ RenderInstance → Render 与传输子资源（可超时退出）

异常：Supervisor → px_workspace.exe --recover → 恢复界面 → 退出
```

Windows 服务不能直接交互式操作用户桌面；Service 在目标用户身份和 Session 中创建助手，
通过受控 IPC 协作。禁止为了恢复而把 SYSTEM GUI 放进用户桌面。
依据：[微软 Interactive Services](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services)。

| 组件 | 拥有的职责 | 不允许负责的事情 |
|---|---|---|
| Console | 应用配置、访问授权、凭证版本、工作区绑定 | 通过客户端传入任意主机命令 |
| Rust Supervisor | 会话核验、进程监督、恢复调度、持久记录确认 | Session 0 直接 ShowWindow、按进程名扫杀 |
| workspace 管理部分 | UI 状态事务、会话内窗口发现、与 Service 心跳 | 修改全机 Shell、关闭无关应用 |
| workspace UI | 任务栏、背景、启动失败提示、窗口激活 | 全局抢焦点、给所有窗口强制 SetParent |
| Render | 连接、认证、画面/输入、宽限期 | 拥有业务进程、注销会话、恢复或销毁工作区 |

管理与 UI 可分线程，不分常驻进程；UI 心跳必须由 UI 事件循环应答。
`--recover` 分支必须在加载完整 UI/媒体/插件前分流，最小化启动依赖；独立恢复逻辑不能复用故障 UI 初始化。
可按需附加到已有健康 workspace；所有恢复和接管通过同一会话级互斥租约串行执行。

## 4. 账号、身份与配置

建议工作区键为 `(application_id, node_id)`，账户命名空间独立于 `grdp_`，凭证由 Console 加密保存。
本提案沿用单工作区单活跃前端、不自动抢占；不同访问者先后进入同一工作区共享文件与状态，UI 必须说明。
用户未登录也必须遵守应用访问策略和工作区占用，不把匿名视为免鉴权。

```text
SessionAppConfig
  schema_version / config_revision
  executable_path / arguments[] / working_directory
  account_role: administrator | standard
  launch_elevation: normal | elevated
  primary_exit_action: show_launcher
  shell_presentation: background_and_taskbar
  allow_auxiliary_windows: true
  tray_policy: unsupported_or_native_access（原型后冻结）
  transport_profile: 未冻结，不复用旧 rdp 配置
```

参数用数组保存，在 OS 边界一次性按 Windows 规则组装；可执行路径独立传递，保留空格、Unicode、反斜杠和空参数。
管理员组成员不等于高完整性进程；提升由 Service 对已发布配置执行受权启动，使用目标用户令牌而不是 SYSTEM。
不能开放客户端任意传入程序/参数后要求提权的 RPC。配置变更只影响新启动，不能因修改配置强杀现有应用。

身份至少包括：workspace_id、账号 SID、Session ID、登录 AuthenticationId/LUID、系统启动标识、
workspace_generation、配置版本、进程 PID + 创建时间 + 规范化完整路径。
Session ID、PID、端口、用户名或 EXE 路径单独都不是可用于接管/清理的身份。
每个专用 Session 最多一个工作区 UI 接管者，不能给一个 Session 配两套自定义任务栏。

## 5. 三套独立状态机

不要把已有 Render AppInstance 的 Stopped 直接显示为工作区销毁。

| 状态域 | 建议状态 | 含义 |
|---|---|---|
| 工作区呈现 | Dormant / Preparing / Active / Restoring / Restored / RecoveryRequired / Failed | 原生界面是否被接管 |
| 业务应用 | NotStarted / Starting / Running / Exited / Crashed / IdentityUnknown | 业务进程状态，不与 UI 混用 |
| 传输实例 | Starting / Connected / GracePeriod / Stopped / Failed | Render/访问连接状态 |

典型组合：Active + Running + Stopped = 工作区和应用运行、无人连接；
Restored + Running + Stopped = UI 故障后普通桌面已恢复、业务仍运行；
RecoveryRequired + Running + Failed = 保留应用、等待安全恢复，禁止自动再隐藏桌面。

## 6. 启动、退出和重连流程

### 6.1 首次启动

1. Console 准入、选择节点、获得工作区占用租约；节点核对专用账号、SID 和配置版本。
2. SessionBroker 建立/复用该账号会话；核对登录身份，不能改用活动 Console/Administrator 会话。
3. 查找未完成恢复事务，先恢复或明确标记冲突，不能在残留上重复接管。
4. Service 启动唯一 workspace；完成身份握手、协议版本、恢复能力及 UI 心跳自检。
5. 发现默认 Desktop、Explorer 和原生界面，保存基线；记录失败则不隐藏。
6. 创建但先不抢焦点的工作区背景和任务栏，UI ready 后逐项接管并验证。
7. 由 Service 受控启动业务根进程：挂起创建，加入本次私有 Job，再恢复执行。
8. 窗口就绪后显示应用；最后才报告工作区可用。任何部分失败走逆向恢复，而非继续黑屏。

### 6.2 连接离开

- Render 在既有适用宽限期内等待，重连撤销旧退出回调；超时只关闭自身资源。
- workspace、业务 Job 和 Session 不属于 Render 的 kill-on-close Job。
- Service 的渲染实例 GC 不能删除持久工作区、恢复日志或应用身份信息。
- 内部 RDP 连接关闭可能使 Session 断开；不得将“进程存活”冒充“显示环境完全不变”。

### 6.3 再次进入

重新授权；核对会话身份和三类状态。健康则连接原工作区；Render 已退出则新建传输实例。
会话已不存在时告知原会话丢失，可按启动配置建立新会话，但不能宣称原程序状态恢复。
若先前已恢复原生界面，不因重连自动重新隐藏；需要显式“重新启用工作区”操作或后续明确批准的恢复策略。

### 6.4 明确操作语义

| 操作 | Render | workspace | 业务应用 / 账号 / Session |
|---|---|---|---|
| 断开访问 | 宽限后停止 | 保留 | 保留 |
| 停止传输实例 | 停止，幂等 | 保留 | 保留 |
| 退出工作区界面 / 恢复原生桌面 | 不隐式改变，按访问授权决定 | 恢复成功后退出 | 保留 |
| 关闭业务应用 | 不隐式停止 | 显示重新打开入口 | 仅明确选中的自有应用，允许取消保存提示 |
| 注销 / 销毁账号 | 独立破坏性管理操作 | 不属于普通 Stop | 本轮不实现 |

用户关闭应用不是崩溃，不立即自动重启；初版崩溃默认显示错误并提供重开，不无限自动拉起。

## 7. 默认桌面与自定义任务栏

- UI 背景处于业务窗口下层；不隐藏整个 Progman/WorkerW 树来赌不同版本行为。
  原型识别具体桌面图标承载窗口，目标不明确则不修改并报告不支持。
- 枚举目标 Session 默认 Desktop 的顶层业务窗口，排除产品自身、外壳基础窗口和不可呈现窗口。
  记录 owner/root-owner、最小化、可见性、标题、图标、DPI；处理模态子窗口，避免激活被禁用的主窗口。
- 外部浏览器、Office 等同 Session 辅助窗口可显示在任务栏；“可显示/可激活”不授予注入、强杀或接管进程权限。
  业务所有权另按启动 Job 与完整路径 AND 验证；通过 COM/单实例唤起的窗口不自动成为自有进程。
- 正常业务的 ShellExecute、预览、提权仍需兼容测试，不能仅凭同 Desktop 推断全部支持。
- 窗口事件优先，低频核对补漏；不可用忙循环、反复 SetForegroundWindow 或全局强置顶。
- 任务栏尺寸优先使用 AppBar 协商，不直接持久改工作区矩形；Explorer 原任务栏隐藏后可能仍占保留区，
  是否出现双重保留是 P0 原型门禁，不宣称 ShowWindow 即可解决。
- 第一版先单显示器；显示器增加/DPI/分辨率变化必须检测，不支持时恢复原生界面而不是使用过期坐标。
- Win/系统快捷键拦截不是安全边界；仅目标交互会话内按产品输入策略处理，不屏蔽 UAC、安全桌面或宿主用户操作。

托盘是兼容性门禁：只最小化到托盘的应用不能“消失”。第一版不伪造完整 Explorer 托盘协议。
原型比较受控临时显示原生任务栏/托盘与正式托盘适配；若原生入口违反发布要求且无替代恢复方法，
该应用标记不兼容，不能把整个能力宣称完成。ManagedShell 只作只读参考，暂不引入 .NET/WPF 依赖。

AppBar API 依据：[SHAppBarMessage](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shappbarmessage)。

## 8. 事务式状态记录与恢复

### 8.1 记录模型

Service 管理安装器控制的状态目录；文件不可由客户端指定路径，不跟随重解析点。
按工作区/登录身份/代次保存版本化 journal，另有 Service 的已确认版本记录。
日志不包含账号密码、连接票据和任意命令；管理员本身可能修改机器，不能声称能防御该管理员篡改。

每项记录：对象语义标识、原值、拟写值、对象代次、准备/应用/恢复阶段、错误码。
窗口 HWND 只作当前观测，不作为下次启动的权威定位依据；同时核对 Shell PID、创建时间、Session、类名与结构。

协议：Observe → Prepare（持久化且确认）→ Apply → Verify → Applied（持久化）。
使用临时文件、校验和、原子替换及适当刷盘；每一步都是可注入崩溃点。
Apply 后尚未写 Applied 就崩溃的窗口也必须能通过 Prepare 的原值/拟写值进行补偿。
恢复倒序执行，逐项记录进度，重复执行应幂等。

### 8.2 恢复规则

1. 核对登录会话身份并取得互斥租约，拒绝旧代次恢复覆盖新工作区。
2. 对仍匹配的对象，只在状态仍等于本次拟写值时恢复原值；已经等于原值算成功。
3. 发现用户/第三方改变了状态则记录冲突，不用旧快照强盖新设置。
4. Explorer 重启后新窗口不能沿用旧 HWND 原值。若仍在 Active，先记录新 Shell 基线再接管；
   若处于恢复，按明确的“恢复默认可用界面”补偿策略处理并记录其不是精确回滚。
5. 移除本次 AppBar 注册/占位，恢复被改过的桌面与原生任务栏，重新核对当前显示拓扑。
6. 原生界面确认可操作后撤掉产品背景和任务栏，释放输入 Hook/窗口事件订阅，提交恢复完成。
7. 某项失败则为 RecoveryRequired，不谎报成功、不删日志，不循环强杀 Explorer。

“原有状态”限于我们实际改过的界面状态；不承诺恢复其他程序内部数据、原先所有窗口位置、
剪贴板、系统崩溃前内存内容或用户之后修改的设置。默认不修改这些内容。

### 8.3 监督与故障处理

| 故障 | 检测与动作 |
|---|---|
| workspace 正常退出 | 先恢复、ACK，再退出；Service 读 journal 确认 |
| 崩溃 / 强杀 | 进程句柄退出事件；启动 `--recover`，不重启业务 |
| UI 卡死 | UI 线程对带序号 ping 应答；持续超时后核对身份并停止精确 workspace，再恢复 |
| Render 崩溃 | 只处理传输；不恢复或关闭工作区 |
| Service 崩溃 | 健康 workspace 保持；短暂 IPC 断开不立即恢复；超过监督租约可自恢复界面 |
| Service 重启 | 重新鉴别现有 workspace，健康则续租；不确定则恢复，不按 PID/端口直接认领 |
| Explorer 重启 | 识别新 Shell 代次，重新建立基线；无法安全接管则恢复/降级 |
| 锁屏 / 安全桌面 / Session 断开 | 标记环境不可交互，不能单凭无画面认定 UI 卡死 |
| 恢复程序崩溃 | 有限退避重试；超限报错并保留日志，提供 Service 管理恢复入口 |
| 系统重启 / 用户注销 | 旧登录身份 journal 标记过期，不对新登录 Session 回放窗口修改 |

建议原型 UI ping 2 秒，持续无响应阈值 15 秒；恢复 60 秒内最多 3 次。它们是待测参数，
不改变 Render 的既有宽限期；必须考虑机器负载、锁屏和断开状态，避免误杀。
无后台重启 UI 的无限循环；恢复后需要明确重启工作区操作。
Service 与 workspace 同时被杀且无人重新启动时，无法保证即时恢复。部署的服务恢复配置只能改善可用性，不能消除此限制。
工作区恢复模式的 EXE/依赖损坏同样会阻塞恢复；更新必须保留可用版本，卸载必须先执行恢复。

## 9. 业务进程所有权及服务重启

业务 Job 独立于 Render 和 workspace UI；不设置因最后句柄关闭就结束业务的 kill-on-close 行为。
由 Service 持有并按安全 ACL/随机标识创建；可以将专用 Job 句柄显式交给 workspace 保活，
句柄白名单不能泄露给业务应用。Service 重启后经身份握手重新获得句柄并核验成员。
若 Service 与 workspace 同时退出导致 Job 身份丢失，业务仍可运行，但标记 IdentityUnknown；
不重新注入、不按路径认领、不强杀，也不盲目启动第二份。需要明确管理流程处理。
旧 game-hook 的 Job AND 路径准入和 stopped 幂等规则完全不变。

依据：[Windows Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)。

## 10. IPC、消息与审计

Service 与 workspace 使用本机命名管道等 IPC，不新增公网/局域网监听端口。
首版沿用既有本机 IPC 约束，限定 Service 与专用用户，核对工作区、对端进程和 Session 身份，
避免串会话及旧进程迟到消息。复杂重放防护和对抗恶意管理员的机制不作为首版前置条件。
请求包含 request_id、workspace_id、generation、config_revision、超时；操作幂等，旧代次拒绝。

拟定命名消息：Hello、UiPing/UiPong、PrepareMutation、MutationApplied、RestoreWorkspace、
RestoreProgress、WorkspaceStatus、LaunchConfiguredApplication。协议枚举生成，禁止散落数字 case。
网络侧通过既有认证控制通道转发 typed 状态/操作，不直接转发任意本机 IPC。
不确定超时先查询状态，不自动重发启动/提权/关闭应用操作。

审计记录状态迁移、恢复原因、SID/Session 绑定、配置版本、精确进程身份、修改项及结果。
默认不记录窗口标题中的用户内容；敏感诊断显式开启并脱敏。

## 11. 代码落点与兼容策略

当前检查基线：Console `ApplicationType` 只有 GameHook/Webview/Rdp；
`service_core/rdp_account.rs` 明确只创建标准账号；`rdp_workspace.rs` 已有持久身份与保护目录逻辑。
这些不是新模式已经实现的证据。

| 位置 | 拟定变更 |
|---|---|
| `rust_server/px_console_server/src/app_schedule/manager.rs` | 新类型与独立配置、工作区状态查询；不改变旧类型默认值 |
| Console `app_schedule/session_workspace.rs`（新） | 独立映射/凭证域/配置版本；不能使用旧 RDP vault 的 AAD/记录类型冒充 |
| `rust_client/px_service/service_core/src/app_instance.rs` | 新传输实例分支；持久工作区不进入实例退出/TTL 清理 |
| `service_core/session_app_{account,workspace,supervisor}.rs`（新） | 新权限策略、恢复状态、监督和 Job 生命周期 |
| `src/px_workspace/`（新） | C++/Win32 管理、经典 DuiLib 最小 UI、最小恢复入口、状态适配和测试 |
| `src/px_render/` | 只在传输方案批准后新增会话精确绑定入口，不接管工作区生命周期 |
| Console Web/Client/Panel | 新类型展示、三域状态、恢复和重新启用操作、能力不足提示 |

旧 RDP 强制标准用户策略不能放宽。若抽取通用账号/凭证能力，旧行为原样保留并有回归测试；
涉及退休分支按仓库规则先归档真实旧内容，不能借机重构无关代码。
原生代码遵守智能指针、RAII、确定初始化及 150 列规则；workspace 不引入 Qt，已有 Qt 模块仍遵守单一所有权规则。
异步回调使用 weak_ptr 锁定，退订和销毁排队事件必须测试。Windows 借用句柄用有类型观察值，拥有句柄用 RAII。

## 12. 完整阶段路线（首版以第 0 节裁剪范围为准）

### P-1：vcpkg + DuiLib 最小 UI demo（下一项任务）

按第 0 节执行，先验证构建、最小依赖和可见界面，发布到独立 demo 目录供用户查看。
未通过前不进入 P0 的会话/Explorer 修改；demo 不承担桌面接管或恢复职责。

### P0：不接传输的恢复原型

专用测试账号/Session，最小背景和任务栏；只操作可识别的原生界面。
必须通过：原状态记录、强杀恢复、UI 卡死、Service 重启、Explorer 重启、应用不中断、其他会话不受影响。
同时验证双重 AppBar 占位、管理员窗口交互、托盘入口；允许通过保留原生入口完成兼容，
不把完整自定义托盘作为门禁。复杂故障项可后续补充，但正常退出和崩溃恢复不能省略。

### P1：产品工作区模型

Console 新类型、独立账号域、状态/租约、参数验证；Service 幂等启动/重连、Job 持久身份、恢复 IPC。
基础恢复和精确所有权与 UI 同步实现后再开放访问；旧模式涉及改动的回归必须通过。

### P2：业务 UI 与兼容性

窗口分组、模态焦点、浏览器、Office 预览、文件对话框、中文 IME、提权、DPI、原生托盘访问。
明确可支持应用清单；复杂代理启动不支持时报告原因，不能把未知窗口强搬或强杀。

### P3：传输选择与接入

用 P0/P2 证据决定会话采集或原生 RDP；该选择是实现前需要确认的产品边界。
验证无人连接后 Session 图形状态、Render 退出和新 Render 接回、Qt/Web 能力，不承诺自动兼容。

### P4：故障验收与交付

分批测试，每批含收尾最多 10 分钟；90 不运行 game，只使用轻量业务应用，Office 不存在则明确待测。
检查每个批次的会话和原生界面恢复，不能把应用 Stop 当作注销或删除账号。
通过后增量构建；所有变更运行产物同步 `build_official/dist` 并校验 SHA-256，远端部署也记录哈希和回滚位置。

## 13. 完整测试矩阵（扩展清单，不要求首版全部完成）

| 类别 | 必测场景 |
|---|---|
| 原始状态 | 任务栏原来可见/隐藏、自隐藏、桌面图标关闭、持久化失败时不修改 |
| 崩溃点 | Prepare 前后、Apply 后未 ACK、恢复一半、恢复已完成未 ACK；重复恢复 |
| 身份 | PID/Session 复用、旧 generation、SID 不一致、同 Session 重复 UI、跨用户 IPC |
| 生命周期 | 断开/宽限内重连/超时重连、Render 强杀、Service 重启、UI 强杀/卡死 |
| Shell | Explorer 重启、新窗口句柄、任务栏再显现、显示拓扑变化、锁屏/UAC |
| 业务 | 子进程、浏览器新实例与已有实例、Office 预览、文件选择、模态窗口、托盘 |
| 所有权 | Job+路径 AND、独立同名进程不接管、Job 身份丢失不重启应用、业务不因 UI 退出被杀 |
| 授权 | 游客策略、第二客户端忙状态、撤权只断访问、不注销、不泄露账号密码 |
| 更新 | UI/Service 协议版本不兼容、旧 journal、可恢复版本保留、卸载前恢复失败则阻止静默卸载 |
| 回归 | game/WebView/RDP 配置和生命周期不变；其他用户桌面、应用、服务不受影响 |

通过标准不是“看见了自定义任务栏”，而是指定范围内业务兼容、故障可恢复、精确会话隔离、
传输退出不影响工作区，以及限制/不支持项明确可见。

## 14. 参考与被否决路线

- streamer `0a92ad9762de5f27ef70c3935ccad75d94eefd19`、AppGuard `37e2e2cdddaeeaee819bb92fe0b30f96552789ec`：
  本地只读参考 `D:/dolit/streamer`、`D:/dolit/dlAppGuard`；不照搬扫杀 Explorer、管理员即安全隔离或自动注销。
- [SEB KioskModeOperation](https://github.com/SafeExamBrowser/seb-win-refactoring/blob/a7f86ea4fff99af7c69b4d3bb89d5264adea4f90/SafeExamBrowser.Runtime/Operations/Session/KioskModeOperation.cs)：
  参考初始化/恢复职责，不采用独立 Desktop 和终止 Explorer 路线。
- [ManagedShell](https://github.com/cairoshell/ManagedShell)：任务、托盘、AppBar 参考，不等于已决定引入库或已验证 Server。
- [桌面归属规则](https://learn.microsoft.com/en-us/windows/win32/winstation/thread-connection-to-a-desktop)：
  默认 Desktop 仍需精确 Session/令牌绑定，不把辅助进程继承视为无条件保证。

本设计不部署微软 Shell Launcher、不实现完整 Explorer 替代品、不引入考试锁机策略；
也不把普通断开改成常驻 Render、注销用户或关闭业务应用。
