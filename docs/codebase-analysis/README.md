# GammaRayPremium 代码分析总览

这组文档基于当前仓库 `GammaRayPremium` 的实际构建图、目录结构和关键入口代码整理，目标是把“这套系统现在是怎么组织、怎么启动、怎么协作”的主线讲清楚。

## 阅读建议

1. 先看 [01-build-and-layout.md](./01-build-and-layout.md)，理解仓库边界和构建产物。
2. 再看 [02-runtime-architecture.md](./02-runtime-architecture.md)，建立进程间协作的整体图。
3. 然后按角色深入：
   - [03-panel-process.md](./03-panel-process.md)
   - [04-render-process-and-plugins.md](./04-render-process-and-plugins.md)
   - [05-client-process-and-plugins.md](./05-client-process-and-plugins.md)
   - [06-web-client.md](./06-web-client.md)
4. 如果要理解本地守护与服务层，再看 [08-service-and-rust-migration.md](./08-service-and-rust-migration.md)。
5. 如果要理解 Guard 守护进程与 Rust 重写，再看 [09-guard-and-rust-migration.md](./09-guard-and-rust-migration.md)。
6. 最后用 [07-module-inventory.md](./07-module-inventory.md) 当索引，按目录回查模块职责。

## 这次分析的边界

本次重点分析“当前主产品路径”：

- 顶层 Premium 包装工程
- `src/GammaRay` 主程序及其子模块
- `src/render_plugins` 渲染端插件
- `src/client_plugins` 客户端插件
- `src/gr_web_client` 浏览器端客户端
- `src/skins`、`src/panel_companion`、`src/hook_capture`、`src/anti_hooking`

以下内容只做边界说明，不做逐文件展开：

- `src/GammaRay/deps/**`：供应商依赖、三方源码、预编译资源
- `src/backup/**`：备份/旧实现
- 顶层 `build_*`、`out`、`cmake-build-*`：构建输出目录
- `src/GammaRayServer`：当前顶层构建已注释掉 `add_subdirectory(GammaRayServer)`，同时工作区存在删除痕迹，说明它不是当前主交付链路的一部分

## 一句话理解这套系统

这不是单体桌面程序，而是一套“控制面板 + 渲染进程 + 远端客户端 + 浏览器客户端 + 动态插件”的远程串流/远程控制平台。顶层 Premium 工程负责把这些能力重新装配成正式版本，并追加官方皮肤、增强插件、反 Hook 保护和打包资源。

