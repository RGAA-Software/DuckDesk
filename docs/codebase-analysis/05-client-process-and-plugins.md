# 原生客户端 `GammaRayClientInner` 与客户端插件

## 1. 客户端入口的定位

`src/GammaRay/src/client/ct_main_ws.cpp` 是原生客户端主入口。虽然文件名带 `ws`，但实际上它不只处理 WebSocket，还承载整个原生客户端启动链。

启动逻辑大致是：

1. 初始化 Breakpad
2. 解析大量命令行参数
3. 设置 OpenGL/软件渲染后端
4. 初始化字体、语言和数据目录
5. 校验直连或 relay 连接参数
6. 初始化 `ClientContext`
7. 生成媒体路径和文件传输路径
8. 组装 `ThunderSdkParams`
9. 检测 Vulkan 能力
10. 创建并显示 `Workspace`
11. 安装低级键盘钩子，把 Win/Alt+Tab 等特殊键转发到远端

所以它不是“播放器”，而是带控制回传和多窗口 UI 的完整远控客户端。

## 2. 启动参数非常丰富

客户端支持的参数大致分为：

### 连接参数

- `host`
- `port`
- `spvr_host`
- `spvr_port`
- `appkey`
- `relay_host`
- `relay_port`
- `relay_appkey`

### 身份和设备

- `device_id`
- `device_rp`
- `device_sp`
- `remote_device_id`
- `remote_device_rp`
- `remote_device_sp`
- `stream_id`
- `stream_name`

### 能力开关

- `audio`
- `clipboard`
- `enable_p2p`
- `only_viewing`
- `show_watermark`
- `force_gdi_capture`

### 界面/渲染

- `split_windows`
- `auto_layout_screens`
- `max_num_of_screen`
- `display_name`
- `display_remote_name`
- `titlebar_color`
- `decoder`
- `gl_backend`
- `disable_vulkan_render`
- `force_software`

这说明客户端不仅连接远端，还要适配复杂显示场景和多种传输方式。

## 3. `BaseWorkspace` 是客户端核心窗口骨架

`BaseWorkspace` 的职责非常重，既管显示，也管输入，也管插件：

- 初始化主题
- 初始化主视图和面板
- 接入 `ThunderSdk`
- 收发媒体消息和文件消息
- 处理拖拽文件
- 管理多显示器和缩放模式
- 更新远端光标
- 发送切换显示器、切换模式、修改 FPS 等控制消息
- 维护悬浮控制条、通知、统计面板
- 连接面板端 `CtPanelClient`
- 加载客户端插件

可以把它看作原生客户端的“会话窗口内核”。

## 4. 原生客户端渲染后端是可切换的

`front_render` 目录说明客户端并非单一渲染方案，而是预留了多种后端：

### OpenGL

- `ct_opengl_video_widget`
- `ct_renderer`
- `ct_shader_program`
- `ct_director`

### D3D11

- `ct_d3d11_video_widget`
- `d3d11_render_manager`

### SDL

- `ct_sdl_video_widget`

### Vulkan / libplacebo

- `ct_vulkan_video_widget`
- `ct_vulkan_checker`
- `pl_vulkan`

启动时会根据参数和能力检测决定实际路径。代码里还专门检查 Vulkan 是否支持 HEVC/YUV444 解码渲染。

## 5. `ThunderSdkParams` 是客户端会话合同

客户端会把解析后的参数收束到 `ThunderSdkParams`，其中包含：

- 连接地址
- 媒体路径 `/media?...`
- 文件传输路径 `/file/transfer?...`
- 客户端/远端完整 device id
- stream id / stream name
- P2P 开关
- UI 展示名
- 语言
- 标题栏颜色
- relay 信息
- decoder 选择
- force_gdi

这说明原生客户端与底层 SDK 的边界是比较清楚的：UI 层负责组装会话参数，SDK 负责会话执行。

## 6. 客户端插件系统

`ClientPluginManager` 的机制和渲染端类似：

1. 扫描 `gr_plugins_client/*.dll`
2. 通过 `GetInstance` 获取插件实例
3. 构造 `ClientPluginParam`
4. 读取插件 `.dll.toml`
5. 调用 `OnCreate`
6. 注册事件回调到 `ClientPluginEventRouter`

### 注入给客户端插件的参数

- `base_path`
- `base_data_path`
- `screen_recording_path`
- `clipboard_enabled`
- `device_id`
- `stream_id`
- `language`
- `stream_name`
- `display_name`
- `display_remote_name`

说明客户端插件主要围绕当前会话和本地持久化目录工作。

## 7. 当前 Premium 客户端插件

### `media_record`

录屏相关能力。应该与会话窗口和本地文件系统密切相关。

### `clipboard`

客户端侧剪贴板接入。目录里带有较多 Windows 剪贴板对象和虚拟文件实现，说明不仅支持文本，还考虑了文件剪贴板语义。

### `file_transfer_client`

这是客户端插件里最重的一个模块。它内部继续拆成：

- `src/core`
- `src/widget`
- `src/common`

从文件名可以看出它不仅有协议/SDK 层，还自带完整文件管理 UI：

- 文件列表
- 文件详情
- 本地/远端路径工具
- 传输任务管理
- 传输记录和日志
- 发送按钮、操作按钮、弹窗组件

这说明文件传输不是客户端里的一个小按钮，而是一整套独立子系统。

## 8. 客户端和面板/渲染端的关系

客户端同时与两个方向交互：

### 面向渲染端

- 收视频/音频流
- 发输入事件
- 发文件传输消息
- 发控制命令

### 面向面板端

通过 `CtPanelClient`，客户端还能和本机面板配合，例如：

- 状态同步
- 控制面板信息回传
- 本地侧辅助能力接入

所以客户端不是完全独立于面板运行的。

## 9. 原生客户端的产品定位

和 Web 客户端相比，原生客户端明显是“完整形态”：

- 多渲染后端
- 更完整的输入回放
- 更重的 UI
- 客户端插件
- 本地录屏
- 复杂多屏能力
- 更完整的文件与剪贴板链路

浏览器端更像轻量接入面，而原生客户端才是功能满配接入面。

