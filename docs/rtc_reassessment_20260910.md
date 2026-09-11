# RTC 重连无视频复核（2026-09-10）

## 结论与修改

- 保留可配置端口、公网 UDP host 候选、失败会话超时及按绑定释放席位的实现。这些改动不等于媒体重连已通过。
- 撤回 `IsMediaConsumerActive()` 改为 `ICE && view` 的尝试。消费者查询独立于捕获周期执行，没有证据支持“等待 SCTP 导致永久停采”；恢复原有交互通道 / 监看墙判定。
- `pipeline.performance clients=0` 来自连接事件统计，不是 RTC 媒体消费者计数，不能据此判定采集未启动。
- RTC DLL 原先未初始化自己的文件日志，旧 `net_rtc_local.dll.log` 不代表当前进程。现在写入公共数据目录 `px_logs/px_rtc_local_<PID>.log`，避免同节点多 Render 共用同一个日志文件。
- 确认并修复 `RtcServer::Exit()` 销毁顺序：DataChannel 析构需要在信令线程同步注销观察者；原实现先停线程再析构通道，清理任务因此无法返回，阻塞该 RTC 工作队列的后续关键帧请求。
- 现在先关闭 Peer、释放通道与轨道、停止音频拉取并释放工厂，再停止 RTC 线程；增加清理完成日志。未改变既有 DLL / libwebrtc ABI 所有权契约。
- 浏览器通用诊断去掉点击探针，以 `getStats()` 中输入数据通道的 `open` 状态检查就绪；此检查不再声称完成键鼠端到端回放验收。Native 验收默认值修正为 `udp_direct`。

## 真实复测

本机真实 Chrome → 90 Render，Console 签发授权票据。每次媒体采样 8 秒、连接预算 35 秒；未做长时间压力测试。

| 阶段 | 结果 |
|---|---|
| 修复销毁顺序前首次连接 | 1080p，通过，新增 362 帧 |
| 修复前第二次连接 | 失败；ICE / SCTP 已连接，捕获和编码持续进行，但不断丢弃 delta 帧等待首个 IDR；第一次清理没有完成 |
| 修复后首次连接 | 1080p，通过，新增 370 帧；观测期丢包 0、冻结 0；启动期报告丢包 6 |
| 修复后第二次连接（不重启 Render） | 1080p，通过，新增 363 帧；启动期及观测期丢包 0、冻结 0 |
| 两次旧会话回收 | 均记录音频拉取停止与 `RTC peer cleanup completed`，随后连接正常 |
| 随后真实 Windows Native（不重启 Render） | `build_official/dist/px_client.exe`：授权 WS + UDP 连接、UI 首帧和文件通道通过；音频为授权成功但源空闲 |

两次成功 Web 连接均选中远端 `39.71.45.66:5000/udp host`；没有以标准 RTC 回退代替直连。

验证还包括 C++ 所有权检查、格式检查、Render 增量构建，以及 `logical_session_registry`、`rtc_payload_authorization`、`rtc_candidate_sdp`、`webrtc_transport_lifecycle` 测试。生命周期单测覆盖重复启停、回调内停止、注销及排队事件；真实建连后的通道析构回归由上述两次 Chrome 连接验证。

此轮未验收标准 RTC / TURN、音频实际播放、文件内容校验或输入回放。既有独立服务、多节点与 Android 真机待办不因本次通过而关闭。最终 DLL 发布时继续核对构建目录、`dist` 和 90 文件 SHA-256。
