# SDK 目录迁移：第一实施检查点

日期：2026-09-07。本文记录已交付的迁移检查点，不代表整个原生传输精简计划完成。

## 已实施

- `src/px_deps/px_client_sdk` 的 72 个文件迁移到 `src/px_client_sdk`，不保留旧路径副本、转发头或链接。
- `px_sdk` 显式导出公开头文件目录；根工程、Android、Panel、测试和维护脚本接入新位置。
- Windows 剪贴板模块补充显式 `px_sdk` 依赖，不再借用根工程的隐式 include 路径。
- 原有 SDK C++ 实现保持不变；目录迁移没有改写 UDP/FEC、认证、文件引擎或解码逻辑。
- 原始文件已归档到 `backup/native_transport_simplification`，清单保存 1,305 个文件的原路径、基础提交、本地状态和 SHA-256。
- Windows 增量发布脚本补齐 `px_voice_apm.dll` 同步。更新占用文件时停止相应进程，发布后恢复服务。

## 验证结果

Windows 使用 `scripts_build\build_cpp_tests.bat` 构建下列目标，未运行 `scripts_build\build_official.bat`，未构建 Rust 或 npm：

```text
px_client px_panel skin_interface skin_loader skin_official skin_opensource
test_sdk_stream_helper test_udp_media_fallback_state test_sdk_websocket_reconnect
test_message_notifier test_async_runtime test_ft_transport_e2e
```

以下 5 组 CTest 全部通过，总计 5.68 秒：

- `common_message_notifier`
- `common_async_runtime`
- `test_udp_media_fallback_state`（本次迁移基线；后续退役回退时替换）
- `sdk_stream_helper`
- `sdk_websocket_reconnect`

`test_ft_transport_e2e` 已编译，本轮没有重新执行真实文件传输端到端测试。
SDK 路径检查、迁移的 C++ 变更检查、原 WebRTC DLL 链接边界检查均通过。
Windows/Android 生成的 Ninja 构建图未引用原 SDK 目录或归档源码。

从 `build_official/dist` 启动 Windows Client，以本机 Render 做 UDP 直连：窗口、UDP 媒体和连续解码正常，
20 秒复核无解码错误。截图确认画面实际显示；这是本机环回冒烟，不是跨网性能或全功能验收。
首次冒烟的脚本断言只识别软件解码器日志，误判了正常的硬件解码；修正为检查连续解码记录后复核通过。
脚本关闭测试窗口时会触发应用退出确认，超时后结束仅本脚本启动的 Client；不以此宣称 GUI 正常退出流程已验收。
两轮冒烟和回归运行时间合计低于 5 分钟，测试 Client 已退出，Panel 和本机 Render 保持运行。

`px_client.exe`、`px_panel.exe`、`px_client_rtc.dll`、`px_voice_apm.dll`、两个皮肤 DLL、皮肤配置和三份语言资源
均已发布，构建产物与 `dist` SHA-256 一致。

Android 在 `src/px_android` 执行 `gradlew.bat :app:assembleDebug --console=plain` 成功：

- APK：`src/px_android/app/build/outputs/apk/debug/app-debug.apk`
- 包名：`yun.pixels.client.debug`；它不是正式包的覆盖安装产物。
- SHA-256：`8AAD87F9AE45345A192F02DDE84182DF3B85CB3E3495E1CF2A41ECDA52FC7BF1`
- 无 USB 手机，未执行安装、卸载或真机验收。
- 尝试 Release 打包时，现有合规输入门禁拒绝继续：缺少 `PIXELS_FFMPEG_SOURCE_ARCHIVE` 和
  `PIXELS_LGPL_RELINK_ARCHIVE`。这是发布输入缺失，不等于 Release 编译已经通过；未绕过门禁。

本机证据位于 `test-results/sdk-move-*`（构建日志、CTest XML、发布日志、产物哈希、冒烟结果和截图）。

## 下一检查点

1. 归档并退役原生 RTC、Relay、旧 UDP/KCP 和 WS 视频回退；保留 Web/Render 的 RTC 路径。
2. Windows Panel/Client 与 Android 固定 UDP+FEC 媒体和 WebSocket 控制/文件，清理通道设置、启动参数和旧配置消费。
3. 核对密码直连、Console 一次性票据、普通会话复用控制 WS 的文件消息、独立文件模式、重连与失败提示。
4. 分离 SDK 共享核心和 Windows/Android 平台适配；当前仍有 Qt 与旧传输依赖，不能声称已完成平台无关 SDK。
5. 复验双端构建和 Windows 功能；手机接入后，仅按正式包签名和包名准备覆盖安装，不卸载、不清空数据。

iOS/macOS 与公网 P2P/Relay 仍按产品决定列为后续工作。本记录随第一阶段 SDK 迁移提交。
