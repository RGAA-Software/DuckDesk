# Pixels 产品编译、产物与使用说明

状态：当前唯一有效流程
适用产品：Pixels Cloud Node、Pixels Client、Pixels Remote、Pixels Android

旧的根 CMake 树、公共 `build_official/dist`、共享 Rust 编译产物、`build_client.bat`、旧端口和旧节点测试方案均已退役，不提供兼容入口。

## 1. 产品与目录

所有生成内容都位于对应产品的独立沙箱：

```text
build_official/
├── cloud_node/{cmake,dist}/                         # 日常聚焦开发
│   ├── official/{cmake,cargo,web,rdp_policy,deployment,dist,installer,reports}/
│   └── customer/{cmake,cargo,web,rdp_policy,deployment,dist,installer,reports}/
├── client/{cmake,dist}/                             # 日常聚焦开发
│   ├── official/{cmake,cargo,deployment,dist,installer,reports}/
│   └── customer/{cmake,cargo,deployment,dist,installer,reports}/
├── remote/{cmake,dist}/                             # 日常聚焦开发
│   ├── official/{cmake,cargo,web,rdp_policy,deployment,dist,installer,reports}/
│   └── customer/{cmake,cargo,web,rdp_policy,deployment,dist,installer,reports}/
└── android/{official,customer}/{gradle,native,dist,reports}/
```

- `cmake`：该 Windows 产品专属 CMake/Ninja 构建树。
- `cargo`：该产品专属 Rust target 和 staging；产品之间不复用已编译二进制。
- `web`：Cloud Node 或 Remote 专属 Web Client 构建结果。
- `rdp_policy`：Cloud Node 或 Remote 专属 RDP Policy 构建树。
- `dist`：可运行的完整产品目录，也是安装包唯一输入。
- `installer/<version>`：Windows 安装包和安装包清单。
- `gradle`、`native`：Android Gradle 与 CMake/NDK 输出。
- `reports`：产品专属测试和验收报告。

下载缓存、依赖源码和工具链缓存可以共享；任何已编译产品文件不得跨产品目录读取。

Windows Rust 使用仓库 `rust_client/.cargo/config.toml` 中的 MSVC `/Brepro`。Cloud Node 与 Remote 即使在各自独立的
`CARGO_TARGET_DIR` 构建，共享 Service 在源码、依赖、编译参数和 revision 相同时也必须得到相同 SHA-256；构建、stage、dist
三层必须逐文件核对。产品清单只由完整产品构建从干净沙箱生成，聚焦构建不得用“重写清单”掩盖 dist 中其他文件的漂移。

## 2. 完整编译规则

完整产品构建每次执行以下行为：

1. 在任何清理或升版前验证 approved trust store、三项最低水位、Official deployment UUID 和规范 HTTPS origin；
2. 删除目标产品整个旧沙箱；
3. 只递增目标产品自己的版本号一次；
4. 从干净目录分别构建同版本 Official 与 Customer 的全部 C++、Rust、Web、RDP 产物；
5. 用严格白名单重新生成两套完整 `dist`，分别写入签名身份 policy/trust 公共材料；
6. 校验 product、distribution、制品清单和 SHA-256；
7. 分别生成支持覆盖安装、同发行升级和卸载的安装包。跨 Official/Customer 覆盖会要求先卸载。

在仓库根目录 `D:\GoCloud\GammaRayPremium` 执行。不要从旧目录复制文件拼装产品。

### 2.1 一次完整构建四个产品

```bat
scripts_build\build_all_products.bat
```

执行顺序为 Cloud Node、Client、Remote、Android Release；任一步失败立即停止。四个产品均独立升版。

### 2.2 只完整构建一个 Windows 产品

```bat
scripts_build\build_official.bat cloud_node
scripts_build\build_official.bat client
scripts_build\build_official.bat remote
```

等价的直接入口为：

```bat
scripts_build\build_cloud_node.bat
scripts_build\build_client_product.bat
scripts_build\build_remote_product.bat
```

这些都是发布级完整构建；每条命令一次升版并同时构建 Official/Customer，不接受旧的 `full`、`incremental` 或 `reconfigure` 参数。

执行前必须设置：`PIXELS_DEPLOYMENT_TRUST_STORE_FILE`、`PIXELS_DEPLOYMENT_CERTIFICATE_VERSION`、
`PIXELS_DESCRIPTOR_REVISION`、`PIXELS_DEPLOYMENT_TRUST_EPOCH`、`PIXELS_EXPECTED_DEPLOYMENT_ID` 和
`PIXELS_OFFICIAL_CONSOLE_URL`。Customer 产物不会写入后两项；它们只用于同一矩阵事务中的 Official 半边。trust store 必须是离线签发流程输出的
规范 JSON，不能使用服务器下载内容或测试 key。预检失败不会删除现有产物，也不会消耗版本号。

### 2.3 完整构建 Android

```bat
scripts_build\build_android_product.bat official debug
scripts_build\build_android_product.bat official debug install
scripts_build\build_android_product.bat customer debug
scripts_build\build_android_product.bat customer debug install
scripts_build\build_android_product.bat release
```

- `debug`：执行 lint、单元测试并生成完整 Debug APK。
- `debug install`：使用 `adb install -r` 覆盖安装，不卸载现有应用。
- `release`：一次预检和一次升版后，为 Official/Customer 生成同版本的签名 APK、AAB、mapping、native symbols、LGPL relink 材料和发布清单；
  只有两边均通过才生成根 `release-matrix.json`。

单发行 Debug 每次调用先删除自己的旧沙箱并提升 Android 版本一次；正式 Release 先同时预检两个发行，再删除整个 Android 输出，且只提升
Android 版本一次。任何缺失的身份、签名或 FFmpeg 合规输入都会在清理和升版前失败。旧的单发行 Release 调用不再提供兼容入口。

`official` 固定编译时的 HTTPS Console origin 与 deployment UUID，设置页不提供服务器编辑；`customer` 使用独立 applicationId 和输出沙箱，
不允许编入 Official 的 UUID/URL，只接受用户填写且签名类别为 `private` 的部署。两类构建都必须内置同一审批后的公开 trust store，并显式设置
`PIXELS_DEPLOYMENT_CERTIFICATE_VERSION`、`PIXELS_DESCRIPTOR_REVISION`、`PIXELS_DEPLOYMENT_TRUST_EPOCH` 最低水位。

Release 必须使用上述统一入口，不能直接调用 Gradle 的 `assembleRelease`/`bundleRelease`。流水线从当前 Android native 构建实际产生的对象自动生成 LGPL relink kit，并根据 `VCPKG_ROOT`（默认 `C:\source\vcpkg`）中已安装的 SPDX 清单锁定和校验 FFmpeg n6.1 对应源码；不再手工提供旧源码包或旧 relink 包。正式签名来自被 Git 忽略的 `src/px_android/keystore.properties`，也可由完整的 `PIXELS_*` 签名变量提供。

## 3. Windows 产物与运行

完整构建成功后从对应 `dist` 启动，不得使用 CMake 树里的 EXE 作为产品验收入口：

```bat
build_official\cloud_node\official\dist\px_panel.exe
build_official\cloud_node\customer\dist\px_panel.exe
build_official\client\official\dist\px_panel.exe
build_official\client\customer\dist\px_panel.exe
build_official\remote\official\dist\px_panel.exe
build_official\remote\customer\dist\px_panel.exe
```

产品边界：

- Cloud Node：完整节点能力，包含云应用、桌面 Host、Render、Service、RDP、WebView、Game Hook 和浏览器远控。
- Client：控制端，包含云应用访问入口，不安装 Service、Render、虚拟显示或手柄组件。
- Remote：桌面远控 Host，不包含云应用、Game Hook、WebView 或 CEF。

三个 Windows 产品不能共存。安装器发现其他产品或手工安装的开发版 Service 时，会要求先卸载并终止安装；不会自动兼容、接管或迁移旧产品。

安装包位置：

```text
build_official/<product>/<official|customer>/installer/<version>/
```

安装、升级或覆盖安装使用对应版本的 `PixelsCloudNode_<distribution>_*_Setup.exe`、`PixelsClient_<distribution>_*_Setup.exe` 或
`PixelsRemote_<distribution>_*_Setup.exe`。安装器在注册表记录发行身份；同产品不同发行不能直接覆盖，须先卸载。卸载使用 Windows“已安装的应用”或产品卸载程序。

## 4. Console 与连接配置

产品只使用当前 Console 身份和权威连接描述：

- Official 的 Console 地址来自已验证安装策略，设置页只读；Customer 在设置页填写私有 Console；
- 支持当前账号登录、注册和云应用会话；
- Android 使用 `client_type=android`；
- 不使用局域网测试机假设，不探测或回退到已退役端口；
- Render 桌面端口和应用端口以 Console/节点返回的当前描述为准。

## 5. Android 产物与安装

Debug APK 位于：

```text
build_official/android/<official|customer>/dist/Pixels-<distribution>-<version>-debug-arm64-v8a.apk
```

Release 产物位于：

```text
build_official/android/<official|customer>/dist/<version>/
```

该目录包含签名 APK、AAB、R8 mapping、native debug symbols、FFmpeg 对应源码、LGPL relink kit、第三方 notices 和记录全部 SHA-256、签名证书及 native Build ID 的 `release-manifest.json`。只有这些文件全部验证成功后，版本目录才会原子发布。

手工覆盖安装：

```bat
adb install -r build_official\android\official\dist\Pixels-official-<version>-debug-arm64-v8a.apk
```

不要先卸载应用，否则会触发重新授权并丢失应用数据。

## 6. 日常聚焦验证

只有在修改和验证单个 C++ 范围时使用聚焦入口；它们不升版、不构建完整安装包。产品参数是必填项：

```bat
scripts_build\build_cpp_product_panel.bat cloud_node 18
scripts_build\build_cpp_product_client.bat client 18
scripts_build\build_cpp_product_render.bat remote 18
scripts_build\build_cpp_product_panel_tests.bat client 18
```

聚焦入口固定使用 `PX_DISTRIBUTION=development`，仍把变化的运行文件发布到对应产品 `dist` 并核对 SHA-256；该目录不含正式 deployment
policy/trust，不能冒充完整发布包。需要交付或制作安装包时，必须重新运行第 2 节的完整双发行构建。

## 7. 清理

完整构建会自动清理目标产品。需要手工清理时：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts_build\clean_product_outputs.ps1 -Product cloud_node
powershell -NoProfile -ExecutionPolicy Bypass -File scripts_build\clean_product_outputs.ps1 -Product all
```

`all` 只删除仓库下的 `build_official` 生成目录；源码、下载缓存、外部工具链和服务器独立输出不受影响。删除后的产物只能通过重新构建恢复。

## 8. 成功判定

只有同时满足以下条件才算完成：

- 构建命令返回 0；
- `product-build.json` 与目标产品、版本和 CMake 目录一致；
- `dist/product-manifest.json` 与产品清单一致；
- `dist/artifact-manifest.json` 中全部 SHA-256 校验通过；
- Windows 两种发行使用同一产品版本，安装包分别位于 `<official|customer>/installer/<version>`；
- 没有公共 `build_official/dist`、公共 Rust 编译目录或其他产品制品混入。

服务端 Console/Auth/Desk 有独立发布流程，不属于上述四个客户端产品沙箱。
