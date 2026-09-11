# Native WebSocket/TCP 对照模式

## 范围与用法

2026-09-11 用户要求恢复 WebSocket 音视频，并提供强制 TCP 参数和 UI 开关，用真实 Windows Client 对比 UDP/FEC。
本次覆盖 Render 直连 WebSocket 音视频；不把旧 WebSocket Relay 的归档代码存在视为 Relay 已恢复或已验收。

- 默认仍为 WS 控制/文件/鉴权 + UDP/FEC 音视频，无自动媒体回退。
- `px_client.exe` 原有启动参数追加 `--force_tcp=1`：音视频通过同一条已认证 WebSocket 发送，不创建 UDP 媒体连接。
- `--force_tcp=0` 或不传：默认 UDP/FEC。
- 设备卡片 → 设置 →「强制 TCP 连接（下次连接生效）」；保存到本地连接记录。
- 云应用卡片菜单 → 同名可勾选选项；按应用保存本地偏好，启动时重新读取，不落盘实例票据。
- 已建立的连接不热切换；断开后重新连接生效。RDP 仍走原有专用可靠通道，不改变其协议。

## 实现边界

- `SdkMediaTransport` 明确区分 UDP 和 WebSocket；每个 SDK 会话使用不可变配置快照。
- SDK 在 TCP 模式移除调用者路径里残留的 UDP 参数；不申请 UDP association，不启动 UDP 探测/超时。
- 恢复 WS 视频、音频、语音的类型化回调与 ACK；UDP 模式仍拒绝 WS 媒体，避免双路重复解码。
- 上行语音在 TCP 模式走已认证 WS，并沿用队列上限；本轮真实语音通话不在验收范围内。
- WS/WSS 在接收回调里关闭时，将关闭工作交给阻塞工作线程，并保持连接对象存活到关闭结束，避免等待自己的 I/O 线程。
- 控制、文件、票据鉴权和 Render 端口映射保持不变，不修改本机网卡或路由器。
- 新增 `TCP media window` 日志，记录每 5 秒收帧率、最大间隔与超过 100ms 的间隔次数；这是接收统计，不等同逐帧显示正确性。
- 统计面板从 SDK 的实际媒体通道状态显示 `WebSocket/TCP` 或 `UDP/FEC + WS`，不再写死 UDP 文案。

## 验证

本机增量构建目标：`test_udp_media_failure test_sdk_connection_params px_client px_panel`。
测试覆盖显式 TCP 收媒体/ACK、控制与文件共用连接、残留 UDP 参数清理、无 UDP 探测、重复启停、回调内关闭与队列中对象释放。

真实测试命令：

```powershell
pwsh -NoProfile -File scripts/test_udp_media_v2_windows.ps1 -ForceTcp -MouseSweep -ObserveSeconds 180 -MouseSweepSeconds 120
```

结果保存在 `test-results/tcp-media/<时间>/`；复用同一游戏、同一客户端和 85% × 75% 鼠标移动区域。
每轮最多 5 分钟。完成前不标记真实联调通过，截图需人工复核。

### 调试环境恢复

11:00 前检查发现 90 的 Console 和 `px_service` 已停止；远程登录可用。
仅启动既有 `Pixels-Console-Debug` 计划任务与 `D:/software/esprit_169811/render/px_service.exe` 服务，未改配置、账户、网络或重新安装。
随后 Console `https://39.71.45.66:4600` 返回 HTTP 200。

### 结果

- 增量构建通过。CTest 两个目标共 12 个用例通过；回调内关闭另外重复 20 次通过；C++ ownership 门禁通过。
- 本机数据库只读核对：`stream.force_tcp` 为 `INTEGER NOT NULL DEFAULT 0`，已有记录默认继续使用 UDP。
- Client、Panel、三种语言资源已发布并核对 SHA-256。通用发布脚本尝试覆盖被其他进程占用的、未变化的 `d3dcompiler_47.dll` 时失败；
  随后单独核对该 DLL、`dxcompiler.dll`、`dxil.dll`、语音 DLL 的源/目标哈希一致，无须停止无关进程或重复替换它们。
- 最终 Client：`08980BDAAF67A3317C0F14FB00394AFB27907F83097306F2256483BB67655BCF`。
- 180 秒测试的 Client：`D7AFD991179E506A7E3E9ED741E7B8D61840D7C651B832220A590A8843EE2817`；随后仅增加统计面板实际通道显示，未再修改传输逻辑。
- 最终 Panel：`B3400BEA0FE3A8EF6434BDE4E6FAA010D30B44A83A5AF68313F75A4768024B34`。

第一次 `test-results/tcp-media/20260911-110638/`：成功收音视频，但鼠标移动仅 4.97 秒即失去前台，不算完整鼠标验收。

第二次 `test-results/tcp-media/20260911-111038/`：

| 项目 | 结果 |
| --- | --- |
| 真实客户端/目标 | dist 内 Windows Client → 90 `39.71.45.66:4613`，2dAdventure 游戏 |
| 会话 | `inst-36-3ceebc12`，结束后通过正常 API 停止测试实例 |
| 观察/鼠标 | 180 秒 / 连续 120.01 秒，7508 次，区域 85% × 75% |
| 通道 | TCP 视频和音频均有收包证据，未启动 UDP 媒体 |
| 35 个收帧窗口 | 平均 30.02 FPS，最低 27.5 FPS |
| 收帧间隔 | 最大 395ms，超过 100ms 共 77 次 |
| 画面抽查 | before、60 秒、90 秒、after 截图显示不同游戏视角，未见花屏 |
| 结论 | WebSocket 直连音视频与鼠标功能调通；严格无卡顿阈值未通过 |

不可把平均 30 FPS 解读为没有短暂停顿，也不把截图抽查解读为逐帧完整视觉回归。
本轮未做真实语音通话、听感/音画同步验收、WebSocket Relay 复接或 Android 编译。

统计面板修正后的最终产物另做 15 秒真实 Client 冒烟：`test-results/tcp-media/20260911-112222/`，
再次收到 TCP 视频、音频与游戏画面；两个窗口为 30.5 / 30.0 FPS，最大间隔 43ms。
该短测不含鼠标压力、窗口数不足 5，因此脚本严格窗口验收仍为 false，不替代上面的 180 秒压力结果。

作为历史参考：同机同游戏此前 UDP 180 秒测试 `test-results/udp-media-v2/20260911-093453/` 平均约 27.07 FPS，
最低 11.8 FPS，最大收帧间隔 499.4ms，超过 100ms 共 83 次。
两次不是同时同环境的严格 A/B；只能说明本次 TCP 的完整收帧情况更好，不能据此宣布网络问题已解决。
