# 标准 RTC / TURN 双机验收报告（2026-08-24）

## 1. 结论

本机 Windows 11 与被控机 R90（Windows Server 2022，设备 `001190520`）之间的标准 RTC 产品链路已经完成代码、自动化和真实 Render/Panel/Service/WebClient 验收。以下项目通过：

> 稳定性追加结论：随后执行的 STAB-01 真实100轮连接/退出为97/100，未达到100%门禁。第45、90轮连接后9秒解码帧增长为0，第56轮30秒内未连接。因此本文的功能项结果仍有效，但当前提交不能据此判为LAN RC PASS；2小时、断网耐久和跨夜测试按依赖失败停止规则暂不继续。

- TURN/UDP 强制 relay、阻断 TURN/UDP 后 TURN/TCP 回退。
- `direct_probe_enabled=true` 自动选择 Direct。
- Direct 探测成功但实际建连失败后，旋转一次性 ticket 并自动重开标准 RTC。
- 活跃会话配置 revision 更新、`SetConfiguration`、ICE restart 和新凭据生效。
- selected candidate、候选类型/地址/协议、TURN 节点和 RTT 展示。
- 1920×1080 画面、系统声音、键鼠、剪贴板、文件上传/下载、多显示器创建/切换/删除。
- 断线重连、PeerConnection 替换、连续连接/退出以及 Service 重启恢复。
- WebClient 麦克风上行、被控端 PCM 解码/播放入口和独立语音挂断统计。

发布状态不是“所有网络环境完成”。两台机器仍处于同一可控局域网，relay/TCP 结果使用受控路由和 UDP 阻断得到。两台分别位于不同运营商/公网/NAT 的真实 TURN relay、对称 NAT/srflx，以及并发 allocation/端口耗尽仍是外部发布门禁。

## 2. 环境与产物

| 角色 | 环境 | 用途 |
| --- | --- | --- |
| 主控 A | 本机 Windows 11，`10.0.0.16` | `px_console`、`px_auth`、Coturn、Chrome CDP WebClient、原生构建 |
| 被控 R90 | Windows Server 2022，`10.0.0.90` | `px_service`、`px_panel`、desktop `px_render`、USBMMIDD |
| 设备 | `001190520` | 真实桌面 Render，端口 20371 |
| 浏览器 | Chrome headless + CDP | 真实 WebClient、WebRTC stats、输入和麦克风轨 |

关键部署校验：

- `px_service.exe` SHA-256：`A8A8859BC2C194798A3399688A31F8FC6986DA1FC3363F5552AE84BD054B73AD`
- `px_panel.exe` SHA-256：`7C1EA75AE826BC191312CAC9DFDCF42B91D4647F011787205EC32C79D0A12322`
- Service 为自动启动；验收结束时 Service、Panel、Render、UserProxy 均存活。
- 报告不记录登录密码、ticket、TURN credential、IPC token、完整 SDP 或语音内容。

## 3. 网络与选路用例

| 用例 | 方法 | 通过条件 | 结果 |
| --- | --- | --- | --- |
| 标准 RTC host | 强制 `rtc`，策略 `all` | 建连且 selected pair 为 host | PASS |
| 强制 TURN/UDP | `iceTransportPolicy=relay`，保留 TURN/UDP | selected pair 为 relay/UDP，画面和 DataChannel 可用 | PASS |
| TURN/TCP 回退 | 强制 relay 并阻断 TURN/UDP，保留 TCP listener | selected pair 标明 TURN/TCP，画面和输入持续 | PASS |
| 自动 Direct | `direct_probe_enabled=true`，R90 可达 | 模式为 `direct`，加载 `net_rtc_local` | PASS |
| Direct 故障回退 | 探测成功后对 Direct 建连作确定性失败注入 | 旧 ticket 不复用；自动申请新 ticket 并进入标准 RTC | PASS |
| 活跃配置更新 | 连接期间发布更高 revision | 调用 `SetConfiguration`，ICE restart 后 revision 更新且画面继续 | PASS，持续 75 秒 |
| 路径统计 | 每 2 秒读取 `getStats()` | 展示模式、revision、local/remote candidate、协议和 RTT | PASS |

TURN UDP/TCP 用例证明了客户端、Render、Coturn 和统计展示的实现闭环；由于两端并非真实异网，该结果不能替代公网/NAT 门禁。

## 4. 标准 RTC 全功能用例

| 功能 | 操作和判定 | 结果 |
| --- | --- | --- |
| 画面 | 首帧后为 1920×1080，持续递增；120 秒稳定阶段结束为 1998 帧且 connected | PASS |
| 系统声音 | 收到独立远端 audio track；页面静音状态不影响视频 | PASS |
| 输入 | DataChannel 打开；真实 CDP 鼠标、按键经 WebClient 转发，输入 RTT 有回报 | PASS |
| 剪贴板 | 写入、读取和 ack 均由真实媒体 DataChannel 完成 | PASS |
| 文件 | 上传、下载、SHA-256 比对和远端删除完成 | PASS |
| 多显示器 | 创建 USBMMIDD 后由1屏变2屏；重连并切换到新增显示器有新帧；删除后回到1屏 | PASS |
| 主动退出 | 连接正常关闭，不触发错误重连提示 | PASS |
| Peer 重连 | 主动关闭当前 PeerConnection，自动创建新 PeerConnection并恢复帧 | PASS |
| 连续连接 | 多轮申请新 ticket、连接、退出，无 ticket 重放和会话占用残留 | PASS |

## 5. 语音与音频证据

WebClient 使用浏览器 fake media device 产生可重复麦克风输入，通过正式 Panel 来电 UI 的自动化点击接受；自动化只操作产品 UI，不绕过 Render/Panel 授权状态机。

| 观测点 | 结果 |
| --- | --- |
| 浏览器 outbound RTP | `bytesSent: 95 -> 7506` |
| R90 首个 PCM | 48 kHz、mono、16 bit、480 frames |
| R90 inbound RTC | 153 packets、7506 bytes、0 lost |
| WebRTC/NetEq | 465120 samples received、145920 emitted、318720 concealed |
| 通话端点结束统计 | `tx_opus=216`、`rx_pcm_samples=207840`、`transport_drop=0` |
| 会话清理 | 远端 hangup 后浏览器语音 sender 关闭，系统声音 track 保持独立 |

该用例证明浏览器采集、RTP 发送、Render 解码 PCM、授权后的通话端点接收和挂断清理。fake media 输入不等价于双物理终端的主观 AEC/外放音质门禁。

## 6. 重启与故障恢复

实测发现并修复了两个仅在真实重启下出现的问题：

1. Panel 原先在系统/WMI探测之后才启动 Service 控制连接；显示驱动切换时可能延迟 AuthInfo，导致 Service 无法连回 Console。现改为先建立控制通道。
2. Service 停止时原先先广播退出，再清理 Render；其他任务可能先结束运行时，使旧 Render 带上一轮一次性 IPC token 存活约60秒。现改为先清理受管 Render/UserProxy，再广播退出。

同时，虚拟显示器 Session worker 查询缩短为8秒，create/remove保留35秒；WebSocket 读循环不再同步等待驱动操作，也不会在失败后立即执行第二次同步 query。

修复后真实 `Restart-Service px_service` 结果：

- SCM restart 调用：278 ms。
- 旧 Render PID 被替换。
- Service → Console、Panel → Service、Render → Service 全部恢复：3334 ms。
- 恢复后 ticket 兑换和标准 RTC 建连通过。

## 7. 自动化门禁结果

| 命令/目标 | 结果 |
| --- | --- |
| `cargo test --manifest-path rust_client/px_service/Cargo.toml` | 52/52 PASS |
| `cargo test -p px_console_server` | 140/140 PASS |
| `npm run test:unit -- --run`（Console Web） | 15/15 PASS |
| `npm run build`（Console Web） | PASS |
| `npm run test:voice`（WebClient） | 19 assertions PASS |
| `npm run build`（WebClient） | PASS |
| `test_voice_call.exe` | 31 PASS；2个条件测试按设计跳过 |
| `px_client px_panel net_rtc net_rtc_local voice_call` | Release/RelWithDebInfo 增量构建 PASS |
| `node --check scripts/cdp_webview_input_stability_test.mjs` | PASS |
| `git diff --check` | PASS |

两个条件跳过项是可配置2小时语音长稳和交互式真实 WASAPI。真实 WASAPI 条件项此前已在 R90 单独执行通过；本次 Web/RTC 用例又补充了浏览器上行和 Render PCM 证据。2小时、双物理声卡主观音质仍按语音发布门禁执行。

## 8. 尚未完成的发布门禁

以下不得从本报告推导为通过：

1. 两台分别位于不同运营商或不同公网出口的物理 Windows 机器，确认真实 relay candidate。
2. 对称 NAT 下 srflx/relay 行为和真实公网 TURN/TCP 回退。
3. 多并发 TURN allocation、relay 端口接近耗尽、长时间高码率视频与大文件并行。
4. STAB-01 已执行但仅97/100：两次连接后视频不增长、一次未连接；修复后必须重新达到100/100。跨夜断网/恢复耐久尚未执行。
5. 双物理终端真实麦克风/扬声器的 AEC、双讲、热插拔和主观音质。

执行上述门禁时应复用本报告的判定字段，并补充公网拓扑、NAT 类型、候选对、TURN 节点、持续时间和资源曲线；不得记录任何凭据。
