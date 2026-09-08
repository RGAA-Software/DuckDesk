# Native SDK 硬件资源与帧接口检查点（2026-09-08）

> 本轮关闭收尾计划 P1、P2a、P2b，不代表 P3–P5 或整个 SDK 已完成。
> 改动保留在工作区，基线 HEAD 为 `e90892046`，本轮未提交、未 push。
> 后续决定：用户了解必要性后确认继续，语音 UDP 改造放在语音收尾阶段，不阻塞其它收尾；§4 保留当时核查与提案记录。

## 1. 已实施的边界

- `SdkParams` 不再包含 D3D11/Vulkan/FFmpeg 设备或 UI 外观字段。Windows 组合入口创建
  `WindowsVideoResources`，视图资源就绪后调用 SDK Init；解码工厂持有只读平台资源。
- Windows 视图由 Qt parent 独占所有权，以 `QPointer` 观察；不再让 Qt parent 和智能指针同时负责销毁。
- Vulkan 设备使用 `AVBufferRef` RAII 引用，并以别名 shared owner 保留创建该设备的 libplacebo renderer。
  最后一个队列帧释放前，设备及其回调所依赖的 renderer 不提前析构。
- 通用 `RawImage` 头仅描述 CPU owning buffer、平面布局、像素格式和平台输出扩展边界，不包含平台设备头。
  校验尺寸、总大小溢出及源长度，支持奇数色度尺寸；新分配数据确定初始化。
- CPU 转换按平面 stride 复制，支持负 stride、padding、NV12 转 I420；队列仍持有旧帧时不复用其写缓冲。
- D3D11 输出拥有 COM texture、设备及源 AVFrame 引用。只有 texture AddRef 不足以占住 FFmpeg 解码池的 subresource；
  保留源帧引用才可防止队列消费期间被后续解码复用。Vulkan 输出保留独立帧引用和设备 lease，不新增 GPU 回读。
- Android Surface 呈现输出以 Presented 元数据表达，不冒充 CPU 图像。Surface 交接继续使用已完成的平台 owner。
- 所有解码器输入统一为 `span<const uint8_t>`。FFmpeg packet 立即取得独立缓冲并保证尾部 padding 为零，
  不借用调用方临时编码字节。codec context、packet、frame、硬件 buffer 使用 RAII。
- 部分初始化失败、未 Init 即 Release、重复 Release 均可清理；移除不安全的失败路径 drain 和无用参数分配。
  D3D 设备交给 FFmpeg 时取得独立 COM 引用，不通过清空共享设备字段完成关闭。
- D3D/OpenGL/SDL/Vulkan 现有消费者同步接入新的帧边界。修正 SDL 图像入口递归及 OpenGL 图像读取的旧临时分配路径。

第三方 FFmpeg、libplacebo 和系统 ABI 不改写；仍允许标注的瞬时 C API 借用。
这不是宣称所有历史解码器内部、Render 插件或全仓旧代码均已完成智能指针迁移。

## 2. 备份

以下均在修改前归档工作区原始字节，保留原路径和状态；本轮结束时再次逐文件核对 manifest SHA-256：

| 批次 | 文件数 |
|---|---:|
| `backup/native_sdk_platform_resources_20260908` | 24 |
| `backup/native_sdk_frame_contract_20260908` | 30 |
| `backup/native_sdk_frame_sprite_20260908` | 1 |

备份不参与编译/打包。之前已有的 SDK 搬迁、分层、连接参数和 Surface 备份继续保留。

## 3. 验证与产物

使用日常 `scripts_build\build_cpp_*.bat` 增量入口，未运行 release-only `scripts_build\build_official.bat`。

| 检查 | 结果 |
|---|---|
| Windows Client + 解码工厂/帧所有权测试目标 | 最终构建通过，`test-results/sdk-p2-windows-delivery-retry.log` |
| Android Debug | 构建通过，`sdk-p2-android.log`；末次 `sdk-p2-android-final.log` 确认 UP-TO-DATE |
| 最终 CTest 8 组 | 全通过，27.32 秒，`test-results/sdk-p2-ctest-final.log` |
| Windows 本机实连 | 20 秒：窗口、UDP 媒体、持续解码正常，无解码错误，`test-results/sdk-p2-smoke.log` |
| Android USB 实连 | 02:07–02:08，文件页返回后仍为 59 fps / MediaCodec hardware，结束会话正常 |
| C++ 所有权/新增 150 列检查 | 通过；新独立文件已按仓库 clang-format 格式化，不机械重排历史文件 |
| SDK 目录、源码分层、Native 单通道、双端设置/JNI 边界脚本 | 全通过 |
| `git diff --check` | 通过；Git 提示的 LF/CRLF 转换不是空白错误 |

8 组分别为 `sdk_source_sets`、`sdk_decoder_factory`、`sdk_connection_params`、`test_udp_media_state`、
`av_frame_ownership`、`udp_media_failure`、`sdk_stream_helper`、`sdk_websocket_reconnect`。
其中解码工厂组 9 个用例、帧所有权组 12 个用例，覆盖资源保活、输出撤销、回调内退出、队列取消、
帧复制/共享、非法尺寸、padding、负 stride、NV12、packet 所有权及部分初始化失败。

中途修正过两处 RAII `.get()` 编译调用；另一次链接因测试进程占用 exe 失败，待测试退出后重建通过。
先前测试结果不作为最终二进制验证，最终构建后重新运行上述 8 组测试。
P1 首次短测遇到发布后 Render 尚未启动完成，待就绪后通过；P2 最终短测也通过。

Windows 已同步到用户使用的 `build_official/dist`，Client、APM DLL、语言资源完成 SHA-256 对照。
末次再次核对 Client 构建树与 dist 的 hash 相同：

```text
px_client.exe
F2E4F8EEF8E28D9DF3649666147548AF6504D7B6B49C65A3161A93D07EBAA6C1

app-debug.apk
5E6E536E5C3DB4EE7C38B460F329039AD68CAC313A2EDF8BDC96DC1C3C93A103
```

手机 `e2b3b128` 使用 `adb install -r -d` 覆盖安装，未卸载/清数据；安装后的 `base.apk` hash 与上述 APK 一致。
手机短测日志出现两次 Surface 解码器创建及 `ThunderSdk exited`，本次进程未检出 Released state/Fatal signal/FATAL EXCEPTION。
结束时手机已退出远控，本机 `px_service` 保持 Running。

未验证：所有 GPU/驱动组合、所有分辨率的 UI 显示、Android 软件解码实机矩阵、人工双端听音、长压测及本轮 Web 首帧。
工厂失败和引用计数测试不能代替所有硬件后端实际出图。没有把旧文件/录制/语音结果当作本轮新增功能的验收。

## 4. P3c-2 提前追踪：需要确认的协议变更

当前真实路径，不按类名判断：

1. Windows Workspace、Android NativeSession 的语音发送最终调用 `ThunderSdk::PostMediaMessage`。
2. `NetClient::PostMediaMessage` 取得 `CurrentMediaConnection()`；该连接由
   `MakeDirectWebSocketMediaConnection()` 创建，是 WS/WSS，不是 `UdpDirectConnection`。
3. Render `VoiceCallRuntime::DispatchAudioFrame` 构造 `kVoiceAudioFrame` 后走 stream-message 路由；
   当前 UDP `SendToStream` 未实现此消息发送，`Broadcast` 只提取普通 `kAudioFrame`。
4. `PxUdpProtocol` 仅有 Video=1、Audio=2、Ctrl=3。Audio 包是系统声音 Opus，缺少通话归属标识；
   Render `UdpRuntimeState::Start` 的接收分流只处理 Ctrl，上行语音不能直接复用现有调用而生效。

因此，“把 PostMediaMessage 换成某个 UDP send”不足以完成该项。把语音直接塞进系统声音包也会混淆播放路径，
并丢失通话归属与迟到包检查。当前不修改线协议或认证契约，遵循收尾计划 §7 的单独确认门禁。

### 建议的最小变更（方向已批准，尚未实现）

后续状态：用户已确认继续这一最小范围，收尾计划已记录；本节保留当时的路径核查，不再作为重复审批门禁。

- 在现有 UDP 媒体连接增加独立 Voice 包类型，使用有长度上限的通话 ID、序号、采集时间及 Opus 数据；
  可封装现有 VoiceAudioFrame 消息，不新增另一套音频引擎。不分片、不走可靠重传，超出单包预算明确拒绝。
- 双向复用现有 WS 认证后的 UDP association/端点绑定；服务端从绑定获得设备和 stream 身份，
  不信任数据包自报的身份。接受通话前、挂断后、旧 association/call ID、重复或过期序号均丢弃。
- Render 增加有界上行语音入口和面向已绑定 stream 的下行语音发送；客户端增加独立语音收发入口，
  不经过文件消息的可靠队列/等待。语音同意、拒绝、配置、静音及挂断仍走 WS。
- 现有关联校验不等于逐包密码学认证；本次提案不默默增加密钥协商、加密协议或扩大公网支持。
  对端点复用、注销与并发迟到包补行为测试，不弱化现有校验。
- Native/Render 同版本一起更新，不加入旧版 WS 语音回退或用户通道选择。Web 继续使用既有 RTC 音轨及控制路径，
  公共 Render 分流必须补回归，不能把 Web 语音也改到 UDP 私有协议。
- 验证包含双向真实包、拒绝/超时/挂断、未授权端点、旧会话/序号、文件并发、短实连；人工听音单独标注。

影响范围：共享 UDP 协议及测试、SDK 网络与语音入口、Windows/Android 接入、Render UDP transport/语音路由。
不涉及 Rust Console 权限变更，不动用户已有无关 Rust 修改，不涉及 iOS/macOS/P2P/Relay。

## 5. 尚未关闭的工作

- P3a：先记录 Windows Panel 启动/续票与 Android Kotlin 授权的实际持有者，再共享尝试、generation、取消和续票决策；
  HTTP/账号凭据不搬进 SDK。Windows 完整 Console 授权不在 Workspace，不能按 Android 的类结构直接套用。
- P3b-1：核对已有 `FtAsyncSession` 与双端剪贴板适配，合并真正重复的任务/会话归属规则，保留 Done 成功语义及 SAF/OLE 边界。
- P3b-2：以现有 `RecordWriter` 收拢录制订阅、generation、失败和断线收尾；平台目录/MediaStore 保留在宿主。
- P3c-1：合并双端语音消息构造及编排，继续复用已有状态机、音频端点和 APM。
- P3c-2：上述独立 UDP 语音包方案待方向确认，不能勾选已完成。
- P4：SDK-only 构建入口、无 Qt UI 的最小消费示例、Windows/NDK 独立构建与接入说明尚未实现。
- P5：本轮完成了 P1/P2 局部回归和发布；其余受影响功能与 Web 保留路径须在后续版本继续验证，不能提前关闭最终验收。
