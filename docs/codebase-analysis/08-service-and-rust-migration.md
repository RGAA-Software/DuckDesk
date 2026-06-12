# Service 模块分析与 Rust 迁移方案

## 当前状态

截至当前仓库状态：

- CMake 已禁用旧的 C++ `GammaRayService` / `GammaRayServiceManager` 可执行文件构建
- 生产构建启用的是 Rust 版本：
  - `rust_client/gr_service` -> `GammaRayService.exe`
  - `rust_client/gr_service_mgr` -> `GammaRayServiceManager.exe`
- `src/GammaRay/src/service/service_manager.cpp` 仍保留为 C++ 包装层，用于让现有 panel/uninstall 代码继续通过原接口调用 Rust manager
- 历史 C++ service 主体代码已迁到 `src/GammaRay/src/service/legacy`

因此，本文后续对 service 的拆解，主要是：

- 解释历史 C++ service 的行为与设计
- 说明 Rust 版本需要保持的兼容契约
- 记录迁移后的最终落点

本文聚焦 service 层。目标有两部分：

1. 把当前 C++ service 的职责、调用链、行为边界讲清楚。
2. 给出一个可执行的 Rust 迁移方案，逐步替换这套 C++ 实现。

## 1. 目录与产物

当前目录已经分为“活跃层”和“历史层”：

- 活跃层：`src/GammaRay/src/service`
  - `service_manager.cpp`
  - `service_manager.h`
  - `CMakeLists.txt`
- 历史层：`src/GammaRay/src/service/legacy`

- 历史层中保留的 Windows Service 主进程
  - `service_main.cpp`
  - `service.cpp`
  - `service_context.cpp`
  - `service_msg_server.cpp`
  - `render_manager.cpp`
- 历史层中保留的 Service 管理工具
  - `service_manager_main.cpp`
- 活跃层保留的 C++ 包装层
  - `service_manager.cpp`
- 历史层中的协议/模型/辅助头文件
  - `service.h`
  - `service_context.h`
  - `service_msg_server.h`
  - `render_manager.h`
  - `render_process.h`
  - `service_messages.h`

活跃构建入口在 [src/GammaRay/src/service/CMakeLists.txt](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/CMakeLists.txt:1)。

历史上这里会形成两个可执行文件：

- `GammaRayService.exe`
- `GammaRayServiceManager.exe` 或等价管理工具

当前活跃产物已改为 Rust 同名二进制。

## 2. 模块定位

这套 service 代码不是普通“后台线程”，而是一个真正注册到 Windows SCM 的系统服务层。它的核心定位是：

- 以 Windows Service 身份常驻
- 通过本地 WebSocket 接收面板和 render 发来的 protobuf 控制消息
- 以当前登录用户身份启动 `GammaRayRender.exe`
- 在 render 异常退出时自动拉起
- 在服务停止或会话变化时清理相关进程
- 提供高权限系统动作入口，例如 `Ctrl+Alt+Delete`

一句话概括：

`GammaRayService` 是 `GammaRayRender.exe` 的本地守护与高权限代理层。

## 3. 现有功能详细拆解

## 3.1 Windows Service 生命周期

历史 C++ service 入口在 [service_main.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/service_main.cpp:132)。

启动流程：

1. 初始化日志到 `ProgramData/gr_logs/godesk_service.log`
2. 初始化 Breakpad 崩溃转储
3. 从命令行读取监听端口
4. 创建 `ServiceContext`
5. 创建 `GrService`
6. 调用 `StartServiceCtrlDispatcher`
7. 由 `ServiceMain()` 正式挂入 Windows SCM

控制回调 `ServiceCtrlHandler()` 处理：

- `SERVICE_CONTROL_STOP`
- `SERVICE_CONTROL_CONTINUE`
- `SERVICE_CONTROL_PAUSE`
- `SERVICE_CONTROL_INTERROGATE`
- `SERVICE_CONTROL_SESSIONCHANGE`

其中 `SESSIONCHANGE` 会继续细分：

- `WTS_CONSOLE_CONNECT`
- `WTS_CONSOLE_DISCONNECT`
- `WTS_SESSION_LOGON`
- `WTS_SESSION_LOGOFF`
- `WTS_SESSION_LOCK`
- `WTS_SESSION_UNLOCK`

## 3.2 `GrService` 的职责

`GrService` 是历史 C++ service 进程里的主控对象，定义见 [service.h](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/service.h:18)，实现见 [service.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/service.cpp:19)。

它负责：

- 初始化并启动 `ServiceMsgServer`
- 挂载 `RenderManager`
- 向 SCM 汇报 `SERVICE_RUNNING` / `SERVICE_STOPPED`
- 维护一个内部任务线程
- 响应 stop/continue 等控制命令
- 清理与 GammaRay 相关的辅助进程
- 调用 `sas.dll` 发送 Secure Attention Sequence

历史实现中的 stop 逻辑在 [service.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/service.cpp:72)：

- 服务状态切到 `STOP_PENDING`
- 退出任务线程
- 调 `StopAll()`

`StopAll()` 会关闭：

- `GammaRayGuard.exe`
- `GammaRayRender.exe`
- `GammaRayClientInner.exe`
- `GammaRaySysInfo.exe`

这说明 service 不只守护 render，也承担“整套附属进程清道夫”的角色。

## 3.3 会话事件处理

会话相关入口在 [service_main.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/service_main.cpp:67)，具体逻辑在 [service.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/service.cpp:93)。

当前行为：

- `OnConsoleConnect()`
  - 延迟 150ms
  - 调 `render_manager_->StopDesktopRender()`
- `OnSessionLogon()` / `OnSessionLogoff()`
  - 只记日志
- `OnSessionLock()` / `OnSessionUnlock()`
  - 只记日志
  - 曾计划做 render 停止逻辑，但目前注释掉了

因此，这部分实现还不完整。当前只有“控制台连接时停掉 desktop render”是真正落地的行为。

## 3.4 `ServiceContext` 提供的基础设施

`ServiceContext` 见 [service_context.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/service_context.cpp:14)。

它封装了 service 运行时依赖：

- `asio2::iopool`
  - 用于后台异步任务
- `MessageNotifier`
  - 用于本进程内事件分发
- `asio2::timer`
  - 用于周期任务
- `SharedPreference`
  - 用于本地持久化
- `listening_port_`
  - WebSocket 服务监听端口

当前定时消息有两种：

- `MsgTimer1S`
- `MsgTimer3S`

实际被使用的是 `MsgTimer3S`，由 `RenderManager` 用来做 render 存活巡检。

持久化文件位置：

- `ProgramData/gr_data/godesk_service.dat`

## 3.5 `ServiceMsgServer` 的协议职责

`ServiceMsgServer` 见 [service_msg_server.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/service_msg_server.cpp:25)。

它是 service 的外部控制入口，基于 `asio2::ws_server`。

职责包括：

- 监听 `0.0.0.0:<port>`
- 接收本地 WebSocket 连接
- 将二进制消息按 protobuf `ServiceMessage` 解析
- 将控制命令分发给 `RenderManager` 或 `GrService`
- 将心跳响应广播给所有连接

当前支持的消息类型：

- `kSrvStartServer`
  - 启动 desktop render
- `kSrvStopServer`
  - 停止 desktop render
- `kSrvRestartServer`
  - 重启 desktop render
- `kSrvHeartBeat`
  - 查询 render 存活状态
- `kSrvReqCtrlAltDelete`
  - 请求发送 Secure Attention Sequence

对应处理函数见 [service_msg_server.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/service_msg_server.cpp:148)。

心跳响应内容：

- 原样回传心跳索引
- 返回 render 状态
  - `kWorking`
  - `kStopped`

## 3.6 `RenderManager` 的业务职责

`RenderManager` 是历史 C++ service 模块里最核心的业务层，见 [render_manager.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/render_manager.cpp:27)。

它负责：

- 启动 `GammaRayRender.exe`
- 停止 `GammaRayRender.exe`
- 重启 `GammaRayRender.exe`
- 周期性扫描系统进程，判断 render 是否存活
- 记录上次启动参数
- render 异常退出后自动重拉起

### desktop render 启动链

`StartDesktopRender()` 的行为：

1. 组装命令行参数字符串
2. 读取持久化中的上次参数
3. 扫描当前系统进程
4. 判断 desktop render 是否已在运行
5. 如果参数变化，则先停旧 render
6. 保存新的工作目录、应用路径、参数
7. 调 `StartDesktopRenderInternal()`

关键点在 [render_manager.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/render_manager.cpp:186)：

启动 render 实际调用的是：

- `ProcessUtil::StartProcessInSameUser(...)`

这意味着它不是简单 `CreateProcess`，而是尝试以“当前登录用户上下文”启动 render。这个能力正是 service 需要保留的核心能力之一。

### desktop render 停止链

`StopDesktopRender()` 会：

- 先关闭已记录的 desktop render pid
- 再扫一遍系统进程
- 把所有 `GammaRayRender.exe` 都杀掉

这里是按进程名兜底清理，不区分 desktop / inner 模式。

### 自动重启逻辑

构造函数里监听 `MsgTimer3S`，每 3 秒做一次：

1. 获取全部进程
2. 调 `CheckAliveRenders()`
3. 如果 desktop render 不存在，但历史启动参数还在，则重新拉起

见 [render_manager.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/render_manager.cpp:29)。

### 持久化的数据

写入 `SharedPreference` 的键有：

- `render_work_dir`
- `render_app_path`
- `render_app_args`

这让 service 在自己重启后，仍知道上一次应该守护哪个 render。

## 3.7 `ServiceManager` 的职责

`ServiceManager` 是安装/删除/查询 service 的工具层，见 [service_manager.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/service_manager.cpp:37)。

它支持：

- `Install()`
  - 打开 SCM
  - 如果服务不存在则 `CreateServiceW`
  - 如果服务已存在但停止，则 `StartService`
  - 配置失败自动重启
- `Remove(bool uninstall_service)`
  - 停止依赖服务
  - 停止本服务
  - 删除服务
  - 可选附带杀 `GammaRay.exe`
- `QueryStatus()`
  - 调 `sc query`
- `GetServiceExecutablePath()`
  - 查询 SCM 中记录的可执行路径

历史控制台入口在 [service_manager_main.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/service/legacy/service_manager_main.cpp:14)，支持：

- `install`
- `remove`
- `query`
- `sr`

## 4. 与其他进程的调用关系

这部分 service 不是孤立存在的。

## 4.1 Panel 如何使用 service

在 [render_panel/gr_context.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/render_panel/gr_context.cpp:101) 中，panel 会初始化：

- 服务名 `GammaRayService`
- 可执行路径 `GammaRayService.exe <sys_service_port>`

在 [render_panel/gr_render_controller.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/render_panel/gr_render_controller.cpp:22) 中，panel 会把 render 启动参数打包成 `ServiceMessage` 发给 service。

参数覆盖面很大，包括：

- 编码器类型
- 分辨率
- FPS
- 音频采集
- WebSocket / WebRTC / UDP 端口
- device id
- relay 参数
- 安全口令
- panel/render 端口
- appkey

因此 service 自身并不理解这些业务语义，只是转发并负责以正确权限把 render 拉起来。

## 4.2 Panel 如何检测 service 状态

在 [render_panel/network/gr_service_client.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/render_panel/network/gr_service_client.cpp:22) 中，panel 会：

- 建立到 `127.0.0.1:20375/service/message` 的 WebSocket 连接
- 每 1 秒发一次 `kSrvHeartBeat`
- 从 `kSrvHeartBeatResp` 中获取 render 是否存活

## 4.3 Render 如何使用 service

在 [render/network/render_service_client.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/render/network/render_service_client.cpp:20) 中，render 自己也会连接 service 并发心跳。

在 [render/rd_app.cpp](/D:/thunder_cloud/GammaRayPremium/src/GammaRay/src/render/rd_app.cpp:1146) 中，render 还会通过 service 发起：

- `kSrvReqCtrlAltDelete`

原因很直接：这个动作需要高权限，不适合直接放在普通 render 进程里。

## 5. 当前 C++ 实现的问题

如果目标是不再维护 C++ 版本，这部分问题也决定了 Rust 迁移的优先级。

## 5.1 功能上未完成的部分

- `RenderManager::StartRender()` / `ReStartRender()` / `StopRender(RenderProcessId)` 基本未实现完成
- 多 render 实例管理只是预留了 `RenderProcess` 结构，但没有形成完整能力

## 5.2 协议与安全性偏弱

- WebSocket 没有真正做授权校验
- `/service/message` 只是客户端约定，服务端没有严格校验 path
- 心跳响应是广播给所有 session，不是精确回给请求方

## 5.3 进程控制过于粗糙

- 停止 render 时按进程名全杀
- `app_path` 参数传进来了，但实际启动时直接硬编码使用 `work_dir/GammaRayRender.exe`
- 会话事件逻辑只做了一半

## 5.4 代码结构老化

- Windows Service API、进程控制、业务协议、守护策略混在一起
- 依赖 `asio2` + Qt 风格工具类 + 自定义 `SharedPreference`
- 可测试性弱
- 难以做单元测试和分层替换

## 6. 迁移到 Rust 的目标定义

如果要停止维护 C++ 版本，Rust 版本不应该只是“功能大致差不多”，而应该明确替换以下职责：

1. 替换 `GammaRayService.exe`
2. 替换 `ServiceManager`
3. 兼容现有 protobuf `ServiceMessage` 协议
4. 兼容现有 panel / render 的连接方式
5. 保持“以当前登录用户启动 render”的能力
6. 保持自动守护与自动重启能力
7. 保持 `Ctrl+Alt+Delete` 代理能力

第一阶段不必同步重写 panel / render 端，只需要先让 C++ panel 和 C++ render 能无感连接 Rust service。

## 7. 现有 Rust 基础可以怎么复用

仓库已经有 Rust workspace：

- [rust_client/Cargo.toml](/D:/thunder_cloud/GammaRayPremium/rust_client/Cargo.toml:1)

现有成员：

- `base`
- `gr_auth_mgr`
- `gr_sysinfo`

这说明仓库已经接受：

- Rust workspace 组织方式
- 独立二进制 crate
- `tokio`
- `tracing`
- `tokio-tungstenite`
- 本地工具库抽取

可直接复用的方向：

- `base::log_util`
  - 可做 service 日志基础
- `base::kv_storage`
  - 可以参考，但当前实现是基于当前 exe 目录；service 更适合改成 `ProgramData/gr_data`
- 现有 `tokio` / `tokio-tungstenite` 风格
  - 可直接用于新的本地 WS server / client

## 8. 建议的 Rust 新架构

建议在 `rust_client/` 下新增至少两个 crate：

- `gr_service`
  - 替换 `GammaRayService.exe`
- `gr_service_mgr`
  - 替换 `ServiceManager`

如果希望长期可维护，建议再拆一个共享 crate：

- `gr_service_core`
  - 放协议适配、状态模型、守护策略、配置持久化、进程抽象

推荐结构：

```text
rust_client/
  Cargo.toml
  base/
  gr_auth_mgr/
  gr_sysinfo/
  gr_service_core/
  gr_service/
  gr_service_mgr/
```

### `gr_service_core` 建议模块

- `config`
  - 监听端口、存储路径、服务名、进程名常量
- `storage`
  - 持久化 render 启动参数
- `proto`
  - `ServiceMessage` 编解码适配
- `service_state`
  - 当前连接、render 状态、最后启动参数
- `render_manager`
  - start/stop/restart/check_alive
- `process_guard`
  - 周期巡检与自动拉起策略
- `session_monitor`
  - Windows session change 映射
- `sas`
  - `Ctrl+Alt+Delete` 封装
- `windows_process`
  - 当前用户进程启动、枚举、结束

### `gr_service` 建议模块

- `main.rs`
  - 初始化日志、加载配置、启动 service
- `windows_service_host.rs`
  - 与 Windows SCM 对接
- `ws_server.rs`
  - 本地 WebSocket 服务
- `command_dispatch.rs`
  - 把 protobuf 消息转成内部命令

### `gr_service_mgr` 建议模块

- `install`
- `remove`
- `query`
- `path`

尽量用同一个 Windows Service 管理封装，而不是把 Win32 调用散在入口函数里。

## 9. C++ 到 Rust 的一一映射建议

### 9.1 生命周期与 Service API

C++：

- `legacy/service_main.cpp`
- `GrService::Run()`

Rust：

- 使用 `windows-service` crate 对接 SCM
- 在 service entry 中创建 `tokio` runtime
- 用 `mpsc` 或 `watch` 管理 stop / pause / session 事件

迁移原则：

- 先保持控制面一致
- 不要一开始就改变 Windows Service 名、启动参数格式、日志路径

### 9.2 WebSocket 服务

C++：

- `asio2::ws_server`

Rust：

- `tokio` + `tokio-tungstenite`
- 或 `axum` / `hyper` + WS 升级

建议：

- 第一阶段只保留本地未加密 WS
- 消息格式继续使用现有 protobuf
- 不改变路径 `/service/message`

### 9.3 定时器与后台任务

C++：

- `asio2::iopool`
- `asio2::timer`
- `MessageNotifier`

Rust：

- `tokio::spawn`
- `tokio::time::interval`
- `tokio::sync::{broadcast, watch, mpsc}`

迁移时不需要照抄 `MessageNotifier`；直接换成更简单的 channel 模型即可。

### 9.4 SharedPreference 持久化

C++：

- `SharedPreference`
- `godesk_service.dat`

Rust：

- 短期可用 JSON/TOML 文件持久化
- 中期可用 `sled`
- 路径固定到 `ProgramData/gr_data`

建议：

- 只保存 service 真正需要恢复的字段
  - `work_dir`
  - `app_path`
  - `app_args`
  - `last_mode`

不要把 C++ `SharedPreference` 的宽泛接口原样复制到 Rust。

### 9.5 进程控制

C++：

- `ProcessHelper::GetProcessList`
- `ProcessHelper::CloseProcess`
- `ProcessUtil::StartProcessInSameUser`

Rust：

- 枚举和终止进程可用 Win32 API 封装
- “以当前用户身份启动”需要重点实现

这一块是迁移难点，建议分两层：

- `process_snapshot`
  - 负责查 PID、路径、命令行
- `user_session_launcher`
  - 负责获取活动 session token 并 `CreateProcessAsUserW`

如果无法一次做完，建议第一阶段直接通过现有 C++ helper 暴露极小 DLL 或命令行桥接，但这是临时过渡，不应长期保留。

### 9.6 Ctrl+Alt+Delete

C++：

- `LoadLibraryW("sas.dll")`
- `GetProcAddress("SendSAS")`

Rust：

- 用 `windows` crate 调 Win32 API
- 动态加载 `sas.dll`
- 保持和现有实现一致

这是独立、边界很清楚的一块，适合优先迁掉。

## 10. 建议的迁移顺序

不建议一次性重写全部 service。风险最大的是“进程权限 + 启动 render + 会话切换”。更稳妥的顺序如下。

## 阶段 0：协议冻结

先冻结当前 service 对外契约：

- 服务名
- 启动参数格式
- WS 地址和路径
- protobuf 消息类型
- 心跳响应格式

这样 Rust 版可以直接替换服务进程，不要求 panel/render 立刻改代码。

## 阶段 1：先做 Rust `ServiceManager`

优先替换 `service_manager.cpp`，原因：

- 边界清楚
- 与 render 守护逻辑无关
- 易于验证
- 可以先建立 Rust 对 Win32 SCM 的封装

交付目标：

- `install`
- `remove`
- `query`
- `get executable path`

## 阶段 2：实现 Rust WS 服务骨架

先不接入复杂进程控制，只做：

- 启动 Windows Service
- 监听本地 WS
- 解析 protobuf
- 返回心跳
- 打日志

交付目标：

- panel / render 能连接上 Rust service
- 心跳协议完全兼容

## 阶段 3：迁移 render 状态持久化和守护循环

实现：

- 启动参数持久化
- 周期巡检
- render 存活检测
- 自动重启策略

这一阶段仍可先把“真正启动 render”保留为桥接调用，只要服务主逻辑已经切到 Rust。

## 阶段 4：迁移当前用户上下文启动能力

这是关键阶段。

需要完整替换：

- `StartProcessInSameUser`

完成后 Rust service 才能真正取代 C++ service。

建议单独做集成验证：

- 未登录用户
- 本地登录
- 锁屏
- RDP 登录
- 重新连接控制台

## 阶段 5：迁移 SAS 和会话事件

实现：

- `Ctrl+Alt+Delete`
- `SESSIONCHANGE`
- `CONSOLE_CONNECT`
- `LOCK/UNLOCK`

这一步完成后，Windows 行为层就基本闭环了。

## 阶段 6：删掉 C++ service 构建链

在 Rust service 稳定后：

- 从 CMake 产物中去掉 C++ `GammaRayService`
- panel 默认改为指向 Rust 产物
- 最后移除 `src/GammaRay/src/service/legacy/**`

## 11. 迁移时必须重点验证的风险

## 11.1 最高风险：当前用户上下文启动

这是整个迁移最容易出问题的地方。因为 render 不是服务自己运行，而是要跑到当前交互用户桌面。

需要验证：

- token 获取是否稳定
- `CreateProcessAsUserW` 所需权限是否满足
- 锁屏/解锁后桌面切换是否正确
- 多用户 session 下是否会起错桌面

## 11.2 命令行兼容性

当前 panel 发送了一大批参数给 service，但 C++ service 实际只依赖其中少数。Rust 版要明确：

- 哪些参数只是透传给 render
- 哪些参数 service 自己要使用
- `app_path` 是否开始真正生效

建议在 Rust 版里修正当前问题：

- 直接按传入 `app_path` 启动，不再硬编码 `work_dir/GammaRayRender.exe`

## 11.3 状态恢复行为

当前自动重启依赖持久化的：

- `render_work_dir`
- `render_app_path`
- `render_app_args`

Rust 版如果改变格式，需要考虑是否：

- 做一次迁移脚本
- 或直接允许服务升级后丢失上次恢复状态

## 11.4 心跳广播行为

当前 C++ 服务把心跳响应广播给所有连接。这个行为本身不理想，但如果 panel/render 有隐含依赖，贸然改成“只回请求方”可能引入兼容问题。

建议：

- 第一阶段保持兼容
- 第二阶段再评估是否收敛

## 12. 推荐的落地策略

如果目标是“以后不再维护 C++ service”，推荐策略不是一次重写，而是：

1. 先让 Rust 版在协议层完全兼容
2. 再逐步替换进程管理和系统权限逻辑
3. 最后从构建系统移除 C++ service

更具体一点：

1. 新建 `gr_service_mgr`，先替换服务安装管理
2. 新建 `gr_service`，先只跑 WS 心跳和日志
3. 再补守护循环和持久化
4. 最后攻克“以当前用户身份启动 render”

这样你可以很快摆脱对 C++ 的新增维护，而不是把整个系统停在“大重写未完成”的状态。

## 13. 建议的 Rust crate 清单

最小可行版本：

- `gr_service_mgr`
- `gr_service`

推荐版本：

- `gr_service_core`
- `gr_service_mgr`
- `gr_service`

如果要把 protobuf 一并正规化，还可以加：

- `gr_service_proto`

用于统一管理 `ServiceMessage` 的 Rust 生成代码与适配层。

## 14. 最终建议

这块 service 代码适合迁到 Rust，原因很明确：

- 边界相对集中
- 对外协议简单
- 业务复杂度主要在进程与系统交互，不在 UI
- 已有 Rust workspace 可承接

但这不是“把 C++ 语法翻译成 Rust”就能完成的迁移。真正需要保住的是三件事：

1. Windows Service 生命周期
2. 当前登录用户上下文启动 render
3. 本地协议兼容

只要这三件事迁稳，剩下的逻辑在 Rust 里会比现在更清晰、更可测、更容易继续维护。
