# 共享 SDK 单一原生传输交接（2026-09-07）

## 本轮完成

- SDK 归档 RTC、Direct RTC、Relay、旧 UDP/KCP 共 10 个连接源文件，以及原生 ICE 重启测试。
- NetClient 不再进行协议分派或接收通道枚举；只建立 UDP/FEC 媒体与 WS/WSS 可靠连接。
- SDK 参数移除传输类型、Relay 端点、P2P 和 ICE 配置；Windows、Android 调用同步更新。
- Windows 工作区的 Relay 重连、RTC 重启与专属鉴权监听退出活动代码。
- Windows Client 不再链接 RTC/Relay 库，Android 构建图也不再引入 Relay。Web/Render RTC 未修改。
- 原生重复启动/停止门禁覆盖独立文件会话；已退出的连接对象不可重新启动。
- 默认 C++ 测试入口、文件 E2E 入口、链接检查与发布/收集脚本同步调整，不再要求或打包旧 Client RTC DLL。

原文件完整归档于三个批次，共 89 个原文件，均已按 manifest 逐项校验 SHA-256：

- `backup/native_sdk_transport_core_20260907`
- `backup/native_sdk_transport_tools_20260907`
- `backup/native_sdk_transport_build_entries_20260907`

受 WebRTC 专项审查规则保护的 `px_deps/px_webrtc_client` 适配器未重写，仍在源码树中，
但不再是 Native Client 的依赖。语音 APM DLL 继续提供降噪/回声处理，不是 RTC 传输。

## 验证与交付

`scripts_build\build_cpp_tests.bat px_client test_udp_media_state test_udp_media_failure test_sdk_stream_helper test_sdk_websocket_reconnect test_ft_transport_e2e`
最终增量构建退出码为 0。未执行 release-only 全量构建。

Android `gradlew.bat :app:assembleDebug` 最终构建成功，当前 Debug Ninja 图无 RTC/Relay/KCP 旧连接输入。
未安装、卸载手机应用；未进行真机功能测试。

6 组 CTest 全部通过，共 16.31 秒：

- `common_message_notifier`、`common_async_runtime`
- `test_udp_media_state`、`udp_media_failure`
- `sdk_stream_helper`、`sdk_websocket_reconnect`

覆盖 UDP 失败后控制/文件保留、拒绝 WS 音视频、回调内注销/退出、延迟回调销毁、
重复启停，以及既有 WS/WSS 重连生命周期。文件传输 E2E 程序仅编译，
本轮未执行需要真实账号票据的上传/下载/删除验收。

7 个旧 CLI 参数均被拒绝。移走 Client RTC DLL 后，20 秒本机默认 UDP 会话的窗口、
首帧与连续解码检查通过，无解码错误。测试总时长低于 5 分钟，不代表浏览器、语音播放、
完整 GUI 操作或公网连接已验收。

Windows Client、语音 APM 与语言资源已发布至 `build_official/dist`，逐项 SHA-256 一致；
`llvm-readobj --coff-imports` 确认 Client EXE 不再导入 Client RTC DLL。发布后服务恢复，
Panel 保持运行，测试 Client 已退出。旧运行 DLL 可从以下位置恢复，仅作归档，不再装入产品：

`build_official/retired-client-runtime/92585fdcbed84f61807f95ee301a5b44/px_client_rtc.dll`

产物 SHA-256：

| 产物 | SHA-256 |
|---|---|
| Windows Client（构建树与 dist 一致） | `8CA62653A539817344CCAA6BD2B5C23295F1AD73D4601ED5EB854FEBB595A78A` |
| Android Debug APK | `704A53739744C807D0E164ED55098B48FC6A6919DAC0DE588BD6BF2B20B6EA60` |

新增 `scripts/check_native_sdk_transport.ps1` 检查 SDK 拓扑；既有路径、双端入口、
WebRTC 链接边界和 C++ ownership 门禁均通过。NetClient 与新增测试通过 clang-format 检查。
本机证据位于 `test-results/native-sdk-core-*`。

## 仍待完成

1. 旧 RTC/Relay 诊断显示、消息定义及未消费的参数残留继续收敛。
2. SDK 解码器、Qt 与平台适配尚未分层，尚不能宣称完全独立跨平台 SDK。
3. 语音实时传输边界仍需单独核查适配；当前仅确认没有随 Client RTC 连接退役而误删 APM。
4. 数据库旧通道列/共享线协议枚举不做破坏性删除或重编号。
5. iOS/macOS 适配、手机功能测试和正式签名 Release 合规输入仍未完成。
