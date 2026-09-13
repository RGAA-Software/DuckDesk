# px_panel shadcn/ui 视觉重设计

状态：共享组件与 Panel/Client 接入已完成本轮返工，等待实机视觉验收

2026-09-13 复检结论：上一版完成了组件封装和功能接线，但侧栏宽度、DPI 字体、卡片比例、页面栅格与设计稿存在明显偏差，不能记为完成。
当前以 960 × 640 logical px / 150% DPI 实拍为硬基准逐页返工；只有设计对齐、功能回归和发布哈希三项都通过后才恢复“完成”状态。
日期：2026-09-13

## 1. 目标与边界

本方案废弃此前的 px_panel 现代化 A/B 方案，从零建立一套以 shadcn/ui `new-york-v4` 为视觉和组件参考的 Pixels 桌面组件系统。

- 最外层原生窗口维持现状，不由 ImGui 绘制窗口圆角、裁切遮罩或外层毛玻璃。
- Windows 10 使用系统直角；Windows 11 使用 DWM 提供的系统圆角。内容层的 `WindowRounding` 不得改变原生窗口轮廓。
- 保留 Panel 六个主页面、五个设置子页以及 Client 悬浮控制器/文件传输/反馈的全部信息和操作。只重排层级，不增删产品功能。
- 安全页沿用同一组件系统；本轮视觉稿不添加或删除安全页字段。
- 简体中文和英文共用同一套布局、业务状态和组件。
- 深色和浅色主题使用相同语义令牌，不允许页面内定义独立颜色。
- 高级视觉效果是可持久化设置。开启时仅内部浮层可使用轻微模糊和更柔和阴影；关闭时改用不透明表面，尺寸和交互不变。

## 2. 官方参考

官方仓库已作为只读参考拉取到：

```text
.tooling/references/shadcn-ui
```

- 上游：`https://github.com/shadcn-ui/ui`
- 固定参考提交：`2b3e6d4f8d9161fe5c19340dc383aade392012dd`
- 提交时间：`2026-09-12T16:59:53+04:00`
- 重点参考目录：`apps/v4/registry/new-york-v4/ui/`
- 许可证：MIT

此检出只用于理解布局、状态、变体和令牌，不加入 CMake、安装包或运行时依赖。React、Radix 和 Tailwind 代码不能直接进入产品。

## 3. 视觉原则

1. 信息优先：页面标题、分组标题、正文、辅助信息形成稳定的四级字号，不依赖大面积高饱和背景表达层级。
2. 边框优先：默认卡片采用 1 px 语义边框和纯色表面；阴影只用于弹窗、菜单、Toast 和需要抬升的短暂状态。
3. 控件紧凑：默认控件高 34 logical px，小控件 28 logical px，图标按钮按同档高度取正方形；DPI 只在运行时应用一次。
4. 圆角克制：输入框和按钮 6 px，内部卡片 10 px，浮层 10 px。外层窗口不使用这些值。
5. 状态完整：每个可交互组件必须具有 normal、hover、active、focus、disabled 和 invalid 状态。
6. 图标统一：操作图标使用项目选定的 Lucide SVG 几何，保持 1.75 px 视觉线宽和 16/18/20 px 三档尺寸；标题栏和 Client 悬浮球的品牌标识
   必须使用从 `src/px_panel/icon.ico` 原生帧提取的 32/48/64 px PNG，不得重新绘制近似 Logo 或使用字符模拟品牌图标。最近设备卡片使用固定版本
   `@tabler/icons-png@3.34.1` 的 Windows、Apple、Android、App Store PNG，按主题前景色着色；来源与 MIT 许可证随运行时发布到
   `resources/licenses/Tabler.txt`。
7. 1080p 清晰度：正文基础字号为 16 logical px；Roboto Regular、Roboto Medium 和两套字体合并的微软雅黑字形统一由 FreeType 原生 hinting
   光栅化。标题 Logo 与平台 PNG 提供 100%/150%/200% 三档资源并按当前显示缩放选择，避免从单张大图做过度缩小采样。FreeType 许可证随
   运行时发布到 `resources/licenses/FreeType.txt`。

## 4. 语义令牌

| 令牌 | 浅色 | 深色 | 用途 |
| --- | --- | --- | --- |
| `background` | `#FAFAFA` | `#09090B` | 根内容背景 |
| `foreground` | `#18181B` | `#FAFAFA` | 主要文字 |
| `card` | `#FFFFFF` | `#101014` | 卡片/面板 |
| `muted` | `#F4F4F5` | `#18181B` | 次级表面 |
| `muted_foreground` | `#71717A` | `#A1A1AA` | 辅助文字 |
| `border` | `#E4E4E7` | `#27272A` | 常规边框 |
| `input` | `#D4D4D8` | `#3F3F46` | 输入控件边框 |
| `primary` | `#007F49` | `#009A59` | Pixels 主操作/选中，取自产品图标主体绿；浅色主题加深以保证白字对比度 |
| `primary_foreground` | `#FFFFFF` | `#FFFFFF` | 主按钮文字 |
| `accent` | `#ECFDF5` | `#052E22` | hover/选中弱背景 |
| `success` | `#16A34A` | `#22C55E` | 在线/正常 |
| `warning` | `#D97706` | `#F59E0B` | 重试/降级 |
| `destructive` | `#DC2626` | `#EF4444` | 删除/错误 |
| `ring` | `#009A59` | `#8CEEC0` | 键盘焦点环；深色高光取自产品图标薄荷绿 |

透明度只能由主题系统生成，页面不得硬编码 alpha。文本对比度按 WCAG AA 验证。

产品 Logo 直接从 `src/px_panel/icon.ico` 的原生 32、48、64 像素帧提取，分别用于 100%、150%、200% DPI；不得从低分辨率帧放大。

## 5. shadcn 到 Dear ImGui 的组件映射

| shadcn 参考 | Pixels 组件 | 必须实现的变体/行为 |
| --- | --- | --- |
| Button / Button Group | `Button`, `IconButton`, `ButtonGroup` | default、secondary、outline、ghost、destructive；xs/sm/default/icon |
| Input / Input Group / Field | `TextField`, `FormField` | 前后图标、说明、错误、密码显示、复制 |
| Select / Combobox | `Select`, `SearchSelect` | 键盘导航、搜索、选中标记、边界自动翻转 |
| Checkbox / Switch | `Checkbox`, `Switch` | 标签整体可点、混合态（仅 Checkbox）、禁用态 |
| Card / Item | `Card`, `DeviceCard`, `ApplicationCard` | header/content/footer/action 插槽；不使用页面专属渐变 |
| Badge | `StatusBadge` | 在线、离线、运行、停止、警告 |
| Dialog / Alert Dialog | `DialogHost`, `ConfirmDialog` | 真正窗口内容区居中、遮罩、焦点圈定、Esc/Enter 规则 |
| Dropdown / Context Menu | `PopupMenu` | 一级按需展开；子菜单 hover/键盘后再出现；窗口边缘避让 |
| Popover / Tooltip | `Popover`, `Tooltip` | 锚点定位、延时、不可遮挡关键操作 |
| Sonner / Toast | `ToastHost` | 右下角堆叠、成功/警告/错误、可关闭、超时 |
| Tabs / Sidebar | `TabBar`, `NavigationRail` | 明确选中/未选中、无不必要滚动条 |
| Skeleton / Spinner / Progress | `AsyncState` | 加载、空、错误、重试；不可阻塞主线程 |

组件 API 使用强类型参数和枚举，不接受 `void*`/`std::any` 样式包。项目维护的 C++ 不保存、传递或捕获裸指针；异步 UI 回调使用 `weak_ptr` 并在执行点 `lock()`。

## 6. 页面信息架构（功能不变，任务重新排序）

上一版将组件样式替换误当成页面重设计，页面仍保留旧有的分组顺序。本版按用户进入页面后的真实任务重新排序，同时建立以下不可违反的功能清单。任何视觉实现不得因为空间不足隐藏、合并或发明功能。

### 全局框架

- 标题栏固定为 40 logical px，不绘制底部分隔线。左侧按 `PNG Logo + Pixels(版本)` 排列，右侧最小化/关闭使用 40 × 40 命中区；默认透明，
  hover 只显示圆形背景，所有元素垂直居中。
- 左栏固定宽 200 logical px：圆形头像与名称在顶部纵向排列并水平居中，六个页面入口在中部，不显示底部退出程序按钮。页面入口固定为
  150 × 35 logical px 并在侧栏内水平居中；入口内容左对齐，图标左边缘距离入口左边缘 30 logical px。左栏与外层窗口之间不保留额外 margin。
- 导航选中态使用浅色背景、左侧 3 px 指示条和高亮文字，不再整块使用高饱和蓝色。
- 主区域取消包住整个页面的大边框。页标题、页面状态和全局动作位于统一页头，内容由实际任务决定布局。
- 设计基准仍为 960 × 640 logical px；4K 截图等比放大，不把逻辑窗口扩大为 1440 × 960。

### 远程控制

- 页面标题旁显示管理服务状态；右侧不添加宣传性说明或额外动作。
- 第一视觉层是“本机设备”：标题与“远程控制”使用相同字号；左卡展示 ID、名称和临时密码及其复制、编辑、显示/隐藏操作，右卡展示桌面链接和
  网页地址及其复制、打开操作，两张卡片固定高 185 logical px。九位设备 ID 按 `XXX XXX XXX` 分组并使用 Medium 字重，复制仍读取无损原值。
- 第二视觉层以“远程控制”标题和管理服务状态开始，该标题行必须紧邻“连接到远程设备”命令 Card；输入框接受设备 ID、`link://` 或
  `IP[:端口]`，连接为唯一 primary 动作，刷新为 outline 动作。
- 第三视觉层是最近设备：严格三列、最多两行、最多六项，按最近连接时间排序。卡片左侧显示 Console 报告的平台图标，不显示右上角省略号；
  保留在线状态、9 位码/IP、设备名、双击和右键行为。
- 完整字符串是状态数据，UI 省略只影响绘制，复制和打开永远读取原值。

### 设备列表

- 主体采用约 39%/61% 的双列 master-detail：左列 Card 内依次放置设备数量、刷新、搜索和设备列表；右列详情只负责阅读与操作。
- 列表项只展示在线状态、设备名和 9 位码/IP；选中后右侧展示名称、ID、主机、端口、状态。
- 操作分成两行：第一行开始控制（primary）、仅观看（outline）；第二行文件传输、复制、编辑设备（ghost/outline）。删除位于详情右下角的 destructive 区域，与常规动作拉开距离。
- 删除后首页最近设备和设备列表使用同一数据源同步更新。

### 云应用

- 刷新位于页头右侧，不另占内容行。
- 主体只呈现三列应用网格，不套额外的大容器。卡片由图标、两行名称、状态和 overflow 按钮构成。
- 双击启动；右键菜单完整保留启动应用、仅观看、停止应用、强制 TCP、强制 Relay。
- 停止状态使用中性 Badge，运行状态使用 success Badge。长名称最多两行并提供 Tooltip，业务调用始终使用原始名称/ID。

### 服务状态

- 顶部三张健康 Card 分别表示控制器驱动、渲染服务和节点服务；每张只呈现已有名称和状态，渲染服务保留重启。
- 已连接客户端使用紧凑 metric Card，数值仍为现有数据，不添加趋势或统计图。
- 网络地址为下方独立详情 Card，原样保留 `192.168.31.6（有线）`、Panel 监听端口 4999、桌面连接端口 4601。
- 页面不出现硬件统计图，也不从状态值推导新功能。

### 设置

- 语言、主题作为页头 segmented controls；高级视觉效果放在常规设置内，与渲染代价说明同属一个 FormField，不伪装成页面导航。
- 内容采用 118 logical px 子导航 + 表单区：常规、网络、安全、控制器、关于保持不变。
- 码率使用数字步进器并在同一行标出 Mbps；帧率和编码使用 Select；调整分辨率使用 Switch，宽高在开启时可编辑；采集音频使用 Checkbox；保存是表单末尾唯一 primary 动作。
- 960 × 640 logical px 下常规页无需内部滚动；其他标签只在内容确实溢出时出现滚动条。

### 安全页

- 主页面是安全记录，不是安全设置：页头保留访问记录/文件传输记录 Tabs，以及“清空全部”危险操作。
- 访问记录表保留连接类型、开始时间、结束时间、访问设备、目标设备、持续时间、操作；文件传输表保留结果、开始时间、结束时间、访问设备、目标设备、方向、文件名、操作。
- 每条记录保留复制、复制 JSON、删除；删除单条和清空全部都复用要求长期密码的确认 Dialog，并展示密码错误状态。
- 不因最初五张参考图没有安全页而删除、合并或新增任何安全选项。

## 7. 普通/高级视觉效果

两种模式共享一棵 UI、同一交互区域和同一布局：

| 效果 | 普通模式 | 高级模式 |
| --- | --- | --- |
| 页面/卡片 | 不透明纯色 | 不透明纯色 |
| 内部菜单/Popover | 不透明表面 + 细边框 | 可选 8–12 px 背景模糊 + 细边框 |
| Dialog | 50% 黑色遮罩 + 不透明内容 | 50% 黑色遮罩 + 轻微半透明内容/模糊 |
| Toast | 不透明表面 | 轻微半透明/模糊 |
| 阴影 | 单层低成本阴影或关闭 | 两层柔和阴影 |
| 动画 | 80–120 ms 颜色切换 | 120–180 ms 淡入/缩放；遵循减少动画设置 |

如果 D3D11 特性、系统合成或性能预算不满足要求，运行时只降级效果，不改变功能。

## 8. 实现结构（设计冻结后执行）

```text
src/px_panel/ui_imgui/
  design_system/
    theme_tokens.*
    style_scope.*
    typography.*
    icons.*
  components/
    button.*
    form_controls.*
    card.*
    overlays.*
    navigation.*
    feedback.*
  effects/
    visual_effect_policy.*
    d3d11_blur_renderer.*
  pages/
    ...现有页面按职责接入组件
```

- 组合根创建主题、组件上下文、浮层宿主和页面；页面不直接操作 D3D11。
- `StyleScope`、字体、纹理、D3D11 状态和订阅全部用 RAII。
- 业务动作仍由现有 controller/workflow 完成，视觉重做不得复制连接、启动应用或持久化逻辑。
- 中英文目录键必须在无 UI 环境下进行 parity 测试。
- 组件状态使用截图/绘制命令快照测试；交互测试覆盖弹窗居中、菜单逐级展开、焦点恢复、跨 DPI 和重复开关效果。

## 9. 设计产物

- `px_panel_functional_redesign.svg`：可编辑的远控首页完整排版稿。
- `px_panel_functional_redesign.png`：SVG 的栅格预览。
- `px_panel_functional_redesign_board.png`：五页功能重排概念板，仅作为整体视觉参考；SVG 和本文档是实现约束。
- `px_panel_page_layouts.svg` / `.png`：设备列表、云应用、服务状态、设置四页的可编辑布局总览。
- `px_panel_security_layout.svg` / `.png`：安全记录页与删除确认弹窗布局。
- `feature_parity_matrix.md`：以当前源码为准的逐项功能守恒清单。
- `component_first_implementation_plan.md`：先组件库、后页面重排的完整开发与验收计划。

## 10. 视觉返工对照产物

- `implemented_*.png`：被 2026-09-13 复检否决的上一版实拍，只保留为差异证据，不是验收基线。
- `rework_remote_control.png`、`rework_device_list.png`、`rework_cloud_apps.png`、`rework_server_status.png`、`rework_security.png`、
  `rework_settings.png`：当前返工过程截图，最终验收前仍允许覆盖更新。
- `implemented_remote_control.png`：上一版远程控制页，960 × 640 logical px、4K/150% DPI 实拍。
- `implemented_remote_control_basic.png`：关闭高级视觉效果后的同布局实拍，用于确认模式切换不改变尺寸与 hit-test。
- `implemented_device_list.png`：设备列表 master-detail 实拍。
- `implemented_cloud_apps.png`：云应用三列卡片实拍。
- `implemented_server_status.png`：服务健康卡与网络详情实拍。
- `implemented_security.png`：安全记录表与操作区实拍。
- `implemented_settings.png`：设置子导航、表单与外观切换实拍。
- `implemented_client_startup_dialog.png`：Client 紧凑启动错误 Dialog 实拍。

实现使用 `src/px_ui` 的语义主题、RAII 样式范围以及按钮、表单、导航、数据、浮层和反馈组件。Panel 与 Client 页面不再直接创建可由组件表达的原始 ImGui 按钮、输入、复选、下拉、滑杆、进度条、菜单项或模态框。Panel 持久化的语言、深浅主题与普通/高级视觉效果通过启动信封同步给 Native/RDP Client；缺失新字段的旧启动数据继续使用兼容默认值。

高级模式当前提供半透明内部浮层和悬浮控制器柔和阴影；普通模式使用不透明表面。真正的背景采样模糊仍按能力降级规则关闭，未把 D3D11 专属实现塞入跨平台 `px_ui`，也未改变外层 Windows 10/11 系统轮廓。

### 2026-09-13 组件复检结果

- Button：primary、secondary、accent、outline、ghost、destructive、link、图标、忙碌、禁用与 focus ring 已统一。
- Form：Text、Search、Password、TextArea、带加减按钮的 Number、Slider、Checkbox、Switch、Select 已统一；密码显示按钮和选中标记不再由页面拼装。
- Overlay：所有现存 Panel/Client Modal 均使用无原生 ImGui 标题条的 Dialog Header/Content/Footer；Context Menu 使用自绘图标项、危险项、禁用项和分隔线。
- Feedback：Inline Alert、右下角 Toast、错误 Dialog、Tooltip 均使用语义状态图标和主题令牌。
- Identity/Navigation/Data：Avatar、Badge、侧栏项、Tabs、Selectable Row、空状态、Spinner、Progress 已进入共享组件层。
- Client：悬浮球使用产品 Logo；一级菜单和显示、控制、工具、语音、设置二级菜单的可见文字与动作图标已经补齐。
