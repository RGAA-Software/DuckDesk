# 原生诊断清理与 SDK Qt 依赖解除交接（2026-09-07）

## 本轮完成

- Windows 统计页移除 ICE Path、TURN/RTT 两行及旧协议判断，固定显示原生组合 `UDP/FEC + WS`；既有网络延迟统计保留。
- Windows 设置移除闲置的通道枚举、Relay/P2P/ICE/强制直连字段和分类方法。
- SDK 移除 16 个闲置信令、房间、旧进度与统计/重试消息定义，以及 RTC 统计字段。
- SDK 参数不再保留已无消费者的 RTC 密码、票据目标设备、Direct RTC 重试凭据和 takeover 字段；
  一次性 WS 票据、nonce、instance 与 UDP association 保持原流程。Windows/Android 停止向 SDK 复制无用字段。
- 空的 `RetryConnection` 调用链与 `ReportStatistics` 被归档移除；WS/WSS 自身的自动重连没有移除。
- 进度条总步数归 Windows UI，不再由 SDK 接口提供。改动的连接标签/进度条控件以 Qt 父对象拥有，使用 `QPointer` 观察。

## SDK 与 Qt

调用核对发现，SDK 的 Director、Sprite、Renderer、ShaderProgram、GLFunctions 及 shader 常量是重复旧实现，
Windows 当前使用自身 `front_render/opengl/ct_*`，Android 没有调用这组辅助代码。
共 10 个文件完整归档后退出活动源码。

`px_sdk` 因而移除 Qt、glm 的直接链接，并显式关闭 AUTOMOC/AUTOUIC/AUTORCC。
SDK 产品源码没有 Qt include；可选 Windows 文件传输 E2E 程序单独使用 Qt Core。
`gl/raw_image.h/.cpp` 是实际被双端使用的帧数据实现，仍保留，不能将整个 gl 目录视为死代码。

用 `llvm-readobj --coff-imports` 核对 `test_udp_media_failure.exe`，
该 SDK 集成测试程序既不导入 Qt DLL，也不导入 Client RTC DLL。
这验证当前 SDK 链接链路不需要它们，不等于已经完成独立 SDK 工程或 iOS/macOS 支持。

原代码归档于以下两个批次，共 32 个原文件，所有 manifest SHA-256 均已验证：

- `backup/native_diagnostics_cleanup_20260907`
- `backup/native_sdk_unused_gl_20260907`

## 验证与交付

Windows 使用增量入口：

```text
scripts_build\build_cpp_tests.bat px_client test_udp_media_failure test_sdk_websocket_reconnect
    test_udp_media_state test_sdk_stream_helper test_ft_transport_e2e check_cpp_ownership
scripts_build\build_cpp_tests.bat px_client
```

最终构建均成功；未运行 release-only 全量构建。
Android `gradlew.bat :app:assembleDebug` 在 Qt 辅助代码退出后再次成功；未安装或卸载手机应用。

6 组 CTest 全部通过，共 16.21 秒：

- common_message_notifier、common_async_runtime
- test_udp_media_state、udp_media_failure
- sdk_stream_helper、sdk_websocket_reconnect

覆盖既有的媒体失败隔离、延迟回调销毁、回调内退出/注销、重复启停以及 WS/WSS 重连。
文件 E2E 程序只编译，未执行真实账号上传/下载验收。
7 个旧 CLI 参数均拒绝；20 秒本机默认 UDP 会话的窗口、媒体就绪与连续解码检查通过，无解码错误。
本轮测试总时长低于 5 分钟；未进行统计页逐项点击/截图、浏览器或真机功能验收。

Client、语音 APM DLL 与语言资源已同步至 `build_official/dist`，逐项哈希一致。
发布时临时停止占用文件的 Render 服务，随后恢复；Panel 保持运行，测试 Client 已退出。

| 产物 | SHA-256 |
|---|---|
| Client（构建树与 dist 相同） | `C9FEADC3EECFC4CCE9B2BA453325E242739700A5A7AD2B105271D5360343726E` |
| Android Debug APK | `A9D6760159F33EEF9599BA7E58AB23CBCBB1CE2B002325FAA979A5BA6DFD9BB7` |

SDK 路径/传输拓扑、双端入口、WebRTC 链接边界与 C++ ownership 检查通过。
SDK 消息头和新增 Qt 布局块通过 clang-format 检查，未整文件格式化旧 UI。
本机日志与 XML 位于 `test-results/native-diagnostics-*`。

## 下一步

1. 帧数据与解码器的资源所有权：CPU 数据、D3D11 纹理、Vulkan AVFrame 和 Android Surface 的生命周期与接口。
2. 将实际 Windows/Android 解码适配从共享流程拆出，并提供可独立消费的构建边界。移除直接 Qt 依赖只是前置工作。
3. 单独核对语音实时传输；本轮没有改变音频播放、APM 或 Web/Render RTC。
4. iOS/macOS、真机验证和 Release 合规输入仍未完成。共享协议枚举、数据库旧列未做破坏性变更。
