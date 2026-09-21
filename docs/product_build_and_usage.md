# Pixels 产品编译、产物与使用说明

状态：当前唯一有效流程
适用产品：Pixels Cloud Node、Pixels Client、Pixels Remote、Pixels Android

当前发布入口只生成 Pixels `official` 与 Pixels `customer`。OEM 是独立发行线，不得通过修改现有 Customer 的名称、图标、URL 或清单后
交付。OEM 构建入口尚未开放；开放前必须同时提供唯一 `oem_id/release_namespace`、独立品牌/应用/安装身份、独立 TUF 初始根、私有更新策略
和跨 Official/Customer/其他 OEM 的拒绝测试。现有双发行矩阵继续保持两项，不能把未实现的 OEM 算作已完成产物。

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

Cloud Node 与 Remote 的 Official/Customer Web Client 也是发行绑定制品，不是可在两种发行之间复制的通用静态目录。完整矩阵构建从对应
`deployment` 目录注入 policy、approved trust store 和产品 build 水位；缺少材料、发行不匹配或非正 build 会在 Vite 构建阶段失败关闭。
正式 bundle 只接受带 `console_origin` 的 Console 资源启动描述符，并在向 Render 使用 frontend token 前完成签名部署身份和 nonce 证明。
未来 OEM Web/Windows/Android 产物必须从其 OEM 专属沙箱生成，并携带同一 OEM 命名空间；不得读取 Official/Customer 的已编译 bundle、
update root 或安装清单。

## 2. 完整编译规则

完整产品构建每次执行以下行为：

1. 在任何清理或升版前验证 approved trust store、三项最低水位、Official deployment UUID 和规范 HTTPS origin；
2. 删除目标产品整个旧沙箱；
3. 只递增目标产品自己的版本号一次；
4. 从干净目录分别构建同版本 Official 与 Customer 的全部 C++、Rust、Web、RDP 产物；
5. 用严格白名单重新生成两套完整 `dist`，分别写入签名身份 policy/trust 公共材料；
6. 校验 product、distribution、制品清单、SHA-256，并拒绝任何 ZLMediaKit/Coturn 退役文件名或组件目录；
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

Cloud Node/Remote 安装器会在每次成功覆盖时把当前完整安装包保存到受保护的机器更新缓存，供下一次升级失败时精确回滚。缓存 ACL 只允许
SYSTEM 和本机管理员，Service 在授权升级前记录并保护旧包 SHA-256 及当前产品清单中的签名证书 DER SHA-256；新包的签名证书固定值由
Console 审批记录与 TUF `pixels` 元数据共同绑定。runner 使用前既复核 Authenticode 链，也精确比对对应证书固定值。安装、覆盖升级、自动更新、回滚和
卸载共用一个全局安装互斥锁；并发操作返回 Windows Installer busy（1618），不会同时改写安装目录或回滚点。

执行前必须设置：`PIXELS_DEPLOYMENT_TRUST_STORE_FILE`、`PIXELS_UPDATE_ROOT_FILE`、`PIXELS_DEPLOYMENT_CERTIFICATE_VERSION`、
`PIXELS_DESCRIPTOR_REVISION`、`PIXELS_DEPLOYMENT_TRUST_EPOCH`、`PIXELS_EXPECTED_DEPLOYMENT_ID` 和
`PIXELS_OFFICIAL_CONSOLE_URL`。Customer 产物不会写入后两项；它们只用于同一矩阵事务中的 Official 半边。trust store 必须是离线签发流程输出的
规范 JSON，不能使用服务器下载内容或测试 key。`PIXELS_UPDATE_ROOT_FILE` 必须是离线审批并签名的 TUF 1.0 初始根；Official 和 Customer
都内置同一 Pixels 更新信任根，Customer 可使用自己的镜像地址，但不能以私有描述或重签方式改变制品发行属性。预检失败不会删除现有产物，
也不会消耗版本号。

Windows 正式构建还必须设置以下代码签名输入：

- `PIXELS_WINDOWS_SIGNING_CERT_SHA1`：Windows `My` 证书存储中证书的精确 SHA-1 选择值；
- `PIXELS_WINDOWS_SIGNING_CERT_SHA256`：审批记录中的证书原始 DER SHA-256 固定值，防止只凭较弱选择值误签；
- `PIXELS_WINDOWS_SIGNING_STORE`：只能是 `current_user` 或 `local_machine`；
- `PIXELS_WINDOWS_TIMESTAMP_URL`：无凭据的 HTTPS RFC 3161 时间戳地址；
- `PIXELS_WINDOWS_SIGNTOOL`：可选的固定 `signtool.exe` 路径，未设置时使用 PATH 或已安装 Windows SDK。

私钥必须已经由 Windows 证书存储、硬件令牌或构建机密钥提供者安全暴露给该证书；构建脚本不接受 PFX 密码参数，也不把私钥或密码写入
命令行。清理和升版前的预检会核对双指纹、私钥可用性、代码签名 EKU、证书有效期、HTTPS 时间戳配置、SignTool 以及固定 NSIS 版本。
正式 `dist` 中所有 Pixels 自有 PE、生成的 `Uninstall.exe` 和最终 Setup 均须使用同一审批证书签名并带时间戳，随后由 SignTool 和
PowerShell 独立复核签名状态、签名者 SHA-256 与时间戳；任一项失败都不发布版本目录。

仓库固定 NSIS 3.12（正式签名卸载器至少需要 3.08，项目要求不低于 3.11 的 SYSTEM 安全修复基线）。安装器直接封装已验证的 `dist`，
不再使用旧 `Nsis7z`/`nsProcess` 插件和额外 `app.7z` 层；工具包按 vendored 字节处理，不能在提交时自动换行或格式化。

### 2.2.1 TUF 离线发布权威

`px_update_authority` 是明确离线运行的更新仓库生成工具，不是在线服务，也不会上传、覆盖或切换正在提供服务的仓库。入口为：

```bat
cargo run --locked --manifest-path rust_server/Cargo.toml -p px_update_authority -- generate-key
cargo run --locked --manifest-path rust_server/Cargo.toml -p px_update_authority -- create-root
cargo run --locked --manifest-path rust_server/Cargo.toml -p px_update_authority -- rotate-root
cargo run --locked --manifest-path rust_server/Cargo.toml -p px_update_authority -- publish
cargo run --locked --manifest-path rust_server/Cargo.toml -p px_update_authority -- promote-filesystem
```

`generate-key` 每次只通过 `PIXELS_TUF_KEY_OUTPUT` 创建一个新 Ed25519 PKCS#8 私钥，父目录必须已经按生产私钥目录限制权限，已有文件绝不覆盖。
正式初始根至少使用 2 个、至多 5 个独立 root key，门限不得低于 2；targets、snapshot、timestamp 各用一个彼此及 root 都不同的 key。
`create-root` 的输入为：

- `PIXELS_TUF_ROOT_SIGNING_KEYS`：root 私钥绝对路径的 JSON 数组；
- `PIXELS_TUF_ROOT_THRESHOLD`、`PIXELS_TUF_ROOT_VERSION`、`PIXELS_TUF_ROOT_EXPIRES_AT`；
- `PIXELS_TUF_TARGETS_SIGNING_KEY`、`PIXELS_TUF_SNAPSHOT_SIGNING_KEY`、`PIXELS_TUF_TIMESTAMP_SIGNING_KEY`；
- `PIXELS_TUF_ROOT_OUTPUT`：不存在的输出文件，时间使用带时区的 RFC 3339。

`rotate-root` 额外要求 `PIXELS_TUF_CURRENT_ROOT_FILE` 和旧 root 私钥绝对路径 JSON 数组
`PIXELS_TUF_CURRENT_ROOT_SIGNING_KEYS`；其余 root/角色 key、门限、到期和输出变量代表新根。版本只能由工具在当前版本上加一，新到期时间必须
晚于当前 root。输出必须同时达到旧 root 门限和新 root 门限，工具会分别用旧、新 root 验证后才创建；只持新 key 生成的自签 root 会被拒绝。
将轮换 root 作为 `<version>.root.json` 与仓库元数据按版本顺序发布并等待客户端水位推进后，才能离线撤去旧 key；不能用新 root 直接替换
客户端内置的初始 root。

`publish` 需要上述三个在线角色私钥，以及 `PIXELS_TUF_ROOT_FILE`、`PIXELS_RELEASE_SPEC_FILE`、`PIXELS_RELEASE_ARTIFACT`、
`PIXELS_TUF_REPOSITORY_OUTPUT`、三个 `PIXELS_TUF_*_EXPIRES_AT`。追加发布时还必须给出 `PIXELS_TUF_PREVIOUS_REPOSITORY`。工具验证 root 自签门限、
角色密钥隔离、到期顺序、ReleaseSpec、制品大小/SHA-256、历史仓库全部签名和全部历史目标字节；角色版本自动严格递增。每个 target name 永久
不可复用，输出目录也不可覆盖。轮换后的第一次追加发布要求新 root 恰为上一仓库 root 的 `N+1` 且同时满足旧、新门限；新候选保留连续
版本化 root 链，并从最早保留根重新验证整库，禁止把自签新根直接接到旧仓库。新输出在同父目录的随机 staging 中完整生成并由正式 `tough`
客户端重新下载验证目标后才一次重命名提交，包含
`metadata/`、`targets/` 和带 root/release 摘要及角色版本的 `publication.json`。

Windows 的 `PIXELS_RELEASE_SPEC_FILE` 不手工抄写。从正式安装器版本目录生成：

```bat
python scripts\prepare_windows_update_release.py ^
  --release-directory <build_official\product\distribution\installer\version> ^
  --approved-signer-sha256 <外部审批的证书DER SHA-256> ^
  --metadata-base-url https://updates.example/metadata/ ^
  --targets-base-url https://updates.example/targets/ ^
  --channel stable ^
  --output <不存在的绝对release-spec.json路径>
```

该入口复用独立安装包验证器，重新检查 installer manifest、制品 SHA-256、Authenticode、时间戳和外部 signer pin，并从已验证事实生成固定的
`windows/product/distribution/channel/x86_64/build/installer` target name；输出使用排他创建且不覆盖。生成后的 spec 和同一 installer 文件才交给
`px_update_authority publish`，因此 TUF 发布不能靠修改 JSON 把另一产品、发行、build 或签名者带入目录。

该命令只生成一个不可变候选目录。发布系统还必须把候选同步到独立临时位置、核对 `publication.json`，先提交 targets 与非 timestamp 元数据，
最后原子切换 `timestamp.json`；不能直接对线上目录运行本工具。root 私钥保持离线，日常 `publish` 不接触 root 私钥。正式 Windows ReleaseSpec
中的 `platform_signer_sha256` 必须来自已独立验证的安装器 manifest 和审批证书固定值，不能由仓库地址或 TUF 在线角色密钥替代。

`promote-filesystem` 用于部署主机上的本地或挂载式静态源站目录，不执行 SSH、对象存储 API 或 CDN 刷新。必须提供绝对路径
`PIXELS_TUF_CANDIDATE_REPOSITORY`、`PIXELS_TUF_LIVE_REPOSITORY`，以及审批系统在传输外独立固定的候选
`publication.json` 小写 SHA-256：`PIXELS_TUF_APPROVED_PUBLICATION_SHA256`。首次发布先在 live 同父目录复制并完整验签，再用目录重命名提交；后续只接受
三个在线角色版本各加一、历史目标不变且 root 链相同或追加一个合法根的下一代候选。执行器先发布不可变 targets/root，再逐文件原子切换
`targets.json`、`snapshot.json`，最后切换 `timestamp.json` 和审计清单。切换前写入持久 `promotion.pending.json`；进程或主机在任一步中断后，必须用
同一候选和同一审批 SHA 续跑，另一候选会 fail closed。成功后执行器重新从初始根加载线上目录、验证全部目标并删除 journal。

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
  每份 APK/AAB 的 ZIP 条目还必须通过 ZLMediaKit/Coturn 退役组件审计，只有两边均通过才生成根 `release-matrix.json`。

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

正式签名包先做只读的新旧版本预检：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/validate_windows_installer_lifecycle.ps1 `
    -ExpectedProduct client `
    -ExpectedDistribution official `
    -PreviousReleaseDirectory build_official/client/official/installer/<old-version> `
    -CurrentReleaseDirectory build_official/client/official/installer/<new-version> `
    -ConflictReleaseDirectory build_official/remote/official/installer/<current-version> `
    -ApprovedPreviousSignerSha256 <approved-old-certificate-sha256> `
    -ApprovedCurrentSignerSha256 <approved-new-certificate-sha256> `
    -ReportPath build_official/client/reports/official-installer-lifecycle.json
```

该命令强制从命令行接收预期产品、预期发行和两个外部审批的证书 SHA-256，拒绝把相邻 manifest 的自我声明当成信任根，也拒绝用另一个
合法签名的 Pixels 产品/发行替换目标；随后验证两个安装器的 schema、产品/发行、
严格递增版本、安装器 SHA-256、Authenticode 状态和时间戳。无证书轮换时两个审批值相同，续期时分别给出旧值和新值；默认绝不安装或
卸载。只有在专用、已提升权限且确认三个 Pixels 产品和 `px_service` 均不存在的干净 Windows 验收机上，才增加
`-ExecuteLifecycle`。执行态依次验证旧版安装、同发行升级、同版覆盖、安装目录精确文件集及逐件 hash、自研 PE 与卸载器签名、Service 产品
边界和最终卸载清理。提供另一产品的正式包时，还会先安装该产品，要求目标安装返回 1638 且原产品逐件不变，再清洁卸载；执行器也会注册
一个不启动的受控 `px_service` 探针，要求目标安装同样返回 1638，随后只在探针身份未变化时删除它。每阶段原子写报告，产品失败后保留现场
而不自动删除证据。Cloud Node、Client、Remote 的 Official/Customer 六组必须分别
执行，不能用 development dist、自签名包或 NSIS 语法构建替代。
两个审批值均为必填，不能使用通配、只提供新证书或从包内自我声明放宽签名者切换。

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
- 正式发布候选在专用 Windows 验收机完成对应的新旧签名安装包生命周期报告；
- 没有公共 `build_official/dist`、公共 Rust 编译目录或其他产品制品混入。

服务端 Console/Auth/Desk 有独立发布流程，不属于上述四个客户端产品沙箱。
