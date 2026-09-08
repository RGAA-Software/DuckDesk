# Native 解码启动、光标 DPI 与 USB 真机检查点

日期：2026-09-07。基础提交：`694a5d6bb`。本检查点的双端构建和限定短测已通过，不等同于整个客户端或 SDK 平台隔离完成。

## 解码启动花屏修复

上一检查点 APK 能收到约 60 FPS，但实际截图绿屏/花屏。首个 UDP 帧丢失并请求 RFI 后，MediaCodec 以空 SPS/PPS 启动。
重组器允许首个普通帧及 RFI 恢复帧上送；尚无解码参考图像时，这不是安全的解码启动条件。

共享 SDK 创建/重建解码器之前增加每个显示器独立的 `DecoderStartupGate`：

1. 必须同时具有关键帧标记和完整参数集，才进入解码器创建路径。
2. H.264 要求 SPS/PPS；HEVC 要求 VPS/SPS/PPS 全部存在。参数集语法仍交给真实解码器验证。
3. 等待期间丢弃不满足启动条件的帧，按单调时钟最多每秒请求一次关键帧。
4. 只作用于创建/重建阶段，不丢弃正常解码中的 P 帧，不改变 UDP/FEC + WebSocket 产品边界。

修正版日志确认 `csd-0=26 bytes, csd-1=8 bytes`，MediaCodec 创建 3840×2160 解码器，随后每 5 秒约 293–308 帧。
截图确认实际桌面正常、无此前绿屏/花屏；这仅排除本次观察到的启动缺陷，不宣称所有编码器、弱网和切屏场景已经验收。

## Windows 自连控制通道中断修复

首次短测首帧解码后几秒中断。WS/WSS 补充错误原因日志后明确为
`The WebSocket message exceeded the locally configured limit`，不是 UDP 心跳或启动门禁超时。

临时 Render 元数据诊断发现 `kCursorInfoSync`（类型 70）的字节数依次为：
1,201,247 → 2,702,769 → 6,081,189 → 13,690,033 → 30,802,533。
150% DPI 下，收到的物理光标被默认 DPR=1 的 QPixmap 再放大，Render 自连捕获后反复反馈，每轮面积约增为 2.25 倍。
已对照本机 Qt 6.8.3 Windows cursor adapter 的 `target DPR / pixmap DPR` 和热点换算实现确认。

新增 `cursor_image.h` 做小型 Qt 边界转换：

- 校验尺寸、RGBA 字节数、热点与有限正 DPI；像素复制进 QImage 自有存储，不保留消息缓冲区借用。
- 为物理像素设置接收窗口 DPR，并将热点转换为逻辑坐标，避免反复放大。
- 删除只按 bitmap 内容去重的缓存，避免相同图像但热点或目标 DPI 变化被吞掉。
- 保持原 WS 接收上限及传输拓扑；不增加回退或放宽限制。WS/WSS 保留错误码/原因日志，不记录认证或业务消息内容。

临时 Render 大消息解析诊断已从活动代码撤回，Render 重新编译、发布。
修正版 Windows 20 秒自连持续解码，每 5 秒约 299–304 帧，未再观察到上述断开。
这不是所有缩放、多显示器热切换或远端输入组合的全面验收。

## Android USB 验证

- Xiaomi 22021211RC，Android 14；手机与本机同一局域网。此前仅剩约 274 MB 的安装阻塞已解除，当前约有 34 GB 可用。
- 最终 Debug APK 使用 `adb install -r -d` 覆盖安装成功，未卸载、未清数据、未删除用户文件。
- 局域网 D-6 直连实际画面恢复正常；最终 APK 从账号设备 D-6 入口也成功进入远控，截图确认正常桌面。
- 展开/收起工具栏、系统返回结束确认、结束回设备页、设置/传输返回设备页进行了手工短检查。
  最终 APK 另验证 HOME 后返回实际画面正常，并结束会话回到设备页；没有把 FPS 统计当作画面证据。
- 测试 APK 曾被手机返回 `INSTALL_FAILED_USER_RESTRICTED`，未更改安全设置；本轮不声称仪器化矩阵通过。
- 手机既有 Console 地址为 `https://localhost:30500`。增加 `adb reverse tcp:30500 tcp:30500` 后账号设备恢复刷新，
  不改账号配置或 TLS 校验；USB 断开后此转发不再可用，不代表 Console 的公网或纯 Wi-Fi 地址已部署。

本机截图（含开发桌面，不加入 Git）：
`test-results/android-startup-gate-session.png`、`android-startup-gate-resume.png`、
`android-native-final-account-session.png`、`android-native-final-resume.png`。
账号连接成功不等于文件、剪贴板、语音、录制及远程应用启停均已完成验收。

## 构建、自动化与发布

Windows 聚焦增量构建通过，未运行 release-only 全量构建：

```text
scripts_build\build_cpp_tests.bat px_client test_sdk_stream_helper test_udp_media_failure test_sdk_websocket_reconnect
scripts_build\build_cpp_tests.bat px_client px_render test_client_cursor_image
```

Android `:app:assembleDebug` 通过，最终构建用时 1 分 47 秒。
8 组 CTest 全部通过（17.46 秒）：帧所有权、UDP 状态/故障隔离、WS/WSS 重连、回调生命周期、流辅助和光标图像。
流辅助新增 3 项测试；光标新增 4 项测试，覆盖 100/125/150/200/300% DPI 各 64 次转换、像素源销毁、非法尺寸/长度/热点和 DPI。
光标循环测试复现 Qt 尺寸换算规则；实际 Windows 自连由冒烟补充，不把单元测试当作真实桌面捕获测试。

Native SDK 目录/传输、Windows 入口、Android JNI、WebRTC DLL 链接和 C++ 所有权静态门禁通过。
新辅助头与测试以及流辅助改动通过仓库 clang-format；不格式化无关旧 C++ 文件。
Windows 20 秒冒烟结果为 `processAlive/windowCreated/udpMediaReady/framesDecoded=true`、`decodeErrors=false`。
发布后首次调用冒烟时服务尚未生成 Render，脚本在启动客户端前退出；Render 就绪后正式短测通过。
真机和运行时测试采用短场景，每次均少于 5 分钟；构建耗时单独记录。

| 最终产物 | SHA-256 |
|---|---|
| Windows Client | `4192EC3F8C37B00CEFBBC31AB6C3B691F76527E91C52003B83581CFE37891566` |
| Windows Render（撤回临时诊断） | `A2FA877D8A3E39078331003560D2BFFEEE899A9682C01D36C498005E0D0095DF` |
| Android Debug APK（已覆盖安装并短测） | `86862A4137FBA9D050ECAAC297F69A5CDF64BA30E3F43A619BC221BA8343FD66` |

Windows Client、语音 APM DLL、语言资源、Render、Logo 与两套 Render RTC DLL 已同步至 `build_official/dist` 并核对 SHA-256。
Render RTC DLL 是 Web/Render 的既有运行时，不重新进入 Native Client。发布停止的服务已恢复，手机已结束测试会话。
日志位于 `test-results/native-cursor-dpi-*`、`native-ws-final-android-build.log` 和 `sdk-move-windows-smoke.json`。

## 归档与剩余范围

修改前源码完整保存在以下批次，均有原路径、基础版本、修改状态、原因和 SHA-256 清单，不参与构建：

归档文件保留原始 CRLF 和历史空白，不能为通过空白检查而改写备份；活动源码的 staged diff 空白检查通过。

- `backup/native_decoder_startup_20260907`：5 个启动链路原文件。
- `backup/native_ws_disconnect_diagnostics_20260907`：2 个 WS/WSS 原文件。
- `backup/native_ws_payload_diagnostics_20260907`：临时定位前的 Render router 原文件；活动实现最终已恢复。
- `backup/native_cursor_dpi_20260907`：3 个 Windows 工作区/构建原文件。

下一步继续 CPU RawImage、D3D11、AVBufferRef 设备上下文和 Android Surface 的所有权整理，再分离平台解码适配与独立 SDK 构建。
文件/剪贴板、语音、带音录制、弱网/锁屏及真实应用授权仍需单项短测；iOS/macOS 和统一能力协商仍按产品决策另行推进。
正式发布继续等待既有 FFmpeg 源码/LGPL relink 等合规输入，不放宽门禁。本轮不混入已有 Rust 修改。
