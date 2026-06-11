# 构建系统与仓库布局

## 1. 仓库的分层方式

仓库不是单一 CMake 工程，而是“两层工程叠加”的结构：

### 第一层：`src/GammaRay`

`src/GammaRay` 本身已经是一套完整的应用工程，负责：

- 面板主程序 `GammaRay`
- 渲染进程 `GammaRayRender`
- 远端客户端 `GammaRayClientInner`
- 服务相关可执行文件
  - 当前由 Rust `gr_service` / `gr_service_mgr` 产出同名 `GammaRayService.exe` / `GammaRayServiceManager.exe`
  - `src/GammaRay/src/service` 只保留 C++ wrapper 和历史 `legacy` 参考实现
- 基础插件接口库
- 开源版默认皮肤和资源

### 第二层：仓库顶层 `GammaRayPremium`

顶层 `CMakeLists.txt` 在 `src/GammaRay` 之上再叠加一层 Premium 组装逻辑，主要做三件事：

1. 选择构建变体
   - `TARGET_TYPE=Official`
   - `TARGET_TYPE=OpenSource`
2. 把 Premium 独有模块编进来
   - `src/render_plugins`
   - `src/client_plugins`
   - `src/skins`
   - `src/panel_companion`
   - `src/anti_hooking`
   - `src/hook_capture`
3. 在构建后把插件、皮肤、协议文件、打包脚本复制到运行目录

因此可以把顶层工程理解为“发行版装配器”。

## 2. 当前实际的构建入口

顶层有两个批处理入口：

- `build_official.bat`
- `build_opensource.bat`

两者本质上都是：

```bat
cmake -S . -B build_xxx -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_TYPE=...
cmake --build build_xxx -j18
```

说明当前默认交付形态是：

- 生成器：`Ninja`
- 编译类型：`RelWithDebInfo`
- 平台：Windows/MSVC

## 3. 环境与依赖约束

`env_premium.cmake` 直接写死了几个关键外部路径：

- `VCPKG_ROOT`
- `QT_ROOT`
- `VK_SDK_ROOT`

这说明项目不是“拿来即编”的纯自包含仓库，而是依赖本机约定环境。

从顶层和 `src/GammaRay/CMakeLists.txt` 可以确认关键依赖包括：

- Qt 6.5+
- vcpkg 管理的 SDL2、gflags、GTest、glm、libvpx、FFTW3、cpr、mimalloc、OpenSSL 等
- Vulkan SDK
- FFmpeg 7.1.1
- WebRTC 头文件/预构建资源
- OpenCV 4.10
- Breakpad
- 大量仓库内置私有库和三方源码

## 4. 顶层目标图

顶层 `src/CMakeLists.txt` 实际启用的子目录是：

- `GammaRay`
- `client_plugins`
- `render_plugins`
- `hook_capture`
- `panel_companion`
- `skins`
- `anti_hooking`

`GammaRayServer` 被明确注释掉，说明它目前不在主构建路径内。

## 5. 顶层总目标 `tc_build_premium_all`

顶层自定义目标 `tc_build_premium_all` 依赖两大类内容：

### 基础工程产物

- `tc_build_all`
- `GammaRay` 体系内部各可执行文件和基础库

### Premium 扩展产物

- render 插件：
  `plugin_amf_encoder`、`plugin_file_transfer`、`plugin_frame_carrier`、`plugin_frame_resizer`、`plugin_gdi_capture`、`plugin_media_recorder`、`plugin_net_relay`、`plugin_net_rtc`、`plugin_net_rtc_local`、`plugin_net_udp`、`plugin_nvenc_encoder`、`plugin_obj_detector`、`plugin_clipboard`、`plugin_ssl_proxy`、`plugin_joystick`
- client 插件：
  `plugin_media_record_client`、`plugin_client_clipboard`、`plugin_file_transfer_client`
- 皮肤：
  `skin_interface`、`skin_official`
- 保护模块：
  `tc_protection`

这清楚说明 Premium 工程的核心思路是：基础产品保持在 `src/GammaRay`，增强能力尽量插件化放在顶层。

## 6. 构建后复制行为意味着什么

顶层 `POST_BUILD` 做了大量复制操作，这些操作本身就是运行时布局规范：

### 渲染插件目录

运行目录下的 `gr_plugins/` 存放：

- `.dll`
- 对应 `.dll.toml` 配置文件

### 客户端插件目录

运行目录下的 `gr_plugins_client/` 存放：

- `.dll`
- 对应 `.dll.toml`

### 皮肤目录

运行目录下的 `gr_skins/` 存放：

- `skin_config.toml`
- `skin_official.dll`

### Web 客户端协议同步

顶层会把 `tc_message_new` 下的几个 `.proto` 文件同步到：

- `build/.../src/GammaRay/web/proto`
- `src/gr_web_client/proto`

这表示浏览器端协议不是独立维护，而是从主协议仓内复制出来的镜像。

## 7. 目录职责速览

### 顶层业务目录

- `src/GammaRay`：基础主工程
- `src/render_plugins`：Premium 渲染端插件
- `src/client_plugins`：Premium 客户端插件
- `src/gr_web_client`：浏览器端客户端
- `src/skins`：皮肤实现
- `src/panel_companion`：面板伴生能力库
- `src/hook_capture`：采集与 Hook 能力
- `src/anti_hooking`：保护库

### 非主业务目录

- `build_official`、`build_opensource`、`cmake-build-*`、`out`：构建产物
- `src/backup`：历史实现/备份
- `src/GammaRay/deps`：供应商依赖

## 8. 对代码阅读最重要的判断

阅读这套仓库时，最容易误判的点有两个：

1. 顶层不是“全部源码”，它更像发行版壳层。
2. `src/GammaRay/deps` 文件非常多，但大部分不是你需要日常理解的业务代码。

真正的阅读主线应该是：

`顶层 CMake -> src/GammaRay 主程序 -> render/client 插件接口 -> Premium 插件实现 -> WebClient`

