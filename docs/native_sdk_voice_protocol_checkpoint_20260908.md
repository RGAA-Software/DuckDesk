# P3c 语音协议子步骤（2026-09-08）

这一步只完成共同消息构造，不等于 P3c 完成：双端通话控制器仍待合并，Native 实时语音目前仍走 WS，UDP 迁移尚未实施。

- `sdk_voice_protocol.h/.cpp` 进入 `pixels::transport`，共用请求/响应、音频配置与语音帧构造，不依赖音频设备或解码器。
- `VoiceAudioFormat` 是现有固定 Native 语音格式的唯一常量定义，现有端点沿用这些值，未改 WebRTC PCM 或协议格式。
- 请求序列由各控制器自己的 `VoiceCallRequestSequence` 持有，不再使用两份可变静态计数器；相关性仍为 callId + requestId。
- Windows BaseWorkspace 与 Android NativeVoiceCall 均使用共同构造函数；Client 旧协议 cpp/h 与旧测试移除，修改前字节保留在 backup。
- `px_voice_call` 显式链接实际使用的异步/加密组件；独立语音测试此前缺 DeferJoin/OpenSSL 符号，修正后链接通过。

## 验证

- Windows Client、Render、`net_rtc`、`net_rtc_local`、SDK 协议测试与语音基础测试编译成功。
- Android 最新依赖修改后再次构建/JVM 测试通过：`sdk-voice-protocol-final-android-build.log`，33 秒。
- 根测试 48 项通过：SDK 协议 8 项 + 语音基础 40 项，两组总 2.92 秒。
  显式排除可配置长稳测与 WASAPI 硬件测试，使用 dummy/injected 后端，不冒充听音或手机实测。
- 独立 Windows core 消费构建及生命周期通过；SDK 语音/剪贴板协议两组通过，1.04 秒。
- SDK 传输边界、Android JNI/传输检查、C++ 所有权检查、diff whitespace 检查通过。

日志在 `test-results/sdk-voice-protocol-*-build.log`、`sdk-voice-protocol-final-ctest.log`、
`sdk-voice-protocol-independent-core-ctest.log`。最初使用了 DLL 输出名而非 `net_rtc` CMake 目标名导致构建入口失败，
之后改用实际目标；不能把这次入口错误算作产品编译失败。

## 产物

Client/Render 及相关运行库、语言资源已同步 `build_official/dist` 并核对 SHA-256；px_service 运行中。
Android APK 仅构建，手机仍被另一项目占用，未覆盖安装。

| 产物 | SHA-256 |
|---|---|
| px_client.exe | 3B3A94FE8183844D778A5A784C4F6E1F9E9CCC15A88B86A2F491A1082ED72D51 |
| px_render.exe | 544CB04D014CF15A30943262913EEDEEE3F0F25D1EC27F35E1B099095631245E |
| Android Debug APK | FF17F79195771DFCC098D9F127EB66558283487742423409A5EA1D8E2A92F6BB |

发布日志：`sdk-voice-protocol-client-publish.log` / `sdk-voice-protocol-render-publish.log`。
备份：`native_sdk_voice_protocol_20260908`（11 文件）、`native_sdk_voice_link_dependencies_20260908`（1 文件）。未提交推送。
