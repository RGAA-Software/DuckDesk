# 清空在途帧后的历史拥塞状态修复

2026-10-11，候选 `flight-drain-20261011`。本批已实施、构建、通过完整 Setup 安装到 90，并完成普通 direct 和强制 90 Relay 的各一分钟游戏连接；卡帧任务仍未完成。

## 复现与修改

回放上一批 Relay 日志：encoder 473 的累计确认释放五帧，pending/bytes 已为零，但历史 forward delay 仍为 135332µs，后续批继续继承缩小额度。旧实现的确定性回归测试失败：新批仍受 32768 字节额度和 300000µs 历史延迟限制。

`VideoFlightWindow::Observe` 现在只在有效累计确认清空全部待确认帧时，恢复原有 256KiB 正常额度、重建收发时序基线并清空已完成批的吞吐采样。部分确认、未前进反馈保留债务；下一次真实前向拥塞仍能收紧。未扩大 QUIC 缓存/拥塞窗口，未修改 Rust、Relay 或网卡。

47 项媒体测试通过，609 行变更的命名/所有权检查通过。保留旧实现失败日志与首次全测失败日志；首次两个测试夹具分别不足 250ms 吞吐采样周期、已进入稀疏探测时刻，修正时间线后通过。development 六份运行文件均与构建树 SHA 相同。

## 安装身份

90 使用 `D:\112233\flight-drain-20261011`。完整 Cloud Node 3.3.97 Setup 安装成功，314 个 manifest 文件逐一校验，Service Running。

- Setup SHA：`924909B2C6BB8958EED522FDCA31D3D4EFFB91936B56E23F46D03C773FACEAAE`
- Manifest SHA：`037E544F0F794EC65BD8EB05B3CC5B3F1C91BBD6346A5809929C87C3CDBC5548`
- Render SHA：`4BD3D2A46997FB3F9A7DF98BCA33B7B0362627DC40421159AE29420BB7315216`
- Node Client SHA：`0B55BBF54E5EA1ED1136609CDFB2355696F7A14645178AE2499EF4B840D67042`

Server 与两 Relay 软件未更新，已有应用 20000kbps 配置保留。无持久网络配置改动。

## 实际采样

客户端完整帧交付，不能称为显示器呈现测量。关闭/宽限阶段排除；没有截图、抓包、混传或故障注入。实际路径日志分别为 direct 与 `https://39.71.45.66:4605/` Relay。

| 游戏路径 | 有效采样 | 完整帧交付 FPS | P99 间隔 | 最大间隔 | >100ms |
| --- | --- | --- | --- | --- | --- |
| direct | 60s | 59.30 | 45.578ms | 130.440ms | 2 |
| 90 Relay | 60s | 59.22 | 51.329ms | 512.826ms | 1 |

两次没有同环境旧实现对照，不据此宣称性能提升或回退。Relay 客户端触发 50MiB 轮转，已从 `.1.log` 恢复 1129 条同 trace 记录；最初缺首段的统计作废，最终游戏交付 3552 帧。没有为取得好结果重跑。

direct 两次空档对应源端 150.939/140.582ms 发帧间隔，接收组帧 22/24µs。源端正常额度 262144，历史 forward growth 只有约 10ms 或 92ms，并非本批已修复的清空状态问题：回程反馈出现 211/227ms 到达间隔，而生成间隔约 50ms，待确认字节达到 271700/265100 后暂停捕获。恢复时仍有实际未确认债务。需要单独回放正常额度被回程滞后消耗的边界，不能直接增大额度。

Relay 512.826ms 空档位于 wire 1436→1437 / encoder 1605→1606：源端产生间隔 19.059ms、提交完成间隔 18.554ms，当前帧在 8.290ms 内本地提交全部分片。接收首批分片到末片相隔 390.913ms，其中批间空档约 225.578、32.021、126.693ms；最后组帧仅 68µs。采样中没有 `receive_worker` 的处理 >=20ms/等待 >=50ms 记录，但这仍不足以区分 iroh 内部排队与 Relay 外部 TCP 链路阻塞。不得把本地提交成功当成已上网，也不得归咎物理网卡。

两个 WebView 各 5s 连接/宽限停止通过；页面加载时长间隔仍需按编码源输出区分，不计作持续 60FPS 性能验收。

## 交接

272 资源会话均 closed，两 Relay fresh/ready；Service Running，90 只剩桌面 Render PID 27448 / session 2，既有 Windows 会话保留。无新提交/push。

同前缀 installation/build/summary/gaps/after JSON、测试和双端 gzip 日志为证据；`analyze_flight_drain.py` 重建采样，`20261011_flight_drain_log_hashes.json` 保存日志 SHA。

下一步继续 S1/S2：先固定 direct 正常额度被迟到反馈消耗的实际时间线，并对齐 Relay 1437 分片本地提交、iroh 实际发送/接收及 Relay 转发时刻，定位 391ms 分片空档。当前不进入三路径长稳结项，不扩展 Android/业务验收。

## 接续批次：10Mbps 应用预算对照

同一已安装候选，只将游戏应用 `8ebdbd8e-925d-4885-9401-e87d0418bff9` 的 Console `bitrate_kbps` 从 20000 临时调至 10000，分别再做一分钟正常游戏连接。源端日志确认 total=10000000、video wire=9600000、encoder ceiling=6500000、未发送队列额度19200；未改网络/Relay/其他应用。

首次立即启动在配置同步期间返回 503 unavailable；保留失败日志。随后调度预览 eligible=true、拒绝原因为空，再启动成功。不是性能采样失败，也未用不断重跑来择取好样本。

| 游戏路径（10Mbps） | 有效采样 | 完整帧交付 FPS | P99 间隔 | 最大间隔 | >100ms |
| --- | --- | --- | --- | --- | --- |
| direct | 60s | 59.88 | 37.247ms | 89.439ms | 0 |
| 90 Relay | 60s | 60.02 | 44.500ms | 59.963ms | 0 |

该结果支持继续调查高预算与拥塞的交互，**不同时间段的公网状态不受控，不能证明码率是唯一根因或修复已达长稳要求**。10Mbps 不是代码固定值，仍消费现有按应用配置。应用预算已恢复20000，revision=4；原 spec 校验未被他人修改后才恢复。配置、双端 gzip 和统计在同前缀 `10mbps_*` 文件。

274 资源会话均 closed，两 Relay fresh/ready。下一步仍按上述 S1/S2 定位；三路径各三段五分钟、显示链关联与异常分类没有冒标通过。

本批也重新确认只读参考版本 Sunshine `3cba9baebac882b336be3ebe129ee612cb189853`、Moonlight common `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac`。Sunshine `src/stream.cpp:1714` 保持跨帧发送节奏，Moonlight 的参考恢复规则仍适用。只读 iroh-relay 1.3.0 客户端 `src/client/tls.rs:293`、服务端 `src/server/streams.rs:221` 都设置 TCP_NODELAY，不能凭约200ms空档猜测未关闭 Nagle；未修改第三方。
