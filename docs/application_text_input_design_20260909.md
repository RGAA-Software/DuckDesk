# game / WebView 跨客户端文本输入设计与实施计划

创建：2026-09-09；更新：2026-09-10。状态：Qt/桌面 Web → WebView 核心中文输入已实机验收；
game 和移动浏览器尚未验收。本设计不是全功能验收通过报告。

本文整理用户确认的方向：仅显示 Hook/CEF 画面的应用不能依赖远端输入法窗口；
Qt Client 和 Web Client 均提供主动输入界面，应用检测仅触发角落提示，不强制弹窗。
输入范围不限中文，也包括其他本机输入法提交的 Unicode 文本。
用户已批准进入实施，进度见 [实施与剩余验收](application_text_input_progress_20260909.md)。
不授权修改外部参考仓库、不新增 Windows 用户、不改变 RDP 模式。

## 0. 最新确认：只扩展现有连接上的消息

用户已明确否决额外 WebSocket/辅助连接方案。该决定覆盖本文早期 WS 子通道设想：

- Qt Client：复用现有已认证 WS/WSS 控制连接。
- Web Client：复用现有已认证、可靠且有序的 `media_data_channel`，沿用 protobuf + TLV。
  不使用不可靠的 `input_data_channel` 承载整段文字。
- 只增加能力、目标状态、屏障、提交和回执消息；不新增连接、路由、通道或票据交换。
- 既有媒体和普通输入路径不改架构；仍以输入代次屏障处理跨通道迟到事件。
- 90 上线后已完成 Qt Windows 和桌面 Chromium → WebView 的真实系统拼音、中文/emoji 和选区替换测试。
  用户确认 90 无法运行 game，本轮只收尾 WebView；移动浏览器和 game 不宣称通过。
  证据和限制见 [实机验收记录](webview_chinese_input_acceptance_20260910.md)，不能以单测替代实机结果。

早期额外 WebSocket 实现已按仓库规则归档；活动代码不得恢复该旁路。

## 1. 开发前基线与前述讨论的修正

| 层 | 开发前源码事实（不是最新实施状态） | 不应据此宣称 |
|---|---|---|
| Qt 视频输入 | `src/px_client/front_render/ct_video_widget.cpp` 从 QKeyEvent 发送按键与文本；未接完整 IME 事件 | Qt game/WebView 已支持本机拼音输入 |
| Qt RDP | `src/px_client/rdp/rdp_view.cpp` 有独立 inputMethodEvent | RDP 实现已被其他模式复用 |
| Web 输入 | `web/px_web_client/src/rtc/input.ts` 已有隐藏 textarea、compositionstart/end、input，UTF-8 限长 4096 | Web Client 完全没有 IME；或各浏览器已验收 |
| Web 传输 | InputController 使用 RTCDataChannel；App.vue 将 input_data_channel 标注为不可靠输入通道 | 输入消息天然都通过可靠 WebSocket |
| 协议 | `src/px_deps/px_message/px_message.proto` 已有 kTextInput=580、TextInput | 已有带确认、去重、目标代次的整段文本提交协议 |
| Render | `NetworkEventIngress::ProcessTextInput` 当前只接受 CEF 输入目标，非空且不超过 4096 字节 | game 已有可用文本接收后端 |
| WebView | 已有字符提交及近期文本剪贴板快捷键修复 | 剪贴板通过就等于输入法通过 |

本文 supersede 讨论中“所有客户端均无输入法代码”“现有输入均走 WS”“直接把整段文本塞入旧 4096 字节入口”的假设。
现有隐藏 textarea 的行为需要迁移评审，不能直接加第二个输入框让两条通路同时发送。
当前中文剪贴板验证见 `webview_clipboard_fix_20260909.md`，与本设计分开验收。
最新源码、构建和发布状态以实施进度文档为准，不把本节保留的开发前基线当作当前缺失清单。

## 2. 目标、首版范围与非目标

首版同时覆盖 Qt Windows Client、桌面 Web Client、移动 Web Client，以及 game-hook/WebView 两种模式。

- 客户端本机输入法选词；明确点击“发送文字”后提交最终文本。
- 自动检测只产生不抢焦点的提示，手动入口始终可用。
- 普通控制、浮层编辑、等待结果分开管理；不能把拼音或选词按键重复发送给游戏。
- 不使用剪贴板作为新文本提交通道，不需要 clipboard 权限；需要有效 input/control 权限。
- 不自动发送 Enter、不自动重发、不向其他进程或实例回退。
- 首版不实现逐字远端预编辑同步，不承诺候选框跟随远端 caret，不承诺所有游戏兼容。
- 文件、富文本、网页 Clipboard API、语音输入权限、密码自动填充不在本次范围。
- 不变更现有媒体传输、RDP 生命周期、多客户端准入政策或用户隔离模型。

## 3. 用户体验

### 3.1 入口与提示

工具栏统一命名“输入文字”，中文只是使用场景之一。提示示例：“需要输入文字？打开输入面板”。
Qt 可提供可配置快捷键，`Ctrl+Alt+I` 仅为候选，须检查当前快捷键冲突；Web/移动端以按钮为主。
浏览器快捷键可能被宿主保留，按钮必须独立完成所有操作。

自动提示按同一次输入激活周期去重；退出输入状态、切换实例或断开时隐藏。
允许关闭自动提示，不移除手动入口。只对有控制权限且后端支持该能力的客户端显示可发送入口。
观察者不应看到可用的“发送”按钮，也不能通过脚本调用绕过权限。

### 3.2 面板行为

- 提示不自动打开面板、不自动聚焦，不依据远端消息自动弹手机软键盘。
- 面板支持本机原生输入法、多行、标点、emoji，提供发送、关闭和明确的执行状态。
- Enter 保持选词/换行语义；首版不用 Enter 或 Ctrl+Enter 直接提交，避免组合输入误触。
- composing=true 时发送禁用；等待平台确认 commit 后才能读取最终文本。
  鼠标点击发送引起的组合结束有时是异步的，不能依赖焦点丢失后立即读值。
- 文本原样提交，不 trim、不 Unicode normalize；空字符串不发。
  多行文本本身的换行可能触发游戏行为，应显示“多行文字可能被目标应用解释为提交”的提示。
- 点击关闭不发送。草稿仅内存保存、按应用实例隔离；注销、权限丢失、实例切换时清除。
  暂时断线可保留当前草稿，但置为不可发送；重新绑定后需用户重新确认目标。
- 等待结果期间锁定本次发送快照，防止双击；允许编辑下一份草稿，但成功回执只能清除原快照，不能清除新编辑内容。
- 无可访问的远端输入框位置时使用本机固定位置，不伪造 caret 坐标。

### 3.3 Web 特殊处理

使用可见、有 label 的原生 textarea；不要让可聚焦输入控件同时 aria-hidden。
打开动作须来自用户点击，才能可靠触发移动软键盘。候选框交给浏览器/操作系统。
测试 compositionend 与最后 input 的不同事件顺序、beforeinput、isComposing 和历史 keyCode=229；
不把 Chrome 一种事件顺序假定为所有浏览器规则。

面板应在全屏容器内；视频元素独占全屏无法包含浮层时，提示切换到容器全屏或退出全屏，不静默丢失界面。
打开面板前退出 pointer lock；关闭后不自动重新锁鼠标，由下一次用户操作恢复。
使用 visualViewport/实际可视区域处理软键盘遮挡、安全区、横竖屏与缩放，不只监听 window resize。
输入面板内 touch/wheel 不能被远端手势监听器吞掉；IME 候选操作不能变成远端点击。

## 4. 输入状态机与焦点：不能只看一个 bool

客户端状态：`Control → Editing → Sending → Editing/Control`；可进入 `Disconnected`、`ReadOnly`。
另外维护 `composing`、待确认请求、草稿版本，不用“暂停视频”代表“暂停输入”。

| 事件 | 本机行为 | 远端行为 |
|---|---|---|
| 打开面板 | 暂停普通键鼠转发，聚焦本机控件 | 释放此前按下的键/按钮；保留输入目标，不盲发 CEF focus=false |
| 本机编辑/选词 | 更新草稿，不实时发送 | 不产生游戏操作 |
| 点击发送 | 冻结快照，检查组合结束及权限 | 按最新服务端绑定验证并提交 |
| 关闭面板 | 恢复控制入口 | 不自动补发 Enter，不自动点击旧坐标 |
| 切到其他程序/标签页 | 释放输入、停止发送 | 按真实失焦规则处理，目标可能失效 |
| 断线/失权 | 标记未完成请求不确定，禁止发送 | 撤销绑定；队列执行前再次校验 |

需要区分面板内部焦点切换、浏览器 window blur、visibilitychange、应用后台与远端文本框失焦。
普通键的 tracking 必须完整，不只释放 Ctrl/Shift；detach、销毁和权限撤销也要释放/清理。
不恢复“用户在打开面板前按住的 W”等旧按键，需用户重新按下。

## 5. 目标有效性与输入状态提示

服务端维护 `Unknown / Editable / NotEditable` 及检测置信来源，而不是把 UNKNOWN 当作 false。
状态通知不是输入授权；客户端看到提示也不能绕过 input/controller lease。

共同强校验：当前应用实例、授权连接、控制租约代次、目标存活；任何失效均拒绝。
目标标识由服务端生成并解析，客户端不能提供任意 PID、路径、HWND 或 DOM selector。

WebView：CEF 输入状态通知可表明当前是否可输入，但不能唯一识别某个 DOM 元素。
首版目标代次覆盖导航、frame 销毁、明确退出输入状态、失权及实例重启；
不宣称能区分每次同 frame 内的两个 input 切换。发送作用于当时有效的聚焦目标，界面明确说明。
若要求严格“原输入框”，需后续 renderer 侧元素令牌机制，不能靠通知 bool 假装实现。

game：IMM Hook 仅是自动提示信号。没有通知或检测未知不禁止手动打开面板，
但发送仍需确认是本次授权 Hook 进程的窗口。无法确定窗口时保留草稿并提示重选，不猜窗口。
同进程多输入框焦点也可能变更，不能承诺识别游戏引擎内部具体控件。

## 6. 协议与可靠传输

### 6.1 语义消息与协议唯一来源

| 消息 | 关键字段 |
|---|---|
| TextInputCapabilities | 协议版本、支持最终文本提交、支持状态提示、最大 UTF-8 字节数、提交后端 |
| TextInputState | 实例标识、目标代次、Unknown/Editable/NotEditable、来源、可选画面归一化位置 |
| TextInputSubmit | request_id、预期实例/目标代次、UTF-8 text；客户端不指定进程窗口 |
| TextInputResult | request_id、结果、稳定错误码、可选接受字节数；不回显正文 |

实施协议名称为 `ApplicationTextCapabilities/State/Submit/Result/Barrier/BarrierResult`，
对应 610–615；`Message.input_generation` 字段号为 616，旧 `kTextInput=580` 保持语义。
`px_message.proto` 是枚举值唯一来源。CMake 从该文件生成轻量 `message_type_ids.h`，
RTC 分类代码引用 `px::wire` 命名枚举，不再复制十进制魔数，也无需引入完整 protobuf/Abseil 头。
浏览器现有消息常量与协议副本同步生成/核验；协议分类、权限和黄金字节测试检查一致性。

租约及输入代次使用十进制字符串以避免 JavaScript 精度损失。目标代次是服务端产生的有界不透明标识，
game 可包含 PID、进程创建时间、窗口和计数器等组成部分；客户端仅回传令牌，不根据它寻找或授权进程。

首版建议最大 16 KiB UTF-8，作为能力协商值，而不是偷偷放宽现有 4096 字节入口。
客户端用 UTF-8 字节计数；服务端拒绝非法编码、超限、NUL 和不允许的控制字符，
保留 tab/CR/LF 的策略需一致且在界面说明；不得按 wchar_t 数量计算字节长度。
限制队列深度、单连接速率与 request_id 长度，过载返回 busy，不无限排队。

结果至少包括：accepted/submitted、unsupported、permission_denied、target_changed、target_unavailable、
invalid_text、busy、failed、outcome_unknown。仅排入队列与真正执行不能共用一个模糊 success。
`submitted` 表示 CEF/窗口消息接口接受，不代表文字已显示或业务表单已提交。
出现部分注入后失败应返回部分/不确定，不能自动重发整段。

### 6.2 载体与旧协议兼容

按第 0 节最新决定，Qt 直接在既有 WS/WSS 控制连接上发送新消息；Web 在既有可靠有序
`media_data_channel` 上发送相同协议，使用原有 `sendControlMessage` 和 TLV 收发入口。
Web 运行时门禁为 `ordered === true`、`maxRetransmits === null`、`maxPacketLifeTime === null`，
并要求连接已经打开；任何部分可靠或不可靠通道都不能作为整段文本载体。
不创建 `/application-text` 路由，不额外续票，不另建控制者或保活绑定。
信令 WebSocket 不承担应用提交；不为此恢复 Native RTC/KCP/Relay 或 WS 视频回退。
既有 WS/WSS 的证书与授权要求保持不变，不新增忽略证书或 Windows 凭证暴露路径。

旧 kTextInput 保持原语义和版本兼容，新文本提交用明确的新语义或经协商的扩展，
不能改变旧按键消费者导致游戏重复输入。旧 Web 隐藏 textarea 在普通控制时保留英文/文字输入能力，
在 begin 屏障、面板编辑及 end 屏障等待期间解绑；不能与可见面板同时发送。
无新能力的服务端显示“不支持该功能”，不尝试盲发或全局注入。
浏览器手写 protobuf/TypeScript 定义与 C++/Rust 协议生成结果必须一起更新并做双向序列化测试。

### 6.3 顺序、回执与去重

WS/TCP 或可靠有序 DataChannel 只保证各自连接/通道内的有序传输，
不保证跨通道顺序、应用执行成功或断线后的 exactly-once。
打开面板的输入释放、目标检查、文本提交需通过同一可排序执行队列，或通过屏障回执协调。
不能因为发送了释放按键，就假定另一条不可靠 DataChannel 上没有旧的按键稍后到达。
实现时需定义输入代次/屏障：进入文字编辑后旧控制事件不可再次生效，退出后再启用新一代控制。

服务端以“实例 + 控制租约代次 + request_id”做有界去重。同 id 同正文返回缓存结果，
同 id 不同正文拒绝；缓存仅保留必要摘要和结果，不持久化正文。
每个执行任务持有可验证 lease/target token，执行前再次校验；单纯接收时验证不足以防旧排队请求。
连接丢失而回执未到时标记结果不确定，不自动重发，即使重新连接同一 Windows 会话也不例外。
若屏障回执丢失、超时或状态无法确认，按拒绝放行原则继续暂停普通输入，要求重新连接建立明确代次；
不能直接恢复旧代次或把“不确定”显示为失败后自动重发。重连后先检查远端实际内容，再由用户决定下一次提交。

## 7. 接收端与参考实现

### 7.1 WebView

在 `src/px_render/webview/webview_runtime.*` 增加输入状态回调及明确的文本提交适配，
CEF UI 队列执行，校验 browser/frame/当前状态。评估优先使用 `ImeCommitText`；
如果采用 CHAR 提交必须证明中文、代理对、多行与选择替换行为符合要求。
这属于实现前的 API/行为验证，不预先声称二者完全等价。
不经剪贴板、不触发全局按键，不在 CEF 内自行打开远端系统输入法。
组合文字、候选位置与网页输入框位置同步列为后续增强，不阻塞最终文字提交首版。

### 7.2 game-hook

Render → 已认证、属于本实例的 Hook IPC → 游戏内窗口适配器。
先参考 streamer 的 WM_CHAR 提交，但不直接复制全局/主窗口假设。
进程准入严格为本次私有 Job 成员 AND 规范化完整路径匹配；排除自启游戏、同名进程与重启替身。
窗口身份包含所属进程和存活验证，考虑 HWND/PID 复用；优先可靠的目标线程焦点子窗口，
未知时失败而非扫描全桌面/按标题找窗口。不得回退到全局 SendInput。
游戏消息必须在合适线程投递；接口返回成功仅说明投递，不保证引擎消费，
按游戏兼容矩阵记录支持情况，而不是承诺所有 UE/Unity 应用都通用。

### 7.3 streamer 证据（只读）

仓库 `D:/dolit/streamer`，核对 revision `0a92ad9762de5f27ef70c3935ccad75d94eefd19`。
核对的关键源文件当时没有本地修改；这些是源码证据，不是当前 GammaRay 的实测。

- `src/hook_capture/api_hook/win32_api_hook.cc`：安装 ImmAssociateContext / Ex Hook。
- `src/hook_capture/api_hook/sandboxie.cc:492`：输入激活提示，注释涉及 UE4/Unity。
- 同文件 `:776`：kInputChinese 转 UTF-16，向 GetMainHwnd 返回的窗口 PostMessageW(WM_CHAR)。
- `src/cloudapp/webgl/webgl_handler.cc:753`：OnVirtualKeyboardRequested 发送输入状态提示。
- 同文件 `:1095`：InputText 过滤部分控制字符，再发 CEF KEYEVENT_CHAR。
- `src/cloudapp/client_message_handler.cc:260`：按运行模式路由 kInputChinese。
- `protocal/cloudapp.proto:1403`：InputChinese 仅包含 text，不能当作新方案已具备回执和去重。
- 该项目远程桌面模式另有 KEYEVENTF_UNICODE 路径；不要与 game 的进程内 WM_CHAR 路径混淆。

借鉴协议语义、提示检测和进程内提交思路；不照搬裸指针、异步 this 捕获、正文日志、
主窗口猜测及不同分支授权不一致的细节。外部仓库保持只读。

## 8. 代码落点与所有权

| 模块 | 预计改动 |
|---|---|
| 协议 | px_message.proto、新能力/结果消息、Web rtc/proto.ts 对齐；核对 Rust 消费者 |
| Qt | 新独立文本面板与智能所有权工作流；ct_base_workspace、px_render_view、VideoWidget 的输入状态协调 |
| Web | 可访问的文本面板组件；App.vue/FloatBall.vue 接入口；InputController 拆出编辑仲裁；可靠提交适配器 |
| Render | NetworkEventIngress 授权入口；独立的提交队列/去重工作流；CEF 与 Hook 两个具体适配器 |
| Hook | 已有 IPC 路由扩展、IMM 检测、窗口绑定、文本提交；不新增旁路进程发现 |
| Console | 仅复用 input 权限和既有会话授权；不新增子通道、路由或票据交换，不增加 clipboard 依赖 |

具体新文件名由实施时当前目录结构确定，不因本表创建大而空的框架。
C++ 必须遵循 `cpp_smart_pointer_standard.md`：Qt parent + QPointer 或独立智能所有权二选一；
异步 weak_ptr/受控 CEF 引用，回调执行时校验存活，线程与注册 RAII，禁止新增业务裸指针/裸 this。
Web 组件卸载必须注销事件、终止待完成请求和清空计时器，避免新旧面板同时监听。
退休旧分支先完整归档至 backup，保存当时未提交内容及清单；不把参考仓库复制进本仓库。

## 9. 实施阶段与完成门禁

1. **基线与协议**：核对现有所有客户端输入通道、应用准入与生命周期；定 capability/结果/代次及既有可靠载体契约。
   门禁：不可靠通道不承载整段提交、无跨通道顺序假设、旧客户端保持兼容。
2. **Qt + Web 手动面板**：两个客户端同时实现本机编辑与输入仲裁、状态和错误展示。
   门禁：真实 IME 选词不泄漏按键；手机用户点击可弹键盘；无双发送。
3. **WebView 闭环**：两客户端都走可靠提交，CEF 最终文字提交及结果返回。
   门禁：非剪贴板依赖、权限/失焦/导航负向验证通过。
4. **game 后端**：Hook IPC 与窗口校验，目标游戏兼容验证；另备自有最小测试程序验证消息行为。
   门禁：只操作自有 Job+路径匹配进程，目标变化不误投，全局输入回退不存在。
5. **自动提示**：CEF/IMM 状态检测、提示去重、禁用设置、手动兜底。
   门禁：不抢焦点、不自动弹手机软键盘、不自动开启发送权限。
6. **交付**：短批次回归、文档、dist/远端发布、哈希清单；保留失败与未覆盖项。

## 10. 验收矩阵与测试边界

每个实际交互测试批次最多 10 分钟；先通知用户暂不操作键鼠/剪贴板，完成及时恢复。
编译不作为长时间压力测试；不以截图、合成事件或一个平台通过代替全部验收。

- Qt Windows、桌面 Chromium、Firefox、移动 Android Chrome/iOS Safari 分别记录测试结果；
  没有设备时明确待验收，不宣称浏览器全覆盖。
- game/WebView 分别覆盖：普通中文、候选词翻页、取消组合、连续提交、中英切换、标点、emoji、
  多行、选区替换、空字符串、最大长度、非法编码、超限拒绝。
- 本机合成 QInputMethodEvent/DOM composition 单测与真实系统输入法操作分开；
  真实候选框可见、位置可用需要实机确认，不能仅验证 committed string。
- 面板编辑时 W/Space/Enter/Ctrl 不泄漏；打开前按下的键释放；关闭后下一次真实按键正常。
- Web pointer lock、全屏、软键盘遮挡、触摸、横竖屏、标签页后台恢复、组件反复挂载销毁。
- 无 input 权限、观察者、旧 lease、伪造实例、跨连接 request_id、过期票据、重连代次、重复消息、
  同 id 不同内容、队列过载、断线前已执行但回执丢失，不允许重复/越权执行。
- CEF 导航/frame 销毁；game 目标退出、同名外部进程、HWND/PID 复用、多窗口和路径空格/Unicode。
- 排队期间销毁、callback 内关闭、注销监听期间分发、重复 start/stop；检查无 use-after-free、卡键和输入正文日志。
- 运行时只停止本次测试实例，不注销 A/B Windows 会话，不停止无关游戏、桌面 Render 或全局 RDS 服务。

实际输入位置只作提示坐标时不得影响目标授权；拿不到位置时固定面板仍须可完成输入。
回执为 submitted 的用例还需观察目标文本内容，不能把 API 接受自动当作画面正确。

## 11. 构建、发布、回滚与状态记录

Qt/Render 用 `scripts_build/build_cpp_*.bat` 对应增量目标，不运行 release-only build_official.bat。
Web 用其自身检查、单测和构建入口，发布到真实服务目录及 dist 所需 web assets；
具体实际服务位置在实施前确认，不把源码修改视为用户可访问页面已更新。
涉及 C++、TS、Rust 协议时同步生成/检查，不手改第三方生成器或 CEF/FreeRDP 上游源码。
所有变更运行产物（exe/DLL/语言/网页资源）发布后做 SHA-256/资源清单校验，验证浏览器无旧缓存。
远端替换保留精确版本备份，不覆盖前一批备份，不为发布重启整个 Windows/RDS。

验收记录必须包含客户端/浏览器版本、应用模式、构建哈希、测试实例、输入法、
通过/失败/未覆盖、是否使用真实 IME，以及测试进程收尾状态。
第一版完成标准是 Qt 与 Web 两端在声明支持的平台上都完成闭环，不是只有 Qt 演示成功。

## 12. 已冻结边界与尚待实机验证的细节

- 载体已经冻结：Qt 既有 WS/WSS、Web 既有可靠有序媒体控制 DataChannel；不增加连接或票据。
- 确认输入释放/旧 DataChannel 事件的代次屏障，不以 TCP 可靠性代替该问题。
- 首批目标游戏及其文本窗口/引擎兼容路径；没有真实目标游戏验收不能宣称 game 完成。
- Ctrl+Alt+I 是否冲突、16 KiB 限制与单请求结果语义，完成协议评审后固定。
- 手机浏览器是否首版全部承诺或仅列明确支持版本，以实际设备验收为准。

这些属于实施门禁，不阻塞先做纯状态机、协议测试与两个客户端面板原型；
未确认项不能用静默回退、全局输入或跳过权限检查绕过去。
