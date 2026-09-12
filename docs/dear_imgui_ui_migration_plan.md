# Panel 与 Client 跨平台 UI 迁移计划

创建：2026-09-11。修订：2026-09-12。Panel 与 Client 正式目标均已切换到 SDL3 + Dear ImGui；迁移保持业务语义，
不保留 Qt 运行依赖。当前文档以活动构建目标和本轮验收记录为准，早期阶段性描述只保留为实施历史。

## 当前连续交付目标（2026-09-12）

本轮连续完成以下两项后才结束，不以中间可编译版本、局部页面完成或未经验证的替代入口作为完成：

1. 清理鉴权冗余。四种正式类型（远控、RDP、Game、WebView）的 Native 启动全部迁移出 Console 一次性连接票据，删除 Native
   签发、传递、兑换、过期和旧票据重连路径；Relay Native 同样只使用 Render 设备密码，不保留连接票据。保留 Web/RTC 的浏览器短期授权、Console
   登录/节点管理、服务间鉴权和 Render 设备密码鉴权。ID、IP、`link://` 三种入口在启动实际 Client 前完成地址解析、密码验证、在线/占用检查和能力检查。密码验证成功后
   才写入平台凭据保险库，Windows 使用 Credential Manager/DPAPI，不写日志、命令行或明文配置。
2. 将正式 `px_client.exe` 的全部 UI 迁移到 SDL3 + Dear ImGui。迁移覆盖启动进度、远控工作区、RDP、Game、WebView、标题栏、
   浮动控制器、设置、确认/错误提示、文件传输、录制、语音、剪贴板交互、中文输入、多屏/多窗口和退出流程。业务协议、编码、
   捕获、输入和四种类型语义保持不变；正式 Client 目标和运行闭包不得导入、加载或部署 Qt 组件。

完成门禁：相关定向构建和测试通过；正式 Panel/Client 从 `build_official/dist` 启动；变更的 EXE、DLL、语言、字体和 Web 资源完成
同步及 SHA-256 一致性检查；运行模块无 Qt；关键 UI 事件有明确响应；本机和 90 可执行的真实连接回归单轮不超过 5 分钟。

完成状态（2026-09-12）：上述两项目标及门禁已经完成。Native 活动启动链已不再依赖 Console 一次性连接票据或会话预留；正式
`px_client.exe` 的工作区、工具栏、文件/剪贴板/语音/录制交互、RDP 适配、启动错误、调试等待和连接拒绝提示均已由
SDL3 + Dear ImGui 承载。Win32 只保留窗口、凭据保险库、进程、输入、D3D11 和其他系统/第三方 ABI 边界，不再承担 Client UI。

实施历史（截至 2026-09-11，以下为迁移过程记录）：

- 已固定 SDL3/ImGui 上游 revision，建立 `px_ui`、`px_desktop_shell` 与独立 `px_panel_imgui_preview` 定向构建。
- Windows SDL3 + D3D11 + ImGui 窗口已编译并通过重复短时启动；正式 `px_panel` 已切换到新入口，Client 仍保持原入口。
- 已提供类型化简体中文/英文词典、Pixels 深色/浅色主题及词典完整性、主题重复缩放测试。
- 桌面外壳已按窗口、D3D11 渲染、ImGui 生命周期、字体、标题栏和 composition root 拆分；持有资源使用 RAII/智能指针。
- Network 页面已拆成独立 draft/page/presenter/port；正式 ImGui Panel 已接入现有配置读取、授权解析、Console 验证、保存及设备注册工作流，预览版继续使用隔离数据。
- Windows 最大化按钮已通过原生 `HTMAXBUTTON` 接入 Snap Layout，平台 subclass 由 RAII 管理；显示缩放变化会从基础尺寸重放主题和字体配置。
- 4K（3840×2160、150%）已完成窗口居中、字体、标题栏、导航、操作按钮和端口表格的实机可视验收；窗口初始/最小尺寸及自定义命中区域统一随 DPI 缩放。
- 官方 ImGui SDL3 后端已负责文本输入光标区域到 SDL 的传递，Windows SDL 后端据此定位系统 IME 候选框；仍需人工完成跨屏 DPI、中文候选窗和 Snap 菜单的可视验收。
- 深浅主题状态已归一到桌面壳层，正常关闭、语言测试、ownership 门禁及 build/dist 哈希核对通过。
- 旧 Qt 网络页的 Console 验证和设备注册异步操作已抽到 UI 无关工作流，旧页面继续调用相同实现；下一步由 ImGui 业务适配器复用，避免复制网络语义。
- 服务状态页已接入现有 Application、Statistics 和消息系统，显示驱动、Render、Service、网络、端口和音频状态；安装驱动和重启 Render 复用原业务入口。
- Remote Control、Cloud Applications、Security Records、General、Network、Security、Controller、About 均已替换为真实 ImGui 页面和类型化业务端口，不再保留占位页。
- 左侧账户区已提供真实的登录、注册和退出流程；网络请求在现有工作线程执行，ImGui 只持有输入和结果状态，失败不再依赖旧登录对话框。
- 远控设备页已覆盖启动控制、仅观看、停止、文件传输、锁屏、重启、关机、删除和设备设置；危险操作使用 ImGui 确认框，连接方式互斥规则保持不变。
- 正式入口已支持隐藏自启、单实例唤醒、关闭到托盘和显式退出；构建产物已按仓库交付规则同步到 `build_official/dist` 并核对哈希。
- 语音呼叫同意流程已迁移为独立 ImGui 模态控制器，覆盖托盘唤醒、协议校验、忙状态、取消、超时、接受/拒绝和决策回包。
- 旧 `px_panel_qt_legacy` 构建目标已经移除；旧 QWidget 源码不再参与正式 Panel 展示层。2026-09-11 发现的 Qt Core/Network
  间接依赖已经解除，正式 `px_panel.exe` 的导入表、运行模块和发布闭包均以零 Qt 为硬门禁。
- Hardware 统计图及整个 Hardware 页面按 2026-09-11 产品决定暂缓，不进入本轮 Panel 迁移、正式切换或验收门禁；底层硬件采集和 Console 上报保持不变。
- 远控/云应用资源发现、授权和 Client 启动已经由无窗口会话控制器承担，Console 资源同步进一步拆分为独立目录组件；正式目标不存在隐藏 QWidget。
- 2026-09-11 已完成的定向构建、15 项测试、ownership 门禁及发布哈希核对，只证明当前中间版本可构建，不能作为 Panel 迁移验收。
  远控和云应用启动按钮还出现过无反应，且 90 上中间版本启动崩溃；两项都必须在最终无 Qt 构建上重新验收。

## 1. 目标与硬边界

采用 SDL3 + Dear ImGui 建设 Windows、macOS、Linux 可复用的桌面 UI。先完成 Windows Panel，再迁移 Windows Client。
其他桌面平台可通过独立 UI 示例验证复用性，完整产品适配另行推进；Android/iOS UI 不属于本轮。

本次迁移必须保持现有功能、业务判断、协议和行为：

- 保留刚恢复的 WebSocket 媒体直连、WebSocket Relay，以及 `force_tcp`、`force_relay` 的命令行、UI 配置、持久化和选路语义。
- 保留现有 UDP/FEC、认证、授权、控制权、输入、文件、剪贴板、录制、语音和 RDP 工作区功能。
- 保留连接参数校验、互斥规则、重试间隔、超时、错误码和失败处理。现有固定间隔重连不改为指数退避。
- 保留 Panel 启动 Client、Service/Render 管理、Console 调度、配置下发、更新和退出的业务行为。
- 不借 UI 迁移增删连接方式、修改默认参数、升级媒体算法或重新设计 SDK、Core、会话和窗口业务模型。
- 不把多会话 Tab、跨窗口拆合、多屏映射等额外功能自动加入迁移；已有能力保持，新增能力单独规划。
- Game、Mobile Clients、Plugins 页面与本地 Steam 扫描保持用户已确认的退役状态。不得将旧 Game 页面退役误解为删除 Console 云游戏功能。

功能清单以迁移开始时的活动代码、近期提交和真实验收结果为基线。历史文档仅作线索，不能作为删除现有能力的依据。
发现代码与用户决定冲突或已有故障，单独记录；本次不静默改变其语义，不把未通过测试的现状写成已验收。

## 2. 技术选择

| 层次 | 选择 | 职责 |
|---|---|---|
| 桌面窗口与事件 | SDL3 | 窗口、输入事件、DPI、显示器、事件等待与唤醒 |
| UI | Dear ImGui + 官方后端 | 控件、布局、文本交互和绘制 |
| 共享产品视觉 | px_ui | 主题、字体、图标、少量 Pixels 定制组件 |
| 桌面外壳 | px_desktop_shell | SDL3 生命周期、UI 渲染接入、平台窗口行为 |
| 产品页面 | Panel/Client 各自维护 | 页面布局、局部交互状态、调用现有业务入口 |

Windows 首版使用 SDL3 + D3D11 官方 ImGui 后端。其他平台增加所需 UI 后端，不要求统一 GPU API。
本计划不确定或重构视频解码、采集和媒体渲染方案；Client 只完成新 UI 与现有画面展示边界的必要接合。
不因为迁移 UI 将 Panel 改用 SDL_GPU，也不要求重写 Client 的其他现存显示路径。

SDL3 是公共桌面平台基础，Windows 原生扩展负责 DWM、Snap Layout 等细节。
macOS 保留原生交通灯，Linux 根据窗口系统支持使用原生装饰。公共标题栏内容和产品视觉可共享，系统行为由平台适配。
跨平台 UI 可复用不等于 Service、Render 或全部产品功能已经跨平台可用。

## 3. 共享代码保持精简

建议目录：

```text
src/
├── px_ui/                  主题、资源、必要的定制组件
├── px_desktop_shell/       SDL3 外壳、UI 后端、平台窗口扩展
├── px_panel/
│   └── ui_imgui/           Panel 页面与薄 UI 适配
└── px_client/
    └── ui_imgui/           Client 页面与薄 UI 适配
```

第三方 SDL3、Dear ImGui 固定版本并保留许可证，依照仓库依赖布局纳入构建；外部参考项目不作为绝对路径构建依赖。
项目维护代码和上游代码分开，上游代码不做机械所有权改造。

具体集成方式：

- 上游源码放入 `src/px_deps/px_sdl3` 与 `src/px_deps/px_imgui`，固定到已审阅的 tag/commit，并随仓库记录许可证与版本；构建不得依赖开发机上的全局安装。
- 首批固定 SDL `release-3.4.16`（`fa2c02bb6e21974a89ea9824bc53c9932abe5f9c`）和 Dear ImGui `v1.92.9b`
  （`f1cc2ae15e53a861a874c3034aae6798fde194ab`）；升级必须单独编译并回归窗口、输入、字体和渲染生命周期。
- SDL3 首阶段静态链接。Dear ImGui 拆为上游 core、官方 `imgui_impl_sdl3` 平台后端和 Windows `imgui_impl_dx11` 渲染后端；项目代码不复制或修改官方后端实现。
- 项目 CMake 目标按职责保持为 `px_ui`、`px_desktop_shell` 和产品 UI 目标；第三方目标仅向必要消费者暴露，不把 SDL/ImGui 头文件扩散到业务层。
- 同一窗口只使用 `imgui_impl_sdl3` 处理 ImGui 平台输入，不再叠加 `imgui_impl_win32`。Windows 原生扩展只负责 DWM、非客户区命中测试、Snap Layout 和系统菜单。
- `px_desktop_shell` 使用类型化 RAII 管理 SDL 窗口、渲染资源、事件循环和销毁顺序；D3D11 COM 对象使用 `ComPtr`，上游/操作系统要求的原始指针只作为瞬时 ABI 参数。
- 事件循环使用 SDL 等待/唤醒能力，根据输入、业务状态更新和动画截止时间渲染；后台业务通过受控 UI 投递唤醒，不持续空转刷新。
- Panel 迁移期间保留 Client 当前使用的 SDL2 和相关链路，不通过替换全局依赖影响 Client。只有进入 Client 阶段并完成基线后，才单独处理其 SDL 版本与窗口接合。

页面可以直接使用 ImGui 标准控件和布局 API。只封装需要统一样式、复杂交互或重复使用的组件，
不逐个包装所有 ImGui API，不预先建立完整自研 UI 框架。
设备卡片、授权页面、会话工具栏等业务 UI 优先留在所属产品，确认有实际复用需求再抽取。

页面通过薄适配层读取现有状态、调用现有命令。业务状态仍由原有模块负责，不在 ViewModel 中建立第二套连接状态机。
焦点、输入草稿、选择项、弹窗显示等局部 UI 状态由页面保存。
业务层不需要为了换 UI 全部改成不可变快照架构；跨线程展示数据采用安全快照或受控投递即可。

结构设计是迁移门禁，不允许以原型速度为由形成大文件或 God Object：

- composition root 只装配依赖；SDL 生命周期/窗口、D3D 等渲染后端、ImGui 上下文、事件循环、平台标题栏分别承担单一职责。
- 产品页面只负责布局和局部交互状态；配置持久化、授权、节点管理和进程控制通过小型、类型化业务适配器进入，页面不直接实现业务流程。
- 平台实现依赖公共抽象，产品 UI 依赖业务能力接口；业务模块不得反向依赖 SDL、ImGui 或具体渲染 API。
- 按真实变化原因拆文件和类型，同时禁止为了“模式”制造无消费者接口、服务定位器、全局单例或大量只有一个转发方法的抽象层。
- 代码评审同时检查文件规模、职责数量、依赖方向和可独立测试性；仅把大文件机械切成多个片段不算完成架构拆分。

多语言从第一个页面开始建设：首批提供简体中文与英文词典，页面只使用类型化文案键，不复制两套布局或业务流程。
语言切换、缺失键检查、字体字形覆盖和中英文布局共同进入组件测试；新增语言应只需增加词典与必要字体资源。
首批沿用现有 Panel 的 Roboto 与 Microsoft YaHei 字体选择，不重复引入字体包；跨平台适配时在平台字体提供器中补齐等价字体。

主题能力同样从首批提供 Pixels 深色与浅色两套语义色 token。主题切换复用同一组件、页面和业务状态，页面不得散落独立色板；
正式接入时由应用设置适配器保存语言与主题。重复切换主题不得重复累积 DPI 尺寸缩放。

## 4. 完全去除 Qt 的硬性要求

Panel 第一阶段的最终目标是正式 `px_panel` 完全解除 Qt 依赖。为保持既有业务语义，可以重写必要的适配实现，
但不能继续把旧 Qt 后端整体静态或动态链接进新 Panel。

完成门禁同时检查以下四层：

- 源码层：正式 Panel 目标的项目代码不包含 Qt 头文件，不声明或传递 `Q*` 类型，不使用信号槽、Qt 事件循环、Qt 网络或 Qt 进程 API。
- 构建层：`px_panel` 的直接和传递链接闭包不包含任何 `Qt6::*` / `Qt::*` 目标，也不通过聚合库间接带入 Qt。
- 二进制层：检查 PE 导入表和运行时模块，不能出现任何 Qt DLL；不得通过延迟加载、插件或辅助 Panel 进程规避检查。
- 发布层：Panel 独立运行目录不复制 Qt DLL、Qt 插件、QML 或 Qt 资源。共享总发行目录中即使因未迁移的 Client 暂存 Qt，
  也必须证明 `px_panel.exe` 不加载它们；最终 Client 迁移后再从总包清除其剩余副本。

必须替换的适配包括：

- 将界面边界的 QString、路径、图片和翻译结果转换为适合新 UI 的表示。
- 将 Qt 信号槽/UI 队列替换为具有相同线程约束、顺序和取消语义的投递。
- 替换 QTimer 时保持原有间隔、重复/单次语义、触发线程和停止行为。
- 替换窗口、托盘、系统对话框、剪贴板和进程启动接口时，保持参数、返回结果和业务流程。
- Hardware 页面本轮不迁移；底层硬件采集和统计算法保持原状，后续恢复页面时再单独拆开 Qt 图表边界。
- 调整构建依赖和资源加载，使新 UI 不再经工具库或皮肤加载器间接依赖 Qt。

认证、连接、调度、持久化、超时和进程管理的产品语义保持不变；其 Qt 实现本身不受保留约束，必须迁移为标准 C++、
项目异步运行时、SDL/Win32 平台适配或其他无 Qt 的现有基础设施。迁移发现既有生命周期缺陷时单独记录和测试。

品牌、版本信息和皮肤中实际承担的功能配置必须清点并保留语义。
纯视觉主题可改为数据；不能因为 skin DLL 名称像 UI 就直接删除其中业务判断。
仍由 Client 或其他模块使用的旧皮肤 ABI，在其消费者迁移前保持原契约。

## 5. UI 小样与 DearSQL 标题栏参考

先建立小型独立 UI 示例：标题栏、导航、输入框、按钮、状态提示和一个弹窗。
只实现第一个业务流程所需组件，后续随页面增长。

优先验证中文输入与候选框定位、UTF-8 编辑、密码遮罩、复制粘贴、Tab 焦点、模态遮挡、
DPI 动态变化、最小化恢复和空闲 CPU/GPU。
ImGui 的完整无障碍、复杂文字排版能力有限，不能把这些能力宣称为迁移自动获得。

事件循环应支持等待和唤醒，按输入、状态变化和动画需要刷新；必须覆盖光标闪烁、Tooltip、
后台结果以及非客户区 hover 的刷新。不能简单地等待视频帧而让无视频时的 UI 冻结。

DearSQL 参考：

- 仓库：https://github.com/dunkbing/dearsql
- 本地：`D:/source/reference/dearsql`
- 分析 revision：`91aed7813e23c2445b12a6a7512b25f8ecb95b12`
- Windows 消息与 DWM：`src/platform/windows_platform.cpp`
- 标题栏绘制和命中测试：`src/platform/windows_titlebar.cpp`
- 平台接口：`include/platform/titlebar.hpp`

其 Windows 使用 GLFW + D3D11 + ImGui 自绘标题栏；macOS 使用原生 NSToolbar/标题栏附件，
Linux 使用 GTK HeaderBar。仓库截图展示 macOS 外观，不能据此声称 Windows 效果已经实机验证。

Pixels 借鉴紧凑布局、统一背景、克制按钮和原生窗口行为：

- Windows 使用非客户区命中测试处理拖动和边缘缩放，最大化按钮保留 HTMAXBUTTON/Snap Layout。
- 最大化尊重任务栏工作区，标题栏和内容区使用一致的 inset。
- 控件区域与拖动区域明确分开，处理不同 DPI 和多显示器坐标。
- 保留 Windows 系统按钮、macOS 原生交通灯及适当的 Linux 装饰。
- Windows 原生消息扩展必须与 SDL3 的窗口生命周期协调；需要 subclass 时使用 RAII 注销和恢复。

DearSQL 的“鼠标在区域内松开即点击”手工判断不直接照搬。Pixels 按钮需具备按下、捕获、释放、
取消、焦点与弹窗遮挡语义；优先利用 ImGui 控件行为绘制定制外观。
标题栏菜单需要正常的 popup 层级，不规定必须复制其透明 popup host 技巧。
不照搬全局平台实例、原始对象指针和固定像素常量。

DearSQL 当前 LICENSE 标注 FSL 1.1，并约定 2028-03-14 转为 Apache 2.0；
参考 checkout 保留原许可证，具体代码引入时记录来源和适用许可。本轮仅作参考。

## 6. 第一阶段：Panel

| 批次 | 工作 | 完成条件 |
|---|---|---|
| P0 基线 | 清点活动页面、命令行、配置、皮肤职责与业务入口；记录短时运行结果及依赖 | 功能与源码对应，已知失败单独记录 |
| P1 UI 小样 | SDL3 + D3D11 + ImGui，标题栏及首批基础控件 | 输入、DPI、窗口行为和空闲刷新通过 |
| P2 首个完整流程 | 主窗口 + 网络授权页 + 现有真实授权调用，随用随拆 Qt UI 依赖 | 保存、验证、状态、错误与旧版语义一致 |
| P3 逐页迁移 | 按基线迁移其余页面、托盘、更新提示及系统弹窗 | 每批均能操作和短时回归，原业务入口保持 |
| P4 正式切换 | 新 UI 成为 px_panel.exe 唯一入口，旧 Qt UI 和 Qt 业务后端退出正式 Panel 构建 | Panel 源码、链接和运行时 Qt 清零，旧 Client 仍能工作 |
| P5 产品验收 | 本机、90、Auth/Console 验证完整流程 | 功能基线全部通过且无静默按钮，才开始 Client 迁移 |

P3 顺序：Server Status → Remote Control → Cloud Applications → 设置与安全记录 → 尚未迁移的辅助弹窗。
Hardware 整页已明确暂缓，不属于 P3/P4/P5 门禁；最终活动页面清单仍由 P0 决定，不得遗漏常规设置、控制器设置、关于等入口。

每页迁移都同时处理需要的 Qt 业务边界适配，不要求所有 Core 先完成拆分。
第一阶段继续调用现有 Qt Client，通过真实启动与连接验证接口兼容。

Panel 产品验收覆盖普通/隐藏自启、单实例唤醒、授权、Service 下发与恢复、Console 连接、
Render 启停、设备和云应用启动、现有强制连接设置、配置保存、错误显示、托盘、更新提示和退出。
不将未开发的业务功能列为 UI 迁移必须补齐的工作。

## 7. 第二阶段：Client

Panel P5 完成后，重新读取当时 Client 的活动代码、近期提交和验收记录建立 C0 基线。

| 批次 | 工作 | 完成条件 |
|---|---|---|
| C0 基线 | 连接、媒体、RDP、输入、文件、剪贴板、录制、语音、窗口和配置入口 | 逐项记录入口、状态来源、旧 UI 和已有测试结果 |
| C1 首个完整流程 | 复用桌面外壳、连接状态 UI、现有会话入口和画面接合 | 真实连接和画面可用，业务行为与基线一致 |
| C2 逐项迁移 | 工具栏、设置、输入 UI、文件窗口、录制/语音提示、现有 RDP UI 等 | 每批保持协议、参数和业务判断不变 |
| C3 正式切换 | 新 UI 接管 px_client.exe，解除自身 Qt 依赖并归档旧 UI | Panel 启动、直接启动、文件模式和现有其他入口工作 |
| C4 产品验收 | 本机双目录、本机到90的真实连接与交互回归 | 基线功能等价、窗口交互正常、产物完成发布 |

必须保留 UDP/FEC、WebSocket 媒体直连和 WebSocket Relay 的现有选择、互斥和错误处理。
已在 `src/px_client/ct_main_ws.cpp` 核实 `force_tcp`、`force_relay` 参数和 SDK 媒体/路由选择；
Panel 的 `devices/stream_settings_dialog.cpp` 及数据库仍保存这些设置。
`src/px_client/CMakeLists.txt` 链接现有 RDP 工作区，迁移需覆盖其展示入口，不能遗漏或借机退役。

视频解码器、传输算法、远端输入协议、会话管理和录制封装继续沿用现有实现；
只替换与 Qt 窗口或控件直接耦合的展示边界。
剪贴板、文件传输和录制保持 Client 内置模块，不恢复已退役的 Client DLL 插件边界。

现有多窗口、多显示器和中文输入能力按 C0 基线迁移。新增浏览器式会话 Tab、
窗口拖出/合并或新增显示模式不作为本次功能等价验收要求。

## 8. 跨平台 UI 验证

UI 示例与完整产品分别验收。共享页面可使用明确标注的测试适配器验证布局与交互，
无需先开发 macOS/Linux 的 Service、Render 或完整客户端；产品验收必须使用真实业务接口。

每个平台记录：编译情况、标题栏/窗口行为、字体和中文输入、DPI、键盘焦点、弹窗、
剪贴板、最小化恢复、空闲刷新和关闭生命周期。
没有对应平台执行环境时明确标为待验收，不能仅凭 Windows 编译推断跨平台已完成。

macOS/Linux UI 验证可在共享组件形成后进行，不改变“Panel 产品先完成、Client 产品后迁移”的顺序。
移动端及其他平台完整功能适配保持独立计划，不由本轮 UI 选型决定其业务能力。

## 9. Qt 清理、构建与交付

Panel 第一阶段要求展示层和业务适配层均不依赖 Qt。正式 Panel 不得保留 Qt Core/Network，也不得通过旧后端、插件、
辅助 Panel 进程或动态加载间接使用 Qt。整个安装目录是否仍因未迁移 Client 暂存 Qt，是单独的发布清理事项。

- 为迁移完成的程序建立独立最小运行目录，仅复制其声明的依赖和资源，检查传递导入和动态加载并执行真实流程。
- Panel 阶段共享 `build_official/dist` 中的 Qt、皮肤和相关资源仍可能被 Client/其他组件使用，必须保留。
- Client 迁移完成后仍要清点其他使用者；只有确认无活动消费者的文件才能从共享打包清单移除。
- 必需的 Helper 进程和 DLL 若仍使用 Qt，应如实列入目标依赖，不能仅检查主 EXE 即宣布 Qt 清零。
- 用户运行目录始终为 `build_official/dist`；独立最小目录只用于依赖验证，不代替正式交付。
- 使用定向 `scripts_build/build_cpp_*.bat`，必要时增加 UI 定向目标；不运行 release-only `build_official.bat`。
- 每批 changed runtime artifacts、语言和资源同步到 dist，并验证构建树与 dist 的 SHA-256。
- 迁移期间旧正式入口保持可用；最终不保留双 UI 运行开关。退役 Qt UI 归档到 `backup/`，共享消费者用完后再退出对应构建。

## 10. 每批质量门禁

1. 对照基线说明改了哪些 UI、替换了哪些 Qt 边界，以及业务语义如何保持。
2. 编译相关目标并执行与变更相关的测试，真实测试单轮不超过 5 分钟。
3. 新增/触及项目 C++ 遵守智能所有权、确定性初始化、RAII、150 列和项目格式规则。
4. 异步/UI 接合变化覆盖排队回调后销毁、dispatch 中注销、callback 内关闭、重复启动/停止。
5. 执行项目 C++ ownership 检查，完成 dist 同步与哈希核对。
6. 记录通过项、未测项、基线已有故障和新回归；功能退役必须有新的明确产品决定。

所有权和初始化标准见 `cpp_smart_pointer_standard.md`。第三方和系统 ABI 指针仅在边界使用，
项目长期资源采用智能所有权或类型化 RAII；本轮不借 UI 迁移重设计已有插件 ABI。

### Panel 第一阶段实施结果（2026-09-12）

- P0～P4 已完成：正式程序名保持 `px_panel.exe`，唯一入口为 SDL3 + D3D11 + Dear ImGui；旧 Qt Panel 应用、Qt 页面、
  Qt 测试和 `windeployqt` 已退出正式 Panel 配置。迁移期未被使用的 Qt Port 适配文件也已删除；Client 尚在使用的皮肤目标
  仍独立构建，不属于 Panel 运行闭包。
- 已迁移 Remote Control、Cloud Applications、Server Status、Network、General、Controller、Security、About、访问记录、
  文件传输记录和语音同意弹窗。已按产品决定移除 Game、Plugins、Mobile Clients 和 Hardware 页面。
- 页面事件由类型化 Port 转发到独立产品模块；授权、账户、Console 查询、节点注册、Service/Render 启停、Client 启动、
  文件传输、配置持久化、密码同步、日志收集、审计记录和自动锁屏均有实际处理或明确错误结果，不保留静默按钮。
- Service 首次连接后的 Render 启动消息改为在 WebSocket 就绪队列发送，避免升级回调期间因状态尚未提交而丢失；
  Panel、Service、Render 三方重连均使用固定短间隔，不使用指数退避。
- 构建脚本会执行产品生命周期/审计测试、词典完整性测试、发布同步及 SHA-256 校验，并检查 PE 导入表和链接闭包；
  `px_panel.exe` 不导入或加载 Qt DLL。产品测试还覆盖连接选项的保存、重载和清理；项目 C++ 所有权门禁也作为交付检查执行。
- 本机已验证普通启动、隐藏自启、单实例唤醒、关闭到托盘、4999 监听、Panel→Service 4603、Render→Panel 4999、
  Render 4601 和无 Qt 运行模块。90 已部署与 `build_official/dist` 哈希一致的 EXE、字体和语言资源，并验证同一启动链、
  进程响应及无 Qt 运行模块；最终人工页面交互由用户从正式发布目录验收。
- 页面验收补充修正了导航选中态和无滚动布局、链接省略展示与完整复制/打开、错误弹窗居中，以及 Guest 运行实例识别；
  桌面链接保留地址、Panel/Render/Relay 端口及授权信息等完整既有载荷，网页地址保留 `?c=` 连接令牌，不得用简化的设备 ID/密码
  链接替代。Guest 默认并发与登录用户保持为 3；曾经使用 Console 一次性连接票据完成验证，但该方案已被下述最终连接模型取代，
  不得继续作为 Native 连接实现依据。

Panel 第一阶段完成后才进入 Client 的 C0 基线。Hardware 页面继续延期，不纳入本轮页面功能。

### Native 连接模型最终决定（2026-09-12）

#### 产品入口与统一语义

- 保留设备 ID、`IP[:端口]` 和 `link://` 三种 Native 入口，三者共用地址解析、凭据读取和启动前鉴权流程。
- 设备 ID 是常规入口：Panel 向 Console 查询在线状态、当前公网入口、端口和节点能力，然后使用用户输入或本机已保存的设备密码。
- IP 是局域网和维护场景的明确直连入口；`link://` 提供设备 ID、候选地址、端口和内置密码。两者可以跳过 Console 地址解析，
  但不得绕过 Render 密码验证。
- Console 是设备注册、在线状态、地址、端口、能力、用户/设备关系和应用调度的管理中心。
- 远控、RDP、Game、WebView 是当前四种正式会话/应用类型，必须全部保留。本轮不评估、不合并、不删除它们，也不改变各类型既有的
  传输、渲染和启动语义。公共鉴权流程只生成类型化连接描述，随后交给对应类型的现有启动适配器。

#### 删除 Console 一次性连接票据

- 四种 Native 类型的公共连接入口不再签发、传递、消费或重用 Console 一次性连接票据；各类型迁移不得改变其功能语义。
- 删除活动 ImGui Panel 的设备/应用票据启动路径、正式 Client 的票据启动参数及票据重连，以及 Native 用户可见的“票据已过期”错误路径。
  Relay Native 控制请求传递与直连相同的设备密码哈希，由目标 Render 验证后建立当前物理连接绑定；浏览器 Web/RTC 的短期授权独立保留。
- Render 正常运行所需的连接 ID/会话 ID只是生命周期标识，不得重新承担一次性授权票据的职责。
- 旧调用者必须先迁移到设备密码流程，再删除服务端接口和协议字段；不得留下两个并行的 Native 鉴权模型。

#### 启动前预检，不做会话预留

`px_client` 不能作为连接探测器。Panel 必须先完成以下预检：

1. 解析并校验输入；设备 ID 模式通过 Console 获得当前节点入口。
2. 检查 Console/Render 在线状态和目标控制端点的可达性；媒体通道在业务会话内按对应类型建立。
3. 向目标节点验证设备密码并读取节点能力；应用模式还必须先由 Console 确认实例已运行并返回稳定连接描述。
4. 校验本机对应类型的可执行文件、必要运行资源和双方能力是否满足本次连接。
5. 全部成功后才启动 `px_client`；任何预检失败只由 Panel 的居中弹窗报告。

不创建短期会话预留，不提前锁定 Render，也不为消除检查与连接之间的极小竞态引入额外协议。如果预检通过后设备恰好被其他用户
抢占，接受该竞态：Client 不展示无效工作窗口、立即退出，并把明确错误交回 Panel 弹窗。退出或重连不得复用旧授权；使用保存的设备
密码重新鉴权即可。具体会话存活和释放规则由远控、RDP、Game、WebView 各自的既有实现负责，本轮不统一其传输层。

#### 本机凭据保存

- 首次输入 ID/IP 和密码，或首次解析 `link://` 后，必须先由 Render 验证；只有验证成功才允许保存。
- Windows 使用 Credential Manager（由 DPAPI 保护）保存远端设备密码。普通配置仅保存设备 ID、名称、地址和凭据索引，不保存明文。
- 下次连接时 Panel 从 Credential Manager 读取密码到受限内存，完成同一套预检和鉴权，因此用户可以免输入。
- 密码被拒绝时立即删除失效凭据并重新显示密码输入框；网络不可达或设备占用不能误删有效密码。
- 密码不得发送给 Console、写入日志、写入命令行或明文配置。Panel 向 Client 传递连接凭据必须使用受控本机 IPC。
- 后续平台通过统一的凭据保险库接口适配：Android Keystore、iOS/macOS Keychain；平台 UI 不直接依赖具体保险库实现。

#### 本机信息复制

- 本机设备 ID、设备名称和临时密码分别提供复制按钮，并提供“复制全部”。
- 密码即使处于遮罩显示状态，复制操作仍复制完整真实值；省略显示、遮罩和布局宽度不得截断剪贴板内容。
- “复制全部”采用稳定的多行键值格式；成功后使用短暂非模态提示，不弹出阻塞窗口。

### 两项目标实施结果（2026-09-12）

#### Native 鉴权精简

- 正式 ImGui Panel 的 ID、`IP[:端口]`、`link://` 三种入口统一执行 Render 配置探测和设备密码校验，校验成功后才调用
  `PanelClientLauncher`。ID 模式先经 Console 取得当前设备端点；云应用先启动/确认实例，再读取 `native-connection` 稳定描述。
- Console 新增用户/Guest 的 Native 连接描述接口，只返回当前实例、设备、地址、端口、类型和必要的 RDP 受保护配置，不签发
  一次性连接票据。RDP 配置生成已独立为中性模块，不再依赖票据请求类型。
- Panel 与 Client 之间使用继承匿名管道传递启动 JSON；秘密不进入命令行和日志，写入后立即清零缓冲。普通远控只传设备密码哈希；
  RDP 工作区秘密使用 `SecretBuffer` 管理。验证成功的设备密码由 Windows Credential Manager 保存，网络故障和占用不删除凭据，
  只有明确的密码拒绝才删除。
- 已删除“密码预检同时创建一次性会话预留”的实现和 API。Native WebSocket 使用设备密码哈希、nonce 和稳定 stream ID 准入；若预检
  后发生占用竞态，Render 仍是最终仲裁者。Client 在首个有效会话状态前保持隐藏，永久拒绝时显示明确错误并退出，不展示无效工作区。
- Native 与浏览器 Web/RTC 均直接使用设备密码哈希在 Render 鉴权；Console 只下发路由、实例和 RTC 配置，不再签发、续期或兑换远程连接票据。

#### `px_client` Dear ImGui 迁移

- 正式 `px_client.exe` 唯一入口现为 SDL3 + D3D11 + Dear ImGui，旧 QWidget Client、Qt 文件传输窗口、Qt 剪贴板模块和 Qt 录制模块
  均不参与正式 Client 构建图；`clipboard.dll`、`ft.dll`、`record.dll` 不再作为 Client 插件恢复。
- 新 Client 按启动配置、会话、窗口、工具栏、文件传输、音频输出、RDP 协议和类型化中英文本拆分，没有单文件承载全部职责。
  正式目标覆盖 UDP/FEC、WebSocket 直连、WebSocket Relay、硬/软解码选择、画面与音频、鼠标键盘/UTF-8 文本输入、剪贴板、文件传输、
  录制、语音、显示器切换、分辨率、虚拟显示、截图、统计、帧率、声音、全屏、深浅主题和简体中文/英文切换。
- Client 控制入口保持远程工具的轻量悬浮球形态：默认只显示悬浮球，可拖动到窗口内任意位置；单击才展开或收起
  “显示/控制/工具/语音/设置”两级菜单，单纯 hover 不触发菜单。菜单作为画面上的独立 overlay，不挤占视频布局空间。
- SDL 输入适配独立于窗口和菜单：覆盖左右修饰键、导航键、功能键、Windows OEM 标点与数字小键盘；窗口失焦时主动释放仍按下的键鼠，
  持续启用文本输入并只将 IME/Unicode 提交交给文本协议，避免普通 ASCII 键盘事件与文本事件重复发送。
- Client 视频窗口按显示器刷新节奏持续呈现；Panel 等非视频程序继续采用事件驱动刷新，避免把远程画面错误降到低频 UI 空闲刷新。
- RDP 画面、输入、动态分辨率、音频和 Unicode 输入继续使用固定 FreeRDP SDK；RDP 文本剪贴板已改为独立无 Qt `cliprdr` 适配器，
  与 SDL 系统剪贴板互通。第三方/系统 ABI 的瞬时指针均留在有注释的边界，项目持久资源使用 RAII 和智能指针。
- Client 启动信封保留 `force_tcp`、`force_relay`、强制软件解码、GDI 捕获、调试等待及其他现有连接偏好；已退役页面不因 UI 迁移恢复。
- 启动请求无效、调试等待和连接拒绝均使用居中的 ImGui 对话框；正式 Client 活动源码中不再调用 Qt 对话框或 Win32 `MessageBox`。
- 定向构建脚本只构建 Client/测试及必要 RTC 目标，并将 EXE、FreeRDP 运行库、语音处理 DLL、字体和语言资源同步到
  `build_official/dist` 后逐项验证 SHA-256。正式 Client 的 Qt 导入与运行模块检查是最终验收门禁。

#### 最终验收（2026-09-12）

- `scripts_build/build_cpp_client.bat` 通过；3 项 Native/RDP 无票据启动信封测试通过。`build_official/src/px_deps/px_client.exe` 与
  `build_official/dist/px_client.exe` 的 SHA-256 均为 `D1B81CEFDCC3BA4532AB3A5257BC37326906046D727A88D496D8A3AF8985156E`；
  FreeRDP、语音、字体和语言运行资源逐项哈希一致。
- `scripts/test_native_imgui_node90.ps1` 使用正式 dist Client、当前 Console 和 90 Render，分别完成 UDP/FEC 与强制 WebSocket 真实首帧
  验收；两次均为 `PASS`、`QtModuleCount=0`、`DecoderRebuildCount=0`，测试实例退出后由 Console 清理。无启动信封与错误密码两种
  失败路径的 ImGui 对话框也已分别验证可见且 Qt 模块数为 0。
- `scripts_build/build_cpp_panel.bat` 的 6 项 Panel 测试通过，正式 Panel 哈希为
  `1ABED28231AE97C6A02D91AC6CD7B07E59A02F7EE685018C9C3230EBD4D0CD5B`，构建树、dist 和 90 部署副本一致；本机及 90 运行模块均无 Qt。
- Console 的 Native 描述结构测试明确禁止 `ticket`、`renewal_token`、`reservation`、`expires_at` 字段；RDP 密钥脱敏测试和
  `cargo check -p px_console_server` 通过。90 已部署最终 Console，二进制 SHA-256 为
  `6EBE5A6F900B5EFF2C8F68CC4CF2961E2A416019618BC615DF2B81B6C522DB6B`，仓库配置哈希为
  `A0F57C72B0E2FC56AFAD2F85079678318CBC9A76B6975FAD3E055572A8929F8B`。
- `scripts/check_cpp_ownership.ps1` 通过；新 Client 与本轮触及的维护代码没有新增项目裸指针、手工所有权或异步 `[this]` 捕获。

## 11. Qt 清理收尾（2026-09-12）

- 根构建图不再查找 Qt、设置 Qt 自动代码生成或加入 Qt 控件库；Panel、Client、Render、SDK、WebRTC 适配器和捕获目标均不链接 Qt。
- 旧 Panel/Client QWidget 实现、旧皮肤、硬件统计 UI、`px_qt_widget` 和仅供旧 Qt 文件传输测试使用的入口统一迁入
  `backup/qt_legacy_20260912`，不参与任何活动构建或发布。保留该目录仅用于历史追溯，不允许从产品 CMake 再次引用。
- Panel 仍需使用的 Render API、运行管道、字体和语言文件已迁入 `ui_imgui/product` 与 `src/px_ui/resources`；活动代码不依赖备份目录。
- 发布脚本会主动删除 `Qt5/Qt6` DLL、平台/图像/样式等 Qt 插件目录、旧皮肤和旧多屏插件；全量收集脚本也不再复制这些内容。
- `scripts/check_no_qt.ps1` 同时检查活动源码/构建声明、正式产品导入表和 `build_official/dist` 运行闭包。Client、Panel、Render
  的定向构建入口都会执行该门禁，防止后续重新引入 Qt。

## 12. 参考

- [Dear ImGui](https://github.com/ocornut/imgui)
- [官方后端](https://github.com/ocornut/imgui/blob/master/docs/BACKENDS.md)
- [DearSQL](https://github.com/dunkbing/dearsql)，本地路径和分析 revision 见第 5 节
- [前期讨论](https://chatgpt.com/share/6aa3bce2-d618-83ee-a7f9-bc31e9ec5736)
- [架构总览](architecture_overview.md)：包含历史信息，功能范围以本次活动代码清点为准
- [C++ 所有权标准](cpp_smart_pointer_standard.md)
