# libyuv / leveldb 从源码迁移到 vcpkg 方案

> 状态：**已完成并验证通过**  
> 目标：把 `src/gr_deps/tc_3rdparty/libyuv` 和 `src/gr_deps/tc_3rdparty/leveldb` 的源码构建方式替换为 vcpkg 管理，减少子模块体积并统一依赖来源。  
> 验证构建：`build_official.bat` 于 `2026-06-13` 成功完成（`[88/88]`，`Done. Dist folder: build_official/dist`）。

---

## 1. 当前现状

### 1.1 源码内嵌在 `tc_3rdparty` 子模块中

- **位置**：`src/gr_deps/tc_3rdparty/libyuv`、`src/gr_deps/tc_3rdparty/leveldb`
- **构建触发**：`src/gr_deps/tc_3rdparty/CMakeLists.txt` 中：
  ```cmake
  add_subdirectory(libyuv)
  add_subdirectory(leveldb)
  ```
- **leveldb 构建选项**：
  ```cmake
  set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
  ```

### 1.2 项目中的使用点

| 依赖 | 使用位置 | 原用法 |
|------|---------|--------|
| `libyuv` | `src/gr_render/plugins/net_rtc/desktop_capture.cpp` | `"third_party/libyuv/include/libyuv.h"` |
| `leveldb` | `src/gr_deps/tc_common_new/CMakeLists.txt` | `target_link_libraries(... leveldb ...)` |
| `leveldb` | `src/gr_render/CMakeLists.txt` | `target_link_libraries(... leveldb ...)` |
| `leveldb` | `src/gr_panel/CMakeLists.txt` | `include_directories(.../tc_3rdparty/leveldb/include)` |

---

## 2. 修改清单

### 2.1 根 `CMakeLists.txt`

新增 vcpkg 查找：

```cmake
find_package(libyuv CONFIG REQUIRED)
find_package(leveldb CONFIG REQUIRED)
```

### 2.2 `src/gr_deps/tc_3rdparty/CMakeLists.txt`

移除源码构建：

```cmake
#add_subdirectory(libyuv)
#add_subdirectory(leveldb)
```

同时删除 `LEVELDB_BUILD_TESTS` / `LEVELDB_BUILD_BENCHMARKS` 的强制设置。

### 2.3 链接目标与头文件路径

| 文件 | 修改内容 |
|------|---------|
| `src/gr_deps/tc_common_new/CMakeLists.txt` | `leveldb` → `leveldb::leveldb` |
| `src/gr_render/CMakeLists.txt` | `leveldb` → `leveldb::leveldb` |
| `src/gr_panel/CMakeLists.txt` | 删除/注释 `tc_3rdparty/leveldb/include` 手动 include 目录 |
| `src/gr_render/plugins/net_rtc/desktop_capture.cpp` | `"third_party/libyuv/include/libyuv.h"` → `<libyuv.h>` |

### 2.4 清理子模块目录

从 `tc_3rdparty` 子模块工作树中删除：

- `src/gr_deps/tc_3rdparty/libyuv`
- `src/gr_deps/tc_3rdparty/leveldb`

> 注：`tc_3rdparty/webrtc/include/third_party/libyuv` 中的 WebRTC 私有 libyuv 头文件保留不动，仅用于兼容现有 WebRTC 预编译头依赖。

---

## 3. vcpkg 安装命令

在 `C:/source/vcpkg`（由 `env_premium.cmake` 指定）中使用 triplet `x64-windows-static-release`：

```bat
vcpkg install libyuv:x64-windows-static-release
vcpkg install leveldb:x64-windows-static-release
```

已确认产物：

- `libyuv.lib`
- `leveldb.lib`
- 对应 CMake 配置：`libyuv-config.cmake`、`leveldbConfig.cmake`

目标名：

- `libyuv` → `yuv`
- `leveldb` → `leveldb::leveldb`

---

## 4. 验证结果

执行：

```bat
build_official.bat
```

结果：

- 配置阶段成功找到 `libyuv` 和 `leveldb` 的 vcpkg 包。
- 编译/链接阶段无新增错误。
- 最终 `[88/88]` 完成，`build_official/dist` 正常生成。

---

## 5. 注意事项

1. **CRT 一致性**：项目使用 `/MT`（静态 CRT），必须使用 `x64-windows-static-release` triplet；使用默认 `x64-windows` 会导致 `LNK2038` runtime library mismatch。
2. **SIMD 行为**：vcpkg 安装的 `libyuv` 在 MSVC 下默认不启用 SIMD 加速，与原先 bundled MSVC 构建行为一致。
3. **include 路径**：vcpkg 通过 toolchain 自动注入 include 目录，无需再手动添加 `tc_3rdparty/...`。
4. **下游目标继承**：`tc_common_new` 以 `PUBLIC` 链接 `leveldb::leveldb`，因此依赖 `tc_common_new` 的目标无需重复链接 leveldb。
