# Windows 产品构建与安装包拆分改造方案

日期：2026-09-15  
状态：实现完成；2026-09-16 起采用产品完整构建沙箱，不再兼容根 `build_official`、公共 `dist` 或共享编译产物
适用范围：Windows 原生 Panel、Client、Render、Service、RDP、WebView、Game Hook、虚拟显示与安装器，以及 Pixels Android 产品版本构建

## 1. 目标

当前 Windows 交付物由一个全量构建图、一个 `dist` 收集器和一个 NSIS 安装器组成。所有功能被打入同一个应用包，导致：

- 纯客户端携带被控端、服务、驱动、Hook、CEF 和 Web 资源；
- 独立远控产品无法从二进制层面排除云应用、云游戏和 WebView；
- `collect_dist.py` 扫描构建目录，可能把日志、旧资源或服务端产物带入安装包；
- 当前安装器始终安装 Service、虚拟显示、手柄等 Host 组件，无法生成只包含访问方能力的 Client；
- 当前 RDP Client 与 RDP Host 运行时没有形成完整、统一命名的可发布组件。

本次改造保留一个代码仓库和共用核心模块，建立三个独立 Windows 产品构建、三个严格白名单分发目录和三个安装器。Android 不参与 Windows 安装包拆分和互斥安装，但作为第四个独立发布目标纳入同一版本管理规则。禁止维护多份源码分支，也禁止先生成全量包再靠删除文件制作精简版。

## 2. 已确认的产品决定

1. `Pixels Cloud Node` 是全功能云端渲染节点，包含桌面远控、文件传输、RDP、云应用、云游戏、Game Hook、WebView、浏览器远控等完整能力。
2. `Pixels Client` 是控制端产品，不具备被控制和托管应用的能力；包含 `px_panel.exe`、`px_client.exe` 和 `px_osinfo.exe`。
3. `Pixels Remote` 是独立远控产品，包含桌面远控、文件传输、RDP、系统信息、虚拟显示和手柄能力；不包含云应用、云游戏、Game Hook 和 WebView。
4. `Pixels Remote` 必须包含 `px_joystick.exe`，Render 远控版也必须保留手柄输入链路。
5. 项目发布的 RDP 主产物统一使用 `px_rdp_` 文件名前缀。不得只对既有上游 RDP SDK 文件做磁盘重命名，必须从固定 SDK 构建阶段生成匹配的导入库和 PE 依赖名。
6. WebView 与浏览器远控不是同一个能力。WebView/CEF 只进入 Cloud Node；RTC 和 `web_client` 是否进入 Remote 由浏览器远控能力决定。本方案默认 Remote 保留浏览器远控。
7. Cloud Node、Client 和 Remote 三个产品严格互斥。同一台机器只能安装其中一个；检测到其他产品、旧开发版服务或其他产品安装标记时，安装器必须提示用户先卸载并终止安装，不得自动卸载、覆盖或共用运行时。
8. Cloud Node 与 Remote 使用同一个完整能力的 `px_service.exe`。Service 不做产品级 Cargo feature 裁剪；Remote 包含完整 Service 代码但正常产品流程不调度云应用、云游戏、Game Hook 或 WebView。
9. Client 当前保持管理员权限和现有系统级运行方式。本阶段不改造为按用户、无提权安装；Client 仍不得安装或启动 Service、Render、虚拟显示和手柄驱动。
10. 三个产品均处于开发阶段，不提供旧单体产品、旧配置、旧节点身份、旧文件名或旧安装目录的兼容迁移。发现旧产品时要求用户先卸载，不建立导入、转发、双份文件或运行时 fallback。
11. Cloud Node、Client、Remote 和 Android 的产品发布版本完全独立演进。编译或发布一个目标不得自动修改另外三个目标的版本，也不得要求四个产品保持相同版本号。
12. Cloud Node、Client、Remote 和 Android 的公司品牌统一为 `Pixels`。Windows PE 版本资源、安装器 Publisher/Company、卸载信息、产品清单、界面中公开的作者或供应商字段，以及 Android 发布元数据均不得使用 RGAA 名称或 RGAA 组织链接。源码历史作者注释不作为产品品牌元数据；当前仍由权威配置使用的网络服务地址也不通过品牌替换擅自改写，待对应服务正式迁移后单独删除。
13. 四个产品的编译、暂存、运行和报告产物必须完全位于各自的 `build_official/<product>/` 沙箱中。允许共享下载、源码和工具链缓存，但安装包收集器不得直接读取共享编译产物。旧根构建树和公共 `build_official/dist` 不提供兼容入口。

### 2.1 品牌元数据规则

- Windows 自研 EXE、DLL 和安装器的 `CompanyName`、`Publisher`、`Manufacturer`、公开 `Author` 与版权归属统一为 `Pixels`；
- 产品界面不展示或跳转到 RGAA 组织主页；对外网站使用 Pixels 当前正式入口；
- Android 应用标识、应用名、签名配置和发布清单使用 Pixels 身份，不从 Windows Edition 推导公司字段；
- 第三方二进制保留其原始厂商和签名信息，不得伪装成 Pixels；
- 构建验收扫描产品自研元数据与最终分发物，发现 RGAA 公司名、发布者、作者或组织链接即失败。
- 四份 `packaging/products/*.toml` 中的 `company = "Pixels"` 是产品公司名的唯一构建输入；版本工具必须校验该值，并将其传给 CMake/PE、NSIS 和 Android Gradle。各下游不得自行定义另一家公司名。

## 3. 产品能力矩阵

| 能力或组件 | Cloud Node | Client | Remote |
|---|---:|---:|---:|
| `px_panel.exe` | 是，全功能节点界面 | 是，控制端界面 | 是，远控界面 |
| `px_client.exe` | 是 | 是 | 是 |
| `px_osinfo.exe` | 是 | 是 | 是 |
| `px_render.exe` | 是，全功能版本 | 否 | 是，远控精简版本 |
| `px_service.exe` | 是，完整通用版本 | 否 | 是，与 Cloud Node 相同的完整通用版本 |
| `px_service_manager.exe` | 是 | 否 | 是 |
| `px_function.exe` | 是 | 否 | 是 |
| `px_display.exe` 与 Parsec VDD | 是 | 否 | 是 |
| `px_joystick.exe` 与手柄输入 | 是 | 否 | 是 |
| `px_gh.dll`、`px_gh_injector.exe`、`px_gh_address.exe` | 是 | 否 | 否 |
| CEF、WebView 和 CEF locales | 是 | 否 | 否 |
| 云应用与云游戏调度 | 是 | 只作为访问方 | 否 |
| 原生桌面远控与文件传输 | 是 | 是，访问方 | 是 |
| RDP Client | 是 | 是 | 是 |
| RDP Host Proxy 与策略模块 | 是 | 否 | 是 |
| `px_render_rtc.dll`（Direct Host；`px_render_rtc_remote.dll` 已退役归档） | 是 | 否 | 是，默认保留浏览器远控 |
| `web_client/` | 是 | 否 | 是，默认保留浏览器远控 |
| Render `settings.toml` | 是 | 否 | 是，精简配置 |
| `px_service.toml` | 是 | 否 | 是，精简配置 |

`px_console.exe`、Console 前端、服务器程序、`px_logs/`、`debug.log`、旧 Qt `output/` 资源均不属于以上三个桌面产品，必须由各自服务端发布流程单独交付。

## 4. 产品能力模型

新增唯一的产品 Edition 参数：

```text
PX_PRODUCT_EDITION=CLOUD_NODE|CLIENT|REMOTE
```

### 4.1 独立产品版本与每次构建递增

Windows Edition、Android 平台目标与版本号是彼此独立的概念。四个产品清单分别保存自己的发布版本和版本代码：

```toml
# packaging/products/cloud_node.toml、client.toml、remote.toml 或 android.toml
product = "cloud_node"
edition = "CLOUD_NODE" # 仅 Windows 产品需要
product_version = "3.3.67"
product_version_code = 30367
```

四个清单中的 `product_version`、`product_version_code` 互不联动；版本代码只要求在同一产品内单调演进。自动递增沿用当前三段式规则：`patch` 每次加一，`X.Y.99` 的下一版为 `X.(Y+1).0`，`X.99.99` 的下一版为 `(X+1).0.0`；`minor` 和 `patch` 的取值范围均为 `0–99`。`product_version_code` 使用 `major*10000 + minor*100 + patch`，因此同一产品的每次自动构建也会得到严格递增且唯一的整数版本代码。版本工具必须显式接收产品目标，例如：

```text
python set_product_version.py --product cloud_node --bump
python set_product_version.py --product client 4.0.0
python set_product_version.py --product remote --show
python set_product_version.py --product android --bump
```

每次调用完整产品构建入口都必须在开始构建前将当前目标的 `product_version` 和 `product_version_code` 原子提升一次，一次构建调用只能提升一次。构建失败也保留已经消耗的版本，不回滚、不复用同一版本生成另一组二进制。现有 `set_app_version.py --bump` 同时修改 CMake、Cargo 和唯一 NSIS 版本的全局行为必须退出产品发布流程；不得用一次整编同时提升多个产品版本。

“每次构建递增”以产品构建入口为边界，不以编译器进程、Gradle 子任务或 CMake target 数量计数：

- `build_cloud_node.bat`、`build_client_product.bat`、`build_remote_product.bat` 每次调用分别只提升自己的版本一次；
- Android 的 `build_android_product.bat official|customer debug|release` 每次调用只提升 Android 版本一次，然后把新值同时传给该次 Gradle 的 `versionName` 和 `versionCode`；
- 同一次 Android 调用中的 lint、单元测试、`assemble`、`bundle` 和 `install` 子任务共享同一个已提升版本，不得各自再次提升；
- Android Studio 同步、Gradle 配置、纯测试、lint、`core-native` 构建以及 `scripts_build/build_cpp_*.bat` 定向 C++ 构建不产生完整产品包，因此不提升产品版本；
- 直接执行会生成 APK/AAB 的 Gradle 任务时必须由产品构建入口提供版本环境，禁止继续静默使用 `versionCode=1`、`versionName=1.0.0` 默认值；
- `--show`、清理和只读校验不提升版本。

共享组件版本与产品发布版本分离：

- `px_service.exe`、`px_service_manager.exe`、`px_function.exe`、`px_osinfo.exe` 及共享库记录自身组件版本、Git revision 和构建 ID；
- Edition 专属的 Panel、Render 和安装器可以在 PE `ProductVersion`、UI 和卸载信息中使用当前产品版本；
- 同一个共享二进制不得为 Cloud Node 和 Remote 重新盖不同产品版本，否则会破坏共享产物的哈希与来源一致性；
- `product-manifest.json` 同时记录 `edition`、`product_version`、`product_version_code`、共享组件版本、Git revision、构建 ID 和各文件 SHA-256；
- Service 从安装包写入的不可变产品描述读取并上报 Edition 与产品版本，不能把自身 PE 文件版本误当成当前产品版本；
- Console 按 `(edition, product_version)` 展示和判断节点版本，不跨 Edition 比较版本大小；
- Android APK/AAB 的 `versionName` 和 `versionCode` 只读取 Android 产品清单，Android 构建不得使用任一 Windows Edition 的版本；
- 共享组件发生变化时，不要求四个产品同时发布。某个产品在吸收该组件并构建时，只提升自己的产品版本。

现有 `TC_APP_VERSION` 和 Cargo workspace version 在迁移期间只作为共享组件/源码构建版本，不再充当 Windows 或 Android 产品的统一发布版本。Console、Auth、Desk Server 及 Android 之外的其他平台产品有各自发布流程，不随任一 Windows Edition 或 Android 的产品版本自动变化。

从该参数生成不可变、强类型的产品能力描述，至少包含：

```text
desktop_client
desktop_host
file_transfer
rdp_client
rdp_host
cloud_app_catalog
cloud_app_host
game_hook
webview_host
browser_remote
virtual_display
joystick
system_information
```

产品清单是 Edition 与能力组合的唯一数据源。CMake、Panel、Console 和 Packaging 使用由该清单生成或校验的类型化结果，禁止分别手写彼此可能漂移的能力表。

能力在以下各层生效：

- CMake：决定编译哪些源文件、链接哪些库、生成哪些目标；
- Panel：决定页面和操作入口是否存在；
- Render：在编译图和启动入口拒绝当前产品不具备的模式；
- Service：始终构建完整通用版本，读取安装包写入的 Edition 描述并向 Console 上报，但不参与产品级源码裁剪；
- Console：根据节点上报的 Edition 和对外启用能力决定是否允许调度相应工作负载；
- Packaging：只收集当前产品声明的运行时文件。

不得把“隐藏一个菜单”当作 Render 和安装包的产品隔离，也不得为 Remote 编译或打包 CEF、Game Hook、WebView 等禁止能力。Service 是明确例外：Cloud Node 与 Remote 共用完整 Service 二进制，产品差异不通过 Service Cargo feature 表达。

## 5. CMake 构建图拆分

使用三个完整隔离的 Windows 产品构建沙箱，避免 CMake Cache、条件源文件、编译宏、静态库、Rust、Web 和 RDP Policy 产物互相污染：

```text
build_official/
├── cloud_node/{cmake,cargo,web,rdp_policy,dist,installer,reports}/
├── client/{cmake,cargo,dist,installer,reports}/
├── remote/{cmake,cargo,web,rdp_policy,dist,installer,reports}/
└── android/{gradle,native,dist,reports}/
```

提供三个产品聚合目标：

```text
px_build_cloud_node_all
px_build_client_all
px_build_remote_all
```

提供对应入口脚本：

```text
scripts_build/build_cloud_node.bat
scripts_build/build_client_product.bat
scripts_build/build_remote_product.bat
```

不提升版本的定向验证使用 `scripts_build/build_cpp_product_panel.bat <product>`、`scripts_build/build_cpp_product_client.bat <product>`、
`scripts_build/build_cpp_product_panel_tests.bat <product>` 和 `scripts_build/build_cpp_product_render.bat <cloud_node|remote>`；它们分别进入独立产品构建树，不能复用其他 Edition 的 CMake Cache。

发布级完整构建分别使用 `build_cloud_node.bat`、`build_client_product.bat` 和 `build_remote_product.bat`；每个入口先且只提升对应产品的独立版本一次，再将 CMake 配置到 `build_official/<product>/cmake/` 并只构建该产品聚合目标。旧的根构建入口不是兼容入口。日常开发仍使用 `scripts_build/build_cpp_*.bat` 定向构建，这些 focused 入口不生成完整产品包、不提升产品版本，也不得借拆包改造绕过现有增量构建规则。

`px_service.exe`、`px_service_manager.exe`、`px_function.exe` 和 `px_osinfo.exe` 仍使用同一套完整源码与 feature 组合，但分别通过产品专属 `CARGO_TARGET_DIR` 构建、暂存和收集。Cloud Node 与 Remote 不维护删减版 Service；相同输入应产生相同哈希，但任何产品不得从另一产品目录取文件。Windows Rust 目标统一启用 MSVC `/Brepro`，避免 CodeView PDB GUID 等非运行时随机元数据令两个独立产品构建产生不同摘要；移除或绕过该参数必须有新的可复现性证据。`web_client` 也分别输出到 Cloud Node 和 Remote 的 `web/`，Console 前端不进入任何桌面产品。共享目录只允许保存下载包、依赖源码和工具链缓存。

旧 `build_official.bat` 和 `build_client.bat` 语义已退役，不得恢复为根构建树或另一套 Client 产品入口。

顶层 CMake 中 Cargo、MSBuild、CEF、Hook SDK 等依赖发现也必须按产品能力延迟执行。构建 Client 时不应要求本机准备 CEF、Parsec VDD 构建环境或 Host Service Rust 目标。

### 5.1 Android 产品构建入口

Android 使用独立入口 `scripts_build/build_android_product.bat official|customer debug|release`。该入口必须：

1. 调用版本工具，只提升 `packaging/products/android.toml` 一次；
2. 读取提升后的 `product_version`、`product_version_code` 和当前 Git revision；
3. 为同一次 Gradle 调用设置 `PIXELS_VERSION_NAME`、`PIXELS_VERSION_CODE`、`PIXELS_GIT_REVISION`；
4. Debug 生成可覆盖安装的 arm64 APK；Release 复用现有签名、LGPL source/relink、APK/AAB 和原子发布门禁；
5. 从 Gradle `output-metadata.json` 反查 APK/AAB 版本，必须与 Android 产品清单完全一致；
6. 生成包含 Android 产品版本、Git revision、APK/AAB SHA-256、签名证书指纹和 native ELF Build ID 的发布清单。

现有 `src/px_android/scripts/build_release.ps1` 必须接入上述版本入口或成为其内部实现，不能继续依赖调用者手工设置版本。`src/px_android/app/build.gradle` 在生成 APK/AAB 的任务中必须拒绝缺少显式版本输入；Gradle 的纯测试、lint、IDE sync 和 library/native focused 构建可以继续不触发产品版本变化。

## 6. Render 拆分

把当前 Render 组织成共用核心和按能力组合的模块，不维护两个独立实现：

```text
render_core
├── 会话与生命周期
├── 原生 WS/UDP/Relay
├── 编码、音频与文件传输
├── DDA/GDI 桌面采集
└── RDP 透明承载公共部分

render_cloud_features
├── Game Hook
├── WebView/CEF
├── 云应用实例支持
└── 云游戏专用输入与启动流程

render_remote_features
├── Desktop Host
├── RDP Host
├── Virtual Display
└── Joystick
```

Cloud Node 和 Remote 最终都输出名为 `px_render.exe`，但来自不同构建目录：

- Cloud Node Render 链接全部模块；
- Remote Render 不编译 WebView 源文件，不链接 `libcef.dll`，不编译 Hook 注入路径，不接受 `game-hook` 或 `webview`；
- Remote 默认保留 RTC，因为浏览器远控仍是远控传输能力，不属于 WebView；
- Client 不生成 Render。

Remote Render 的验收必须检查 PE 导入表，确保不存在 CEF、EasyHook、Hook 辅助组件等禁止依赖。

## 7. Service 与 Console 行为

只构建一个完整通用的 `px_service.exe`，Cloud Node 与 Remote 使用相同代码和能力集合。Service 保留桌面、RDP、虚拟显示、云应用、云游戏、Game Hook 和 WebView 的现有实现，不增加产品级 Cargo features，也不要求 Remote Service 从二进制中删除这些代码。

安装包为 Service 提供不可变的 Edition、产品版本和能力描述。Service 在节点握手中上报这些安装产品信息；这里的“启用能力”描述当前安装产品允许 Console 使用的功能，不代表 Service 二进制是否包含相应代码，产品版本也不取自共享 Service 自身的 PE 文件版本：

- Cloud Node 上报完整节点能力；
- Remote 只上报桌面远控、文件传输、RDP、浏览器远控、虚拟显示、手柄和系统信息；
- Client 不安装 Service，也不作为 Host 节点注册。

Console 只能向声明对应能力的节点调度工作负载，正常流程不会向 Remote 下发云应用、云游戏、Game Hook 或 WebView 实例。Remote Render 本身仍必须在编译图和启动入口排除这些模式；Service 不作为产品裁剪和禁止依赖的验收边界。

协议变化使用追加字段，保留已有 protobuf 字段号和语义。由于产品仍在开发阶段，缺少 Edition 或能力字段的旧节点不做推断和兼容回退：Console 将其视为不满足新产品协议并拒绝调度，提示升级或卸载旧产品。

## 8. Panel 产品界面

Panel 使用同一套页面代码，由产品能力决定组合：

### Cloud Node

- 远程控制；
- 设备列表；
- 云应用；
- 运行环境；
- 设置；
- 本机被控开关和完整节点管理。

### Client

- 远程控制；
- 设备列表；
- 云应用访问入口；
- 本机系统信息；
- 客户端设置。

Client 不显示本机被控开关、Render 状态、Service 管理、虚拟显示和节点应用托管入口。Panel 可以继续以管理员权限运行，但不得创建 Host 专属的 Service bridge、节点 presence、Render 管理或驱动管理模块；管理员权限不改变 Client 只作为访问方的产品边界。

### Remote

- 远程控制；
- 设备列表；
- 运行环境；
- 设置；
- 本机被控开关、RDP 和手柄状态。

Remote 不创建云应用页面实例，导航中也不保留占位入口。

## 9. Client 中的 `px_osinfo`

`px_osinfo.exe` 进入三个产品包，但生命周期所有者不同：

- Cloud Node、Remote：继续由 `px_function.exe` 在交互用户会话中启动和监督；
- Client：没有 `px_function.exe`，由 Panel 在本地 `/sys/info` 服务准备完成后启动同目录的 `px_osinfo.exe`；
- Panel 退出时只停止自己本次启动并持有句柄的子进程，不按进程名清扫其他产品实例；
- 重复启动、Panel 重启和 `px_osinfo` 异常退出必须可恢复；
- 三个产品互斥安装，不需要为产品并存引入 Edition 作用域的实例键；保留当前单产品单例约束即可；
- Panel 启动 `px_osinfo` 时显式传递本机 `/sys/info` 端口，不依赖 `px_osinfo` 的隐式默认端口。

Client 的系统信息只用于本机诊断和 UI 展示，不得因为包含 `px_osinfo` 而开放被控端口或注册节点服务。Client 继续使用管理员权限和现有 ProgramData 数据根，不在本阶段迁移到按用户存储。

## 10. Remote 中的手柄组件

Remote 必须包含：

- `px_joystick.exe`；
- Render 手柄输入模块；
- 所需的 ViGEm 安装资源和许可证；
- 设置页中的手柄驱动状态、安装与修复入口。

Cloud Node 与 Remote 互斥安装，不建立两个 Pixels Host 产品之间的驱动共享和引用计数。安装器只删除能够证明由当前产品安装的驱动；安装另一个 Host 产品前，用户必须先完成当前产品及其产品自有驱动的卸载。

Client 不包含或安装手柄驱动；它只发送已经授权的客户端手柄输入事件。

## 11. RDP 产物统一命名

### 11.1 发布名称

项目可识别的 RDP 主产物使用以下名称：

| 用途 | 发布名称 |
|---|---|
| RDP 代理进程 | `px_rdp_proxy.exe` |
| RDP 代理服务库 | `px_rdp_server_proxy.dll` |
| RDP 服务端核心库 | `px_rdp_server.dll` |
| RDP 客户端库 | `px_rdp_client.dll` |
| RDP 协议核心库 | `px_rdp_core.dll` |
| RDP 平台抽象库 | `px_rdp_winpr.dll` |
| Pixels RDP 安全策略模块 | `px_rdp_policy.dll` |
| RDP SDK 清单 | `px_rdp_sdk.json` |
| RDP 部署清单 | `px_rdp_deployment.json` |
| RDP 代理证书 | `px_rdp_proxy.crt` |
| RDP 代理私钥 | `px_rdp_proxy.key` |
| Console 信任锚 | `px_rdp_console_ca.der` |

OpenSSL、zlib、cJSON、OpenH264 等通用第三方运行库不是项目 RDP 主产物，保留上游名称，但放入受控的 RDP runtime 组件并由清单记录哈希。

公开产物的文件名、目录名、PE 导入名、UI、日志、错误、配置键和清单键都只使用 Pixels RDP 命名，不暴露上游 RDP SDK 的品牌名称。依法必须保留的第三方版权和许可证正文集中放入许可证目录，不作为产品功能名称或用户界面文案。

### 11.2 禁止直接改文件名

Windows PE 的导入表记录 DLL 名称。把旧 RDP SDK DLL 复制后改成 `px_rdp_core.dll`，不会修改其他 DLL 和 EXE 中的导入记录，运行时仍会寻找旧名称。

正确实施方式：

1. 在固定上游 RDP SDK 隔离构建副本中设置各目标 `OUTPUT_NAME`；
2. 同一次构建生成新 DLL 与匹配的新 import library；
3. `px_client.exe`、`px_rdp_proxy.exe` 和所有 RDP DLL 使用新 import library 重新链接；
4. 代理配置继续使用 FreeRDP 规定的逻辑模块名 `policy`，项目加载器只将它解析为 `px_rdp_policy.dll`，不尝试旧文件名；
5. 更新固定 SDK 清单、许可证、补丁基线与全部 SHA-256；
6. 用 PE 导入表测试确认不再引用任何旧的上游 RDP SDK 文件名；
7. 更新 CMake staging、Service 部署检查、安装器、脚本、测试与文档中的全部旧文件名。

迁移完成后不发布新旧双份 RDP DLL，也不使用兼容复制或运行时 fallback。

### 11.3 RDP Client 与 Host 文件集

Client、Cloud Node 和 Remote 均包含 RDP Client 运行时：

```text
px_rdp_client.dll
px_rdp_core.dll
px_rdp_winpr.dll
px_rdp_sdk.json
通用第三方依赖与许可证
```

Cloud Node 和 Remote 的静态安装包额外包含 RDP Host 程序与库：

```text
rdp/px_rdp_proxy.exe
rdp/px_rdp_server_proxy.dll
rdp/px_rdp_server.dll
rdp/proxy/px_rdp_policy.dll
通用第三方依赖与许可证
```

安装或节点注册完成后，才在当前节点的受控运行目录生成或安全下发以下节点身份材料：

```text
rdp/px_rdp_deployment.json
rdp/px_rdp_proxy.crt
rdp/px_rdp_proxy.key
rdp/px_rdp_console_ca.der
```

节点身份材料不属于静态产品白名单，安装包不得因为这些文件尚未生成而判定缺件。代理私钥不得作为所有机器共用的静态安装包内容。安装或节点注册过程必须为当前节点生成或安全下发身份材料，再生成带固定证书指纹的 `px_rdp_deployment.json`。

## 12. 严格白名单打包

建立显式制品组清单和产品清单：

```text
packaging/
├── artifact_groups.toml
└── products/
    ├── cloud_node.toml
    ├── client.toml
    ├── remote.toml
    └── android.toml
```

`artifact_groups.toml` 用静态条目声明来源根、来源相对路径、目标相对路径、树包含模式、是否可缺省以及是否属于 Pixels 自研 PE；不进行目录猜测。四个 `packaging/products/*.toml` 分别保存各自的产品版本、版本代码、能力和 Windows 制品组。三个 Windows 清单是安装器文件名、Edition 专属 PE 产品版本、UI 展示、Console 上报和输出清单的唯一产品版本来源；Android 清单是 APK/AAB `versionName`、`versionCode` 和 Android 发布清单的唯一版本来源。制品组不得反向要求四个产品版本相同，共享组件也不得修改其他产品的版本。

输出目录：

```text
build_official/cloud_node/dist/
build_official/client/dist/
build_official/remote/dist/
```

Android 不进入 Windows 产品目录。Gradle、native、APK、AAB、mapping、native symbols、LGPL relink 材料和发布清单全部位于 `build_official/android/`，版本目录使用 Android 自己的 `product_version`。

收集器必须：

- 仅复制清单声明的文件，不扫描并猜测可发布内容；
- 在目标目录的同级临时目录完成组装、签名和校验，通过后再原子替换当前产品目录；失败时保留上一份完整可用的产品目录；
- 声明文件缺失、固定第三方输入哈希错误、源码产物与分发副本哈希不一致或出现未声明文件时失败；
- 生成 `product-manifest.json`、`sha256sums.json` 和许可证清单；
- 检查产品禁止文件和 PE 禁止依赖；
- 对签名后的 EXE、DLL 和安装器计算最终发布哈希；构建生成物的哈希写入输出清单，不反向硬编码成下一次构建的输入哈希；
- Cloud Node 与 Remote 在同一 Git revision 和共享组件构建批次中，从共享 Rust 构建区复制同一个完整 `px_service.exe`，并验证两个当批产品包内的 Service SHA-256 一致；不同时间、不同 revision 发布的两个产品不要求 Service 哈希相同；
- 不把 RDP 节点私钥、节点证书和部署清单当作静态包文件；
- 不把源码树中的日志、缓存、旧 Qt 资源、Console Server 或测试程序带入包中。

## 13. 安装器拆分

生成三个独立安装器：

```text
PixelsCloudNode_<version>_Setup.exe
PixelsClient_<version>_Setup.exe
PixelsRemote_<version>_Setup.exe
```

每个 `<version>` 只取对应产品清单的 `product_version`。构建或发布其中一个安装器不得修改另外两个安装器的版本输入。NSIS 使用 Edition 专属的生成版本文件，不能继续让三个安装器共同包含唯一的 `setup/proj_version.nsh` 作为产品版本来源。

### Client 安装器

- 要求管理员权限，沿用当前系统级安装和 Panel 运行方式；
- 安装到独立的 Program Files 产品目录并写入独立卸载注册表键；
- 不注册 Windows Service；Panel 可保留当前管理员计划任务自动启动行为；
- 不安装虚拟显示和手柄驱动；
- 启动 `px_panel.exe` 的 Client 产品配置。

### Cloud Node 安装器

- 要求管理员权限；
- 安装 Service、用户代理、虚拟显示、手柄、RDP Host 和全功能 Render；
- 安装完整 WebView/CEF、RTC、Hook 与浏览器远控资源。

### Remote 安装器

- 要求管理员权限；
- 安装 Service、用户代理、`px_osinfo`、虚拟显示、手柄、RDP Host 和 Remote Render；
- 不安装 CEF、WebView、Game Hook 和云应用资源；
- 默认安装 RTC 与 `web_client` 以保留浏览器远控。

三个产品使用不同产品名、安装目录、卸载注册表键、快捷方式和升级标识，并在各自卸载信息中写入独立产品版本，但严格互斥安装，不支持同机并存。由于任意时刻只有一个 Pixels 产品，Service 名、计划任务名、IPC 名、端口和单例名可以继续使用当前统一名称，不做产品作用域化。安装和覆盖判断只比较同一 Edition 的版本；不同 Edition 的版本号没有顺序关系，始终按互斥安装处理。

安装器必须在停止进程、覆盖文件或安装驱动之前完成互斥检查：

1. 读取另外两个产品及旧开发版的卸载注册表标记；
2. 检查现有 `px_service` 的注册状态和可执行文件路径；
3. 检查已知其他产品安装目录和产品标记；
4. 检测到其他产品或无法归属当前 Edition 的旧服务时，显示已安装产品和卸载提示并终止；
5. 不自动卸载其他产品，不覆盖其目录，不导入其配置、节点身份或证书；
6. 同一 Edition 的覆盖安装可以沿用当前升级流程，但不承担旧单体产品和其他 Edition 的兼容迁移。

不能只依赖 Service 检测，因为 Client 不安装 Service；卸载注册表标记和产品安装标记是三个产品互斥的主要判据，Service 路径检查是 Host 与残留安装的安全补充。

## 14. 验收门禁

### Client

- 能登录、显示设备和云应用、发起桌面/RDP/文件传输；
- 能启动并显示 `px_osinfo` 上报的本机信息；
- 安装目录不存在 Render、Service、CEF、Hook、虚拟显示和手柄安装组件；
- 安装器和 `px_panel.exe` 按当前决定要求管理员权限；
- 进程和端口检查确认不存在 Service、Render 或绑定外部网卡的被控监听服务；允许清单声明的 `127.0.0.1` Panel、Client、`px_osinfo` 内部通信端口。

### Remote

- 桌面远控、文件传输、RDP、浏览器远控和手柄全部通过；
- 无显示器机器可通过虚拟显示进入桌面；
- 不显示云应用入口；
- Console 不向 Remote 调度 Game Hook、WebView、云应用和云游戏；Remote Render 的编译图和启动入口不接受这些模式；
- 同一 revision、同一共享组件构建批次生成 Remote 与 Cloud Node 时，两包内的 `px_service.exe` SHA-256 一致；跨独立产品发布批次不要求哈希相同，Service 不作为产品裁剪边界；
- 安装目录与 PE 导入表都不存在 CEF、EasyHook、`px_gh*`；
- RDP 主产物全部使用 `px_rdp_` 前缀，旧名称不存在。

### Cloud Node

- 现有桌面、文件传输、RDP、云应用、云游戏、Game Hook、WebView、RTC 和手柄回归全部通过；
- 完整 RDP Host 文件和节点专属信任材料通过校验；
- 不包含 Console Server、日志、缓存和测试产物。

### Android

- 连续两次调用 `build_android_product.bat official debug` 时，Android `product_version` 和 `product_version_code` 每次各提升一次，后一次 `versionCode` 严格大于前一次；
- 单次入口内的 lint、测试、APK、AAB 或 install 子任务共享同一个版本，不发生多次提升；
- Release 构建与 Debug 构建使用同一个 Android 独立版本序列，每次完整产品构建均消耗一个新版本；
- APK/AAB 的 Gradle metadata、应用内版本展示和 Android 发布清单与 `packaging/products/android.toml` 一致；
- 直接执行 APK/AAB 生成任务但未提供产品入口写入的版本环境时明确失败，不回退到 `1` 或 `1.0.0`；
- 构建失败不回滚已提升版本，下一次构建继续使用新的更高版本；
- Android 版本变化不修改 Cloud Node、Client、Remote、Console、Auth 或 Desk Server 的版本。

### 通用交付

- 每个构建树产物与对应 `dist/<product>` 文件逐项 SHA-256 一致；
- Windows 自研 PE、三个安装器及卸载注册表的 Company/Publisher 均为 `Pixels`，Android 发布元数据使用 Pixels 品牌；产品界面、公开 author/vendor 字段和产品清单不存在 RGAA 品牌或组织链接；
- 第三方组件保留自身真实 Company/Publisher/签名者，品牌检查不得篡改或误报第三方厂商；现行网络端点按权威配置独立验收，不以字符串替换冒充域名迁移；
- 三个产品分别生成安装、同 Edition 覆盖安装和卸载测试报告；另外验证任意其他 Edition 或旧开发版存在时安装被明确阻止；
- 分别构建 Cloud Node、Client、Remote 和 Android，验证每次只有目标产品清单及其产物版本发生变化，另外三个产品版本保持不变；Windows 目标同步验证安装器名称、Edition 专属 UI/PE 版本和 Console 上报，Android 同步验证 APK/AAB metadata；
- 验证共享 Service 的组件版本与产品版本分离，Service 上报安装描述中的产品版本，且打入两个产品时不会因重新盖产品版本而产生无来源的二进制差异；
- 安装器清单、实际文件集和 PE 依赖闭包完全一致；
- 中英文产品名称、功能入口和错误提示保持目录一致；
- 不执行旧产品配置、节点身份、证书、目录或文件名迁移，不存在兼容复制和运行时 fallback；
- 既有智能指针、异步生命周期、确定性初始化与 150 列 C++ 规范继续作为硬门禁。

## 15. 实施阶段

1. 引入三个 Windows Edition、Android 产品目标、四个独立产品版本清单和强类型能力矩阵，固定 Windows 产品互斥、Service 完整通用、Client 管理员权限和不兼容旧产品的行为；先不改变现有默认 Cloud Node 行为。
2. 用显式产品版本工具替换全局 `set_app_version.py --bump` 产品发布行为；建立 `build_official/<product>/` 四个完全独立的产品沙箱，CMake、Cargo、Web、RDP policy、Gradle、Native、dist 和 reports 产物均不跨产品共享，确保每次入口调用只提升和构建指定产品一次。
3. 改造收集器为组件白名单和临时目录原子发布，清理当前 `dist` 污染，并先用 Cloud Node 清单核对现有完整产品。
4. 完成 Client 产品图、Client Panel 页面组合及 `px_osinfo` 生命周期；保留管理员权限，但不创建 Host 运行时模块。
5. 拆分 Remote Render，保留桌面、文件传输、RDP、RTC、虚拟显示及手柄；Remote 继续使用完整通用 Service。
6. 从固定上游 RDP SDK 构建链完成全部 `px_rdp_` 重命名并更新导入闭包。
7. 补齐 Cloud Node/Remote 的 RDP Host 安装和节点身份生成流程，明确静态包与节点运行期材料边界。
8. 参数化或拆分 NSIS，生成三个管理员安装器，完成跨 Edition/旧产品互斥、同 Edition 覆盖、卸载、自动清单和禁止依赖验收。

每个阶段必须保持当前可构建产品可用，不允许一次性删除全量打包流程后再长期补齐缺失能力。实施期保留旧构建入口只用于保证开发连续性，不构成最终产品的安装、配置、协议或运行时兼容层；新产品交付后不得继续发布旧全量包。

### 15.1 2026-09-15 实施完成记录

已完成的实现：

- 四份独立产品清单、独立版本递增工具、三个 Windows 发布入口、Android 发布入口和三个互不复用 CMake Cache 的产品构建树；发布入口一次只消耗目标产品的一个版本，focused C++、lint 和单元测试不提升版本；
- 产品能力驱动的 Panel 组合：Remote 构建图不包含云应用页面与产品端口，Client 构建图不包含服务状态页面与产品端口，也不生成 Render；Client 的 `px_osinfo.exe` 由 Panel 监督，Host 产品继续由 Function 监督；
- Cloud Node 与 Remote 各自在 `build_official/<product>/cargo/` 构建和暂存完整 Service；两者使用相同源码和能力配置，但不共享已编译二进制。Service 严格读取相邻 `product-manifest.json`，上报 `Pixels` 公司、Edition、产品版本和能力，缺失或错误描述直接失败；
- Console 严格接受当前 Cloud Node/Remote 身份，按产品能力执行桌面、RDP、云应用、Game Hook、WebView 等调度准入，不推断旧节点身份且不提供兼容回退；
- Remote Render 从编译图排除 Game Hook、CEF/WebView，在启动入口拒绝不支持模式；PE 导入闭包门禁同时拒绝 CEF、EasyHook 和 `px_gh*`；
- 固定 RDP SDK 构建链生成 `px_rdp_` DLL、导入库和代理程序；Client 只加载新名称，策略加载器只解析 `px_rdp_policy.dll`；SDK 清单记录固定 revision、两个补丁、许可证和全部运行时哈希；
- Cloud Node/Remote 静态包包含 RDP Host，部署脚本显式选择 `cloud_node` 或 `remote` 及其新安装目录，并在节点侧生成带证书指纹的部署描述；静态包不包含节点私钥、节点证书或部署描述；
- 显式制品组白名单、临时目录原子发布、每次复制的来源/目标 SHA-256 硬校验、最终清单校验和 Pixels 自研 PE 依赖边界检查；
- 三个 NSIS 安装器使用独立名称、目录、卸载键和产品版本；安装前检测其他 Edition、旧开发版目录或无法归属的 Service，提示卸载并终止，不自动卸载和迁移；
- Windows Panel、Client、Render、RDP Policy 与三个安装器的 VersionInfo 使用 `Pixels`；Android Gradle/发布脚本只接受 Android 清单注入的独立版本和 Pixels 公司名；
- `px_display` 的第三方 WPF 构建通过命名互斥锁串行化，避免多个 Edition 并行构建时争用第三方源码树的 `obj`；Panel 测试使用系统动态分配端口，避免三个产品测试并发冲突。

本机验证结果：

- Cloud Node、Client、Remote 的 Panel 和 Client 均完成定向构建，三套 Panel 产品测试全部通过；Cloud Node 与 Remote Render 均完成定向构建；
- `cargo test -p px_service`：83 项通过；`cargo test -p px_console_server`：175 项通过；
- Android `:app:lintDebug testDebugUnitTest`：418 个 Gradle task 成功；该纯验证未提升 Android 产品版本；
- 三套 `dist` 已从对应构建树重新原子发布：Cloud Node 322 个、Client 47 个、Remote 84 个制品；来源/目标哈希、清单哈希和 PE 产品边界全部通过；同批 Cloud Node/Remote 的 `px_service.exe` SHA-256 一致；
- 三个 3.3.67 安装器均已由 NSIS 实际编译并生成发布清单，安装器自身与内嵌 `app.7z` 的 SHA-256 均已复核；
- 产品清单、Pixels 品牌、版本工具自测、C++ 所有权/初始化规则、Render 退役模块、Render 架构边界、RDP SDK 来源与导入闭包、PowerShell/Python/Node 语法检查全部通过；活动构建、打包和部署脚本只使用当前 Console 节点描述中的权威公网端点。

2026-09-15 尚未执行的实体安装矩阵与公网业务验证已于 2026-09-16 继续完成，结果记录如下。文件传输、浏览器远控、Game Hook 和云游戏的完整产品回归仍按各功能专项验收执行，不把本轮安装器生命周期和 WebView/RDP 验收扩写为这些功能的通过证据。

### 15.2 2026-09-16 安装器与公网节点验收记录

本轮补齐并实际验证了三个 Windows 产品的安装生命周期：

- 安装器在 64 位注册表视图写入独立卸载项，同时能够识别并一次性迁移 3.3.67 安装器遗留的 32 位同产品卸载项；
- 同产品升级沿用已登记的自定义安装目录，停止本产品进程后完整替换安装树，避免已退役文件在升级或同版本覆盖后残留；
- 安装确认前不停止服务、不结束进程、不改驱动；静默安装不启动 Panel；卸载确认后才执行服务、进程、驱动、文件、快捷方式与注册表清理；
- 卸载项补齐 `QuietUninstallString`、`DisplayIcon` 和版本/Publisher 信息，Cloud Node、Client、Remote 均支持静默卸载、版本升级和同版本覆盖安装；
- Host 产品继续使用同一个最大能力 Service，Client 不安装 Service、Render、虚拟显示或手柄组件；三个产品继续严格互斥，不建立兼容或共存行为。

实体公网 Windows 节点上的执行结果：

- Cloud Node：3.3.67 → 3.3.68 升级、3.3.68 同版本覆盖、覆盖时清除旧哨兵文件、Client 跨产品安装返回 1638、卸载后目录/双注册表视图/Service/进程清理，全部通过；
- Client：3.3.67 → 3.3.68 升级、旧 32 位卸载项迁移、3.3.68 同版本覆盖、卸载、无 Host Service/Render，全部通过；
- Remote：3.3.67 → 3.3.68 升级、旧 32 位卸载项迁移、3.3.68 同版本覆盖、Service 重启、卸载后 Service/进程清理，全部通过；
- 最终发布候选 3.3.69 再以 3.3.68 为基线完整复测：三个产品分别完成版本升级、3.3.69 同版本覆盖和卸载；Cloud Node 再次验证跨产品安装返回 1638；全部关键文件哈希与 3.3.69 `dist` 一致；
- 三个产品在升级和覆盖后均核对关键 EXE 的 SHA-256 与对应 `build_official/<product>/dist` 完全一致；三套 `dist` 分别通过 47、84、322 个制品的清单、哈希和产品依赖边界校验；
- 公网节点最终恢复为 Pixels Cloud Node 3.3.69，Service 正常运行；重新部署的 RDP 运行库、策略、证书和密钥逐项哈希校验通过；
- 公网 Cloud Node WebView/Native 端到端通过，实际动态端口为 4613，公网 TCP、工作区就绪和客户端解码帧均通过；RDP 端到端同样通过并收到连续解码帧。

同时修复 Panel 体积回归：此前 `px_panel.exe` 通过 `px_desktop_shell` 的公开依赖错误带入完整媒体 SDK、静态 FFmpeg、Vulkan 和 libplacebo。现已建立仅供 Panel 使用的轻量桌面壳目标，Client 的完整媒体壳保持不变。旧 Panel 约 108.6 MiB；3.3.69 的 Client、Remote、Cloud Node Panel 分别为 16,455,680、17,470,976、17,550,848 字节，并与各自 `dist` 副本 SHA-256 一致。
