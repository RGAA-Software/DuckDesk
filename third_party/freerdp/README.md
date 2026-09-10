# FreeRDP 源码依赖

本目录管理源码和依赖清单，不保存 SDK 二进制。`source/` 是官方
https://github.com/FreeRDP/FreeRDP.git 的 Git 子模块；父仓库 gitlink 固定提交
`aa8650b300aa4cabd85d9c72b431301509b9043f`（3.31.0）。不要使用 `submodule update --remote` 自动升级。
主仓库保存源码版本引用，子模块保存上游源码历史；普通 clone 后需初始化子模块，或使用 `git clone --recurse-submodules`。

## 新开发机器

前提：Git、CMake、Visual Studio 2022 C++ 工具链及 Windows SDK，能够访问 GitHub 和依赖下载源。
这是 Windows x64 SDK；不代表整个 GammaRay 的 Qt、CEF 等开发依赖也已经自动配置。

```bat
git submodule update --init -- third_party/freerdp/source
scripts_build\build_cpp_rdp_sdk.bat
scripts_build\build_cpp_rdp_policy.bat
scripts_build\build_cpp_client.bat
```

第一步也由 SDK 构建入口在源码缺失时自动执行。不需要 `D:\dolit\rdp`。
SDK 默认生成于 `.cache/rdp_sdk`，Client 默认读取它；源码、依赖、补丁未变时构建目录可复用。
构建失败会停止，不会自动采用任意系统 FreeRDP。

vcpkg 使用 `vcpkg.json` 固定 baseline，独立 manifest 安装 OpenSSL、OpenH264、libusb、cJSON 和 zlib。
其中 overrides 保留原 SDK 的 OpenSSL 3.2.0#2、OpenH264 2021-03-16#3 和 zlib 1.3，
避免此次依赖管理迁移顺便升级 DLL ABI。这些版本是兼容性基线，不是最新版本或安全性认证；依赖升级单独审查。
默认优先使用 `VCPKG_ROOT`；未设置时自动获取到 `.cache/rdp_vcpkg` 并 bootstrap。
已有 vcpkg 必须与 baseline 一致且 tracked 文件干净，不会自动切换/重置用户的已有 checkout。
依赖装在各自构建目录的 `vcpkg_installed`，不修改 vcpkg classic 的 installed 包集合。
首次构建需要下载和编译；离线构建必须预先准备子模块、vcpkg 源码/工具和下载或二进制缓存。

如需自定义目录，四个位置参数按顺序为：只读源码、构建目录、SDK 安装目录、vcpkg 目录。
在 PowerShell 中不要依靠空字符串跳过 `.bat` 位置参数；使用无参默认入口，或传入全部四个非空路径。
也可直接调用 `scripts/prepare_rdp_sdk.ps1` 的具名参数。

## 源码和补丁边界

- 子模块保持原样；补丁仍位于 `patches/freerdp/0001-mf-output-state.patch`。
- 仅向独立 `.cache/rdp_source_<revision>_<patch-hash>` 副本应用补丁。
- 校验基线、完整 diff、额外文件；不重置意外改动，也不修改外部参考仓库。
- 构建目录默认由源码、补丁、依赖清单摘要区分，避免升级时复用旧配置。
- SDK 包含头文件、导入库、运行 DLL、proxy EXE/导入库、许可证及摘要清单。
- 构建结束自动运行 `scripts/verify_rdp_sdk.ps1`，也可以单独指定 `-SdkDirectory` 复验。
- 节点部署可直接使用 SDK，不再强制要求原 FreeRDP 构建树；显式 `FreeRdpBuild` 保留兼容。
- 旧 demo/proxy probe 脚本仅为外部参考验证，不是产品 SDK 构建入口。

## 升级步骤

1. 在子模块 fetch 并 checkout 待审查的官方提交，将新的 gitlink 随主仓库提交。
2. 审查上游是否修复当前 MF 解码问题；重做或移除补丁，禁止盲目套用。
3. 同步固定版本校验：`scripts/prepare_rdp_sdk.ps1`、`scripts/verify_rdp_sdk.ps1`、Client 的 `rdp_freerdp_sdk.cmake`、
   `proxy_policy/CMakeLists.txt`、`scripts/deploy_rdp_node.ps1`。可搜索旧完整/短 revision 检查遗漏。
4. 如升级传递依赖，同步修改 vcpkg baseline，确认运行 DLL 名称、许可证和 ABI。
5. 在新的独立构建/SDK 目录生成，检查依赖清单、patch/runtime/proxy 摘要，不直接覆盖正在测试的发行包。
6. 验证 decoder、proxy policy、连接、resize、音频、剪贴板、重复连接/退出及错误路径。
7. 通过产品发布脚本发布 Client/Render，核对 dist SHA-256；升级部署需单独安排真实机器回归。

不要提交 `.cache`、私钥、凭证或编译好的 SDK。子模块本身遵循上游许可证，发行物保留依赖许可证。

## 本次验证（2026-09-10）

后续又执行了关闭二进制缓存的干净构建、全新 Client/测试编译与 dist 发布，9/9 自动测试通过；
默认 SDK 已切换且旧产物保留备份。详见 [干净构建验证记录](../../docs/rdp_source_clean_build_validation_20260910.md)。
下方是之前隔离验证的历史记录，其“未覆盖默认 SDK/dist”仅描述当时状态。

- 从固定子模块建立独立补丁副本，manifest 模式安装固定版本依赖并编译 proxy/SDK；完整入口及重复增量构建退出 0。
- SDK 安装到带空格的目录，Windows PowerShell 校验头文件、导入库、许可证、版本和运行文件摘要通过。
- 将整个 SDK 复制到另一个带空格的目录，再校验并编译 `rdp_proxy_policy` 通过，未引用外部 Qt RDP demo。
- 新 proxy 的 `--version` 正常输出 3.31.0 / aa8650b，未建立远端连接。
- 复制已有 `test_client_rdp_decoder.exe` 到隔离目录，仅配上本次生成的 SDK DLL，2/2 测试通过（177 ms）：
  首帧格式变化后取帧与重复重置；只有码流头、没有图像时不读取空表面。这是新 DLL 的运行验证，不是重新编译整个 Client。
- 完整性检查会拒绝没有依赖来源信息的旧 SDK。PowerShell 语法和 `git diff --check` 通过；上游子模块保持干净。
- 本次只验证依赖构建链，没有覆盖现有 `.cache/rdp_sdk`、`build_official/dist` 或 90 的部署；未进行连接/音频等整套远端验收。
- 首次 OpenSSL 构建约 7 分钟；下载和首次构建耗时不属于解码测试耗时，后续可复用 vcpkg 二进制缓存。

本次隔离产物 SHA-256（不是现有 dist 的摘要）：

- `freerdp3.dll`：`CF18AB0E9C8DF557FD8310F9C0F09AE15C6588A68BDF3641848552A0B76B3F42`
- `freerdp-proxy.exe`：`F49E696B61F5D5FE5D3A3234B8BE3939DBEC4ABCA2D60709E4F1103854424C4E`
