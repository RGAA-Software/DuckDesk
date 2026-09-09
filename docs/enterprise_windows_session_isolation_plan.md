# 企业 Windows 会话隔离：讨论与参考记录

> 日期：2026-09-08。状态：早期方案与调研记录，尚未实现或完成实机验收。
> 最新方向见 [RDP 应用模式设计](rdp_application_mode_design.md)：新增与 game-hook/webview 平行的 RDP 模式，
> 直接使用 FreeRDP 的画面、输入及受控虚拟通道，不再在目标用户 Session 里另起 Render 做 DDA/GDI 桌面采集。
> 下文第 1–8 节及第 12 节保留早期设计背景，不作为当前实施计划；第 9–11 节的源码与官方资料仍供参考，
> 其中“仅保活”“禁用画面后继续采集”等优化设想不适用于新的活跃 RDP 画面来源。
> 用户已明确需要独立用户、RDP 维持会话和企业级隔离能力。本文将其转为实现边界与分阶段验收。
> 用户纠正：game-hook、webview 不需要新建 Windows 用户，沿用现有用户环境与应用实例机制。
> 新建用户、独立 Windows Session 与 RDP 维持仅用于新增的隔离桌面模式；不得将其变成 game-hook/webview 的启动前置条件。
> 首期用户信任模型尚待确认：以下会话方案面向企业内部受管应用；互不信任且能运行任意程序的租户另需虚拟机级部署设计。
> 本文提出企业模式的新契约，不表示已有单桌面产品行为已被修改。

本文是本轮讨论的持续记录入口：第 1–8 节为产品边界、实现差距与设计，第 9 节为决策状态，
第 10 节为参考仓库索引，第 11–12 节为 FreeRDP/60fps 调研与短时验证方案，第 13 节为交接状态。

## 1. 产品目标与边界

保留现有主机桌面和 game-hook/webview 应用入口，新增独立的企业隔离桌面入口。
本文“工作区”专指隔离桌面绑定的 Windows 用户身份、交互会话及数据环境。
现有 game-hook/webview 不分配新 Windows 用户，不为其额外建立 RDP 会话，也不强制迁入隔离桌面工作区。
隔离桌面无法恢复时报告不可用，禁止回退到当前 Console 或其他活跃用户会话。

- 第一阶段建议使用 Windows Server RDSH，具体支持版本由实机验证锁定；不承诺 Windows 客户端系统的多用户并发。
- 建议隔离桌面第一版按企业用户分配持久工作区；重连复用原身份与配置，不逐次生成新用户。
- 隔离桌面默认一名控制者，关闭观察和接管；现有 game-hook/webview 的角色策略保持其既有产品契约。
  企业协助访问必须明确授权、可撤销并审计；不是知道设备密码即可访问员工桌面。
- 使用标准 Windows 用户和明确 ACL。不同用户共享宿主内核和硬件，不能把该方案宣传为虚拟机或强租户沙箱。
- 用户可见界面使用“我的工作区”“连接”“断开”“结束会话”。“断开”保留工作，“结束会话”可能关闭未保存应用，必须清楚提示。
- 沿用当前 Native UDP+FEC 媒体、WS/WSS 控制与文件，Web 使用现有 WebRTC；本机 RDP 仅是隔离桌面的会话基础设施。
  不恢复已退休的 Native RTC/Relay 选择入口。

微软分别定义用户、会话和虚拟机安全边界；本设计选择标准用户与会话边界，不将其等同于虚拟机隔离。
参考：[Windows 安全服务标准](https://www.microsoft.com/en-us/msrc/windows-security-servicing-criteria)。

## 2. 已核实的实现基础

### 2.1 外部只读参考

- `D:/dolit/dlAppGuard`，核查时 HEAD `37e2e2cd`：
  - `app/service/cloudapp/cloudapp.go:206`：创建或复用用户，现实现随后加入管理员组；企业方案不沿用管理员授权。
  - `app/tools/rds/rds.go:108`：启动外部 RDP 客户端，传分辨率、音频等参数；现实现通过命令行传密码，不沿用这种凭证传递方式。
  - `app/service/container/streamer.go:378`：按用户名定位 Windows Session，启动采集进程，区分用户与 SYSTEM 身份。
  - `app/service/container/win_session.go:305`：检查会话、锁屏和采集状态，异常后重新登录；空闲缓存、断开和注销另有分支。
- `D:/dolit/streamer`，核查时 HEAD `0a92ad976`：
  - `src/cloudapp/desktop/win/desktop_capturer_win_impl.cc:257`：DDA/GDI 初始化、失败恢复及桌面就绪检查。
  - `src/cloudapp/desktop/win/desktop_manager_win_impl.cc:118`：会话内默认桌面或专用桌面管理。
- 尚未核实外部 RDP 客户端内部实现，不能承诺其无界面运行、Session 0 可用、停止图像更新或低开销。

### 2.2 GammaRay 当前差距

| 位置 | 已核实的现状 | 企业模式需要的改变 |
|---|---|---|
| `rust_client/px_service/service_core/src/state.rs` | 一个 `last_desktop_launch`、`desktop_pid`、桌面心跳 | 每个工作区独立的实例状态、健康与持久化 |
| `rust_client/px_service/src/service_host.rs::start_desktop` | 启动不同桌面规格时先停止旧桌面 | 按工作区串行化启动，其他用户不受影响 |
| 同文件 `stop_desktop` | 枚举受管理 Render/user proxy，未按工作区/SID/Windows Session 筛选 | 基于已登记的所有权精确停止；旧管理入口不能触达企业实例 |
| `rust_client/px_service/src/windows_process.rs` | 启动接口依赖活动 Console，存在 service-token 启动路径 | 显式目标 Session 和用户 SID，禁止企业工作区身份降级与 SYSTEM 回退 |
| `service_core/src/process.rs::ProcessSnapshot` | PID、路径、命令行、父 PID | 补充 Session、SID、进程创建时间和实例代次；不能按进程名确定所有权 |
| `src/px_render/session/logical_session_registry.h` | 单个 Render 内的控制者/观察者和连接租约 | 保留每实例控制权，增加其与工作区及 Windows 身份的可信绑定 |
| `service_core/src/app_instance.rs`、`service_host.rs::start_app_instance` | 已有 game-hook/webview 注册表、独立端口和启动就绪，使用现有用户启动接口 | 保留应用启动路径，不增加账号创建或 RDP 前置步骤 |
| `service_host.rs::stop_app_instance` 及启动失败清理 | 存在按游戏 EXE/view 路径查找进程的兜底 | 与新隔离桌面共存时，禁止清理其他模式或用户拥有的同路径进程；就绪探测也需限定归属 |
| `src/px_render/app/win/app_manager_win.cpp:271` | 用户启动失败有 SYSTEM 上下文回退分支 | 作为应用模式独立审查项，不据此强制迁移账号；隔离桌面路径不得复制这种身份回退 |
| `src/px_render/webview/webview_runtime.cpp` | CEF OSR、按实例分 profile、启动/退出清理临时 profile，`no_sandbox = true` | 保留既有 OSR/profile 行为；沙箱和持久 profile 属于独立待评估项，不作为本次新建用户工作的一部分 |

现有 LogicalSession 是网络访问会话，不是 Windows Session；不能用增加观察者数量来实现多用户桌面。
现有 `docs/logical_session_product_definition.md` 含历史传输策略；传输边界以更新的
`docs/native_client_sdk_transport_decision.md` 为准。企业细粒度权限作为新模式设计，不悄然修改旧角色语义。

## 3. 身份链与职责

```text
企业账号 / 管理策略
       ↓ 签发限定工作区与能力的访问许可
Console 调度 → px_service
                 ├─ 现有应用实例管理（现有用户环境）
                 │    ├─ game-hook：Render + 游戏 + Hook
                 │    └─ webview：Render + CEF + 实例 profile
                 └─ 新增隔离桌面工作区管理
                      ├─ Windows 账号 / SID / profile / ACL
                      ├─ 本机 RDP 会话保持进程
                      └─ 指定 Windows Session 内的 desktop Render / user proxy
客户端 → 已授权的应用实例或隔离桌面 → 对应 Render 的 LogicalSession
```

每个工作区至少保存以下类型明确的身份：组织 ID、用户主体 ID、工作区 ID、主机 ID、Windows 用户 SID、
Windows Session ID、Windows 登录身份标识（如 logon LUID）和工作区 generation；其桌面实例另外保存
Render 实例 ID、进程创建时间和实例 generation。game-hook/webview 保留现有应用实例身份，不强制增加工作区绑定。
Session ID 和 PID 都可能复用，不能单独作为授权或重启后接管进程的依据。

- Console 负责企业身份、分配、能力策略、配额和审计；客户端不能自行指定 Windows 用户名或 Session ID 获得访问权。
- Service 负责特权账号操作、会话协调、精确进程生命周期；通过现有 composition root 注入具体能力，避免新建全局服务定位器。
- 隔离桌面 Render 固定绑定一个工作区并校验启动身份。普通用户不能伪造另一个实例的注册、心跳或健康状态。
- user proxy、音频、剪贴板和设备代理绑定同一身份链；确需特权的操作使用小型、受限、可审计的 Service 接口。
- RDP 保持进程只得到必要登录能力。凭证不通过 argv、共享环境变量、日志或开放的本机 HTTP 端点传递。
  具体秘密存储和传递机制在原型阶段选择，需限制至服务身份并支持轮换与撤销。

### 3.1 应用实例契约

工作区是新增隔离桌面的 OS 身份与资源边界，AppInstance 是现有应用的运行与发布边界，
LogicalSession 是客户端访问边界。它们可共用调度、授权和审计能力，但生命周期分别管理。

| 模式 | Windows 用户与 RDP | 运行与隔离范围 |
|---|---|---|
| 现有主机桌面 | 使用现有桌面环境 | 保持现有远控行为 |
| 新增隔离桌面 | 创建/复用专属标准用户，建立独立 Session 并由 RDP 维持 | 用户、桌面、数据与访问权限隔离 |
| game-hook | 不新建用户，不新增 RDP 维持流程 | 独立 Render/游戏实例、既有 Hook 捕获与输入终点 |
| webview | 不新建用户，不新增 RDP 维持流程 | CEF 离屏渲染、直接页面输入与实例 profile |

- 应用访问授权不自动扩大为完整桌面权限；三种内容模式分别使用自己的输入终点。
- game-hook/webview 的账号隔离和 RDP 依赖保持现状；不把新模式的标准用户创建、登录或注销流程接到应用启动/停止路径。
- 同一 Windows 用户下的独立应用进程或 CEF profile 不代表 OS 用户隔离。应用实例的数据分离与隔离桌面的 SID/ACL 边界分别描述。
- 已发现的同路径游戏进程清理问题需要在共存门禁中覆盖，防止现有应用操作误伤新隔离桌面的进程。
- WebView 继续使用现有临时 profile；持久登录态、浏览器沙箱和网页网络权限作为后续独立议题，本文不宣布已经实现或改变其行为。

### 3.2 工作区与实例的独立生命周期

停止 game-hook/webview 只处理该应用实例，不执行工作区注销或 RDP 清理；停止隔离桌面也不得影响现有用户环境中的应用实例。
工作区按自身连接、保留租约和管理操作决定回收。重启其 Render 保留工作区身份；重建 Windows 登录会话推进工作区 generation，
使该工作区旧授权失效。应用模式使用原有实例状态机，不经过工作区 Provisioning/LoggingOn。
就绪验证按模式区分：WebView 使用首帧，game-hook 验证本实例游戏与捕获通道，隔离桌面额外验证目标 SID/Session 和桌面采集。

## 4. 隔离规则

本节用户 SID、工作区和 RDP 条款用于新增隔离桌面模式；不要求 game-hook/webview 创建 OS 用户。

| 对象 | 必须满足的规则 |
|---|---|
| 身份与权限 | 默认标准用户；不得赋予宿主管理员或调试权限。平台管理员是明确的受信角色，员工隔离不承诺对抗宿主管理员 |
| 桌面与输入 | 采集线程、输入目标与会话身份一致；恢复期间停止输入，拒绝旧 generation 的事件；禁止 Console 回退 |
| 数据与文件传输 | profile、工作目录、临时目录、下载、录制使用 SID ACL；默认只访问授权目录。用最终打开句柄校验路径与重解析点，避免检查/使用竞态 |
| 剪贴板 | 每个 Session 独立代理，按策略区分上传/下载及文本/文件；禁止共享全局内容缓存 |
| 音频与语音 | 验证采集只覆盖当前工作区的目标应用/会话，不能直接假定系统 loopback 已隔离。无法证明隔离时禁用该能力 |
| 网络与 IPC | 每实例独立端点和秘密；同机其他用户也视为不可信调用方。校验调用身份、工作区和能力，不能因为来源是 loopback 就放行 |
| RDP 重定向 | 默认关闭 RDP 剪贴板、磁盘、打印机、USB、麦克风等重定向；必要的音频/显示设置显式验证，防止产生第二条未受管数据通道 |
| 设备与驱动 | USB、手柄、摄像头、虚拟显示器逐项证明会话归属；全局设备不能仅靠用户名实现隔离，未验证项默认不可用 |
| 进程 | Render 与应用在目标用户运行；进程树以受控 Job/身份记录管理。停止 A 不得影响 B；Job 只用于生命周期和资源管理，不作为完整安全沙箱 |
| 资源 | CPU/内存/并发可配置限额；GPU、显存、编码器和磁盘吞吐先做能力探测及准入，不承诺通用硬件 GPU 硬隔离 |
| 审计 | 记录分配、连接、授权变化、恢复、结束、协助和文件操作元数据；不记录密码、票据、文件内容或剪贴板正文 |

隔离桌面的访问许可绑定组织、用户、工作区、Render 实例、generation、能力、有效期和防重放信息；
每条控制、文件和媒体关联路径均验证同一目标。撤销必须能终止既有绑定，而不仅阻止下次连接。
设备共享密码不得绕过企业身份或列举、访问员工工作区。控制权转移只在同一工作区内发生。

## 5. 生命周期与故障恢复

本节描述新增隔离桌面的生命周期；game-hook/webview 保持独立应用实例生命周期。

工作区主状态：`Requested → Provisioning → LoggingOn → Ready → Disconnected → Draining → Stopped`。
实例使用独立的 `Starting → Ready → Draining → Stopped` 状态。
对客户端报告可用同时要求：工作区 SID/登录会话校验通过、实例 Render 注册成功、授权通道可用、模式对应的捕获健康
（静态桌面不强制持续新帧）。
故障恢复进入 `Recovering`，确定性错误进入 `Failed` 并附明确原因。

- 一个工作区同时只能有一个状态变更流程；创建/重连请求有幂等键，多个客户端重试不得重复创建账号和 Render。
- 客户端断网、RDP 断开、Render 崩溃是三个独立事件；客户端断网不立即注销 Windows 用户。
- RDP 异常先暂停授权输入，重新确认 SID/Session，再恢复 RDP 和采集；不得直接注销用户丢失工作。
- 同一账号恢复且 Windows 登录身份未改变时可重新绑定；身份或 Session generation 改变后旧连接全部失效。
- Service 重启后重新核对会话、进程句柄/创建时间、SID 和实例记录，再决定接管或清理；不能只凭 PID 认领进程。
- 密码错误、账号禁用、RDS 配置错误等停止盲目重试并告警；暂态错误使用有界重试节奏，避免无限创建进程。
- 结束工作区先撤销连接与输入，再停止应用/Render/RDP，最后注销会话；持久账号和数据不随断连自动删除。
- 持久化仅保存必要身份和策略引用。临时账号删除、profile 清理与员工数据保留分别设定，不能混成一个 Stop 操作。

Windows 明确区分 Active 与 Disconnected，已登录不能代替可连接状态检查：
[WTS 会话状态](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/ne-wtsapi32-wts_connectstate_class)。

## 6. 分阶段交付

| 阶段 | 交付物 | 完成门禁 |
|---|---|---|
| P0 可行性 | 选定 RDP 保持客户端、Server 测试环境与部署条件；测量本机 RDP 开销 | 两个标准用户同时拥有可采集的独立会话；管理会话退出后仍工作；确认凭证与 RDP 重定向边界 |
| P1 Service 基础 | 工作区注册表、显式 Session 启动、独立端口/IPC、账号与进程所有权、恢复状态机 | 启停 A 不影响 B；SID/Session/PID 复用拒绝；Service 重启正确恢复 |
| P1a 现有模式共存 | 保留 game-hook/webview 的现有用户与启动路径，限定进程清理归属 | 应用启动/停止不创建或注销用户、不额外连接 RDP；现有应用与隔离桌面互不误停 |
| P2 端到端授权 | Console 工作区分配、按工作区签发与撤销、Render 绑定、客户端入口 | 跨用户票据、端口扫描、重放、媒体关联及设备密码绕过全部失败 |
| P3 数据与设备 | 文件/剪贴板/音频/录制隔离、资源准入、企业能力策略 | 双用户数据、音视频和输入负向测试通过；未验证设备功能不可用 |
| P4 管理与发布 | 管理列表、协助授权、审计、数据保留、部署文档、失败诊断与升级迁移 | 单桌面回归及完整双用户验收通过，发布物同步与 SHA-256 一致 |

目录建议：纯状态及策略放在 `service_core` 的工作区模块；Windows 账号、WTS、进程与 RDP 适配放在 `px_service`。
Render 增加明确的工作区启动上下文和授权校验，沿用现有媒体模块，不复制第二套 Render 实现。
所有新增 C++ 遵循项目智能指针、确定性初始化、RAII 和异步关闭规范；不得改动只读外部参考项目。

RDP 客户端选型先验证无交互启动、Session 0/辅助进程部署方式、可控停止/重连、证书校验和安全凭证注入，
不能直接依赖开发机上已经登录的管理员桌面。AD 域账号接入与身份联合保留为后续真实需求，不预建多套适配框架。

## 7. 测试与发布约束

延续用户“短时测试，最多 10 分钟”的要求：每次实际测试执行设置不超过 10 分钟总截止时间，
不以多个连续长窗口规避限制。完整功能分批验收，报告实际覆盖和未覆盖项；短测结果不宣称长期稳定性已验证。

最小实机矩阵使用同一 Server 的用户 A/B，两路客户端显示不同动态标记并记录目标 Session：

1. 同时启动、断开、重连；一个账号重复请求不得创建多份工作区。
2. A/B 键鼠、文本与文件剪贴板、上传下载、录制和测试音轨互不混入。
3. A 访问 B 的目录、IPC、票据、输入租约、媒体端点均被拒绝；覆盖路径穿越、重解析点与检查/使用竞态。
4. 结束 A、强杀 A 的 RDP/Render、锁定 A、服务重启，核对 B 的连接和应用进程不受影响。
5. 制造迟到回调、撤销中请求、注销重登后 Session/PID 复用，确认旧 generation 无法重新接管。
6. 配额不足、凭证失效、RDP 登录失败、静态桌面与采集设备丢失：返回正确原因，资源最终回收。
7. 验证 user proxy、USB/手柄等设备边界，未实现项记为不可用；禁止在结果中标为通过。
8. 回归原有单桌面、Native 与 Web；检验企业工作区不可通过旧 Stop 或共享设备密码越界操作。
9. 在现有用户环境启动 game-hook，并在隔离桌面运行同路径程序，覆盖 UE 启动器退出、view 成孤儿及启动失败清理；两侧不得相互误判或误停。
10. 回归 WebView 的 OSR、实例 profile 及现有退出清理；启动/停止 game-hook/webview 时核对没有新增用户或额外 RDP 保持进程。
11. 仅有游戏/网页访问许可的用户不能通过平台入口获得隔离桌面访问权；停止应用不触发隔离桌面回收，停止隔离桌面不关闭现有用户中的应用。

自动化覆盖队列中对象销毁、派发中注销、回调触发关闭、反复启停及初始化各失败点。
交付 C++ 使用 `scripts_build/build_cpp_*.bat`；只有明确要求全量发布构建才用 `build_official.bat`。
客户端运行产物必须同步至 `build_official/dist` 并核对 SHA-256 后才报告可验证。

## 8. 部署条件与待定项

- 第一版面向受管企业员工还是不可信外部租户：待用户确认，决定会话隔离与 VM 方案的实施范围。
- Windows Server/RDSH、并发与 GPU 驱动需在目标环境实测；GPU 能力参考
  [微软 RDS 支持配置](https://learn.microsoft.com/en-us/windows-server/remote/remote-desktop-services/rds-supported-config)。
- RDS CAL 是部署要求的一部分，本机回环 RDP 加自有串流协议不能据此假定豁免；部署时核对组织适用授权。
  参考：[RDS CAL](https://learn.microsoft.com/en-us/windows-server/remote/remote-desktop-services/rds-client-access-license)。
- 需要一台可创建两个测试用户的 Server 环境。已有 10.0.0.90 的机器角色和系统能力先只读核实；
  实际安装 RDS、改策略或创建用户前明确列出具体变更，不因设计需求直接改变当前远控测试环境。
- 首期账号生命周期、重连保留时长、并发上限和默认数据能力属于待定产品参数，不能把本文建议值视为已实现能力。

## 9. 讨论结论与决策状态（2026-09-08）

| 议题 | 当前记录 | 状态 |
|---|---|---|
| 企业级产品方向 | 需要用户隔离、独立桌面会话及配套权限管理 | 用户明确要求 |
| 参考模式 | AppGuard 建立/维持 RDP 会话，streamer 在会话内部采集、编码 | 本地源码已核实 |
| game-hook / webview | 沿用现有用户环境，不新建用户；不增加本轮 RDP 会话维持流程 | 用户明确纠正，后续设计必须遵守 |
| 隔离桌面 | 专属用户、独立 Windows Session、RDP 维持及 Render 会话归属 | 本轮设计方向，未实现 |
| FreeRDP | 适合作为会话保持客户端的候选；拟用库封装受管 helper | 调研结论与实现建议，尚未完成选型验收 |
| 60fps | 有可配置的 RDP 上限依据，1080p60 可作为首个验证目标 | 不是本项目已有实测能力或 SLA |
| 内部员工 / 外部租户 | 尚未确定；本文先按受管员工设计，不声称满足任意不可信程序强隔离 | 待确认 |
| 标准用户、默认关闭协助/接管 | 建议的隔离桌面安全默认值 | 设计建议；不改现有应用角色语义 |
| 账号持久化、空闲保留、数据保留与配额 | 已列入设计，但具体产品参数未确定 | 待确认 |
| 测试时间 | 实际测试总截止不超过 10 分钟，清理时间计入；报告未覆盖内容 | 沿用用户限制 |
| RustDesk 参考目录 | 统一使用 `D:/source/rustdesk`；主仓库内旧副本已按用户要求移除 | 已执行 |

用户曾询问应用模式是否也能采用这类设计，随后明确 game/webview 不需要新用户。
最终边界以上表为准，不将中间讨论中的“所有应用都绑定新用户工作区”作为实施要求。

## 10. 参考仓库与源码导航

以下版本于 2026-09-08 读取。版本号用于定位当时的源码，不表示已跟随远端最新提交。
外部路径是本机查阅位置，不应写入 GammaRay 构建依赖、安装包或运行时搜索路径。

| 本机目录 | 核查版本 | 用途 |
|---|---|---|
| `D:/dolit/dlAppGuard` | `37e2e2cdddaeeaee819bb92fe0b30f96552789ec` | Windows 用户、RDP 登录与会话守护 |
| `D:/dolit/streamer` | `0a92ad9762de5f27ef70c3935ccad75d94eefd19` | 会话内桌面管理、采集及健康状态 |
| `D:/dolit/rdp` | `b9183a9` | 自有、尚未完成的 Qt RDP 客户端；已有较完整协议/图形/输入/剪贴板实现，新模式优先复用基础 |
| `D:/dolit/rdp/FreeRDP` | `6c867799c4a50b7c3abf88d32c0f4e7123ce9caa`；describe 为 `3.26.0-1-g6c86779` | FreeRDP 源码，包含一条标签之后的 MF AVC444 双流解码修复 |
| `D:/source/rustdesk` | `7f804a0e45ba51ccefafbdc457b1a1adc4b2efdd` | 既有文件传输研究，以及连接、会话和后续公网方案的参考 |

`D:/dolit/rdp` 的 README 将依赖简称为 FreeRDP 3.26.0；准确复现时必须使用实际源码提交及构建选项。
这两个 RDP checkout 在本次核查时 `git status --short` 均无输出。其他项目只记录所读 HEAD，不能据此假定其所有工作树文件都未经修改。
既有 Sunshine、Moonlight 本地参考路径仍见根目录 `AGENTS.md`，本轮未重新审查其源码或版本。

### 10.1 AppGuard 的完整会话链

所有下列路径相对 `D:/dolit/dlAppGuard`：

| 代码入口 | 行为及参考价值 |
|---|---|
| `app/http/handle/serviceHandle/serviceHandle.go::StartUser` | 接收启动信息，默认连接 `127.0.0.1`；可带 RDP 端口，构建会话记录并选择 Console/非 Console 分支 |
| `app/service/cloudapp/cloudapp.go::CreateCloudUser` | 用户不存在时创建；现实现加入管理员组，不能作为企业隔离权限模板 |
| `app/tools/winuser/winuser.go::CreateUserByNativeApi` | `NetUserAdd`、密码属性等本地账号操作 |
| `app/tools/rds/rds.go::SessionLoginByApi` | 拉起 RDP 客户端，设置账号、宽高、音频、多屏并返回进程 ID；不传宽高时使用 1920×1080 |
| `app/service/container/streamer.go::StartCloudappByApi` | 将用户名解析为 Windows Session；区分 SYSTEM/用户启动路径，说明会话位置和进程权限是两个维度 |
| `app/tools/pro/pro.go` | WTS 用户 token、指定 Session 的进程创建以及 SYSTEM token Session 赋值 |
| `app/task/task.go::ContainerWinUserGuard` | 有流路时按模式检查会话；非 Win10 的常规 RDP 会话走 `ServiceSessionCheck` |
| `app/service/container/win_session.go::ServiceSessionCheck` | 查询连接/锁屏状态；约 30 秒重试条件后重新登录；检查采集健康并在必要时断开恢复 |
| 同文件 `ServiceSessionBreakCheck` | 没有流路时按缓存策略断开或注销；与常规维持会话的分支不同 |
| 同文件 `DisConnSessionByKill` | 跟踪 RDP 启动进程及兼容子进程进行清理；只借鉴生命周期责任，不照搬 PID 作为唯一所有权证明 |
| `app/tools/rds/rds.go::RedirectRdpConsoleByUsername` | 使用 tscon 将会话转为 Console；这不是本轮拟实现的多用户 RDP 持续连接模式 |
| `app/service/cloudapp/cloudapp.go::RemoteServiceReg` | RDP 图形/GPU/会话策略及帧率设置；包含全机策略变更，不能整段无条件移植 |

`ServiceSessionCheck` 在 Active 但采集异常时也会进行恢复：NotReady 超过约 15 秒、Error 超过约 5 秒会重新查询采集状态，
若仍异常则断开目标 Session，供下一轮重新登录。上述值是参考实现条件，不是 GammaRay 已选定的恢复超时。
账号密码命令行传递、管理员组、自动注销等参考行为必须按本设计重新评估；不复制真实凭证进文档。

### 10.2 streamer 与 FreeRDP 客户端入口

`D:/dolit/streamer`：

- `src/cloudapp/desktop/win/desktop_capturer_win_impl.cc`：DDA/GDI 路径、RDP 刚登录时的初始化失败、周期重新探测和桌面就绪检查。
- `src/cloudapp/desktop/win/desktop_manager_win_impl.cc`：默认桌面、专用 Desktop、SwitchDesktop 与采集线程桌面绑定。
- `src/cloudapp/server/http/server_info_request_handler.cc`：`RDPCaptureStatus` 输出；AppGuard 通过它将图形健康纳入会话恢复。
- `README.md`：记录 RDP 60fps 帧率限制和 GPU 策略相关经验；不能代替目标机器实测。
- `src/win_service/cloud_app_manager.cc`：另一个远控守护路径中的 UI/Console/活跃 RDP 会话选择。
  这是现有桌面回退参考，隔离桌面禁止采用“失败后任选其他活跃会话”的策略。

`D:/dolit/rdp`：

用户已确认这是自有实现，不仅是上游参考样例。详细功能、历史验证边界和服务端桥接改造点见
[自有 Qt RDP 客户端功能盘点](rdp_qt_client_reuse_inventory.md)。

- `README.md`、`cmake/FreeRDP.cmake`：Windows Qt/VS 构建与 FreeRDP 依赖。配置启用 Media Foundation H.264 解码后端；
  启用后端不代表每台机器、每次协商都实际走 GPU 解码。
- `src/RdpSession.cpp`：client-common 上下文、连接与事件泵、停止、GFX/H.264/AVC444 选项；可参考协议适配责任。
- `src/RdpView.*`、`src/MainWindow.*`：GUI 显示和输入；会话保持 helper 不需要照搬完整 Qt UI。
- `tools/connect_test/`：连接诊断工具，尚未作为 GammaRay 原型执行。
- `rdp_benchmark_report_smoke.md`、`rdp_benchmark_smoke.csv`：历史测量证据；其中的呈现率不代表会话内 Render 采集率。

没有证据证明 `D:/dolit/rdp` 就是 AppGuard 配置启动的 ContainerClient/dlrds 程序；二者分别记录，不合并为同一实现。

### 10.3 RustDesk 的定位

RustDesk 的根目录旧副本曾作为文件传输协议/引擎迁移参考，历史研究基线为 `7aa98d43c`，未作为本仓库跟踪的子模块。
现参考目录为 `D:/source/rustdesk`，不再使用 `D:/GoCloud/rustdesk` 或本仓库根下的 `rustdesk/`。
已有 C++ 文件传输引擎 `src/px_deps/px_ft_engine` 是活动产品代码；移除参考副本不代表移除该功能。
详情见 [文件传输迁移记录](rustdesk_file_transfer_migration_plan.md)。后续公网“RustDesk 方案”仍未确定为直接集成代码或服务器协议，
不能将该参考路径当成新增生产依赖。

## 11. FreeRDP 与 60fps 调研结论

### 11.1 可胜任的角色与尚未证明的能力

FreeRDP 提供 RDP 客户端库、连接事件处理、图形管线与重连相关能力，适合作为隔离桌面会话保持进程的候选。
账号创建、工作区准入、Windows 身份校验、采集健康、服务重启恢复和数据隔离仍由 GammaRay 实现。
工程判断是采用库封装独立受管 helper；尚未证明现有 Qt 客户端可直接替代服务化实现。
依据：[FreeRDP 项目](https://github.com/FreeRDP/FreeRDP)、
[3.26.0 官方客户端示例](https://github.com/FreeRDP/FreeRDP/blob/3.26.0/client/Sample/tf_freerdp.c)。

| 用途 | 性能关注点 | 对本产品的含义 |
|---|---|---|
| 用 FreeRDP 显示远端画面 | GFX、AVC420/AVC444、解码、纹理上传、窗口呈现 | 存在多种调优能力，但不能用一个“高性能”开关保证 60fps |
| FreeRDP 保持本机用户 Session | 会话持续可用、消息处理、图形确认和额外编解码成本 | 实际画面由该 Session 内 Render 采集；不需要用户观看 RDP 窗口 |
| 多用户并发 | 每会话 DWM、应用、RDP 与 Render 的 CPU/GPU/内存开销 | 单用户 60fps 不证明多用户同时 60fps |

FreeRDP 支持 GFX/H.264 等设置，具体后端由构建与协商决定；AVC444 的目标包含文字色彩质量，不代表一定比 AVC420 更省资源。
以所用版本自身帮助及源码为准，避免照搬旧 Wiki 参数：
[3.26.0 参数定义](https://github.com/FreeRDP/FreeRDP/blob/3.26.0/client/common/cmdline.h)、
[3.26.0 参数处理](https://github.com/FreeRDP/FreeRDP/blob/3.26.0/client/common/cmdline.c)。

### 11.2 Windows Server 的 60fps 上限

微软 KB 2885213 给出以下设置，将所述远程显示帧率上限配置为 60fps；实际帧率还取决于应用和计算资源，设置后要求重启。
这不是设置完成后自动保证 Render、编码器和实际客户端均达 60fps。
来源：[Frame rate is limited to 30 FPS](https://learn.microsoft.com/en-us/troubleshoot/windows-server/remote/frame-rate-limited-to-30-fps)。

| 项目 | 值 |
|---|---|
| 注册表路径 | `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Terminal Server\WinStations` |
| 名称与类型 | `DWMFRAMEINTERVAL`，DWORD 32 位 |
| 值 | 十进制 `15` |
| 本轮执行状态 | 只记录文档和参考代码，没有修改本机或远端注册表、没有重启机器 |

AppGuard 的 `cloudapp.go:193` 设置同一数值；`:170` 设置 `bEnumerateHWBeforeSW`，以启用 RDP 会话硬件图形相关策略。
不把这些设置视为跨 Windows 版本、GPU 驱动和并发负载的性能保证。Server/GPU 适配依据还包括
[微软 RDS 支持配置](https://learn.microsoft.com/en-us/windows-server/remote/remote-desktop-services/rds-supported-config)。

### 11.3 隐藏窗口、抑制更新与低开销不是同一件事

隐藏 FreeRDP 客户端窗口可能只省去最终呈现，仍可能存在服务器 RDP 编码、回环传输和客户端解码成本。
因此不能由“用户看不到 RDP 窗口”推导额外开销近乎为零，也不应直接从 FreeRDP 解码画面再转码作为本方案默认链路。

RDP 的 Suppress Output PDU 能请求服务器关闭或恢复显示更新，但协议定义没有保证它同时维持本产品所需的
会话内 DDA/GDI 更新和有效帧率。这一影响是待验证项，不能将“禁用 RDP 画面 + Render 60fps”写成已证明优化。
来源：[MS-RDPBCGR Suppress Output](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpbcgr/0be71491-0b01-402c-947d-080706ccf91b)。

原型先以正常图形更新建立可工作的基线，再比较“省略本地呈现”和“协议抑制显示更新”。
无论哪种模式，都必须正确处理协议事件和必要确认；不能简单停止读连接或不处理图形确认来假装保活。
不引入模拟鼠标输入绕过企业锁屏策略作为默认保持机制；锁屏与空闲策略由工作区管理显式定义。

### 11.4 现有历史报告的证据限度

`D:/dolit/rdp/rdp_benchmark_report_smoke.md` 记录的是 2026-08-26 的 3.418 秒短采样：

| 指标 | 报告值 |
|---|---:|
| 解码更新率 | 29.55 updates/s |
| 发布帧率 | 0.29 fps |
| 实际呈现帧率 | 0.00 fps |

报告说明隐藏或最小化时呈现率可为零，不能用于流畅度结论。该数据既不能证明已达 60fps，也不能证明 FreeRDP 最大只能 30fps；
不是 GammaRay 隔离桌面的端到端基准。图形更新次数也不应直接当作完整视频帧数。

## 12. 60fps 原型的短时验证方案

状态：待实现、待测试。先完成隔离身份与正常图形链路，再开展性能比较。
单次执行最长 10 分钟，环境安装、策略变更和重启另列明确操作，不在性能测量中隐式执行。
下面 9 分钟为建议预算，余下 1 分钟作为超时保护；失败时优先清理，不自动延长测试。

| 阶段 | 时间预算 | 验证内容 |
|---|---:|---|
| 启动与身份 | 1 分钟 | 测试环境已就绪，建立标准用户会话并校验 Render 的 SID/Session |
| 单用户基线 | 2 分钟 | 1080p、60fps 动态计数画面，保留 RDP 正常更新 |
| 降低 RDP 开销对比 | 2 分钟 | 省略本地呈现；条件允许再试 Suppress Output，发现采集停滞即恢复 |
| 双用户 | 2 分钟 | 不同动态标记和输入目标，同时采集两路资源与帧数据 |
| 故障与清理 | 2 分钟 | 一路 RDP 退出/恢复、另一路不受影响；停止测试进程并核对残留 |

必须分别测量：

- 源画面的动态序号/时间戳，识别重复帧；静态桌面和无变化时的 DDA timeout 不作为低帧率失败。
- Render 有效采集帧率、采集间隔分布、编码输入/输出帧率、编码耗时与排队长度。
- 实际 GammaRay 客户端的解码与呈现帧率、帧间隔、丢帧和端到端延迟；不使用隐藏 RDP 窗口的 paint 计数替代。
- FreeRDP helper、Render、DWM 和应用的 CPU/内存，GPU 3D/编码/解码占用、显存、回环流量与并发数量。
- Windows 版本/补丁、GPU 型号/驱动、分辨率、编码配置、RDP 协商图形模式和是否使用抑制更新。

“达到 60fps”的最终容差与延迟门槛尚未确定。先提供逐阶段实测及分位数，不以平均值接近 60 或重复发帧作达标结论。
1080p60 是首个建议目标；4K、多屏及更多并发需要独立容量测量，不能由显卡型号直接推算可交付人数。
每次报告标明环境、实现版本、覆盖项、失败项和未覆盖项，短时通过不声称长期稳定性已验证。

## 13. 当前交接状态

- 已完成：外部仓库与 GammaRay 关键路径只读核查、产品边界纠正、FreeRDP/Windows 官方依据核对及本文整理。
- 尚未完成：FreeRDP helper、账号/Session 管理实现、企业授权链、指定会话 Render 改造、实际双用户与 60fps 验证。
- 本轮没有创建 Windows 用户、安装 RDS、修改系统策略、启动 RDP 连接或部署新的运行程序。
- 本文不包含连接密码、令牌或测试账号凭证。参考仓库保持只读；未向 GammaRay 引入 FreeRDP 生产依赖。
- 后续从第 6 节 P0 开始，先核实 Server 测试环境及 RDP helper 的运行位置与凭证方式；保留现有 game-hook/webview 用户环境。

以上为早期方案交接记录。最新交接改从 [RDP 模式设计第 7 节](rdp_application_mode_design.md#7-实现阶段与短时测试)
执行，先验证 FreeRDP 协议画面到现有编码链路。RDP 不再仅为会话保持辅助连接，而是新模式的媒体与交互来源。
