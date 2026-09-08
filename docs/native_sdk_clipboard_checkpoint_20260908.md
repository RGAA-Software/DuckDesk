# P3b-1 文件/剪贴板检查点（2026-09-08）

实现及双端编译、短自动测试完成；Windows/Android 系统剪贴板跨进程回归仍列入 P5，
手机当前被另一项目占用，没有操作该应用，也没有把宿主适配器本机测试算成手机 UI 实测。

## 共同规则与修复

- 文件任务已由双方复用 `FtAsyncSession` / `FtEngine`：有序入口、发送背压、取消及 Done 回执保持原实现。
  Windows FtCore/内部模块只映射 Qt 任务和审计；Android NativeSession/Kotlin 保留 JNI 回调、SAF/URI 与页面生命周期。
  没有复制文件引擎，也没有把上传本地 EOF 改成成功。
- `sdk_clipboard_protocol.h` 属于无 Qt/JNI/codec 的 `pixels::transport`：共享描述基本校验、请求身份、响应完整匹配、
  取消后序号不复用、排队更新 epoch。路径解析和 OLE/系统剪贴板不进入 SDK。
- 分块统一 128 KiB，避免 Android 原 256 KiB 载荷加协议头超出可靠消息上限。
  响应必须匹配文件标识、请求序号、偏移、请求长度，声明长度必须等于载荷且不超过请求；先校验再保留载荷。
- Windows 原先直接打开对端传入路径，现在只解析本机当前已发布文件的 opaque 标识。
  双端每次文件发布生成独立 wire 标识，即使宿主复用同一 UI generation 也不复用；替换/撤销/停机后失效。
  读取不得越过当时发布的文件大小，文件增长不扩大权限。
- Windows 每次模块 Start 创建独立 bridge/lifetime token，旧 OLE stream 与排队任务不会随重启复活。
  COM Read 在发送前登记请求身份，失败也推进序号；Exit 持锁撤销并唤醒，支持发送回调内退出。
- Windows 文本与文件更新都交给同一 STA 队列，过期 epoch 不发布；先完整初始化文件描述再 OleSetClipboard。
  OLE 对象用 ComPtr 快照和锁交接，替换/停止取消旧 stream；异步 WM_CLIPBOARDUPDATE 通过 OleIsCurrentClipboard 排除自己的文件发布。
- OLE 结束只有“数据读完整且系统 EndOperation 成功”才上报成功；取消、失败、未读完不再伪装成功。
- Android 下载回调内 Stop/重入不再 self-join；保留 join 协调器收尾，未 detach。
  文件关闭失败不报完成，文件名按 UTF-8 字符边界截断；发布补齐 Windows OLE 所需 ref_path。

## 验证及边界

- 最新 Windows Client 和相关测试编译成功：`sdk-clipboard-owner-windows-build.log`。
- 最新 Android Debug/JVM 构建成功（49 秒，18 项 JVM 测试无失败）：`sdk-clipboard-dispatch-android-build.log`。
  APK 未覆盖安装，避免打断另一项目。
- 八组文件/剪贴板测试通过（5.79 秒）：`sdk-clipboard-dispatch-final-ctest.log`。
  最后补充 OLE 自身通知过滤后，四组剪贴板测试再次通过（1.09 秒）：`sdk-clipboard-owner-final-ctest.log`。
  SDK 协议 6 项、Android 宿主适配器 4 项、COM stream 7 项、Windows 模块 3 项。
- Android 宿主适配器测试实际写入/核对大于一块的文件，注入错误偏移，再测试回调停止和十轮取消。
  Windows COM 测试覆盖错误元数据、取消唤醒、发送失败重试、实际 IStream 读完 + OLE 成功/失败结束。
- 独立 Windows core 消费构建/生命周期及剪贴板协议测试通过；独立结果是在加入最终 UI epoch 用例前，
  最新 epoch 已由根工程和 Android 编译覆盖，P5 再刷新独立矩阵。
- Windows 20 秒真实连接出图通过（`sdk-clipboard-windows-smoke.log`），在最后 OLE 自身通知过滤补丁之前。
  此 smoke 禁用剪贴板，只证明网络/解码未退化，不能算跨进程复制文件验收。
- `test_ft_transport_e2e` 首次缺 Qt DLL，补运行路径后因未配置授权环境而 **SKIP**，不计通过。
  当前文件证据是上述共享引擎测试，实际网络文件验收沿用既有记录并在 P5 按需补测。
- SDK 布局、传输边界、Android JNI/传输检查、C++ 所有权检查及 `git diff --check` 通过。

此次没有修改 Render 用户代理 / Panel 的剪贴板实现。审计看到 `rust_client/px_user_proxy/src/render_client.rs`
仍有直接按请求路径读文件的旧逻辑；它不是本次两端 SDK 的共享代码，不能据本检查点声称整个产品已完成文件授权加固。
需要单独核对 Render 的上游授权及用户代理文件发布绑定，避免把客户端修复误报成全链路安全结论。

## 交付与备份

Windows 已同步 `build_official/dist`，Client、APM DLL 与三种语言资源逐项 SHA-256 一致，
日志 `sdk-clipboard-owner-client-publish.log`；px_service 已恢复运行。

| 产物 | 检查点 SHA-256 |
|---|---|
| px_client.exe | C6822458D5FCBD62100BC66370E477F7BAEC48A40E97361AAD61C6B4340255BE |
| Android Debug APK（已构建、未安装） | 54A754AF978E0A892D4AE0F99E72449A1D8C93A6C04C9489616593CBF64F7FB0 |

修改前工作区字节保存在 `backup/native_sdk_clipboard_contract_20260908`（14 文件）、
`native_sdk_clipboard_android_revoke_20260908`（1 文件）、`native_sdk_clipboard_dispatch_20260908`（2 文件）、
`native_sdk_clipboard_ole_cancel_20260908`（2 文件），已重新逐文件校验哈希。未提交或推送。
