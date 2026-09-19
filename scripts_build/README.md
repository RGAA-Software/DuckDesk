# 编译入口

原仓库根目录的 31 个 `build_*.bat` 已统一移到这里，不保留根目录转发脚本。
脚本通过自身路径定位仓库；每个产品只写入自己的 `build_official/<product>/` 沙箱。
`scripts/` 继续存放公共构建辅助工具、发布和诊断脚本。
完整命令、产物目录和使用方式以 `docs/product_build_and_usage.md` 为唯一权威说明。

在仓库根目录执行：

```bat
scripts_build\build_all_products.bat
scripts_build\build_official.bat cloud_node
scripts_build\build_official.bat client
scripts_build\build_official.bat remote
scripts_build\build_android_product.bat official release
```

上述入口每次都先删除目标产品的旧沙箱，独立升版一次，再构建完整产物。日常 C++ 聚焦验证才使用：

```bat
scripts_build\build_cpp_client.bat client
scripts_build\build_cpp_render.bat cloud_node
scripts_build\build_cpp_common.bat client
scripts_build\build_cpp_tests.bat client test_sdk_voice_call
scripts_build\build_cpp_sdk_standalone.bat windows full
scripts_build\build_cpp_android_common.bat px_common
```

也可以在本目录执行对应文件名；从其他目录调用时使用脚本的完整路径。
产品定向入口必须显式接收 `cloud_node`、`client` 或 `remote`。运行产物分别发布到
`build_official/<product>/dist` 并核对 SHA-256；根 `build_official` 和公共 `dist` 不再是有效构建或运行目录。

`build_cloud_node.bat`、`build_client_product.bat`、`build_remote_product.bat` 是发布流程，会运行 Web/Rust 等相关步骤并递增版本，
不能用于普通 C++ 增量验证。`build_official_tests.bat` 是批量测试构建。

服务端和 Console Web 的独立构建入口也在本目录：`build_px_*_server.bat`、`build_console_web.bat`。
根目录的 `run_official_tests.bat` 和版本维护工具不属于产品发布入口。

移动前的脚本已完整归档至 `backup/build_scripts_relocation_20260908`，归档不参与构建。
