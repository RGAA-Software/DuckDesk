# Pixels Dear ImGui / shadcn 风格组件优先实施计划

状态：组件实现和 Panel/Client 接入已完成本轮复检，等待实机视觉验收
日期：2026-09-13
范围：先完整实现 `px_ui` 组件库，再重排和美化 `px_panel` 与 `px_client`；不修改连接、鉴权、应用启动、设备持久化、媒体、远端输入等业务协议和工作流。

## 1. 固定实施顺序

```text
功能基线冻结
  → 主题/令牌/RAII 基础
  → 基础显示组件
  → 输入与选择组件
  → 浮层与反馈组件
  → 导航/数据组件
  → 独立组件展示程序验收
  → Panel 全局框架和六个页面逐页迁移
  → Client 悬浮控制器、分层菜单、反馈和文件传输迁移
  → 全功能、主题、DPI、性能回归
  → 发布到 build_official/<product>/dist 并核对 SHA-256
```

组件库通过阶段验收以前，不调整产品页面布局。页面重排开始后也不临时保留两套产品 UI；每个页面在独立分支步骤内一次完成组件接入、功能守恒检查和新布局。

## 2. 参考基线

只读参考仓库：`.tooling/references/shadcn-ui`
固定提交：`2b3e6d4f8d9161fe5c19340dc383aade392012dd`
主要参考：`apps/v4/registry/new-york-v4/ui/`

参考的是以下实现思想，不把 React、Radix 或 Tailwind 引入产品：

- 语义颜色令牌，不让页面保存具体颜色。
- `variant + size + state` 的强类型组件变体。
- Dialog、Card、Field 等复合组件的明确结构。
- hover、active、focus-visible、disabled、invalid 等完整状态。
- 组件源代码归产品所有、可直接修改，不依赖黑盒 UI 包。
- 视觉和业务解耦，页面只组合组件并调用现有 Port/Workflow。

## 3. 目标目录与依赖方向

```text
src/px_ui/
  include/px_ui/
    theme_tokens.h
    style_scope.h
    widget_types.h
    components/
      avatar.h
      badge.h
      button.h
      card.h
      checkbox.h
      dialog.h
      empty_state.h
      form_field.h
      menu.h
      number_field.h
      progress.h
      scroll_area.h
      select.h
      separator.h
      spinner.h
      switch.h
      table.h
      tabs.h
      text_field.h
      toast.h
      tooltip.h
  components/
    与公开头对应的实现文件
  tests/
    component_state_tests.cpp
    component_layout_tests.cpp
    theme_token_tests.cpp
    overlay_lifecycle_tests.cpp
  gallery/
    main.cpp
    component_gallery.cpp

src/px_panel/ui_imgui/
  layout/
    panel_shell_layout.*
    page_header.*
    page_metrics.*
  components/
    application_card.*
    device_card.*
    device_details.*
    local_credentials_card.*
    connection_command.*
    service_health_card.*
  pages/
    页面文件逐步迁入；产品专属组合组件不下沉到 px_ui
```

依赖只能为：

```text
px_ui foundation → px_ui components → px_panel product composites → px_panel pages → existing ports/workflows
```

`px_client` 同样只依赖 `px_ui` 组件与现有 `ClientSession` 能力；组件不得反向依赖 Client，会话和媒体层不得依赖具体 UI 控件。

- `px_ui` 不引用 Panel 状态、设备模型或服务接口。
- 产品组合组件只接收展示值和同步动作结果，不访问数据库、网络或进程。
- 现有 Port/Workflow 保持业务入口身份，页面不复制业务判断。
- 新增项目 C++ 不声明、保存、传递、返回或捕获裸指针。ImGui/D3D/SDL 的裸指针只允许作为第三方 API 瞬时边界，立即转成引用或既有 RAII handle。
- 组件绘制不启动异步任务；需要排队的页面动作捕获 `weak_ptr` 并在执行处 `lock()`。

## 4. 阶段 A：功能基线冻结

### A1. 固化功能清单

- 以 `feature_parity_matrix.md` 为主清单。
- 给每个页面动作分配稳定 ID：按钮、双击、右键菜单、Dialog 确认/取消、复制、打开、保存、刷新。
- 记录所有加载、空、错误、禁用和成功状态。
- 不把当前控件位置、尺寸或颜色列入功能基线。

### A2. 建立预览数据

- 扩充现有 `preview_*_port.cpp`，覆盖在线/离线、运行/停止、记录为空/有数据、密码错误、保存失败等状态。
- 预览数据只存在于 `px_panel_imgui_preview`，不进入产品配置或数据库。

### A 阶段验收

- `feature_parity_matrix.md` 中每一项能映射到现有 Port/页面动作。
- 产品业务文件没有因 UI 准备工作发生协议或状态机变化。

## 5. 阶段 B：主题、令牌和 RAII 基础

### B1. `ThemeTokens`

实现深色/浅色两套语义令牌：background、foreground、card、popover、primary、secondary、muted、accent、destructive、border、input、ring、success、warning，以及对应 foreground。

- `ThemeTokens ThemeFor(Theme)` 返回完全初始化的值对象。
- 页面不得直接写 `ImVec4{...}` 作为产品颜色。
- 现有 `ApplyPixelsTheme` 保留为兼容入口，但内部改由令牌生成 ImGui 全局基础样式。
- 根窗口 `WindowRounding = 0`；系统负责 Windows 10/11 外层轮廓。内部组件自行应用局部圆角。
- 主题重复应用必须幂等，不重复累加 DPI 或字体缩放。

### B2. 尺寸和排版令牌

- 控件高度：xs 24、sm 32、default 36、lg 40 logical px。
- 内边距、间距、卡片圆角、控件圆角、边框宽度、图标尺寸、标题/正文/辅助字号全部进入不可变 metrics 值对象。
- 设计基准 960 × 640 logical px；100%、125%、150%、200% DPI 只由统一缩放路径计算一次。

### B3. RAII 范围对象

实现 `ScopedStyleVar`、`ScopedStyleColor`、`ScopedId`、`ScopedDisabled`、`ScopedFont`、`ScopedClipRect`。这些对象：

- 构造即 push/acquire，析构严格 pop/release。
- 不可复制，可移动或直接禁止移动，所有成员确定性初始化。
- 异常/提前返回后 ImGui 栈仍平衡。
- 不保存 ImGui 内部裸指针。

### B4. 组件共同类型

- `WidgetId`：稳定的值类型 ID，避免依赖显示文字生成 ID。
- `WidgetSize`：xs/sm/default/lg/icon。
- `WidgetState`：disabled、invalid、busy 等输入状态。
- 变体使用 `enum class`，不使用字符串、`void*` 或 `std::any` 样式袋。

### B 阶段测试/验收

- 深浅主题所有令牌均被初始化，语义对比关系测试通过。
- 连续应用主题和 DPI 100 次后尺寸无漂移。
- RAII 对象覆盖正常退出、提前返回和嵌套销毁。
- 现有中英文 catalog parity 测试继续通过。

## 6. 阶段 C：基础显示与操作组件

### C1. Button / IconButton / ButtonGroup

参考 `button.tsx`：

- 变体：primary、secondary、outline、ghost、destructive、link。
- 尺寸：xs、sm、default、lg、icon-xs、icon-sm、icon、icon-lg。
- 状态：normal、hover、active、focus-visible、disabled、busy。
- 图标和文字间距固定；IconButton 强制 tooltip/无障碍名称。
- `ButtonGroup` 负责相邻边框和圆角，不改变子按钮行为。
- 尽量用公开 `ImGui::Button` 保留键盘/手柄导航，通过 RAII style scope 改外观；不依赖 `imgui_internal.h`。

### C2. Avatar / Badge / StatusIndicator

- Avatar：头像纹理、名称首字母 fallback、默认用户图标三种内容；支持 sm/default/lg。
- Badge：default、secondary、outline、success、warning、destructive。
- StatusIndicator：在线/离线/运行/停止，圆点和文本使用同一状态模型。

### C3. Card / Separator / Typography

- Card 提供 Header、Title、Description、Content、Footer、Action 区域的组合规范，不通过继承建组件树。
- Separator 支持水平/垂直和可选标题，但不模拟页面主标题。
- Typography 只提供语义字号/字重 scope：page title、section title、body、label、muted、caption。

### C 阶段验收

- 每个变体在深浅主题和所有 DPI 下有展示样例。
- 主按钮只用于页面唯一主动作，destructive 不复用 primary 颜色。
- 组件尺寸和 focus ring 与设计令牌一致。

## 7. 阶段 D：表单组件

### D1. FormField / Label / FormMessage

- FormField 统一 label、control、description、error 的垂直节奏。
- invalid 状态同时改变边框、focus ring 和错误文字；不能只靠颜色，保留错误文本/图标。
- label 区域点击能将焦点交给对应控件。

### D2. TextField / PasswordField / TextArea / SearchField

- TextField 支持 placeholder、前置/后置图标、只读、禁用、invalid。
- PasswordField 内建显示/隐藏动作，但密码值仍由调用方持有。
- TextArea 对应授权信息多行编辑，保留编辑完成事件。
- SearchField 支持搜索图标和清空，不修改搜索算法。
- 显示截断与真实值完全分离；复制、打开和提交只使用真实值。

### D3. NumberField / Stepper

- 数值输入、减、加组成一个控件；提供范围和步长，但业务校验仍由现有 Port 完成。
- 对应码率、分辨率宽高等字段。

### D4. Checkbox / Switch

- Checkbox：checked/unchecked/mixed、disabled、focus-visible。
- Switch：on/off、disabled、focus-visible；仅用于立即表达二态设置。
- 标签整体可点，不让点击文字丢失响应。

### D5. Select / Combobox

- Select 对应帧率、编码器、最大屏幕数、首选解码器。
- 支持方向键、Enter、Esc、当前选中标记、边缘自动翻转和最大高度滚动。
- Combobox 只在确有搜索需求时使用，不将所有 Select 复杂化。

### D 阶段验收

- 鼠标、键盘和手柄导航均能改变值；disabled 状态不产生动作。
- 中英文切换后标签、说明和错误不重叠。
- 输入框、下拉框和步进器在 200% DPI 下不裁字。
- 密码眼睛、复制等后置动作不抢占输入焦点。

## 8. 阶段 E：浮层和反馈组件

### E1. Dialog / AlertDialog

- DialogHost 统一在当前原生窗口内容区居中，不能按屏幕或错误 viewport 居中。
- 支持 title、description、content、footer、可选关闭按钮。
- AlertDialog 明确 default/destructive 动作，Esc/Enter 规则稳定。
- 打开时保存旧焦点，关闭后恢复；遮罩只覆盖本窗口。
- 重复打开、回调中关闭、宿主销毁和业务对象销毁均安全。

### E2. DropdownMenu / ContextMenu / Submenu

- 支持普通项、图标项、复选项、分隔线、危险项、禁用项和一级子菜单。
- 右键菜单锚定点击位置；overflow 菜单锚定按钮。
- 子菜单只在 hover/方向键/点击后逐级出现，父菜单关闭时不闪烁。
- 到窗口边缘时自动翻转或内移，不跑出可见区域。

### E3. Popover / Tooltip

- Popover 用于账户入口和必要的短内容，不替代确认 Dialog。
- Tooltip 有短延时，IconButton 必须提供；浮层避让窗口边缘。

### E4. Toast / Alert / EmptyState / Spinner / Progress

- ToastHost 固定在内容区右下角，支持 success、warning、error、info、关闭和超时堆叠。
- Alert 用于页面内持续状态，不把所有错误都弹成 Dialog。
- EmptyState 保留无设备、无应用、无安全记录等状态。
- Spinner/Progress 只表示真实异步状态，不制造假的进度百分比。

### E5. 高级视觉效果

- 普通模式：所有表面不透明，单层或无阴影。
- 高级模式：仅 Dialog/Menu/Popover/Toast 等内部浮层允许轻微模糊和双层阴影。
- 外层窗口永远不使用自绘圆角或模糊。
- 效果能力由窄接口/策略注入；D3D11 细节留在桌面渲染适配层，`px_ui` 不直接依赖 Windows。
- 不支持或性能不足时只降级效果，布局、颜色语义和交互不变。

### E 阶段测试/验收

- 覆盖回调中关闭、排队通知后宿主销毁、重复打开关闭、嵌套菜单、窗口边缘定位。
- 高级效果开关前后所有控件矩形和 hit-test 区域一致。
- Dialog 永远位于 px_panel 内容区中心，Toast 永远位于内容区右下角。

## 9. 阶段 F：导航和数据组件

### F1. Tabs / SegmentedControl / NavigationItem

- Tabs 用于安全记录、设置子页等内容切换；选中态、焦点态和 hover 明确。
- SegmentedControl 用于语言和主题这种小集合互斥选择。
- NavigationItem 支持 icon、label、selected、disabled；左侧选中指示条属于组件。
- 左栏内容可放下时不得出现垂直滚动条。

### F2. Table / ScrollArea / KeyValueList

- Table 基于公开 ImGui Table API，提供表头、行 hover/selection、空状态、横向溢出和 action column 规范。
- ScrollArea 统一滚动条宽度和边缘，不让页面自行定义样式。
- KeyValueList 用于设备详情、端口和服务信息；label/value 对齐但不使用交替深色行制造噪声。

### F3. 产品组合组件

在 `px_panel` 而非 `px_ui` 实现 DeviceCard、ApplicationCard、ConnectionCommand、LocalCredentialsCard、DeviceDetails、ServiceHealthCard。它们只组合已完成的基础组件。

### F 阶段验收

- 导航选中/未选中一眼可区分，键盘焦点可见。
- 表格在中英文、最窄设计宽度和长字段下可读；复制仍取完整值。
- 设备/应用 Card 的双击与右键区域不互相抢事件。

## 10. 阶段 G：独立组件展示程序（组件阶段总验收）

新增 `px_ui_component_gallery`，不连接 Console、Render、数据库或本地服务。

展示矩阵：

- 深色/浅色。
- 普通/高级视觉效果。
- 100%、125%、150%、200% DPI。
- 每种组件的所有变体和所有状态。
- 中英文切换。
- Dialog、菜单、子菜单、Tooltip、Toast 的真实交互。

组件阶段完成门槛：

1. 本计划 C–F 的组件全部在 Gallery 可交互，不存在页面私有的原始 ImGui 替代品。
2. `px_ui_component_tests`、本地化测试通过。
3. 主题/DPI 重复切换无尺寸累计。
4. 无 ImGui style/color/font/ID 栈失衡。
5. 无新增项目裸指针或异步 `[this]` 捕获。
6. Gallery 通过人工视觉确认后，才开始页面重排。

## 11. 阶段 H：Panel 页面重排顺序

每一步都遵循：接入组件 → 按设计重排 → 对照功能清单 → 预览验证 → 产品构建。禁止一次性重写所有页面。

### H1. 全局 Shell

- `PanelNavigation`、`AccountControl`、语言/主题入口、`NotificationCenter`、语音同意浮层。
- 保持外层窗口、最小化和关闭行为；不引入最大化按钮或自绘外圆角。
- 建立统一 PageHeader 和 960 × 640 logical px 内容栅格。

### H2. 远程控制

- 第一层：连接命令区。
- 第二层：最近设备三列两行、最多六个。
- 第三层：本机连接凭据和分享/网页访问。
- 同步迁移密码 Dialog、本机名编辑、设备右键菜单和编辑/删除 Dialog。

### H3. 设备列表

- 搜索/刷新工具栏。
- 38%/62% master-detail。
- 主、次、幽灵、危险操作重新分级。
- 与首页共享设备状态和动作对象，不复制删除/编辑实现。

### H4. 云应用

- 三列 Card 网格、长名称两行、状态 Badge、overflow 菜单。
- 保留双击、密码 Dialog、启动/观看/停止/强制 TCP/强制 Relay。

### H5. 服务状态

- 三张服务健康 Card；控制器不可用时安装，Render 始终可重启，节点只展示状态。
- 客户端数量和网络地址/端口分区。

### H6. 安全记录

- 访问记录/文件传输记录 Tabs、完整 Table、逐行操作。
- 清空全部和删除单条复用一个长期密码 AlertDialog 工作流。

### H7. 设置

- 常规：编码、分辨率、音频、高级视觉效果、保存和重启提示。
- 网络：授权信息、解析端点、公网地址、端口表、保存/验证/重启提示。
- 安全：自动锁、长期密码、清数据、日志收集和所有状态反馈。
- 控制器：三个窗口选项、最大屏幕数、首选解码器、录像路径、保存。
- 关于：名称、版本、说明、更新、GitHub、网站。

### H8. px_client

- 保留现有浮动控制器：只有命中悬浮球后的拖动才改变位置，单击只展开一级菜单，二级面板按 hover/点击逐层出现。
- 悬浮球、一级导航、二级面板使用共同主题令牌；高级效果开启时使用半透明表面与增强阴影，关闭时使用完全不透明表面。
- 显示页保留显示器、分辨率、帧率、声音、全屏和虚拟屏数量控制。
- 控制页保留安全注意键以及远端鼠标、键盘、滚轮、文本、剪贴板说明。
- 工具页保留文件传输、截图、录制和连接统计。
- 语音页保留呼叫/挂断、麦克风和扬声器静音。
- 设置页保留中英文、深浅主题，并增加与 Panel 相同的普通/高级视觉效果开关。
- 文件传输保留远端路径、本地路径、目录表、上传、下载、任务进度、取消和覆盖确认。
- 启动错误、连接拒绝、媒体暂不可用和断线重试继续使用原有状态与错误分类，只替换呈现组件。
- 不修改 `ClientSession`、硬件解码选择、视频纹理、SDL 输入映射或网络重试逻辑。

### H 阶段完成门槛

- `feature_parity_matrix.md` 逐项勾选，无缺项、无新增业务功能。
- 页面不再直接调用可由新组件表达的原始 `ImGui::Button/InputText/Checkbox/Combo/BeginPopupModal`；允许 Table/布局等底层调用封装在组件实现中。
- 页面文件只负责页面组合，不出现主题颜色、D3D11 效果或数据库/网络实现。

## 12. 阶段 I：验证、构建和交付

### I1. 自动验证

- 新增 `px_ui_component_tests`：令牌、布局、状态、RAII 栈、浮层生命周期。
- 保留并扩充 `px_ui_localization_tests`：中英文 key parity 和主题幂等。
- 运行 Panel product tests，确保 UI 重排未改变业务行为。
- 检查项目维护 C++ 的裸指针和 `[this]` 捕获；第三方源码不机械修改。

### I2. 增量构建

日常只使用：

```bat
scripts_build\build_cpp_panel_imgui_preview.bat client
scripts_build\build_cpp_panel_imgui.bat client
scripts_build\build_cpp_client.bat client
```

不运行 `scripts_build\build_official.bat`，除非用户明确要求完整发布构建。

### I3. 人工回归矩阵

- Windows 10：系统直角；Windows 11：系统圆角；两者外阴影由系统负责。
- 1920 × 1080、4K 与跨 100%/150%/200% DPI 屏幕拖动。
- 深色/浅色、普通/高级效果、简体中文/English 的组合。
- 鼠标、键盘、右键、双击、Tab/方向键/Enter/Esc。
- 长地址、长应用名、空列表、大量安全记录和窗口边缘弹出菜单。
- 重复打开/关闭 Dialog、反复切页、反复启动/退出 Panel。

### I4. 发布验收

- `scripts_build\build_cpp_panel_imgui.bat <product>` 与 `scripts_build\build_cpp_client.bat <product>` 分别将 Panel/Client
  运行时文件、字体、许可证和语言资源同步到 `build_official\<product>\dist`。
- 如果目标文件被占用，停止对应进程后发布。
- 对每个变更的运行时文件核对 build tree 与 `build_official\<product>\dist` 的 SHA-256；任何不一致都不能报告可验收。
- 用户从 `build_official\<product>\dist\px_panel.exe` 启动最终版本。

## 13. 推荐提交边界

1. `feat(ui): add semantic tokens and scoped ImGui state`
2. `feat(ui): add shadcn-style display and action components`
3. `feat(ui): add form controls and validation states`
4. `feat(ui): add overlays menus and feedback components`
5. `test(ui): add component gallery and interaction coverage`
6. `feat(panel): migrate application shell to shared components`
7. `feat(panel): redesign remote control and device pages`
8. `feat(panel): redesign cloud apps service and security pages`
9. `feat(panel): redesign settings pages and visual effects preference`
10. `test(panel): complete DPI theme localization and parity regression`

每个提交必须可构建、可回滚，不把组件库、页面重排和业务修复混在同一个提交中。

## 14. 完成定义

只有同时满足以下条件才算整体完成：

- 所有计划组件实现完毕并通过 Gallery 验收。
- 六个主页面和五个设置子页面全部使用新组件与新布局。
- Client 悬浮控制器、分层菜单、文件传输、启动/连接反馈全部使用同一组件体系，媒体和输入路径保持不变。
- 功能守恒清单全部通过，无功能删减或未经确认的新增。
- 深浅主题、中英文、普通/高级效果和跨 DPI 正常。
- 外层窗口行为符合 Win10 直角、Win11 系统圆角要求。
- 自动测试、增量构建、产品运行验证通过。
- `build_official\<product>\dist` 中全部变化运行时文件与 build tree SHA-256 一致。
