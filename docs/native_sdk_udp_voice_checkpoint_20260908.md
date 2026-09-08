# P3c-2 Native UDP 语音（2026-09-08）

Windows/Android 的实时语音已从 WS 迁到现有 UDP 媒体连接，Render 完成对应上行入口和定向下行发送。
沿用已批准的最小方案，未增加公网 P2P/Relay、Apple 后端、密钥协商或 Web 协议改造；本轮未提交、未 push。

## 实际路径和约束

- 客户端：共享 VoiceCallController → 独立 send_audio → ThunderSdk/NetClient::PostVoiceAudioMessage → UdpDirectConnection。
- Render 上行：UDP Voice 解析 → 当前端点/association 绑定 → typed UdpVoiceFrameEvent → 活跃逻辑会话校验 → VoiceCallRuntime → 原音频端点。
- Render 下行：VoiceCallRuntime 原 Opus 编码 → 已知 Native WS 所属路由识别语音帧 → UdpTransport::SendVoiceFrame → 已绑定 stream。
- 同意/拒绝、音频配置和挂断继续走可靠 WS；Native 实时帧不回落到 WS/RTC/Relay。
  SDK 拒收 WS 语音帧；Render 的 Direct 会话也拒收旧 WS 语音帧。Web 的 RTC PCM/授权分支未迁入私有 UDP。
- 新包类型 4，与视频 1、系统声音 2、控制 3 分开。公共头后包含两个身份长度、Opus 长度、序号、64 位采集时间，再接 association/callId/Opus。
  小端编码；两个身份各 1–128 字节、无 NUL，Opus 1–1275 字节，总包 ≤1400 字节，不分片和可靠重传。
- association 不建立逻辑会话。身份由已绑定端点的服务端记录提供；不存在、撤销、被替换端点、错误关联、过期逻辑会话均拒绝。
  包中保留当前关联码，防止端点地址复用时旧绑定数据混入；这不是逐包密码学认证，也不是加密承诺。
- callId 与原语音序号窗口继续验证同意前、挂断后、旧通话、重复序号。设备 jitter/Opus PLC 沿用原实现。
- 两端各有独立 8 包待发容量；回调取消/销毁也释放容量，不占文件可靠队列，不等待可靠重传。
  Render 上行在 UDP I/O lane 同步分发到有界音频 jitter，不额外堆入无界控制任务队列。
- UDP runtime 以原子 shared_ptr 发布/快照，成功 Start 后才公开；发送与关闭并发时不会读取正在重置的 shared_ptr。
- UDP 媒体故障会结束语音并阻止重试，保留控制/文件在线；用户创建新会话后恢复，不复活已失败的 UDP 会话。

## 验证

| 验证 | 结果与限制 |
|---|---|
| Windows 增量 Client/Render/RTC DLL | 通过；未运行 release-only 构建 |
| Android Debug + 18 项 JVM | 最终通过，55 秒；APK 未安装到正在供别的项目使用的手机 |
| SDK 协议与包预算 | 6 项：完整时间戳/owning payload、所有截断/畸形长度、包上限、普通音频隔离、取消释放容量 |
| SDK 控制器 | 10 项，包括媒体失败结束语音、旧响应不能复活和恢复入口限制 |
| 实际 SDK UDP/Render transport | 5 项：真实双向 datagram、撤销与旧关联、陌生端点、回调内停止、完整 NetClient 同时发送文件和语音 |
| Render 语音/服务/关闭、UDP 故障 | 与上述合计 7 组全通过，15.90 秒；`sdk-udp-voice-final-ctest.log` |
| 剪贴板/文件流/录制/重连回归 | 最新重新链接产物 8 组全通过，14.66 秒；`sdk-udp-voice-related-delivery-ctest.log`，不等于系统剪贴板跨进程实测 |
| 独立 Windows full 消费 | 构建/离线生命周期通过；协议和控制器 2 组通过，0.99 秒 |
| 独立 Android full 消费 | 最新编译/链接通过，未在手机执行 |
| Windows 本机 | 最新 dist 20 秒：窗口、UDP 媒体、持续解码正常，无解码错误 |
| WebRTC 浏览器保留路径 | 最新 Render/dist 认证实连，1920×1080；12 秒采样解码增加 541 帧，0 新增丢包，host/UDP 候选 |
| 目录/Native 传输/双端设置/JNI/所有权/WebRTC DLL 边界 | 通过；diff whitespace 通过 |

真实传输测试注入已授权的测试 association，并直接运行生产 UDP transport/SDK；不等于再次验收 Console 账号流程。
文件并发测试是约 1 MiB 可靠通道负载，不冒充完整文件引擎哈希验收。服务端拒绝/同意和 RTC PCM 边界使用 SDL dummy 后端。
Web 使用本地运行 Render 的凭据在内存中构造启动参数，未将密码、票据或完整启动 URL 输出到日志。
测试脚本独立 headless profile 在结束后清理，没有接管用户浏览器。Web 首帧通过不等于双端真实语音听音通过。
此前只验证协议/控制器的结果保留在旧检查点，不用旧 APK 或旧测试冒充最新手机验收。

## 发布产物

已同步 `build_official/dist` 并逐项匹配 SHA-256；共享 DLL 占用时短暂停止 px_service，发布后恢复运行。

| 产物 | SHA-256 |
|---|---|
| px_client.exe | AE45F25299DEF89442EAEF9544E04D2A51FA6EC0CFBD1FA72F2E36EB2916BF87 |
| px_render.exe | 35A533D3B91DD7B820C3D1616539FD3D4FEF2C8122C3BA22B320FEC8EEC7B3E8 |
| px_voice_apm.dll | 21411B44D9C7C8E6F270B6E932EF48C1511E542D0EA36BE25F37578ECAE669B3 |
| px_render_rtc_remote.dll | FE14E162E03D74FDE50253BCEA94F126337B897BA8591AF4419CEB122808D019 |
| px_render_rtc.dll | 986983F65C52CC3F8B9350492F1A2195D375F0C3F7A629FD4EF43D60CDFEDA61 |
| Android Debug APK（未安装） | DEC27B93A991663A7D1A3B032FEE5360572C46507801BCFEF1BF73C3E8A376EC |

Client 三份语言资源、Render 图像资源也校验匹配，见 `sdk-udp-voice-{client,render}-publish.log`。
普通构建和测试日志均在 `test-results/sdk-udp-voice-*`。

## 备份和剩余验收

修改前工作区字节及哈希保存在以下批次，原文件不参与当前构建：

- `native_sdk_udp_voice_wire_20260908`：2 文件。
- `native_sdk_udp_voice_client_20260908`：8 文件。
- `native_sdk_udp_voice_render_20260908`：11 文件。
- `native_sdk_udp_voice_tests_20260908`：2 文件。
- `native_sdk_udp_voice_availability_20260908`：3 文件。
- `native_sdk_udp_voice_ws_rejection_20260908`：1 文件。

以上 6 个批次的 27 份归档已重新逐项核对 manifest SHA-256，全部匹配。

尚未关闭 P5：手机最新包的界面/重试/剪贴板实测、Windows 系统剪贴板跨进程实际复制、真实设备语音与人工听音。
手机占用期间不切换应用、不安装、不卸载、不清数据。待空闲后只覆盖安装，每个真实场景最多 5 分钟。
Render 用户代理的旧剪贴板路径信任问题仍是此前记录的单独安全审查事项，本批未修改无关 Rust 代码。
