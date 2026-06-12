# Protobuf 从源码迁移到 vcpkg 方案

> 状态：待实施  
> 目标：把 `src/gr_deps/tc_3rdparty/protobuf` 源码构建方式替换为 vcpkg 管理，减少子模块体积和编译时间。  
> 当前源码版本：`5.29.3`（即 protobuf 29.3）  
> 已安装 vcpkg 版本：`6.33.4#2`（triplet: `x64-windows-static-release`）  
> 代码修改状态：**已完成，待 CMake 配置验证**

---

## 1. 当前现状

### 1.1 protobuf 源码内嵌在子模块

- **位置**：`src/gr_deps/tc_3rdparty/protobuf`
- **依赖**：同目录 `abseil-cpp` 子模块专门用于构建 protobuf
- **构建开关**：`config_premium.cmake` 中 `set(Inner_Protobuf_ENABLED ON)`
- **触发构建**：`src/gr_deps/tc_3rdparty/CMakeLists.txt` 根据该开关执行：
  ```cmake
  add_subdirectory(abseil-cpp)
  add_subdirectory(protobuf)
  ```

### 1.2 根 CMakeLists.txt 强制指向源码产物

```cmake
set(Protobuf_SRC_ROOT_FOLDER ${GR_PROJECT_PATH}/tc_3rdparty/protobuf)
if(CMAKE_BUILD_TYPE MATCHES "Release" OR CMAKE_BUILD_TYPE MATCHES "RelWithDebInfo")
    set(Protobuf_LIBRARIES ${GR_PROJECT_BINARY_PATH}/tc_3rdparty/protobuf/cmake/libprotobuf.lib)
endif()
find_package(Protobuf REQUIRED)
```

### 1.3 项目里混用两套链接目标名

| 平台分支 | 使用的目标名 | 说明 |
|---------|------------|------|
| Android | `protobuf::libprotobuf` / `protobuf::libprotoc` / `protobuf::libprotobuf-lite` | vcpkg/CMake 标准命名 |
| Windows / Linux / macOS | `libprotobuf` | 源码构建产生的 target 名 |

### 1.4 Rust 端硬编码 protoc

文件：`rust_base/protocol/build.rs`

```rust
let protoc_path = manifest_dir.join("../../tools/protoc.exe");
std::env::set_var("PROTOC", &protoc_path);
```

### 1.5 vcpkg 已经在使用

- `env_premium.cmake` 已设置：
  ```cmake
  set(VCPKG_ROOT C:/source/vcpkg)
  ```
- 现有 vcpkg 依赖：`cpr`、`mimalloc`、`OpenSSL`、`FFTW3`、`SDL2`、`gflags`、`GTest`、`glm`、`libvpx`、`breakpad` 等
- 说明项目当前是 **vcpkg classic 模式**，不是 manifest 模式

---

## 2. 需要修改的文件清单

### 2.1 开关与根配置（已修改）

| 文件 | 修改内容 |
|------|---------|
| `config_premium.cmake` | `set(Inner_Protobuf_ENABLED OFF)` |
| `CMakeLists.txt` | 删除 `Protobuf_SRC_ROOT_FOLDER` 和 `Protobuf_LIBRARIES` 的强制设置；改为 `find_package(protobuf CONFIG REQUIRED)` |
| `src/gr_deps/tc_3rdparty/CMakeLists.txt` | 删除 `Inner_Protobuf_ENABLED` 相关的 `add_subdirectory(abseil-cpp)` 和 `add_subdirectory(protobuf)` 块 |
| `env_premium.cmake` | 增加 `CMAKE_VCPKG_TARGET_TRIPLET` 默认值为 `x64-windows-static-release`，与已安装的 protobuf triplet 对齐 |

### 2.2 链接目标名替换

以下文件中的 `libprotobuf` 需要替换为 `protobuf::libprotobuf`。如果用到 `libprotoc` / `libprotobuf-lite`，对应替换为 `protobuf::libprotoc` / `protobuf::libprotobuf-lite`。

| 文件 | 当前写法示例 | 建议替换后 |
|------|------------|-----------|
| `src/gr_deps/tc_message_new/CMakeLists.txt` | `target_link_libraries(tc_message libprotobuf)` | `target_link_libraries(tc_message protobuf::libprotobuf)` |
| `src/gr_deps/tc_relay_client/CMakeLists.txt` | `target_link_libraries(${PROJECT_NAME} ... libprotobuf)` | `protobuf::libprotobuf` |
| `src/gr_deps/tc_client_sdk_new/CMakeLists.txt` | `target_link_libraries(tc_sdk PRIVATE libprotobuf ...)` | `protobuf::libprotobuf` |
| `src/gr_render/plugin_interface/CMakeLists.txt` | `target_link_libraries(tc_net_plugin PRIVATE ... libprotobuf ...)` | `protobuf::libprotobuf` |
| `src/gr_render/plugins/net_ws/CMakeLists.txt` | `target_link_libraries(${PROJECT_NAME} PRIVATE ... libprotobuf tc_message)` | `protobuf::libprotobuf` |
| `src/gr_render/plugins/net_relay/CMakeLists.txt` | `target_link_libraries(${PROJECT_NAME} PRIVATE ... tc_relay_message libprotobuf)` | `protobuf::libprotobuf` |
| `src/gr_render/plugins/file_transfer/CMakeLists.txt` | `libprotobuf tc_message ...` | `protobuf::libprotobuf tc_message ...` |
| `src/gr_render/hook_capture/win/hk_obs/CMakeLists.txt` | `... tc_message libprotobuf dxgi ...` | `... tc_message protobuf::libprotobuf dxgi ...` |
| `src/gr_render/hook_capture/CMakeLists.txt` | `target_link_libraries(tc_capture_new tc_message libprotobuf ...)` | `protobuf::libprotobuf` |
| `src/gr_render/CMakeLists.txt` | `target_link_libraries(main app_manager libprotobuf tc_message ...)` | `protobuf::libprotobuf` |
| `src/gr_render/app/CMakeLists.txt` | `target_link_libraries(app_manager tc_message libprotobuf)` | `protobuf::libprotobuf` |

> 注：部分文件里 `libprotobuf` 在 Windows 分支，Android 分支已经是 `protobuf::libprotobuf`，注意不要重复改。

### 2.3 proto 编译逻辑（基本无需改动）

以下文件里的 `protobuf_generate_cpp(...)` 调用可以保留，因为 `find_package(Protobuf REQUIRED)` 会提供该函数：

- `src/gr_deps/tc_server_protocol/CMakeLists.txt`
- `src/gr_deps/tc_message_new/CMakeLists.txt`
- `rust_base/protocol/tc_protocol/CMakeLists.txt`

### 2.4 Rust 端 protoc 路径（已修改）

**文件**：`rust_base/protocol/build.rs`

已改为从 vcpkg 获取 protoc，优先使用环境变量 `PROTOC`，其次 `VCPKG_ROOT` / `VCPKG_DEFAULT_TRIPLET`，默认路径为：

```
C:/source/vcpkg/installed/x64-windows-static-release/tools/protobuf/protoc.exe
```

如果 protoc 不存在会 panic 并提示安装命令。

### 2.5 文档更新

| 文件 | 更新内容 |
|------|---------|
| `docs/gammaray/How_to_build.md` | 在 vcpkg install 列表中增加 `protobuf:x64-windows` |
| `docs/codebase-analysis/01-build-and-layout.md` | 依赖列表中把 protobuf 从“内置三方源码”改为“vcpkg 管理” |

---

## 3. 当前状态与下一步

### 3.1 代码改动已完成

- `config_premium.cmake`：`Inner_Protobuf_ENABLED OFF`
- `CMakeLists.txt`：改为 `find_package(protobuf CONFIG REQUIRED)`
- `src/gr_deps/tc_3rdparty/CMakeLists.txt`：移除源码 protobuf/abseil 构建
- `env_premium.cmake`：默认 triplet 对齐为 `x64-windows-static-release`
- 10+ 个 CMakeLists.txt：`libprotobuf` → `protobuf::libprotobuf`
- `rust_base/protocol/build.rs`：从 vcpkg 获取 protoc
- `docs/gammaray/How_to_build.md`：增加 protobuf 安装命令

### 3.2 验证时发现的阻塞

CMake configure 已能正确识别 triplet 为 `x64-windows-static-release`，但该项目其他 vcpkg 依赖（SDL2、gflags、GTest、glm、libvpx、cpr、mimalloc 等）目前只安装在 `x64-windows`，导致 configure 在查找 SDL2 时失败。

### 3.3 用户需要先行完成的步骤

### 3.1 安装 vcpkg protobuf（已完成）

已安装：

```bash
vcpkg install protobuf:x64-windows-static-release
```

产物：
- 静态库：`%VCPKG_ROOT%/installed/x64-windows-static-release/lib/libprotobuf.lib`
- 工具：`%VCPKG_ROOT%/installed/x64-windows-static-release/tools/protobuf/protoc.exe`
- 依赖 `abseil`、`utf8-range` 已自动安装

### 3.2 确认 protoc 位置

安装完成后，protoc 通常位于：

```
%VCPKG_ROOT%/installed/x64-windows/tools/protobuf/protoc.exe
```

请确认该文件存在，后续 Rust `build.rs` 会依赖它。

### 3.4 可选：清理旧构建产物

建议删除以下目录后再进行 CMake configure：

- `build_official/`
- `out/build/`
- 其他 CMake 构建输出目录

> 注意：旧 `build_official/CMakeCache.txt` 中 `VCPKG_TARGET_TRIPLET=x64-windows`，如果直接复用旧目录 configure，可能仍会从旧 triplet 查找包。建议清缓存或删目录后重新 configure。

---

## 4. 迁移后的预期效果

- `src/gr_deps/tc_3rdparty/protobuf` 和 `abseil-cpp` 不再参与构建
- protobuf 库和 protoc 全部来自 vcpkg
- 构建时间缩短（不再编译 protobuf + abseil 源码）
- 子模块体积可以减小（后续可考虑从子模块移除 protobuf 和 abseil-cpp）

---

## 5. 主要风险与注意事项

### 5.1 CRT 匹配

vcpkg 默认 `x64-windows` triplet 使用 `/MD`（动态 CRT）。如果项目使用 `/MT`，需要：

- 使用 `x64-windows-static` triplet，或
- 自定义 triplet 强制 `/MT`

否则会出现 `LNK2038` runtime library mismatch。

### 5.2 版本一致性

当前源码 protobuf 版本为 `5.29.3`。vcpkg 安装时请尽量选择相同或相近版本，避免生成的 pb 代码 ABI 不兼容。

### 5.3 C++ 与 Rust 使用同一 protoc

C++ 端 `protobuf_generate_cpp` 和 Rust 端 `tonic_prost_build` 必须使用同一个 protoc。建议两者都指向 vcpkg 安装的 `protoc.exe`。

### 5.4 已提交的 Rust 生成文件

以下文件是 protoc 生成的，已提交到仓库：

- `rust_base/protocol/src/grpc_relay.rs`
- `rust_base/protocol/src/relay.rs`
- `rust_base/protocol/src/spvr_client.rs`
- `rust_base/protocol/src/spvr_panel.rs`
- `rust_base/protocol/src/spvr_relay.rs`

迁移后如果 protoc 版本变化，这些文件会在 cargo build 时重新生成，建议提交更新后的版本。

### 5.5 子模块清理（后续可选）

迁移稳定后，可以考虑：

1. 从 `tc_3rdparty` 子模块中删除 `protobuf/` 和 `abseil-cpp/`
2. 更新 `.gitmodules`
3. 更新 `docs/codebase-analysis/01-build-and-layout.md` 中的目录说明

这一步不是迁移的必需步骤，但可以减少仓库体积。

---

## 6. 迁移后验证清单

- [x] `vcpkg install protobuf:x64-windows-static-release` 成功
- [x] `%VCPKG_ROOT%/installed/x64-windows-static-release/tools/protobuf/protoc.exe` 存在
- [ ] 其他 vcpkg 依赖也安装在 `x64-windows-static-release`
- [ ] CMake configure 不报错，能找到 `protobuf::libprotobuf`
- [ ] `tc_message_new`、`tc_server_protocol` 等 proto 库编译通过
- [ ] `gr_render`、`gr_panel`、`tc_client_sdk_new` 等链接 protobuf 的目标编译通过
- [ ] Rust `protocol` crate 能正常编译并重新生成 `.rs` 文件
- [ ] 整体项目 `cargo check --workspace` / CMake build 通过

---

## 7. 实施顺序建议

1. 用户先安装 vcpkg protobuf（本方案第 3 步）
2. 修改 `config_premium.cmake` 关闭 `Inner_Protobuf_ENABLED`
3. 修改根 `CMakeLists.txt` 去掉源码 protobuf 强制路径
4. 批量替换 `libprotobuf` → `protobuf::libprotobuf`
5. 修改 `rust_base/protocol/build.rs` 指向 vcpkg protoc
6. 更新文档
7. 清理构建目录，重新 configure + build
8. 验证通过后，再考虑是否从子模块删除 protobuf/abseil-cpp
