# 编译入口

原仓库根目录的 31 个 `build_*.bat` 已统一移到这里，不保留根目录转发脚本。
脚本通过自身路径定位仓库；源码、`build_official`、`build_sdk_*` 和 `output` 的位置不变。
`scripts/` 继续存放公共构建辅助工具、发布和诊断脚本。

在仓库根目录执行：

```bat
scripts_build\build_cpp_client.bat
scripts_build\build_cpp_render.bat
scripts_build\build_cpp_common.bat
scripts_build\build_cpp_tests.bat test_sdk_voice_call
scripts_build\build_cpp_sdk_standalone.bat windows full
scripts_build\build_cpp_android_common.bat px_common
```

也可以在本目录执行对应文件名；从其他目录调用时使用脚本的完整路径。
所有原有参数保持不变。Client/Render 发布仍写入 `build_official\dist` 并核对 SHA-256。

`build_official.bat`、`build_client.bat` 是发布流程，会运行 Web/Rust 等相关步骤并递增版本，
不能用于普通 C++ 增量验证。`build_official_tests.bat` 是批量测试构建；日常优先使用上述按目标入口。

服务端和 Console Web 的独立构建入口也在本目录：`build_px_*_server.bat`、`build_console_web.bat`。
根目录的 `run_official_tests.bat`、版本维护工具和 `build_doc.md` 不属于本次编译入口迁移，位置不变。

移动前的脚本已完整归档至 `backup/build_scripts_relocation_20260908`，归档不参与构建。
