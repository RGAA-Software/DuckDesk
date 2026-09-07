# Android 固定原生接入：第三检查点

日期：2026-09-07；基于 `92ffb33f6` 继续实施。

## 实施内容

- 原版保存在 `backup/android_native_only`：104 个源文件、构建配置和资源，逐项 SHA-256；未覆盖此前批次。
- 移出活动树的实现包括 WebRTC 会话、SDP/ICE 信令、统计、RTC 录制与语音控制器、RTC 文件/剪贴板 JNI、专属测试及调试接收器。
- 删除 Android WebRTC AAR 和仅供 RTC 使用的 Java protobuf 生成/依赖，更新应用中的组件清单与许可证展示。
- `AndroidRemoteSessionTransport` 不再是协议路由器，仅管理原生会话生命周期、Surface 与票据；其余能力委托同一原生实例。
- JNI 配置删除网络类型、Relay 端点及 ICE 字段；C++ 固定 `kUdpDirect`。Console 响应模型暂留服务端公共字段，但不影响连接方式。
- 已尝试的一次性票据在会话生命周期内记为已使用，重试必须续发；账号配置保留 nonce、设备、实例绑定和权限校验。
- 无观看权限或缺少必要凭据时拒绝启动；输入、音频及剪贴板受票据权限限制。目标端点不可达时失败，不进入 Relay/RTC。
- 增加 JNI 方法、字段、回调名称一致性检查及 R8 保留规则；未加入旧 Android ABI 兼容层。

## 验证

```text
gradlew.bat :app:assembleDebug :core-native:testDebugUnitTest :core-domain:testDebugUnitTest
gradlew.bat :app:dependencies --configuration debugRuntimeClasspath
scripts/check_android_native_transport.ps1
scripts/check_cpp_ownership.ps1
```

最终 Debug 构建成功；34 项单元测试通过（core-native 9 项，core-domain 25 项），测试执行约 0.36 秒。
覆盖原生端点、票据重试、权限映射、目录映射及已有会话工作流。JNI 源码契约检查通过；首次 C++ 编译发现清理范围误含相邻的
非 RTC JNI 函数，已从归档恢复这些原生音频/录制/语音函数，最终编译和名称对照均通过。
运行时依赖图已无 WebRTC 与 protobuf-javalite；APK ZIP 中无 libjingle/WebRTC 原生库。

- APK：`src/px_android/app/build/outputs/apk/debug/app-debug.apk`
- 包名：`yun.pixels.client.debug`；不能覆盖正式包。
- SHA-256：`A9AC83060EC90835AB471F74B1A1D4AD40C62067404BE7DE4C312F4FB3E03E1A`
- 无手机，不安装、不卸载，不声称真机连接、文件、剪贴板、录制或语音已通过本次验收。
- 未重试 Release；原有 FFmpeg 源码和 LGPL 重链接归档输入门禁不变，R8 规则尚未完成 Release 构建验证。

Windows 未在第三检查点修改，继续使用第二检查点已编译、发布且通过 20 秒本机 UDP 冒烟的 `build_official/dist` 产物。
测试总时长低于 5 分钟。本机证据为 `test-results/android-native-only-*`。

## 下一步

1. 清理 Windows/Panel 的强制通道设置、启动参数和 RTC/Relay/旧 UDP-KCP/独立 WS 媒体选择。
2. 清理 SDK 及双端构建中的旧通道实现/依赖；Android C++ 共享目标目前仍编译旧 Relay 代码，入口不可达不等于代码已退出。
3. 继续核心与平台适配分层，复验 Windows、Android 编译和 Windows 功能。Web/Render RTC 本轮未改，不代表浏览器功能已重测。
4. 手机接入后准备正确签名和包名的覆盖安装包，再做不超过 5 分钟的功能复核。
