# Legacy C++ Guard

这个目录保存原始 Qt/C++ 版 `GammaRayGuard` 实现，当前用于行为对照和迁移参考。

当前主构建链已经切到 Rust：

- 活跃产物：`rust/gr_guard` -> `GammaRayGuard.exe`
- 构建接线：`src/GammaRay/CMakeLists.txt`

这个目录当前不再直接参与 `GammaRayGuard.exe` 的正式构建。
