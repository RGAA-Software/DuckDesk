# C++ 按需编译规则

`build_official.bat` 只用于发版前整体编译。它会递增产品版本、安装并构建 Web 依赖、
编译 Rust workspace、构建服务端并重建完整 `build_official\dist`，日常 C++ 开发不得调用。

日常入口：

| 变更范围 | 命令 | 行为 |
| --- | --- | --- |
| Render 主程序 | `build_cpp_render.bat` | 编译 `px_render`，发布到 dist 并校验 SHA-256 |
| Windows Client | `build_cpp_client.bat` | 编译 Client、RTC 和三个客户端插件，发布并校验 |
| Panel | `build_cpp_panel.bat` | 编译 Panel/皮肤，发布并校验 |
| 公共静态库 | `build_cpp_common.bat` | 只编译 `px_common_new` |
| 单个 Render 插件 | `build_cpp_render_plugin.bat net_rtc` | 只编译指定插件并发布对应 DLL |
| 全部 Render 插件 | `build_cpp_render_plugins.bat` | 只编译 Render 插件集合并发布 |
| C++ SDK | `build_cpp_sdk.bat` | 编译 SDK/客户端网络依赖，不运行 Rust/Web |
| 当前异步相关测试 | `build_cpp_tests.bat` | 编译三个默认测试目标 |
| 任意测试/目标 | `build_cpp_tests.bat test_name` 或 `scripts\build_cpp_target.bat target` | 只编译列出的 CMake target |

所有入口优先复用现有 `build_official` Ninja 树，但不会调用 `build_official.bat`；构建树
不存在时只执行一次 CMake 配置，不运行 Rust、npm、Web 或版本递增。可用环境变量
`CPP_BUILD_DIR` 切换构建目录、`CPP_BUILD_JOBS` 调整并行度。主程序和插件快捷入口会按
`AGENTS.md` 要求同步到 `build_official\dist`；目标文件被占用时只停止对应的
`px_render`、`px_client` 或 `px_panel`，随后重新复制并比较构建产物与 dist 的 SHA-256。
