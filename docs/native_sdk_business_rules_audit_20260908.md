# Native SDK 业务规则归属核对（P3 工作记录）

> 2026-09-08：源码审计，不表示 P3 已完成。先完成 P4 构建边界，再按下列清单实施和验证。
> 用户确认语音 UDP 改造保留在语音收尾，不阻塞其余工作。Apple、公网 P2P/Relay 仍不在本轮范围。
> 后续实施结果：[会话尝试](native_sdk_attempt_checkpoint_20260908.md)、[剪贴板](native_sdk_clipboard_checkpoint_20260908.md)、
> [共享录制](native_sdk_recording_checkpoint_20260908.md)。下表保留初始归属，不能把历史风险当作当前未修复结论。

## 当前归属与目标

| 规则/能力 | 当前实际持有者 | 收尾方向 |
|---|---|---|
| WS 连接尝试、超时、退避、generation、异步停止 | 双端 SDK connection 共用 `PxReconnectSupervisor` / `PxConnectionAttemptWorkflow` | 复用现有共享实现，补受影响测试，不创建第二套状态机 |
| WS 授权/占用/策略拒绝 | SDK connection / `MakeSdkWebSocketRejectionError`，宿主接收结构化事件 | 保持终止重试与错误区分，不自动重复消费被拒票据 |
| UDP 关联和媒体失效 | SDK NetClient 的 managed media generation + UdpDirectConnection | 继续独立于文件/账号在线状态 |
| Windows Console 授权启动 | Panel `StreamLaunchAuthWorkflow` / AppStreamList / RunningStreamManager | 留宿主 HTTP、应用实例启动和子进程参数交付；确认每次新启动取新票，不误当作 Workspace 续票 |
| Android 一次性票据尝试 | `AndroidRemoteSessionTransport` / ConnectionTicketAttempt | 过期前 15 秒或尝试过即续票，保留最新轮换令牌；账户 HTTP 和取消结果仍需逐项核对 |
| 文件可靠发送/背压/Done 回执 | SDK PostFileTransferMessage + `FtAsyncSession` / FtEngine | 共用引擎及成功语义，不把 EOF 当接收端完成 |
| Windows 文件 UI、Android URI | Client 内部文件模块 / NativeSession、Kotlin SAF | 系统访问留平台；不把 URI 当桌面路径 |
| 文件型剪贴板 | Windows ClipboardManager/OLE、Android NativeClipboard | 提取真正重复的远端描述/请求校验、会话归属与迟到响应规则 |
| 编码帧封装写盘 | 双端共用 `RecordWriter` | 不复制 MP4/FFmpeg 实现 |
| 录制开始/停止/排队归属 | Windows MediaRecordRuntime、Android NativeSession | 共享录制 run/generation 和有序收尾，保存目录与 MediaStore 留宿主 |
| 通话状态与音频基础 | 已共享 VoiceCallState、VoicePacketTransport、VoiceAudioEndpoint | 继续复用 |
| 通话消息构造/编排 | Windows ct_voice_call_protocol/Workspace、Android NativeVoiceCall | 存在实质重复，提取共享 Native 编排，注入设备端点、发送和状态投递 |
| Native 语音实时通道 | 目前为 WS；VoicePacketTransport 只是有界发送工作队列 | 最后按批准方向接入现有 UDP 连接的独立 Voice 包 |

## 录制风险的原始审计记录

以下四点是实施前记录；现已通过共享 run、显式编码校验和双端编码订阅处理。
真实验证另发现 Windows 原入口漏录音轨并修复。当前证据见 [录制检查点](native_sdk_recording_checkpoint_20260908.md)。

1. Windows `HandleMessage` 排队后由 runtime 的单个 `recording_` 布尔值决定是否接收。
   `StopRecording` 同步调用 End，随后 Start 可重新设置该布尔值；旧排队任务没有 run 标识，存在落入下一次录制的代码路径。
   应以重复开始/停止和队列阻塞用例证明并修复，不能只加一层智能指针。
2. Android 已带 recording generation，但开始/结束/断线收尾分布在 NativeSession 多处；Windows 又有自己的规则。
   新共享模块应持有具体录制 run，并明确“已接收的帧排完再写 trailer”与“取消的迟到任务丢弃”的不同语义。
3. 两端都把非 HEVC 的视频枚举默认映射为 H264。应显式接受已有 H264/HEVC，拒绝其它格式，不能给未知编码贴错标签。
4. Android 录制音频当前在播放解码后的 PCM 回调重新编码 Opus；SDK 已提供编码音频订阅。
   收拢时优先复用原始合法 Opus，核对 48 kHz/双声道/帧采样数和丢包补偿语义后接入，避免无意义解码再编码。

原始风险与后续实测结论分别保留，避免把审计推测当成当时已复现的问题。

## 实施约束

- P3a 不把 Kotlin Activity、Panel 账号 API 或 Console 应用启动工作流搬进 SDK。
- 对已充分共享的部分，以依赖和行为测试关闭；只对真实重复/竞态添加小而具体的模块。
- 新增录制/通话编排需有 typed 状态及 RAII 资源，回调弱引用保活，无服务定位器或通用属性袋。
- 文件/剪贴板必须保持接收端完成与 generation 语义；不恢复 Client 插件 DLL。
- 每次接入前保留当前工作区字节备份；双端构建及受影响短测试通过后再关闭对应条目。
