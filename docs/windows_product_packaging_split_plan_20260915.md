# Windows 产品构建与安装包拆分改造方案

日期：2026-09-15  
状态：已确认产品方向，待实施  
适用范围：Windows 原生 Panel、Client、Render、Service、RDP、WebView、Game Hook、虚拟显示与安装器

## 1. 目标

当前 Windows 交付物由一个全量构建图、一个 `dist` 收集器和一个 NSIS 安装器组成。所有功能被打入同一个应用包，导致：

- 纯客户端携带被控端、服务、驱动、Hook、CEF 和 Web 资源；
- 独立远控产品无法从二进制层面排除云应用、云游戏和 WebView；
- `collect_dist.py` 扫描构建目录，可能把日志、旧资源或服务端产物带入安装包；
- 当前安装器始终请求管理员权限并安装虚拟显示、手柄等组件，不适合纯客户端；
- 当前 RDP Client 与 RDP Host 运行时没有形成完整、统一命名的可发布组件。

本次改造保留一个代码仓库和共用核心模块，建立三个独立产品构建、三个严格白名单分发目录和三个安装器。禁止维护三份源码分支，也禁止先生成全量包再靠删除文件制作精简版。

## 2. 已确认的产品决定

1. `Pixels Cloud Node` 是全功能云端渲染节点，包含桌面远控、文件传输、RDP、云应用、云游戏、Game Hook、WebView、浏览器远控等完整能力。
2. `Pixels Client` 是控制端产品，不具备被控制和托管应用的能力；包含 `px_panel.exe`、`px_client.exe` 和 `px_osinfo.exe`。
3. `Pixels Remote` 是独立远控产品，包含桌面远控、文件传输、RDP、系统信息、虚拟显示和手柄能力；不包含云应用、云游戏、Game Hook 和 WebView。
4. `Pixels Remote` 必须包含 `px_joystick.exe`，Render 远控版也必须保留手柄输入链路。
5. 项目发布的 RDP 主产物统一使用 `px_rdp_` 文件名前缀。不得只对既有上游 RDP SDK 文件做磁盘重命名，必须从固定 SDK 构建阶段生成匹配的导入库和 PE 依赖名。
6. WebView 与浏览器远控不是同一个能力。WebView/CEF 只进入 Cloud Node；RTC 和 `web_client` 是否进入 Remote 由浏览器远控能力决定。本方案默认 Remote 保留浏览器远控。

## 3. 产品能力矩阵

| 能力或组件 | Cloud Node | Client | Remote |
|---|---:|---:|---:|
| `px_panel.exe` | 是，全功能节点界面 | 是，控制端界面 | 是，远控界面 |
| `px_client.exe` | 是 | 是 | 是 |
| `px_osinfo.exe` | 是 | 是 | 是 |
| `px_render.exe` | 是，全功能版本 | 否 | 是，远控精简版本 |
| `px_service.exe` | 是 | 否 | 是 |
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
| `px_render_rtc.dll`、`px_render_rtc_remote.dll` | 是 | 否 | 是，默认保留浏览器远控 |
| `web_client/` | 是 | 否 | 是，默认保留浏览器远控 |
| Render `settings.toml` | 是 | 否 | 是，精简配置 |
| `px_service.toml` | 是 | 否 | 是，精简配置 |

`px_console.exe`、Console 前端、服务器程序、`px_logs/`、`debug.log`、旧 Qt `output/` 资源均不属于以上三个桌面产品，必须由各自服务端发布流程单独交付。

## 4. 产品能力模型

新增唯一的产品版本参数：

```text
PX_PRODUCT_EDITION=CLOUD_NODE|CLIENT|REMOTE
```

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

能力必须在以下各层同时生效：

- CMake：决定编译哪些源文件、链接哪些库、生成哪些目标；
- Rust Cargo feature：决定 Service 是否包含和接受云应用、Game Hook、WebView、RDP Host 等业务；
- Panel：决定页面和操作入口是否存在；
- Service/Render：在协议入口再次拒绝不属于当前产品的模式；
- Console：根据节点上报能力决定是否允许调度相应工作负载；
- Packaging：只收集当前产品声明的运行时文件。

不得把“隐藏一个菜单”当作产品隔离，也不得通过缺少某个 DLL 让不允许的模式在运行时失败。

## 5. CMake 构建图拆分

使用三个独立构建目录，避免同一个 CMake Cache 中的条件源文件、编译宏和静态库互相污染：

```text
build_cloud_node/
build_client/
build_remote/
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

现有 `scripts_build/build_official.bat` 继续作为发布级完整构建入口，但必须显式接收产品版本并只构建该产品聚合目标。日常开发仍使用 `scripts_build/build_cpp_*.bat` 定向构建，不得借拆包改造绕过现有增量构建规则。

顶层 CMake 中 Cargo、MSBuild、CEF、Hook SDK 等依赖发现也必须按产品能力延迟执行。构建 Client 时不应要求本机准备 CEF、Parsec VDD 构建环境或 Host Service Rust 目标。

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

## 7. Service 与 Console 能力约束

为 `px_service` 增加明确的 Cargo features：

```toml
cloud-node = ["desktop-host", "rdp-host", "cloud-apps", "game-hook", "webview"]
remote-host = ["desktop-host", "rdp-host", "joystick"]
```

Remote Service 只接受：

- `desktop`；
- `rdp`；
- 文件传输；
- 系统信息、访问策略和节点管理。

Remote Service 必须在创建进程和分配端口之前拒绝：

- `game-hook`；
- `webview`；
- 普通云应用实例；
- 云游戏实例；
- 与上述能力绑定的启动、停止和状态恢复命令。

节点连接 Console 时上报产品版本和能力位。Console 只能向声明对应能力的节点下发任务。协议变化使用追加字段，保留已有 protobuf 字段号和语义。

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

Client 不显示本机被控开关、Render 状态、Service 管理、虚拟显示和节点应用托管入口。

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
- `px_osinfo` 的实例键必须至少包含产品版本、用户 SID 和 Panel 端口，避免 Client 与 Host 产品并存时错误复用另一个实例；
- Panel 启动参数显式传递监听端口，不依赖隐式固定端口。

Client 的系统信息只用于本机诊断和 UI 展示，不得因为包含 `px_osinfo` 而开放被控端口或注册节点服务。

## 10. Remote 中的手柄组件

Remote 必须包含：

- `px_joystick.exe`；
- Render 手柄输入模块；
- 所需的 ViGEm 安装资源和许可证；
- 设置页中的手柄驱动状态、安装与修复入口。

Cloud Node 与 Remote 可能共用同一个系统级手柄驱动。安装器应记录驱动所有权，卸载时只有在没有其他 Pixels Host 产品声明使用该驱动时才删除驱动，禁止一个产品卸载后破坏另一个产品。

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
4. 策略配置中的模块名同步改为 `px_rdp_policy`；
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

Cloud Node 和 Remote 额外包含 RDP Host 运行时：

```text
rdp/px_rdp_proxy.exe
rdp/px_rdp_server_proxy.dll
rdp/px_rdp_server.dll
rdp/proxy/px_rdp_policy.dll
rdp/px_rdp_deployment.json
rdp/px_rdp_proxy.crt
rdp/px_rdp_proxy.key
rdp/px_rdp_console_ca.der
```

代理私钥不得作为所有机器共用的静态安装包内容。安装或节点注册过程必须为当前节点生成或安全下发身份材料，再生成带固定证书指纹的 `px_rdp_deployment.json`。

## 12. 严格白名单打包

建立组件清单和产品清单：

```text
packaging/
├── components/
│   ├── panel.toml
│   ├── native_client.toml
│   ├── system_information.toml
│   ├── desktop_host.toml
│   ├── rdp_client.toml
│   ├── rdp_host.toml
│   ├── rtc_host.toml
│   ├── webview_host.toml
│   ├── game_hook.toml
│   ├── joystick.toml
│   └── virtual_display.toml
└── products/
    ├── cloud_node.toml
    ├── client.toml
    └── remote.toml
```

输出目录：

```text
build_official/dist/cloud_node/
build_official/dist/client/
build_official/dist/remote/
```

收集器必须：

- 仅复制清单声明的文件，不扫描并猜测可发布内容；
- 每次原子重建目标产品目录，杜绝陈旧文件；
- 声明文件缺失、哈希错误或出现未声明文件时失败；
- 生成 `product-manifest.json`、`sha256sums.json` 和许可证清单；
- 检查产品禁止文件和 PE 禁止依赖；
- 不把源码树中的日志、缓存、旧 Qt 资源、Console Server 或测试程序带入包中。

## 13. 安装器拆分

生成三个独立安装器：

```text
PixelsCloudNode_<version>_Setup.exe
PixelsClient_<version>_Setup.exe
PixelsRemote_<version>_Setup.exe
```

### Client 安装器

- 默认按用户安装；
- 不要求管理员权限；
- 不注册 Windows Service 或计划任务；
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

三个产品使用不同产品名、安装目录、卸载注册表键、快捷方式和升级标识。Host 服务名、计划任务名、IPC 名和单例名也必须产品作用域化，避免产品并存时互相停止或覆盖。Cloud Node 与 Remote 是否允许同机并存需要在实施前固定；在未完成作用域化之前，安装器必须明确阻止两个 Host 产品并存。

## 14. 验收门禁

### Client

- 能登录、显示设备和云应用、发起桌面/RDP/文件传输；
- 能启动并显示 `px_osinfo` 上报的本机信息；
- 安装目录不存在 Render、Service、CEF、Hook、虚拟显示和手柄安装组件；
- 普通用户可安装和运行；
- 进程和端口检查确认没有被控监听服务。

### Remote

- 桌面远控、文件传输、RDP、浏览器远控和手柄全部通过；
- 无显示器机器可通过虚拟显示进入桌面；
- 不显示云应用入口；
- Service 在启动进程前拒绝 Game Hook、WebView 和云应用命令；
- 安装目录与 PE 导入表都不存在 CEF、EasyHook、`px_gh*`；
- RDP 主产物全部使用 `px_rdp_` 前缀，旧名称不存在。

### Cloud Node

- 现有桌面、文件传输、RDP、云应用、云游戏、Game Hook、WebView、RTC 和手柄回归全部通过；
- 完整 RDP Host 文件和节点专属信任材料通过校验；
- 不包含 Console Server、日志、缓存和测试产物。

### 通用交付

- 每个构建树产物与对应 `dist/<product>` 文件逐项 SHA-256 一致；
- 三个产品分别生成可安装、升级、卸载的测试报告；
- 安装器清单、实际文件集和 PE 依赖闭包完全一致；
- 中英文产品名称、功能入口和错误提示保持目录一致；
- 既有智能指针、异步生命周期、确定性初始化与 150 列 C++ 规范继续作为硬门禁。

## 15. 实施阶段

1. 引入产品版本和强类型能力矩阵，先不改变现有默认 Cloud Node 行为。
2. 改造收集器为组件白名单，清理当前 `dist` 污染。
3. 完成 Client 产品图、Client Panel 页面组合及 `px_osinfo` 生命周期。
4. 拆分 Remote Render 和 Remote Service，保留 RDP、RTC、虚拟显示及手柄。
5. 从固定上游 RDP SDK 构建链完成全部 `px_rdp_` 重命名并更新导入闭包。
6. 补齐 Cloud Node/Remote 的 RDP Host 安装和节点身份生成流程。
7. 参数化或拆分 NSIS，生成三个独立安装器。
8. 完成三个产品的自动清单、禁止依赖、安装升级和功能验收。

每个阶段必须保持当前可构建产品可用，不允许一次性删除全量打包流程后再长期补齐缺失能力。
