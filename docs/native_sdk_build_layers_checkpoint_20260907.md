# SDK 构建分层检查点（2026-09-07）

## 范围与结论

本轮先落实共享实现、平台后端和会话装配的构建边界，不修改 FFmpeg ABI，不改传输和解码行为。
这不是完整的 SDK 平台无关化交付：公开参数、帧类型和会话调度仍有平台耦合，见后续事项。

| 构建目标 | 职责 | 边界 |
| --- | --- | --- |
| `px_sdk_core` | UDP/FEC、WS/WSS、网络调度、统计、计时与码流辅助 | 不编译平台解码器；部分公共头仍有间接平台依赖 |
| `px_sdk_platform` | 当前平台的实际解码后端 | Windows：D3D11/FFmpeg、Vulkan、软件解码；Android：MediaCodec、Android 软件解码 |
| `px_sdk` | 会话调度、解码基类、帧数据及最终静态库装配 | 保持两端消费者入口；保留尚未迁出的平台选择逻辑 |

两个内部目标采用 OBJECT 库，最终对象一并进入 `px_sdk`，不引入 DLL、运行时插件或相互依赖的静态库。
源码清单集中在 `src/px_client_sdk/cmake/sdk_source_sets.cmake`，旧 connection 子目录清单已完整归档后退出活动树。
Android 不再编译 Windows FFmpeg 后端或未被使用的旧通用软件解码器；Windows 不编译 MediaCodec。
Android 的 `mediandk`、原生窗口链接依赖归平台目标；JNI 层自身仍使用 `android`，该依赖保留。
所有产品目标关闭 Qt 自动代码生成，FFmpeg 与原生传输策略不变。

## FFmpeg 与 Surface 的准确边界

- FFmpeg 的 `AVBufferRef*` 是外部 C API，不改其签名，也不使用普通 `delete` 回收。
  项目需要长期保留设备/帧上下文时，应持有明确的引用，由专用 RAII owner 调用 `av_buffer_unref`；
  获取另一个独立引用按需使用 `av_buffer_ref`。C++ shared owner 不自动等价于 FFmpeg 独立引用。
- 当前 Android Surface 实际对应 `ANativeWindow`，`NativeSession` 已持有带 release 规则的智能指针。
  当前缺口是 SDK 接口仍使用 `void*` 和整数地址传递窗口，公共层没有表达类型与有效期。
  后续应由 Android 适配层接收类型明确的窗口 owner，平台内部在 NDK 调用边界取借用句柄；不能仅将地址改成整数。
- 本轮不重写解码器资源管理，不把上述 ABI 指针本身当作死代码，也不修改第三方 FFmpeg/libwebrtc 实现。

## 后续事项（未完成）

1. 把 `ThunderSdkParams` 的 D3D11/Vulkan 设备上下文与 Surface 配置移到对应平台装配接口，网络参数独立。
2. 会话层通过类型明确的解码工厂与输出目标接入平台实现，移出具体后端构造和平台分支；不引入万能属性包。
3. 收口 CPU 帧、D3D11 纹理和 FFmpeg 上下文的所有权，并保留排队帧、换 Surface、重建/退出时的生命周期测试。
4. 依赖裁剪完成后再提供可独立配置和集成的核心入口。目前仍依赖工程提供的 px_common、消息生成及依赖环境。
5. iOS/macOS 在真实平台适配时提供对应实现；完整 SDK 对未实现的平台继续明确报错，不提供空后端或虚假支持开关。

## 验证

源码边界测试无需编译器、Qt、FFmpeg 或 NDK，可在任意 CMake 主机检查两套平台源码集合：

```powershell
cmake -P src/px_client_sdk/tests/test_sdk_source_sets.cmake
```

它检查后端选择、集合互斥、全部活动实现被显式覆盖，以及未实现平台必须明确失败；只在测试中枚举源码，不用于产品自动发现。
原生传输检查脚本同步覆盖新清单，并执行此测试。Windows CTest 注册同一项 `sdk_source_sets`。

- Windows 使用 `build_cpp_tests.bat` 编译 `px_client`、文件 E2E、帧所有权、UDP 故障及 WS 重连测试，全部通过。
- 7 组 SDK CTest 全部通过，15.39 秒：`sdk_source_sets`、`test_ft_transport_e2e`、`test_udp_media_state`、
  `av_frame_ownership`、`udp_media_failure`、`sdk_stream_helper`、`sdk_websocket_reconnect`。
- `build_official/dist` 20 秒本机直连冒烟通过：窗口、UDP 媒体、连续解码正常，无解码错误；随后退出测试客户端。
- Android `:core-native:testDebugUnitTest :app:assembleDebug` 通过；USB 手机 `e2b3b128` 覆盖安装成功并启动应用。
  本轮不重复手机远控/文件/语音全流程，不将安装成功当作这些功能的重新验收。
- 原生 SDK/Windows/Android 传输边界、SDK 目录、所有权和 WebRTC DLL 链接检查通过。未改变 WebRTC 源码，未重测浏览器。
- Windows 客户端、语音 APM DLL 和语言资源已同步到 `build_official/dist`，源/目标 SHA-256 一致；服务恢复 Running。
  Render 没有依赖本次 SDK 变更，未重建 Render。

```text
px_client.exe 43FFA558110A27CE9F479C91C843C11157509DBEC18CF9FC8A8E187FC4FDD86A
Debug APK     58DE9A72595256F4D65C156DB1A4127D5758958A58DACADC011A41CAF7616D78
```

本机日志位于 `test-results/sdk-build-layers-*.log`，不提交设备日志或运行时凭据。
Release 仍缺少既有构建门禁要求的 FFmpeg 源码与 LGPL 重链接归档，本轮未生成 Release、未改变门禁。

旧文件保存在 `backup/native_sdk_build_layers_20260907` 和 `backup/native_sdk_build_layer_guard_20260907`，
保留修改前完整字节及 SHA-256 清单，不参与构建。
