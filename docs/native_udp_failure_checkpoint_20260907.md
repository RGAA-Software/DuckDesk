# 原生 UDP 媒体故障隔离：第二检查点

日期：2026-09-07。基于 SDK 抽离提交 `566acd277`，不代表整个传输精简计划完成。

## 已实施

- 原回退状态机及混合文件原件保存在 `backup/native_udp_media_failure`，19 个文件有 SHA-256 清单；归档不参与构建。
- UDP 探测超时或媒体中断改为一次性的媒体故障事件，不发送 WS 媒体回退信号，不关闭可靠控制/文件连接。
- UDP 媒体状态区分未启动、探测、可用、故障、停止；重复启动不重置活动会话，停止后迟到事件不能重新激活。
- UDP 会话拒绝 WS 音视频，过滤发生在录制用的原始消息回调之前。独立文件会话不启动媒体探测。
- Windows 增加媒体故障提示；Android 增加 JNI 事件、领域状态和工作区提示，媒体失败不伪装成控制断线或停止文件传输。
- 失败后需结束当前会话再连接，不自动切换协议，不重新兑换原一次性票据。语音迁移不在此检查点内。

## 验证与交付

Windows 聚焦构建：

```text
scripts_build\build_cpp_tests.bat px_client test_udp_media_state test_udp_media_failure test_sdk_websocket_reconnect
```

5 组 CTest 全通过，共 14.19 秒：`common_message_notifier`、`common_async_runtime`、
`test_udp_media_state`、`udp_media_failure`、`sdk_websocket_reconnect`。
新增套接字集成测试用本地 WS 服务和 UDP 黑洞模拟探测超时，验证控制/文件消息继续传送、拒绝 WS 媒体、
独立文件模式不依赖 UDP、故障回调内注销及关闭。另有并发故障去重、重复启停和迟到事件测试。
首次提前运行时新测试 EXE 尚在链接，文件占用导致未启动；等待构建完成后完整重跑通过。

从 `build_official/dist` 启动客户端，连接本机 Render 的 20 秒冒烟通过：窗口创建、UDP 首帧、连续解码正常，无解码错误。
这不是跨网、真实大文件或所有输入功能验收。总测试时长低于 5 分钟。
发布过程已同步客户端、运行 DLL 和三份语言资源，逐项 SHA-256 一致；发布占用的语音 DLL 时临时停止并恢复 `px_service`。
`px_client.exe` SHA-256：`76DE53243A41B34F7ECF17DAAA03A291B01EF3124418B25F38936F676806514D`。

Android：`gradlew.bat :app:assembleDebug :core-domain:testDebugUnitTest` 成功；25 项领域测试通过，其中 16 项会话工作流测试。
新增用例证明媒体故障不会停止控制/文件会话，配置重播不会抹掉故障，结束后迟到事件无效。

- APK：`src/px_android/app/build/outputs/apk/debug/app-debug.apk`
- 包名：`yun.pixels.client.debug`，不是正式包的覆盖安装产物。
- SHA-256：`1854BB2C1F7421B550FC45CB8F86E62CCE8E231242757F75EA41960B4EFD9E72`
- 无手机，未安装、卸载或声称真机验收通过。Release 合规归档输入缺失仍按第一检查点记录，不绕过门禁。

C++ 所有权增量检查、SDK 路径检查和新增 C++ 文件格式检查通过。
本机日志在 `test-results/udp-failure-*`；直连冒烟结果使用 `test-results/sdk-move-windows-smoke.json`。

## 仍待实施

Windows/Panel 和 Android 的 RTC、Relay、旧 UDP/KCP、独立 WS 媒体入口与通道设置仍需退役，
SDK 与 Qt/平台适配仍需分层。Web/Render 的 RTC 保留，未在本次做浏览器功能测试。
iOS/macOS 和后续 RustDesk 公网方案仅规划。本次两端构建不是最终精简候选版本。
