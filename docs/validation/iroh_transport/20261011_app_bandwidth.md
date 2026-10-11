# 每应用媒体带宽配置交付（2026-10-11）

用户选择每个应用分别配置。Console 应用目录编辑中的“每路媒体带宽预算（kbps）”复用已有 `launch.video.bitrate_kbps`，无需增加数据库字段或全局设置。新增游戏/WebView 默认 10000kbps，已有应用保持保存值；保存后新启动的实例生效，同一应用的每路媒体连接分别使用该预算。RDP 原生协议不套用这个视频编码预算。

Service 使用 `--app_media_bandwidth_kbps` 精确下发，避免整数 Mbps 取整。Render 为每个 iroh Session 创建独立预算，视频发送估算额度为配置的 96%，编码上限为 65%，其余用于 FEC、协议和音频开销；编码仍可根据反馈降低。该预算控制媒体提交节奏，不代表网卡物理层硬限速或保证实际链路带宽。

## 验证

- Web：31 项测试、类型检查和生产构建通过，中英文说明包括预算单位、预留开销及生效时机；线上 JS/CSS 两个资源 SHA 与构建产物匹配。
- Service：28 项应用测试通过，覆盖游戏/WebView 的 128、8001、10000、20000、200000kbps 精确参数传递。
- C++：41 项媒体测试、3 项 UDP 测试、iroh Frontend 贯通测试通过。首次媒体测试与其他构建并行期间一个大帧 deadline 超时；构建结束后不修改测试/代码，原样复测 41 项通过。保留初始失败与复测日志，不以复测覆盖失败证据。
- 六个 development dist 运行文件与构建树 SHA 一致；可读命名、所有权和差异空白检查通过。
- 90 游戏/WebView 各连接 5 秒，均 connected 并在断开后按宽限期停止。游戏进程参数为 `--app_media_bandwidth_kbps=20000`；两次发送预算日志分别于 12:47:43、12:48:17 确认 total=20000000、video_wire=19200000、encoder_ceiling=13000000。

实机沿用已有三个应用的 20000kbps 配置，未修改业务配置。其他数值由参数及前端贯通测试覆盖。本轮没有饱和混传、Pktmon、网卡配置修改或长期性能测试，不能据此宣称短停顿已消除。

## 完整包安装身份

90 工作区：`D:\112233\app-bandwidth-20261011`。Server 与 Cloud Node 均通过完整 Setup 更新，安装后逐文件核对；BJ Relay 安装未变。

| 产品 | 版本 / 文件数 | Setup SHA-256 |
| --- | --- | --- |
| Server | 1.0.56 / 35 | `26a8a0258d33ed26dd0fac24327b2e96ca1a649d292151c791c5f46e666004a8` |
| Cloud Node | 3.3.97 / 314 | `061926482674A522DC8FB4BEC17FFAE53DFF8B5EF2C0B7EFC4A9F7D8797021F0` |

最终节点候选为 `app-bandwidth-final-20261011`。先前同日首个节点候选未包含新 Service，未安装，不作为交付物。Server schema 40 和现有业务配置保持。

节点关键运行文件：

- Render：`4E339940E8EA0334076DB2714801E27922384899F32C20FA8CF984923ABC49CE`
- Service：`EE2CFCC775356BA371766A2049E6F69769538871DDFD314AB919CE8805E11AE0`
- Client：`1814C90D2081B71259F6F452C55C0DDFA05C37856E63A10FB0152887748C09E7`
- Panel：`08A185C5FCD5450F380395D37F815B4197D235DE04435ACCA26D7274FD4CE4CB`

## 清理与证据

264 个资源会话均已关闭，90 Service Running，仅保留桌面 Render 18116/session 2；Windows 登录会话保留。两个 Relay 均 fresh/ready，generation 91/16，control_epoch 70。机器 trace 文件变量为空。

同目录 `20261011_app_bandwidth_*` 保存构建/测试日志、六产物 SHA、Server/Node 安装核验、线上资源 SHA、既有应用预算、进程参数、两次运行预算、连接与宽限退出、最终会话/Relay 状态。两份 Client 日志压缩保存，未压缩日志 SHA 记录在 `20261011_app_bandwidth_live.json`。

下一步仍是 `20261011_ack_stall.md` 的 QUIC 视频排队额度/期限和按帧依赖恢复工作；本次配置交付不替代对原始回程延迟的定位，也不重开已停止的网卡/混传专项。
