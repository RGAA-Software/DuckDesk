# 面板进程 `GammaRay` 详解

## 1. 入口做了什么

`src/GammaRay/main.cpp` 是面板端总入口。启动顺序非常清晰：

1. 申请硬件重启权限
2. 提升进程优先级
3. 创建 `QApplication`
4. 初始化 Breakpad 崩溃上报
5. 清理旧 dump
6. 准备程序数据目录
7. 初始化日志
8. 探测 OpenGL 后端并写入 `GrSettings`
9. 解析命令行参数
10. 通过 `GrRunningPipe` 做单实例/唤醒已有实例
11. 初始化 `SharedPreference`
12. 注册开机自启动任务
13. 枚举显示适配器
14. 初始化字体和语言
15. 创建 `GrWorkspace`

这个入口说明：面板不是轻 UI 壳，而是整个本机控制中心。

## 2. 数据目录与本地状态

启动时会建立如下目录：

- `gr_logs`
- `gr_data`
- `gr_data/client`
- `gr_data/render`
- `gr_data/panel`
- `gr_data/cache`
- `gr_dumps`

这意味着面板默认就是本地状态中心，负责：

- 日志
- 面板/客户端/渲染端分目录数据
- crash dump
- 缓存

## 3. `GrWorkspace` 是 UI 外壳

`GrWorkspace` 是主窗口，核心职责有：

- 初始化主标签页
- 控制窗口关闭/缩放/托盘行为
- 用户登录/注册/头像/资料
- 检查更新
- 协调 `GrApplication`

从头文件看，主标签枚举包含：

- `kTabServer`
- `kTabServerStatus`
- `kTabGames`
- `kTabCoPhone`
- `kTabSettings`
- `kTabSecurity`
- `kTabProfile`
- `kTabHWInfo`

这已经反映了产品核心功能面：

- 设备/服务控制
- 运行状态
- 游戏或应用流
- 安全
- 用户与资料
- 硬件信息

## 4. `GrApplication` 是面板业务控制器

`GrApplication` 不是 `QApplication`，而是面板业务总控对象。它串起了面板端最关键的运行时对象：

- `GrContext`
- `WsPanelServer`
- `GrServiceClient`
- `GrRenderMsgProcessor`
- `ClipboardManager`
- `GrConnectedManager`
- `GrSpvrClient`
- `SpvrScanner`
- `GrUserManager`
- `GrDeviceManager`
- `MonitorRefresher`
- `PanelCompanion`

可以把它理解成“面板进程内的应用服务总线”。

### 它负责的关键动作

- 初始化面板服务
- 启动 Windows 消息循环
- 注册消息监听
- 管理服务端连接
- 和渲染端收发消息
- 检查本机设备信息是否合法
- 同步安全密码
- 连接 SPVR 服务
- 暴露设备管理和用户管理入口

## 5. `GrContext` 是线程、数据库和共享资源枢纽

`GrContext` 的职责比普通 context 重得多，它同时提供：

- 通用任务投递 `PostTask`
- UI 线程任务投递 `PostUITask`
- 延迟任务
- DB 专用任务
- 数据库访问入口
- `GrRenderController`
- `GrRunGameManager`
- `ServiceManager`
- `RunningStreamManager`
- `NotifyManager`
- SPVR 管理器
- 本地 `SharedPreference` 读写

这意味着面板内部的大部分跨模块协作，最后都会经过 `GrContext`。

## 6. 面板端按目录划分的职责

### `render_panel/network`

负责面板侧网络与本地控制接口：

- `ws_panel_server`：面板 WebSocket 服务
- `gr_service_client`：和服务进程通信
- `gr_spvr_client`：和 SPVR 平台通信
- `render_api`：访问渲染端 HTTP API
- `udp_broadcaster`：广播发现相关能力
- `http_handler`：本地 HTTP 处理

### `render_panel/database`

负责本地持久化数据：

- 游戏库 `db_game*`
- 访问记录 `visit_record*`
- 文件传输记录 `file_transfer_record*`
- 流记录 `stream_db_operator`
- 总数据库入口 `gr_database`

### `render_panel/devices`

负责设备与流管理 UI/逻辑：

- 流列表
- 创建流
- 连接信息
- 运行中流状态
- 中继流编辑
- 监视器/连接详情展示

### `render_panel/ui`

负责主标签页和设置面板：

- `tab_server`、`tab_server_status`
- `tab_game`
- `tab_settings`
- `tab_security_internals`
- `tab_profile`
- `tab_hw_info`
- 多个 `st_*` 设置子页

### `render_panel/user`

负责用户态逻辑，如登录状态、用户资料、昵称/密码修改等。

### `render_panel/spvr` 与 `spvr_scanner`

负责远端平台发现、接入和事件管理。

## 7. 面板如何控制渲染端

控制渲染端的核心对象是 `GrRenderController`。它提供：

- `StartServer()`
- `StopServer()`
- `ReStart()`
- `Exit()`

从命名和上下文可以判断，所谓 “Server” 实际上是指本机渲染/串流进程，而不是单独后端服务。

面板和渲染端之间至少存在三条控制路径：

1. 启停进程
2. 通过 HTTP 查询配置/校验密码
3. 通过消息通道同步实时状态

## 8. 面板如何管理设备与平台

`GrDeviceManager` 直接调用 `spvr::SpvrDeviceApi` 完成远端平台交互，包括：

- 申请新设备
- 更新桌面入口链接
- 更新设备名
- 更新使用时长
- 查询设备信息

关键输入来自：

- `GrSettings` 中的 SPVR host/port
- `grApp->GetAppkey()`
- 本机 `device_id`

说明“设备注册和远端平台同步”不是外部脚本做的，而是面板主流程的一部分。

## 9. 面板进程的核心定位

如果只看 UI，很容易误以为 `GammaRay` 只是配置壳。但从代码看，它其实承担了五层责任：

1. 本地控制台
2. 配置中心
3. 设备平台接入端
4. 渲染进程调度器
5. 本地数据库与状态中心

因此它是整套系统里最“厚”的业务进程。

