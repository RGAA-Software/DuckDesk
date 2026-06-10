# GammaRayRender 迁移到 src/gr_render 计划

## 目标
将 `GammaRayRender.exe` 的构建目标和相关源码从 `src/GammaRay/src/render/` 迁移到顶层的 `src/gr_render/` 目录下。
`src/GammaRay/deps/` 中的依赖暂时保持不变。

## 当前结构
```
src/
├── CMakeLists.txt
├── gr_render/                     # 已创建，当前为空
├── GammaRay/                      # submodule
│   ├── CMakeLists.txt
│   ├── deps/                      # 依赖保持不动
│   └── src/
│       ├── CMakeLists.txt
│       └── render/                # GammaRayRender 源码在这里
│           ├── CMakeLists.txt
│           ├── app/
│           ├── network/
│           ├── plugins/
│           ├── plugin_interface/
│           └── settings/
```

## 目标结构
```
src/
├── CMakeLists.txt                 # 增加 add_subdirectory(gr_render)
├── gr_render/                     # GammaRayRender 源码迁移到这里
│   ├── CMakeLists.txt
│   ├── app/
│   ├── network/
│   ├── plugins/
│   ├── plugin_interface/
│   └── settings/
└── GammaRay/                      # submodule
    ├── CMakeLists.txt
    ├── deps/                      # 依赖保持不动
    └── src/
        └── CMakeLists.txt         # 移除 add_subdirectory(render)
```

## 实施步骤

### 步骤 1：物理迁移源码
- 将 `src/GammaRay/src/render/` 下的所有内容复制/移动到 `src/gr_render/`
- 保留目录结构：`app/`, `network/`, `plugins/`, `plugin_interface/`, `settings/`

### 步骤 2：修改 CMake 入口
1. `src/GammaRay/src/CMakeLists.txt`
   - 删除 `add_subdirectory(render)`

2. `src/CMakeLists.txt`
   - 在 `add_subdirectory(GammaRay)` **之后**添加 `add_subdirectory(gr_render)`
   - 必须放在 GammaRay 之后，因为 gr_render 依赖 GammaRay/deps 中的 target

### 步骤 3：修改 src/gr_render/CMakeLists.txt
1. `project(GammaRayRender ...)` 保持不变（target/exe 名称不变）
2. 子目录 `add_subdirectory(...)` 保持相对路径，不需要改
3. `include_directories(${GR_PROJECT_PATH}/deps/...)` 保持使用 `${GR_PROJECT_PATH}`
   - 该变量在 `src/GammaRay/CMakeLists.txt` 中定义，指向 `src/GammaRay`
   - 由于 gr_render 在 GammaRay 之后添加，变量已经可用
4. `add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/app)` 中 `CMAKE_CURRENT_SOURCE_DIR` 自动变为 `src/gr_render`，正确
5. `configure_file` 中的源路径基于 `CMAKE_CURRENT_SOURCE_DIR`，自动正确
6. POST_BUILD copy 命令的目标路径需要检查：
   - 当前 copy 到 `${GR_PROJECT_BINARY_PATH}`（即 `build/src/GammaRay`）
   - 迁移后是否需要改为 `${CMAKE_BINARY_DIR}` 或保持原样？
   - **决策**：暂时保持 copy 到 `GR_PROJECT_BINARY_PATH`（build/src/GammaRay），与其他 exe 保持同目录

### 步骤 4：检查源码中的 include 路径
- render 内部代码使用的相对路径 `#include "..."` 一般不需要改
- 如果存在 `#include "src/render/..."` 或 `#include "render/..."` 形式，需要检查是否需要调整
- 其他模块（如 render_panel）引用 render 头文件的地方需要检查

### 步骤 5：验证编译
1. 删除旧 build 目录 `build_official/`
2. 运行 `build_official.bat`
3. 确认 `GammaRayRender.exe` 正常编译并输出到预期目录

## 风险和回滚
- 风险：CMake 路径问题、include 路径断裂、deps 找不到
- 回滚：还原 git 修改，恢复 `src/GammaRay/src/render/` 和 CMake 配置
