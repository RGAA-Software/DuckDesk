# 语音通话 M1 实机测试报告（2026-08-23）

## 结论

Windows 原生客户端到 `px_panel` 的正式来电授权核心闭环通过：来电窗口在 90 号机交互会话真实显示，接受、拒绝、等待中取消和 Panel 不可用 fail-closed 均有机器可读证据；正式接受后双向 Opus 持续收发并可正常挂断。M1 代码达到“已实现/核心 Console 路径已验证”，尚不能标记完整发布验收，锁屏、RDP、快速用户切换、第二客户端实机竞争和 Render/Panel 重启矩阵仍需补测。

## 候选物与环境

- 源码分支：`master`，基线提交 `fbf30d1e2` 加本报告对应的 M1 工作区变更。
- 被控端：`10.0.0.90`，Windows Server 2022，Administrator 交互会话 1。
- 主控端：本机 RelWithDebInfo 原生客户端，WebSocket 远控流；视频首帧及 `DISPLAY1`、`DISPLAY44` 枚举正常。
- `px_panel.exe` SHA-256：`4E7E64A5266545CBA4A28C2CBF019DE791C5EFEF05CFDA205BA2F606A6A9645C`。
- `px_render.exe` SHA-256：`30708FCF89E4C68ADAE2AC5754877E616F81DC835EEAE8637D35B38BE2C106B1`。
- `voice_call.dll` SHA-256：`0CD52B4B2DC3456E962FF50D62A89A66E9A40A1F14F661BFF12C601F094D232F`。
- 三个远端文件与本地产物 SHA-256 一致；90 号机 Service、Panel、Render 均在测试后恢复运行，端口 20369/20371 可达。

## 自动化结果

执行命令：

```powershell
cmake --build build_official --config RelWithDebInfo --target test_voice_call test_client_voice_call_protocol test_client_virtual_display
ctest --test-dir build_official -C RelWithDebInfo --output-on-failure
```

| 测试 | 结果 | 覆盖 |
| --- | --- | --- |
| `voice_call_core` | 19/19 | 状态机、超时、重放、错误关联、序号、幂等清理、SDL dummy 音频闭环、决定缓存 TTL/容量/更新、三种 Panel IPC round-trip |
| `client_voice_call_protocol` | 5/5 | 请求/挂断关联、固定格式、独立媒体类型、请求 ID 唯一性 |
| `client_virtual_display` | 11/11 | 虚拟显示器协议/UI 状态回归 |
| CTest 汇总 | 3/3 | 三个目标均已正式注册，不再依赖手工逐个运行 |

## 90 号机真实 UI 与媒体

| 场景 | 结果与证据 |
| --- | --- |
| 来电展示 | `px_panel` 会话 1 出现 480×300 的非模态置顶窗口；UIA 读到标题“收到语音通话请求”、访问者说明、开麦风险说明、倒计时、拒绝与接受按钮。拒绝按钮为默认焦点策略。 |
| 接受 | UI Automation 调用真实“接受”按钮；Panel 回传完全匹配的 stream/call/request，Render 二次核对后记录 `accepted by px_panel` 并启动音频端点。 |
| 双向媒体 | 通话约 128 秒后主控主动挂断；主控 `tx=6082, rx=5618, underrun=1628`，被控 `tx=5623, rx=6082, underrun=656`。双方收发均持续增长。 |
| 加固后复测 | 新 Panel 候选再次通过真实“接受”；持续约 56 秒媒体阶段后挂断，主控 `tx=2655, rx=2459, underrun=672`，被控 `tx=2463, rx=2655, underrun=295`。随后同一候选再次通过真实拒绝、30 秒超时及等待中取消。 |
| 拒绝 | 新请求显示正式窗口，UI Automation 调用真实“拒绝”按钮；Render 返回 rejected，未建立媒体。 |
| 等待中取消 | 请求窗口先被 UIA 探测到；主控再次触发挂断，Panel 收到完全匹配的 cancel 后窗口消失；再次探测无来电 QDialog。 |
| 30 秒超时 | 主控保持等待，30 秒后发送相同 call/request 的 `connect=false`，双方回 Idle，窗口关闭；无音频端点残留。 |
| Panel 不可用 | 在 Render 和远控流仍运行时终止 Panel，并立即发起呼叫；Render 记录 delivery unavailable 和 `px_panel unavailable; rejecting`，立即 fail closed，随后 Panel 自动恢复并重新连接。 |
| 视频回归 | Panel/Render 重启后原生客户端自动重连，重新收到配置、两块显示器和持续视频帧；语音测试未破坏远控主链。 |

UI 自动化脚本为 [remote_panel_consent_probe.ps1](../src/px_deps/px_voice_call/tests/integration/remote_panel_consent_probe.ps1)；它按当前交互会话定位 Panel，优先使用稳定 ASCII `AutomationId`（`voice_call_consent_dialog`、`voice_call_accept`、`voice_call_reject`）定位窗口与按钮，导出窗口/控件树和截图，再通过 InvokePattern 执行指定按钮。脚本不保存主机账号或密码，远程执行凭据由测试编排层临时提供。

## 安全与实现核对

- 生产语音代码中已无 `WTSSendMessage`、`MessageBox` 或自动接受分支。
- Panel 只产生用户决定；认证状态、活动 stream、call/request、截止时间和连接存活均由 Render 在开麦前再次核对。
- 重复决定使用有界 TTL 缓存处理；过期、伪造或错会话响应不启动端点。
- Panel 的 `/panel/renderer` 与 `/sys/info` 内部 WebSocket 仅接受 IPv4/IPv6 回环来源；90 号机外部实测两者均被拒绝，同时公开 `/panel` 保持可连接，本机 Render 连接保持正常。自动化脚本为 [remote_panel_channel_policy.ps1](../src/px_deps/px_voice_call/tests/integration/remote_panel_channel_policy.ps1)。
- 正常日志不记录 PCM、Opus payload、密码或 token；帧级数据不逐包写盘。

## 候选组装一致性

本次复测还验证了一个构建门禁：`px_client.exe` 和 `deps/ct_plugins/*.dll` 必须从同一源码/工具链一起构建。若只重编 EXE，却混用新增 `OnTransportConnected` 接口之前的旧插件 DLL，连接后会因插件 vtable 不一致崩溃。这不是语音协议运行时缺陷；使用同批次重编的客户端及四个客户端插件后，视频、文件传输回调和语音均稳定。CMake 已让 `px_client` 显式依赖四个运行时客户端插件，定向构建也会先刷新插件；发布候选仍禁止手工拼接不同批次二进制。

## 未覆盖与后续门禁

以下项目未由本次 Console 冒烟替代，按总测试计划继续执行：

- 锁屏/解锁、RDP 连接与断开、快速用户切换、注销及 Session 0 默认拒绝。
- 第二客户端真实并发来电、接受/取消竞态注入、Panel/Render 在来电过程中的重启。
- M2 的 AEC/NS/AGC、WASAPI 通信设备后端、热插拔、外放人工音质与 2 小时耐久。
- M3 的各传输弱网和优先级；M4 WebClient；M5 安装升级/策略/审计；M6 完整候选发布矩阵。

因此本报告结论为 **M1 核心路径 PASS，完整发布验收未完成**。
