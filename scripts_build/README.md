# 编译入口

原仓库根目录的 31 个 `build_*.bat` 已统一移到这里，不保留根目录转发脚本。
脚本通过自身路径定位仓库；每个产品只写入自己的 `build_official/<product>/` 沙箱。聚焦开发使用该目录下的
`cmake/dist`；发布构建在同一次升版事务中生成 `official` 与 `customer` 两个子沙箱。
`scripts/` 继续存放公共构建辅助工具、发布和诊断脚本。
完整命令、产物目录和使用方式以 `docs/product_build_and_usage.md` 为唯一权威说明。

在仓库根目录执行：

```bat
scripts_build\build_all_products.bat
scripts_build\build_official.bat cloud_node
scripts_build\build_official.bat client
scripts_build\build_official.bat remote
scripts_build\build_android_product.bat release
```

上述 Windows 入口每次都先完成两种发行的身份材料预检，再删除目标产品旧沙箱、独立升版一次，并构建 Official/Customer 两套完整产物。
缺少 approved trust store、最低水位、Official deployment UUID 或 HTTPS origin 时，不清理、不升版。日常 C++ 聚焦验证才使用：

```bat
scripts_build\build_cpp_client.bat client
scripts_build\build_cpp_render.bat cloud_node
scripts_build\build_cpp_common.bat client
scripts_build\build_cpp_tests.bat client test_sdk_voice_call
scripts_build\build_cpp_sdk_standalone.bat windows full
scripts_build\build_cpp_android_common.bat px_common
```

也可以在本目录执行对应文件名；从其他目录调用时使用脚本的完整路径。
产品定向入口必须显式接收 `cloud_node`、`client` 或 `remote`。聚焦运行产物发布到
`build_official/<product>/dist` 并核对 SHA-256；发布运行产物位于 `build_official/<product>/<official|customer>/dist`。
根 `build_official` 和公共 `dist` 不再是有效构建或运行目录。

`build_cloud_node.bat`、`build_client_product.bat`、`build_remote_product.bat` 是发布流程，会运行 Web/Rust 等相关步骤并递增版本，
不能用于普通 C++ 增量验证。`build_official_tests.bat` 是批量测试构建。

服务端和 Console Web 的独立构建入口也在本目录：`build_px_*_server.bat`、`build_console_web.bat`。
根目录的 `run_official_tests.bat` 和版本维护工具不属于产品发布入口。

Web Client 单独修改时，先在 `web/px_web_client` 执行 `npm.cmd run test` 和 `npm.cmd run build`，再运行
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts_build/publish_web_client_development.ps1 -Product all`。该入口只同步 Cloud Node/Remote
各自的 development `web` 与 `dist/web_client`，逐文件核对 SHA-256、刷新 manifest 并执行完整 dist 验证；它不升版、不构建 C++/Rust，
也不能生成 Official/Customer/OEM 安装包。

移动前的脚本已完整归档至 `backup/build_scripts_relocation_20260908`，归档不参与构建。
