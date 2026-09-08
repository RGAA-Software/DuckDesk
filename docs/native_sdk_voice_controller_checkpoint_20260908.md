# P3c-1 共享语音控制器（2026-09-08）

Windows 和 Android 已使用同一个 `VoiceCallController`，不再各自编排通话请求、响应、超时和音频会话。
这一步关闭 P3c-1 的代码/自动验证；UDP 迁移与最终真实语音验收仍属于 P3c-2/P5，不能据此宣称全部完成。

## 实现

- 复用现有 `VoiceCallState`、`VoicePacketTransport`，抽出不含设备的 `px_voice_call_core`；完整 SDK 可独立链接。
- 每通话独立 run、UUID、请求序号、定时器、包队列和设备端口。仅匹配的同意能创建音频，迟到响应/设备错误不能影响下一轮。
- 控制发送与音频发送分别注入；同步发送回调内挂断通过有序控制队列处理，不在状态锁内调用宿主。
- 超时可缩短但不得超过现有 30 秒同意窗口，测试使用短超时；旧有 Render 默认行为不变。
- `VoiceAudioPort` 是真实设备边界。宿主的 `VoiceAudioEndpointPort` 包装原端点；SDK 不强制 SDL、WASAPI、AAudio、APM DLL。
- Windows 保留设备 ID 选择和 Qt 消息显示；Android 保留权限/设备路由/JNI，显式映射共享 phase 到 Java 0/1/2。
- 状态按 revision 丢弃过期投递，错误清理使用弱引用；会话终止先 Close 控制器，再停止 SDK/队列。
- Android 原 `native_voice_call.cpp/.h` 移出活动源码，修改前完整版本保留在 backup。Windows 重复流程同样先归档再替换。
- SDK-only 消费示例实际创建和关闭共享控制器；配置拒绝设备/APM/SDL 目标，非仅编译一个未使用的头文件。

当前语音帧仍走 WS：本批没有改 UDP 包或 Render/Web 线协议，发送返回成功仅表示本地接纳。

## 验证

| 检查 | 结果 |
|---|---|
| Windows 增量 Client/Render/两个 RTC DLL/语音测试构建 | 通过，`sdk-voice-controller-hosts-windows-build.log` |
| Android Debug + JVM | 通过，1 分 45 秒；18 项 JVM 测试，本批未修改 Kotlin |
| 根语音控制器 9 + 协议 8 + 基础 41 项 | 全通过，三组 3.07 秒；排除长稳测和 WASAPI 硬件测试 |
| 实际端点适配器 4 项 | 全通过，0.20 秒；注入后端，不打开物理麦克风 |
| 独立 Windows full 消费工程 | 构建和真实控制器离线生命周期通过，0.94 秒 |
| 独立 Windows 语音控制器/语音协议/剪贴板协议 | 三组通过，1.22 秒 |
| 独立 Android full 消费工程 | 编译/链接通过；本轮未在手机执行 |
| Windows 本机 20 秒实连 | 窗口、UDP 媒体、持续解码通过，无解码错误；不代表真实语音听音验收 |
| SDK 目录/传输、Android JNI、C++ 所有权、WebRTC DLL 链接边界 | 通过 |

日志位于 `test-results/sdk-voice-controller-*`。新增端点测试首轮缺少显式 `<chrono>` 导致编译失败，补齐后重建/测试通过。
控制器最初的 MessageSender 类型别名也已避开 Windows SendMessage 宏；最终双平台构建没有该问题。

## 发布

已按日常增量入口构建并同步到 `build_official/dist`，发布脚本逐项 SHA-256 匹配：

- `px_client.exe`：`41C06F14DEDD2B4FA0AA9567C312401A0AD5FFA4E5FD680834B3FF7D64DF3E74`
- `px_render.exe`：`F3C338F4064969C47F0F15293B85D05889EB8096203A4E14FFD4F42718F2C44B`
- `px_voice_apm.dll`：`21411B44D9C7C8E6F270B6E932EF48C1511E542D0EA36BE25F37578ECAE669B3`
- `px_render_rtc_remote.dll`：`FE14E162E03D74FDE50253BCEA94F126337B897BA8591AF4419CEB122808D019`
- `px_render_rtc.dll`：`986983F65C52CC3F8B9350492F1A2195D375F0C3F7A629FD4EF43D60CDFEDA61`

三份 Client 语言资源也同步并匹配，见 client-publish 日志。占用共享 DLL 时短暂停止 `px_service`，发布后已恢复运行。
Android APK：`D3ABC94D678EF4EC1D2E2422AE585EC93FB150B3EDFB8519CF7B742C6204B5C0`。
手机正在供其他项目使用，本批没有覆盖安装或抢占界面；已安装 APK 仍是此前版本，不能用旧包替代本批验收。

## 备份与后续

- `backup/native_sdk_voice_controller_core_20260908`：8 文件。
- `backup/native_sdk_voice_controller_hosts_20260908`：7 文件。
- `backup/native_sdk_voice_controller_consumer_20260908`：2 文件。

均保存修改前工作区字节与 manifest 哈希，未用 HEAD 覆盖已有修改。本轮未提交、未 push。
接着按已批准方案新增独立 UDP Voice 包，完成两端通话绑定/队列边界及 Render 收发；Web 保留现有 RTC 音轨。
最终手机 UI/语音、跨进程系统剪贴板、Web 首帧和人工听音仍需分别补足，单场景不超过 5 分钟。
