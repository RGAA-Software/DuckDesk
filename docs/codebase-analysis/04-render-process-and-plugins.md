# 渲染进程 `GammaRayRender` 与插件体系

## 1. 渲染进程入口

`src/GammaRay/src/render/rd_main.cpp` 负责渲染端启动，它的主流程是：

1. 解析 gflags 参数
2. 初始化 Breakpad
3. 提升进程优先级
4. 加载 `settings.toml`
5. 用命令行覆盖配置
6. 再从数据库加载动态配置
7. 初始化日志
8. 通过 `QLockFile` 保证同端口单实例
9. 创建 `RdApplication`
10. 初始化硬件信息
11. 进入运行循环

和面板相比，渲染端明显更偏“工作进程”。

## 2. 渲染端配置项体现了它的职责

入口参数大致分为几类：

### 编码

- 编码器类型
- H.264 / H.265
- 码率
- FPS
- 输出分辨率

### 采集

- 音频开关
- 视频开关
- 内部采集 / 全局采集
- 指定音频设备
- mock video

### 网络

- WebSocket / WebRTC
- TCP/UDP 监听端口
- 中继信息
- 信令服务器信息

### 安全和设备身份

- `device_id`
- `device_random_pwd`
- `device_safety_pwd`
- `appkey`

### 会话能力

- 是否允许操作
- 文件传输开关
- 音频开关
- relay 开关

这已经说明渲染端是“采集、编码、传输、安全校验”的集中执行体。

## 3. `RdApplication` 是渲染端总控

`RdApplication` 管理的对象很多，关键包括：

- `RdContext`
- `AppManager`
- `EncoderThread`
- `WsPanelClient`
- `PluginManager`
- 采集插件
- 视频编码插件
- 音频采集/编码插件
- `RenderServiceClient`
- `WinDesktopManager`

它对外暴露的能力也非常直接：

- 投递 IPC 消息
- 投递网络消息
- 处理采集视频帧
- 获取当前工作采集插件
- 获取工作编码插件
- 请求 Ctrl+Alt+Delete
- 处理强制切换 GDI 采集
- 更新当前采集显示器信息

因此 `RdApplication` 本质上是“渲染流水线协调器”。

## 4. 渲染端内部按目录的职责

### `render/app`

偏内核层，负责：

- 进程/应用启动管理
- 定时器
- 编码线程
- 共享信息
- Steam 游戏相关逻辑

### `render/network`

负责网络出入口和面板回传：

- `ws_media_router`
- `ws_ipc_router`
- `ws_panel_client`
- `server_cast`
- `render_service_client`
- `net_message_maker`

从命名看，这里既包含给远端客户端的数据路径，也包含回到面板或服务的控制路径。

### `render/settings`

负责加载和管理渲染侧运行参数。

### `render/plugins`

负责插件加载、事件路由和插件协作。

### `render/plugin_interface`

定义插件抽象层，是整个渲染端扩展机制的基础。

## 5. 渲染端插件模型

插件接口库里定义了多种角色：

- `GrPluginInterface`
- `GrStreamPlugin`
- `GrNetPlugin`
- `GrVideoEncoderPlugin`
- `GrMonitorCapturePlugin`
- `GrFrameProcessorPlugin`
- `GrDataProviderPlugin`
- `GrAudioEncoderPlugin`
- `GrFrameCarrierPlugin`

这不是单一“插件”概念，而是按媒体流水线的不同阶段做了分层抽象。

### 可以这样理解

- `MonitorCapturePlugin`：采什么
- `VideoEncoderPlugin`：怎么压
- `FrameProcessorPlugin`：压前怎么改帧
- `FrameCarrierPlugin`：帧怎么搬运或打标签
- `NetPlugin`：怎么发出去
- `DataProviderPlugin`：不是来自真实采集，而是来自某种数据源

## 6. 插件加载过程

`PluginManager::LoadAllPlugins()` 的逻辑非常关键：

1. 扫描运行目录 `gr_plugins/*.dll`
2. 用 `QLibrary` 动态加载
3. 查找导出函数 `GetInstance`
4. 获取插件 ID，防重
5. 构造 `GrPluginParam`
6. 读取插件同名 `.dll.toml`
7. 调用 `OnCreate`
8. 记录启用状态
9. 插件间互相注入引用

### 插件初始化参数来源

主程序会注入：

- `base_path`
- `base_data_path`
- 音频设备 ID
- WS / UDP 监听端口
- `device_id`
- relay 开关和地址
- `language`
- `appkey`

这表示插件不是孤立组件，而是拿到完整运行上下文后独立工作。

## 7. 插件间协作方式

加载完成后，`PluginManager` 会做两轮“装配”：

### 第一轮：把所有网络插件挂给非网络插件

非 Net 插件可以直接把输出交给网络层。

### 第二轮：把所有插件互相挂接

插件之间可以通过统一接口互相发现和调用。

再加上 `RegisterPluginEventsCallback()`，插件还能把事件回送给 `PluginEventRouter`。

所以渲染端本质上是“主进程 + 多插件协作图”，不是简单链式调用。

## 8. Premium 渲染插件职责清单

顶层 `src/render_plugins` 是 Premium 增量能力的主要落点：

### 采集与画面处理

- `gdi_capture`：GDI 桌面采集
- `frame_resizer`：帧尺寸调整
- `frame_carrier`：帧承载/标识增强
- `obj_detector`：目标检测类能力

### 编码

- `amf_encoder`：AMD AMF 硬编
- `nvenc_encoder`：NVIDIA NVENC 硬编

### 网络

- `net_udp`
- `net_relay`
- `net_rtc`
- `net_rtc_local`
- `ssl_proxy`

### 控制与辅助

- `clipboard`
- `file_transfer`
- `joystick`
- `media_recorder`

这些插件共同补上了 Premium 的关键特性。

## 9. `hook_capture` 的定位

`src/hook_capture` 不是普通业务模块，而是底层采集支撑层。它包含：

- `desktop_capture.cpp`
- WASAPI 音频采集
- DDA 采集
- 光标采集
- `hk_obs` 相关注入和图形 Hook 组件
- `tc_graphics`、`tc_graphics_util`、`tc_graphics_offsets`

顶层构建会把这些产物复制到最终运行目录，说明渲染端采集并不完全依赖单一插件，而是依赖这一整套底层图形抓取链。

## 10. `panel_companion` 与 `anti_hooking`

### `panel_companion`

从 CMake 看它包含：

- `spvr/auth_manager`
- `spvr/spvr_setting`
- `crypto/auth_aes`
- `stat/stat_manager`

这说明它更像“私有业务支持库”，主要为面板补充认证、平台配置和统计能力。

### `anti_hooking`

当前对外只暴露 `TCProtectionDummyImport()`，但编译开关 `TC_PROTECTION_ENABLED` 会把它链接进客户端/主产品，说明它的作用是把保护逻辑装入最终产物，而不是在业务层直接调用大量 API。

## 11. 渲染端在整套系统里的位置

如果说面板是控制中心，那么渲染端就是执行引擎。它的核心职责可以压缩成一句话：

接收配置与控制，选择采集和编码方案，加载网络与功能插件，把远端可消费的媒体流和控制通道稳定输出出来。

