# Native SDK 共享录制检查点（2026-09-08）

> P3b-2 已完成：Windows、Android 接入同一 `RecordingSession`，丢帧时钟补丁测试及双端交付核对完成；不表示 P3/P5 全部完成。

## 实施结果

- `src/px_client_sdk/sdk_recording_session.*`：每轮独立对象、拥有编码消息的有界 FIFO、Stop 拒收和排空、worker 上统一关闭 writer。
- 显式接受 H264/HEVC 与 48 kHz 双声道 Opus，未知编码不默认当 H264；队列溢出明确失败。
- 两端都订阅 SDK 编码视频和编码音频，不复制封装器。Android 不再把播放 PCM 重新编码为 Opus。
- Windows 退出旧 MediaRecorder/MediaRecordRuntime 实现；保存目录、提示留内部 Client 模块；Android staging/MediaStore 留宿主。
- Windows UI intent 带轮次，失效的排队点击不启动旧操作，旧录制失败不覆盖下一轮按钮状态。
- 每轮输出名称带独立标识，重叠收尾不撞同秒文件名；保持原 file_prefix，滚动清理语义不按轮次割裂。
- RecordWriter 保留首个打开、分配、写包、header、trailer、IO/关闭错误；只把实际完成封装的分段计为成功。
- 新 writer 不删除其他 writer 或崩溃文件的 `.recording` 标记；滚动清理跳过带标记的文件。失败文件不会误展示为可播放。
- UDP 丢包空标记不伪装为 Opus；按顺序传递到录制核心，保留缺失包的采样时长，避免跳过空帧导致音轨逐渐提前。

## 实测发现与修复

Windows 原录制仅接 raw message，UDP 音频本就不经过该入口。首次真实文件只有视频轨；现已改成编码音视频订阅，
并从 ClientModuleManager 的通用消息分派移除录制入口，防止视频双投递。修复后文件包含音轨且完整解码通过。

SDK 独立消费新增 `px_media_record` 依赖，但仍不配置 Qt/RTC/Relay；core 消费仍不依赖封装/编解码器。

## 已完成的短验证

| 场景 | 实际结果 |
|---|---|
| 共享 run 测试 | 队列排空、重复 10 轮、旧轮次拒收、回调内 Stop/销毁、异常回调、溢出、未知编码、无关键帧、不可写目录、并发真实 MP4 |
| Windows 内部录制模块 | 重复启停，以及失败的两轮携带各自 UI intent，通过 |
| 既有 RecordWriter | 音视频同步、分段清理、sidecar、提前停止、命名、音频采样时钟，通过 |
| Android 正常停止 | 3840×2160 H264 12.772 秒，48 kHz/2 声道 Opus 12.820 秒；767 视频包、645 音频包；MediaStore 保存成功 |
| Android 录制中结束远控 | H264 6.615 秒、Opus 6.620 秒；402 视频包、335 音频包；返回首页、文件发布和 SDK 退出成功 |
| Windows 正常停止（接线修复后） | 1920×1080 H264 7.754 秒、48 kHz/2 声道 Opus 7.760 秒；两轨完整解码通过 |
| Windows 确认退出时收尾 | H264 16.247 秒、Opus 16.280 秒；marker 清除、文件完整解码、日志记录 ThunderSdk exited |
| 声音数据 | Android 正常录像 mean/max −23.1/−11.8 dB；Windows 修复后录像 −9.8/−0.0 dB，并非只有空音轨；未把此当人工听音验收 |
| Windows UDP 实连 | 60 秒及修复后的 45/60 秒短场景：窗口、媒体关联、连续解码、无解码错误 |

每次场景均不超过 5 分钟，没有长压测。
本机旧 ImageMagick 附带 FFmpeg 的 Opus 解码不可靠；最终采用仓库构建依赖中的 FFmpeg 8.1.1。
检查可变帧率录像时用 `-fps_mode passthrough -enc_time_base:v demux`，避免 null 输出重采样时间基产生虚假重复 DTS 报告。

退出验证中，通用 smoke 脚本会在关闭确认框未确认时 5 秒后强杀；该次测试留下未封装文件及 marker，
已完整移动到 `test-results/recording-forced-termination` 保留证据，不改产品实现来掩盖强杀。
随后点击真实确认框完成正常退出，文件通过。该次通用 smoke 因主动提前退出而报 process-exited，
不是“持续存活”测试通过；退出场景以确认操作、关闭日志和最终 MP4 为证据。

## 构建、交付与证据

- Windows 使用 `scripts/build_cpp_target.bat`，Client、Render、受影响测试目标；没有运行 release 全量脚本。
- Android `:app:assembleDebug` 通过，USB `install -r -d`，没有卸载或清数据。
- P4 完整 Windows/Android 独立消费者均重新构建；Android 离线消费者在手机执行通过；Windows core 回归通过。
- 首次共享录制验收和接线修复日志在 `test-results/sdk-recording-*`；丢帧时钟最终构建/测试为 `sdk-recording-gap-*`。
- 丢帧补丁后 3 组 CTest 全通过（2.99 秒），独立 Windows 消费/录制 2 组通过（0.75 秒）；Android full 独立构建通过。
  首次测试与构建后处理重叠，Client 模块测试被文件锁阻止启动，等待构建完成后重跑通过，没有把 Not Run 当通过。
- 最新 APK 覆盖安装后，手机 base.apk 与本地 SHA-256 一致。

最终产物 SHA-256（构建树与 dist 一致，Android 为本地与已安装包一致）：

| 产物 | SHA-256 |
|---|---|
| px_client.exe | `FE6B822580B549E4E12C79CC2164918741F91590589A518A9B55E2BA72D772E3` |
| px_render.exe | `6974A472761AEDA540737A2AE08EB8B468B950A200C5B931BBED03DF54DC866B` |
| px_voice_apm.dll | `21411B44D9C7C8E6F270B6E932EF48C1511E542D0EA36BE25F37578ECAE669B3` |
| app-debug.apk | `C5EBC67C1BE09F0EAA904FB3509E5658B554EC77ABA7150E61717375B8B1AA36` |

Render RTC 两个运行库及客户端 3 份语言文件也已核对，完整记录见 `sdk-recording-gap-*-publish.log`。
实机录像验证发生在最后的丢帧时长补丁前；最后补丁由真实封装/解码确定性测试覆盖，没有声称再次进行损网实机测试。

## 备份

旧实现按修改前工作区字节保留，未从 HEAD 重建，也没有更改无关 Rust 工作：

- `backup/native_sdk_recording_run_20260908`（13 文件，包含退出的旧 MediaRecorder）
- `backup/native_sdk_recording_notifications_20260908`（4 文件）
- `backup/native_sdk_recording_writer_errors_20260908`（3 文件）
- `backup/native_sdk_recording_ui_generation_20260908`（11 文件）
- `backup/native_sdk_recording_module_tests_20260908`（2 文件）
- `backup/native_sdk_recording_windows_subscription_20260908`（2 文件）
- `backup/native_sdk_recording_audio_gaps_20260908`（6 文件）

## 边界

录制仍使用现有视频接收时钟与音频采样时钟，没有引入采集时间戳协议。
丢包时长保持由确定性封装测试验证，不代表长期损网、磁盘满/拔盘、所有硬件编码格式已实机验收。
没有新增 HEVC 硬件矩阵或蜂窝公网测试。iOS/macOS、公网 P2P/Relay、完整能力协商继续排除。

剩余 P3a、P3b-1、P3c 和 P5 按主计划推进，不能因本批录制通过关闭它们。
