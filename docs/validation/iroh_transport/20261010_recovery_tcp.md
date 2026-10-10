# 连续丢帧恢复与强制 TCP 对照（2026-10-10）

## 范围与实现

本轮仅调试不限速直连短停顿，并按用户要求对比显式强制 TCP。没有 QoS 限速，也没有新增 token、ticket 或第二套认证。

修改前新增的三个回归均失败（`20261010_recovery_tcp_before.xml`）：

- 服务端已提交 IDR/参考恢复帧，但新的恢复请求仍被原 500ms 冷却拒绝。
- 客户端已接收有效恢复帧，但新一轮丢帧仍继承上一轮 1s IDR 冷却。
- 启动参数同时包含 iroh 描述和 `force_tcp=true` 时，iroh 覆盖用户指定的 TCP。

现在服务端按待恢复请求合并：只有恢复帧入发送队列成功才清除匹配请求；入队期间的新请求不会被旧快照清除。无恢复进展时仍保留 500ms 兜底和 32 项上限。客户端有效帧结束本轮恢复时也清除 IDR 冷却。新增 `iroh.recovery_requested` / `iroh.recovery_frame` 日志关联请求到生成帧的时间；该时间不包含客户端收到修复帧的时间。

显式强制 TCP 使用现有直连 WebSocket/TCP 路径，保留原来的 Console 会话授权。默认 iroh 仍使用 iroh；明确强制 Relay 优先于 TCP，仍走 iroh 私有 TLS Relay。没有加入故障时偷偷回退的逻辑。

参考只读源码：Sunshine `D:/source/Sunshine`，revision `3cba9baebac882b336be3ebe129ee612cb189853`，`src/video.cpp` 的待编码 IDR 状态在编码后清除；Moonlight core `D:/source/moonlight-qt/moonlight-common-c/moonlight-common-c`，revision `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac`，`ControlStream.c` 合并待发 RFI 范围并在 IDR 事件处理时清空重复请求。没有修改第三方源码。

修改前全部脏文件快照保存在 `backup/iroh_recovery_tcp_20261010/`。

## 本地验证和交付

- Direct：4 个 CTest 目标通过，涵盖 media datagrams、encoded delivery、launch config、frontend，见 `20261010_recovery_tcp_direct.xml`。
- 私有 TLS Relay：media datagrams、frontend 两目标通过，见 `20261010_recovery_tcp_relay.xml`。断开 Relay 的专用用例需要另一个故障注入 harness，本次未冒称执行。
- 新增 frontend 回归实际跨 QUIC 传送请求，分别检查 IDR、参考恢复帧之后无需等待冷却；原无修复进展时的突发合并测试仍通过。
- 开发 Release 构建已同步 Client/Cloud Node 的 dist，4 对运行文件哈希一致，见 `20261010_recovery_tcp_development_hashes.json`。Client 构建入口附带的输入、剪贴板、文件浏览和 RDP 生命周期检查通过。
- 完整候选 `iroh-recovery-tcp-20261010`，Setup SHA `3DF6833B0A6F17B153C8CB253FF5E5C6BEC2767D765B968EC6E6F0BA939C3261`，manifest SHA `9C4C8868BA4A0C8FC6059C6035BF10E29809AEB794BB39B3CA8EB295191A6798`；远端安装与实测记录在下方追加。

## 对照方法

同一 2dadventure 应用，10 秒预热、150 秒 WM 鼠标拖动、5 秒收尾；每次新建本次拥有的应用实例与客户端，最后关闭并清理。没有改变应用、分辨率和码率配置。截图用于核对实际内容。统计为解码前完整可交付视频帧：iroh 为参考链校验后，TCP 为完整视频消息到达 SDK；不是显示器呈现 FPS。统计窗口均约 5 秒，保留所有窗口；TCP 为毫秒精度、iroh 为微秒精度取一位小数。串行运行不等同于可重复的同一丢包轨迹，不能仅凭两次最大值量化修复收益。

## 实机结果

完整 Setup 已安装到 90，3.3.97、314 项文件 SHA 一致、服务 Running。运行 Render SHA `58DE542BB0365D3C3D0A5DAA24E5D85DCE56A3D6F7C1C6E25E358523A28E9F3B`，见 `20261010_recovery_tcp_delivery.json`。

### 先纠正验证脚本

初轮 `.cache/iroh-native-live-20261010-100532-auto` 的截图出现 Client 退出确认框；SDL 读取真实鼠标坐标，只有 WM 消息不能保证真实拖动。该轮保留为 `20261010_recovery_initial_invalid_drag.json`、两张截图及 `20261010_recovery_tcp_initial_server.log`，**不计为拖动验收**。仍保留其不利观测：有 219.2ms 和 1003.6ms 在线交付空洞；后者包含实际正向积压和缩窗后的源端暂停，不能从记录中删除，也不能拿后续健康一轮宣称所有抖动根除。该问题不同于本次已证实的恢复后冷却缺陷。此前仅使用 WM 消息且未确认游戏实际移动的历史数据同样不能自动当作快速拖动证据。

随后脚本改为在本次拥有的 Client 窗口内实际设置光标位置，再投递按钮消息；输入日志连续 `sent=true`，两张截图均无模态遮罩且显示明显不同游戏视角。两种传输使用同一修正脚本。截图与原始结果分别为 `20261010_recovery_tcp_game*`、`20261010_recovery_udp_game*`。

| 路径 | 全部五秒窗口 FPS | 全程最大间隔 | 全程 >100ms | 预热后最大间隔 | 预热后 >100ms |
| --- | --- | --- | --- | --- | --- |
| 显式直连 WebSocket/TCP | 57.4–60.0 | 174ms | 5 | 163ms | 4 |
| iroh UDP direct | 58.6–60.2 | 140.1ms | 1 | 83.2ms | 0 |

TCP 实例 `20e79f1d-73a3-4f72-9490-95c860527aef`，34 窗口、10151 帧，明确记录 `TCP media video delivered over WebSocket`。UDP 实例 `64be4667-977e-4ebf-8f07-7f7ab7ce6a32`，35 窗口、10502 帧，全部路径样本为 direct，0 次媒体策略 IDR、3 次 RFI。两次窗口数差异来自截图、鼠标系统调用和 Python 调度开销；均发送 9000 个循环鼠标消息，不是精确相同墙钟时长。`20261010_recovery_tcp_measurements.json` 保留全部窗口，预热后统计明确排除最前两个约五秒窗口，而不删除它们。

UDP 的唯一 >100ms：本机 10:12:00.872、frame 153，源端间隔 134700us，接收间隔 140061us，额外传输时间增长 5651us，组帧调用 38us。远端对应 `encoded_frame frame=153 gap_ms=134`，发生在初始化阶段，不能归因于 UDP 丢包后的 500ms 恢复门槛。正常拖动阶段另有 83.2ms 间隔，接收线程一次无包等待 82.8ms，组帧调用 6us；缺少 OS/网卡抓包，不能进一步声称是哪块网卡或哪一跳阻塞。

新日志确认三次 RFI 对应参考恢复帧：

- frame 1838：请求后 1892us 提交修复帧（kind 5）。
- frame 1847：请求后 7145us 提交修复帧。与上次请求仅相隔约 145ms，原固定 500ms 门槛会拒绝它；新逻辑允许处理。
- frame 3036：请求后 1734us 提交修复帧。

以上只是服务端接受请求到恢复帧入队的耗时，不是端到端延迟。在线发送窗口持续约 300/301 帧每五秒、busy/failed 为 0；UDP 最终发送丢包计数 24。10:15:02 起的采集暂停属于 Client 已结束后的宽限期，不计入在线停顿。

TCP 拖动中在本机 10:10:31–36 窗口记录 4 次 >100ms、最大 163ms；对应远端编码统计仍约 60fps，任务最高约 2–4ms。五秒聚合统计不足以证明具体 TCP 重传或系统调度根因。本轮没有声称切 TCP 能根治，也没有把 TCP 当 iroh Relay：这里是原有显式直连 TCP 的实际比较。

**结论：** 三个具体逻辑缺陷有修改前失败、修改后通过的回归；修复后的有效游戏拖动一轮没有复现 UDP 150–220ms 短停顿，连续 RFI 不再被恢复后的固定冷却挡住。仍需保留启动短间隔、首轮非拖动场景的一秒积压及 TCP 抖动的限制，不能宣布长任务全部完成或所有网络环境均无抖动。下一步若复现正常场景停顿，应以该帧发送/接收时间和接收线程等待为入口，必要时抓 OS/网卡包；不要再以降低 IDR 常数试错。

## 清理及接续

仅清理本次实例及 Client。Console 原配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 恢复，临时 Relay PID 50260 停止，QoS 0，仅桌面 Render PID 44988，Console Running，正常 TLS/主机名校验 HTTPS 200。见 `20261010_recovery_tcp_cleanup.json`。90 保留新完整安装，开发文件仍只在 `D:\112233`。

远端操作：上传 `14e58593-c62c-44b0-8236-59e7a5180666` completed；安装 `c4a08eec-2ce0-439c-9143-327ed00ebc9b` succeeded；清理 `868b46dd-3b90-4f6c-a616-28a421e08f7f` succeeded。启用操作 `4797e558-7d51-4c73-8843-674b678f1f4f` 为进程 exit 0 / output_drain_timeout，未重放；通过独立 PID/path/start time、监听端口、配置 SHA 和服务状态确认生效，后已完整清理。

上轮完整候选逐文件核对 SHA 后可逆移动到 C 盘专属目录，见 `20261010_feedback_age_archive.json`。没有提交或 push。本轮只改三个生产实现文件和三个对应测试文件，不覆盖其他已有工作。
