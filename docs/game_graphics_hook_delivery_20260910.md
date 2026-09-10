# Windows Game Hook 四种图形 API 实施与验收

日期：2026-09-10。基线提交：`17bf21f17d6d08b946a61f944a2880d6f96cb0cd`。
承接 [StarIsland 开发计划](starisland_game_input_capture_plan_20260910.md)。

## 实现

- D3D11 / D3D12 保留原有采集入口；OpenGL / Vulkan 通过 `product-frame` 的同步 C ABI 适配进入同一套 `IpcCaptureVideoFrame`。
  C++ 边界立即用 COM 智能指针保留纹理，发送尺寸、真实 DXGI 格式、共享句柄和设备 LUID；队列不保存借用指针。
- OpenGL 在 NV interop 解锁后发送帧；GPU blit 转换为产品要求的顶端起始方向。
  修复错误分支漏解锁，不再依赖 OBS 保活 mutex。没有 NV interop 时明确记录不支持，不假称旧 CPU 分支可用。
  DXGI 跳过 OpenGL SwapBuffers 内部的 Present，避免 resize 的重建间隙误抢占成 D3D11 后端。
- Vulkan 使用产品专属 `layers/pixels-vulkan64.json`，指向实际 `px_gh.dll`。
  Windows manifest 必须使用 `..\\px_gh.dll`：使用正斜线的版本在本机 Loader 返回错误 87，会导致 Godot 回退 D3D12。
- Vulkan 拷贝完成的 fence 有界等待成功后才发送帧；Loader 回调在 Hook 初始化的 release/acquire 就绪门之后才允许采集。
- 游戏先挂起创建并加入私有 Job，再登记所有权、同步写 bootstrap，最后恢复执行。失败时停止本次 Job。
  仍然要求「私有 Job 成员 AND 规范化完整路径一致」，没有恢复外部进程接管。
- Layer 环境只传给本次直接启动的游戏，不写全局环境、注册表或系统 OBS 配置。
- 图形 Hook 游戏以同一登录用户的非管理员、Medium 完整性令牌运行；保留用户及其 profile。
  去除管理员权限时也重新设置用户拥有的默认对象 ACL，避免子进程 DLL 初始化失败。
- bootstrap 在写凭据前设置受保护 ACL：Render 用户 / SYSTEM / 管理员可管理，已准入游戏的真实用户可读。
  失败关闭；拒绝文件重解析点和多硬链接。删除无调用方的字符串 PID 兼容写入口，原实现完整归档。
- Pixels 的命名对象与系统 OBS 隔离，避免被已安装 OBS Layer 判为重复 Hook。
- Loader 提前加载的路径同样登记目标窗口、发送 PID 音频初始化通知；否则视频正常但文字目标和音频初始化会遗漏。

## 实机条件与证据口径

Windows / NVIDIA RTX 3060，驱动 591.74；StarIsland 为 Godot 4.6.2。
使用本机 Auth → Console → Service → Render 正常实例和 ticket 链路；每个交互测试有独立截止时间，单轮不超过 5 分钟。
测试结束只停止本轮实例，不按游戏文件路径清理其他进程。

Vulkan 通过必须同时有本实例 `Hooked Vulkan`、`vulkan shared texture capture successful` 和客户端解码画面。
仅加载 `vulkan-1.dll`、仅启动参数带 `vulkan`，或 D3D12 回退画面，都不算 Vulkan 验收。

| API | 已取得的本轮证据 |
| --- | --- |
| D3D11 | `inst-134-80d0a2e9` 确认真实 D3D11 硬件 swapchain 和动态帧；最终产物再次运行 `inst-152-712bd796`，第 14/29 秒客户端完整画面由红色变蓝色，与样例一致。35 秒后正常停止。 |
| D3D12 | `inst-149-10a929e7`，StarIsland Qt 输入“最终验收🙂”、进入场景、移动和跳跃通过；100 秒有界回归结束后正常停止。此前 D3D12 回退测试不计作 Vulkan 通过。 |
| OpenGL | `inst-140-a1787b32` Qt 完成中文 / Emoji 和游戏控制；`inst-130-c55fef3e` Web 完成中文、Emoji、选区替换、移动/跳跃、断线草稿保留与禁止发送；修复嵌套 Present 后 `inst-143-c9b919fb` 完成 95 秒缩放 / 恢复，始终为 OpenGL 后端。 |
| Vulkan | `inst-122-d2784d9c` Qt 成功输入“最终验收🙂”并进入场景移动/跳跃；`inst-136-ca700e34` Web 完成中文、Emoji、选区替换、移动/跳跃和断线保护，视频为 1280×720。 |

Vulkan `inst-145-8811e421` 另外完成 95 秒窗口缩放 / 恢复：Hook 日志顺序为 1280×720 → 1008×561 → 1280×720，
三次均为 Vulkan 共享纹理初始化，末尾仍有客户端画面。OpenGL `inst-143-c9b919fb` 只在两次人为 resize 时释放重建，
没有旧的每 5 秒重建现象，也没有 D3D11 采集初始化。两实例均正常停止。

最终产物的 Vulkan 只读会话 `inst-155-448d69ee`：ticket 只有 view / audio，16 秒观察得到 1280×720 视频，
文字入口不可用；测试结束正常停止。最后检查没有遗留本轮 StarIsland / D3D11 样例进程。

D3D11 使用仓库中的 `game_d3d11_fixture`：Godot 4 本身没有 D3D11 渲染器，不能把 StarIsland 的 D3D12 当作 D3D11 验证。
Web 两轮采用 Edge headless，中文通过正常文本提交链路；本轮不将旧的实体 IME 截图标成 OpenGL / Vulkan 新证据。

已查看的本机截图（测试产物，不参与打包）：

- `test-results/starisland-vulkan-qt-submitted.png` / `starisland-vulkan-qt-control.png`
- `test-results/starisland-opengl3-web-chinese-submitted.png` / `starisland-opengl3-web-control-restored.png`
- `test-results/starisland-vulkan-web-chinese-submitted.png` / `starisland-vulkan-web-control-restored.png`
- `test-results/starisland-d3d12-qt-submitted.png` / `starisland-d3d12-qt-control.png`
- `.cache/starisland-d3d11-14-1.png` / `.cache/starisland-d3d11-29-1.png`

## 自动检查与交付

已通过 8 个 CTest target：`game_text_input`、`client_application_text_input`、`application_text_protocol`、
`application_text_service`、`ws_ipc_client_lifecycle`、`logical_session_registry`、`game_process_identity`、`game_owned_process`。
其中进程所有权现有 11 个用例，覆盖挂起准入、失败停止、重复停止、失效 weak_ptr、子进程隔离、Unicode 环境仅对子进程生效，
以及非管理员 Medium 子进程真实执行并正常退出、普通权限调用者不依赖服务特权启动子进程。最终 8 个 target 全通过，耗时 8.27 秒。

使用 `scripts_build/build_cpp_tests.bat` 增量构建，不执行发布全量构建。
`publish_cpp_artifacts.ps1` 和 `collect_dist.py` 已纳入 Layer manifest；发布时检查源 / dist SHA-256。
最终运行产物已同步至 `build_official/dist`，源 / dist SHA-256 逐一匹配：

| 产物 | SHA-256 |
| --- | --- |
| `px_render.exe` | `63E4AD4A09327016058C9A7385037C4E326FDF48748D32574C7EE70A9A98E0B6` |
| `px_gh.dll` | `E649BD55E3F9B49B3C509E3760AEDCBCB8B1401BBF46018776C8F3B5E2F43426` |
| `layers/pixels-vulkan64.json` | `733196D8216E9CFABBC21AF9D0890781818EF2FCA4A6A2B520F672F1AA855B25` |

新增 D3D11 样例仅为测试目标，不进入产品打包。C++ 所有权 / 150 列检查及差异空白检查通过。

原始实现存于 `backup/graphics_*_20260910/` 各独立批次，附基线、原路径和 SHA-256；不编译、不打包。
用户原有 Rust / Console 工作区改动不属于本次修改。

## 边界

- 当前是 Windows x64、已准入直接启动游戏的支持；不承诺反作弊进程、任意外部启动器、任意游戏或所有显卡均可 Hook。
- 配置独立 view 可执行文件的启动器链没有本轮 Vulkan 首次实例前的 bootstrap 接入；不能把直接启动的通过结果推广到它。
- OpenGL 依赖 NV shared-texture interop；Vulkan 依赖可与 D3D11 共享的外部内存。
  本轮为单 GPU 验收，不声称混合显卡 / 跨适配器已验证。
- 本轮未新增 iOS / macOS、公网 P2P / Relay、Web 新传输模式或客户端强制选通道设置。

## 参考

- 本地 OBS 源码只读参考：`D:/source/obs-studio`，基线 `88de106cff4bcdf2a511e2e3801ffa3de729f6bd`。
- [Khronos Loader Layer 接口](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md)：进程环境发现及启用 Layer。
- [Khronos Windows 安全环境读取](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/loader/loader_environment.c)：实际检查完整性级别，而非仅检查 TokenElevation。
- [Microsoft Restricted Tokens](https://learn.microsoft.com/en-us/windows/win32/secauthz/restricted-tokens)：仅对子进程应用受限令牌，不修改系统 UAC 或账号。
