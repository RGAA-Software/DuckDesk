# Pixels 自定义 CEF Windows 构建计划（2026-09-15）

## 1. 目标与边界

为 Pixels WebView 构建一套可复现的 Windows x64 CEF Release 发行包，同时启用 Chromium 的硬件媒体解码路径和 FFmpeg 软件解码兜底。
本计划的构建、替换和安装包集成现已完成。`third_party/cef/manifest.json` 固定 Pixels 发行包及 SHA-256，Cloud Node 构建、增量发布和正式安装包均使用同一套 runtime；2026-09-16 已由用户确认视频播放正常。

## 2. 固定版本

- CEF：`151.3.23+gd211df0+chromium-151.0.7922.170`
- CEF 分支：`7922`
- CEF 提交：`d211df08c47ea7284a58f0106ca7a80e716f758c`
- Chromium：由上述 CEF 提交中的 `CHROMIUM_BUILD_COMPATIBILITY.txt` 固定，不单独跟随分支最新提交
- depot_tools：`94e89b10b92cc9d6e58fc8d1b6474b7d29e8a114`（由该 CEF/Chromium compatibility 流程选定）
- 平台：Windows x64
- 配置：Release

固定到项目当前使用的 CEF 提交，可以把后续替换的变量限制为编解码能力和构建产物，不同时引入 Chromium/CEF API 升级。

## 3. 本机目录

- 工作根目录：`D:\GoCloud\cef_151`
- depot_tools：`D:\GoCloud\depot_tools`
- Chromium/CEF 工作区：`D:\GoCloud\cef_151\chromium_git`
- 构建日志：`D:\GoCloud\cef_151\logs`
- 发行包：由 CEF automate 脚本生成在工作区的 `chromium\src\cef\binary_distrib` 下

`D:\GoCloud\GammaRayPremium\third_party\cef` 已切换到 Pixels 自定义发行包；二进制仍不进入 Git，由 manifest 下载并校验。

## 4. 网络与工具链

同步时使用本机 HTTP 代理：

```text
HTTP_PROXY=http://127.0.0.1:7890
HTTPS_PROXY=http://127.0.0.1:7890
```

使用 depot_tools 自带的 Python、Git 包装和 Chromium clang 工具链。Visual Studio 使用 Chromium 当前分支支持的已安装版本；最终采用的 VS、SDK、Python、GN、Ninja 和 clang 版本写入构建记录。
构建进程设置 `DEPOT_TOOLS_UPDATE=0` 并将 depot_tools 固定到上述提交，避免自更新后的脚本与已创建的 vpython 环境发生版本错配。
构建进程移除继承的 `GOROOT`。Dawn 使用 checkout 内的 Go 工具链；保留系统 `GOROOT` 会让 Dawn 的 Go 1.25.0 错误加载系统 Go 1.20.4
标准库。该处理只影响构建子进程，不修改机器级或用户级 Go 配置。
构建进程设置 `PYTHONUTF8=1` 和 `PYTHONIOENCODING=utf-8`，避免中文 Windows 的 GBK 默认代码页在 runhooks 子进程输出中触发解码失败；
该设置不修改系统区域配置。
`GN_OUT_CONFIGS=Release_GN_x64` 限制 CEF project generation 只生成 Release 工程；`--no-debug-build` 限制 automate 不执行 Debug 编译。
若首次 runhooks 在生成 LASTCHANGE 之前中断，脚本使用 Chromium DEPS 中同一条官方 `lastchange.py -o build/util/LASTCHANGE` hook 命令补齐
`LASTCHANGE` 和 `LASTCHANGE.committime`，不伪造版本或时间戳。若 `gpu/webgpu/DAWN_VERSION` 仍缺失，脚本以单任务方式重新执行完整
`gclient runhooks`，补齐 Dawn、Skia、GPU 列表等由 DEPS hooks 生成的版本文件。
首次补跑 hooks 后构建后端会从未完整初始化的 Ninja 状态切换到该版本配置的 Siso；若 autoninja 明确要求清理，只对
`out/Release_GN_x64` 执行一次 `gn clean`，不清理源码或依赖。

Chromium 151 的 LiteRT DEPS URL 指向 googlesource 的 GitHub 镜像，但该镜像的 Git LFS batch 接口会返回 HTTP 405。构建脚本使用进程级
`GIT_CONFIG_*` 将这一条精确 URL 重写到 LiteRT 的 GitHub 原仓库，以获取同一提交引用的 LFS 对象。该设置不写入用户全局 Git 配置，
也不改变 LiteRT 的固定提交。

本机代理对 googlesource 的 Chromium 主仓库大 pack 传输会中途断开并返回 `curl 56`。构建脚本将主仓库的精确 URL 在进程内重写到
Chromium 官方 GitHub 镜像；镜像中的 `151.0.7922.170` 标签指向 `fa19f0c9d2e340c1c5429d5fff181b6c2d51bbae`。其余 Chromium DEPS
仍使用各自原始地址，版本仍由 CEF compatibility 文件固定。

## 5. 媒体构建策略

基础 GN 参数：

```gn
is_official_build=true
is_component_build=false
proprietary_codecs=true
ffmpeg_branding="Chrome"
chrome_pgo_phase=0
symbol_level=1
use_thin_lto=false
```

- `proprietary_codecs=true` 与 `ffmpeg_branding="Chrome"` 用于纳入 H.264/AAC 等 FFmpeg 软件编解码支持。
- VP8、VP9、AV1、Opus 等沿用 Chromium 原生媒体能力。
- 硬件解码由 Chromium 根据 Windows、GPU、驱动和黑名单动态选择。构建和运行时不得添加禁用 GPU、D3D11、视频硬解或 GPU 合成的参数。
- checkout 完成后必须使用该提交实际提供的 `gn args --list` 核对 HEVC、平台媒体和 FFmpeg 相关参数；只设置该版本真实存在的参数，不添加猜测或已废弃的 GN 参数。
- HEVC/H.265 的最终硬件能力受 Windows 组件、GPU 和驱动约束；验收结果必须区分“编译启用”“软件可解”和“当前机器硬件可解”。
- Widevine/CDM 不属于普通编解码构建，本阶段不打包 DRM 模块。
- Chromium 151 已删除 `enable_nacl` GN 参数，因此不设置该过期参数。
- PGO 是编译期性能优化，不决定媒体格式或软硬件解码能力。本机自定义构建设置 `chrome_pgo_phase=0`，避免依赖官方 PGO profile；
  后续性能对比若证明有必要，再独立构建 PGO 版本。

## 6. 执行阶段

1. 检查代理、磁盘空间和构建工具。
2. 拉取 depot_tools。
3. 拉取 CEF `7922` 分支并固定到 `d211df08c47ea7284a58f0106ca7a80e716f758c`。
4. 从固定提交读取 Chromium 兼容版本。
5. 运行 CEF automate，以不保留 Chromium 无关 Git 历史的方式同步固定版本源码、依赖和工具链资源；中断后使用
   `--force-update` 修复并补齐同一 checkout，不执行破坏性 `force-clean`。若失败 clone 只留下缺少 `chrome\VERSION` 的不完整 `src`，
   脚本在验证其绝对路径位于本次工作区后删除该占位目录，再由 gclient 重建。
6. 核对该版本 GN 参数，记录最终生效值。
7. 仅构建 x64 Release，并生成不重复打包压缩档、文档和符号目录的标准 CEF 发行目录。
8. 保存版本、参数、日志、发行包清单和 SHA-256。
9. 使用发行包中的示例程序及编解码测试页完成独立验收。
10. 验收通过后，再设计 GammaRayPremium 的替换、回滚与打包方案。

## 7. 验收矩阵

- CEF/Chromium 版本和提交与固定值一致。
- Release 发行包完整，可运行 `cefsimple` 或等价示例。
- MP4 H.264 + AAC 正常播放且音画同步。
- 测试时禁用 GPU，确认 H.264/AAC 软件解码兜底可用。
- 恢复 GPU，确认受支持的视频进入硬件解码路径，而不是仅确认页面能够播放。
- WebM VP8/VP9 + Opus 正常播放。
- AV1 软件路径正常；本机支持时验证 AV1 硬件路径。
- HLS、MSE、自动播放和有声播放正常。
- HEVC 分别记录容器识别、解码结果和实际软/硬件解码器。
- GPU 进程异常或硬解不可用时能回退软件解码，不导致浏览器进程退出。
- 输出目录中的文件清单和 SHA-256 已保存。

## 8. 风险与资源控制

- Chromium/CEF 同步和 Release 构建通常需要很大的磁盘空间。本次开始前 D 盘可用空间约 174 GiB，属于偏紧配置；使用 automate 官方的
  `--no-chromium-history` 只省略与固定版本无关的 Git 历史，不改变源码和产物。150 GiB 作为告警线而不是增量构建硬门槛；只生成 x64 Release，
  每个阶段都检查剩余空间。实际空间不足时停止在可恢复点，不删除项目或用户数据。
- Chromium 源码和依赖体积大，代理中断后使用 depot_tools/automate 的增量同步继续，不重新创建第二套 checkout。
- H.264、AAC、HEVC 等格式可能涉及专利和分发许可。技术验收通过不代表获得商业分发授权，正式发布前需单独完成许可审查。
- CEF 已进入 Cloud Node 正式构建；更新 manifest、归档或默认目录时必须同步校验下载、构建树、dist 和安装包清单。

## 9. 后续替换规划输入

当前替换已同时覆盖构建链接输入、`build_official\cloud_node\dist` runtime 和 Cloud Node 安装包。后续升级仍必须记录发行包布局、`libcef.dll` 和资源文件版本、sandbox 依赖、locales、SwiftShader/ANGLE 文件、功能验证、产物体积以及 SHA-256，并逐项校验哈希。
