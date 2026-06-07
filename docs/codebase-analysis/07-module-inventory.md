# 模块清单与职责索引

## 1. 顶层目录索引

| 目录 | 角色 | 说明 |
| --- | --- | --- |
| `src/GammaRay` | 基础主工程 | 主程序、渲染端、客户端、服务、接口库 |
| `src/render_plugins` | Premium 渲染插件 | 编码、采集、网络、文件、剪贴板等增强能力 |
| `src/client_plugins` | Premium 客户端插件 | 录屏、剪贴板、文件传输 |
| `src/GammaRayWebClient` | 浏览器客户端 | React + TS + wasm 解码渲染 |
| `src/skins` | 皮肤系统 | 当前主要是 `skin_official` |
| `src/panel_companion` | 面板伴生库 | SPVR、认证、统计、私有逻辑 |
| `src/hook_capture` | 底层采集能力 | DDA、音频采集、Hook/注入链 |
| `src/anti_hooking` | 保护库 | 保护能力装配点 |
| `src/backup` | 历史代码 | 不在当前主链路 |

## 2. `src/GammaRay/src` 主模块索引

| 目录 | 职责 |
| --- | --- |
| `render_panel` | 面板 UI、设备管理、配置、安全、流管理 |
| `render` | 渲染/采集/编码/网络输出主进程 |
| `client` | 原生客户端 |
| `service` | 服务相关逻辑，当前活跃实现是 Rust service 二进制，目录内只保留 C++ wrapper 与 `legacy` 历史源码 |
| `render_panel/guard` | 守护进程 `GammaRayGuard` |
| `hw_info` | 本机/远端硬件信息展示 |
| `theme` | Qt 样式主题 |
| `skin` | 皮肤接口与加载 |
| `crash_reporter` | 崩溃上报程序 |
| `uninstall` | 卸载相关程序 |
| `tests` | 测试代码 |
| `ui/gd_gui` | 自定义 Qt UI 组件库 |

## 3. 面板端子模块索引

### `render_panel/network`

- `ws_panel_server`
- `gr_service_client`
- `gr_spvr_client`
- `render_api`
- `http_handler`
- `udp_broadcaster`

用途：面板和渲染端、本地服务、平台服务之间的通信层。

### `render_panel/database`

- `gr_database`
- `db_game_operator`
- `visit_record_operator`
- `file_transfer_record_operator`
- `stream_db_operator`

用途：面板的本地业务数据持久化层。

### `render_panel/devices`

- `app_stream_list`
- `stream_content`
- `stream_item_widget`
- `running_stream_manager`
- `stream_state_checker`
- `gr_device_manager`

用途：设备、流、连接、远端入口管理。

### `render_panel/ui`

主要分两类：

- `tab_*`：一级标签页
- `st_*`：设置页子模块

## 4. 渲染端插件索引

| 插件目录 | 主要职责 |
| --- | --- |
| `amf_encoder` | AMD AMF 视频编码 |
| `nvenc_encoder` | NVIDIA NVENC 视频编码 |
| `gdi_capture` | GDI 桌面采集 |
| `frame_resizer` | 帧缩放/尺寸处理 |
| `frame_carrier` | 帧承载、标签或叠加类处理 |
| `net_udp` | UDP 传输 |
| `net_relay` | 中继传输 |
| `net_rtc` | RTC 传输 |
| `net_rtc_local` | 本地 RTC 能力 |
| `ssl_proxy` | SSL/本地代理桥接 |
| `clipboard` | 服务端剪贴板 |
| `file_transfer` | 服务端文件传输 |
| `media_recorder` | 服务端录制能力 |
| `joystick` | 虚拟手柄/控制器支持 |
| `obj_detector` | 目标检测类能力 |

## 5. 客户端插件索引

| 插件目录 | 主要职责 |
| --- | --- |
| `media_record` | 客户端录屏 |
| `clipboard` | 客户端剪贴板 |
| `file_transfer_client` | 客户端文件传输系统 |

其中 `file_transfer_client` 还可再拆成：

- `src/core`：协议与 SDK 接口
- `src/widget`：文件管理与传输 UI
- `src/common`：公共定义

## 6. 皮肤系统索引

当前顶层皮肤实现主要是：

- `skins/official/skin_official.*`
- `skins/skin_config.toml`

`skin_official` 通过 `SkinInterface` 提供：

- 应用名
- 版本名/版本模式
- 主色/辅助色/文字色
- 图标与 Logo
- 游戏功能是否启用
- CoPhone 功能是否启用

所以皮肤系统不只改颜色，也直接影响功能显隐。

## 7. 哪些模块最值得优先读

如果后续要继续维护或重构，这些文件最值得优先建立深入理解：

### 顶层装配

- `CMakeLists.txt`
- `src/CMakeLists.txt`
- `src/GammaRay/CMakeLists.txt`

### 面板主线

- `src/GammaRay/main.cpp`
- `src/GammaRay/src/render_panel/gr_workspace.*`
- `src/GammaRay/src/render_panel/gr_application.*`
- `src/GammaRay/src/render_panel/gr_context.*`

### 渲染主线

- `src/GammaRay/src/render/rd_main.cpp`
- `src/GammaRay/src/render/rd_app.*`
- `src/GammaRay/src/render/plugins/plugin_manager.*`

### 客户端主线

- `src/GammaRay/src/client/ct_main_ws.cpp`
- `src/GammaRay/src/client/ct_base_workspace.*`
- `src/GammaRay/src/client/plugins/ct_plugin_manager.*`

### 浏览器主线

- `src/GammaRayWebClient/src/gr_app.ts`
- `src/GammaRayWebClient/src/client/gr_sdk.ts`
- `src/GammaRayWebClient/src/renderer/gr_renderer_manager.ts`

## 8. 当前代码库的结构判断

从维护角度看，这套代码库最重要的三个结构特征是：

1. 主产品是多进程，不是单进程。
2. 主能力高度插件化，尤其是渲染端。
3. Premium 仓库本质上是在基础工程之上做二次装配和增强。

把这三点抓住，后续再看任何目录，都会容易很多。
