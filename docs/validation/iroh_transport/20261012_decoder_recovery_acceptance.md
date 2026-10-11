# 卡帧统一任务验收（2026-10-12）

S1–S4 的本轮工程验收通过。已交付发送准入/回程反馈、丢片参考恢复、日志背压、原生窗口拖动、Vulkan 可用交换链处理及持续解码无输出恢复修复。主线功能原先已完成；本轮收尾的是画面性能与本地额外停顿，Android、NAT/规模/长期稳定性仍未整体验收。

## 根因与修复

旧故障段真实出现 4.264365 秒新图空档，239 次解码输入被接受却没有图像。旧统计把调用数当作产出数，掩盖了这个状态。完整帧交付最大只有120.3ms、上传无拒绝，不能归咎于显示线程阻塞四秒或网卡硬件。SDK 缺少持续无输出的主动恢复；现连续至少4次无输出且持续100ms时释放 decoder、清除参考、立即请求完整配置 IDR，并限制重复请求。底层 FFmpeg 为什么进入该状态仍没有具体 DPB 证据；本项目恢复缺口已有直接证据并修复。详见 [解码恢复实现](20261012_decoder_output_recovery.md)。

修复前/后同类 owned UDP 丢包证据保留：第5段4264.365ms，第6/7段167.681/162.878ms；第7段60×40ms窗口实际丢1718报文。故障段不能用常态≥58FPS门槛衡量。两次修复后实机未出现持续无输出，未声称观察到真实GPU watchdog触发。

为验证完整恢复动作，新增 SDK 集成测试在 loopback 接收、真实视频工作队列及注入 decoder 工厂中制造持续 TRError(0)：04:00:26.854 frame6 触发 video.decoder_output_stalled；确认旧 decoder Release、一次 IDR、预测帧/缺配置关键帧拒绝、完整配置 IDR 创建第二个 decoder 并重新产出图像，以及重复 Exit 安全。11项 SDK 套件通过11.02s，该测试266ms完成（不是公网恢复耗时）。初次失败来自测试服务器把protobuf作为文本WebSocket帧发送，已改为二进制；产品代码没有因此修改。执行记录 [integration_execution](20261012_decoder_output_recovery_integration_execution.log)，状态策略3项及encoded_video_delivery、此前58媒体/显示生命周期检查复用。

## 正常实机样本

同一持续60FPS GameHook动态源，H264预算10000kbps/revision19、默认10秒宽限；每条路径3×300秒，共45分钟。各段480鼠标消息，connected、宽限停止、owned资源关闭。路径证据来自实际 Ip / selected Relay，强制90/BJ都没有改走隐藏UDP。五秒完整窗口扣除起始不完整区间后覆盖约290秒；P99≤50ms是完整窗口计数的阈值判定，不冒充逐帧数值。初90秒数值P99另见JSON。呈现计数为新图被图形API接受，不能称为物理scanout。

| 路径/样本 | 新图平均FPS | 完整交付窗口 | 完整窗口交付P99 | 最大呈现窗口间隔 | 最大完整帧交付间隔 |
|---|---:|---:|---:|---:|---:|
| direct / 1 | 60.016 | 290.496s | ≤50ms | 48.116ms | 47.1ms |
| direct / 2 | 59.991 | 290.552s | ≤50ms | 96.261ms | 137.3ms |
| direct / 3 | 59.998 | 290.573s | ≤50ms | 114.246ms | 114.4ms |
| 90 / 1 | 60.017 | 290.562s | ≤50ms | 57.449ms | 57.3ms |
| 90 / 2 | 59.963 | 290.509s | ≤50ms | 90.181ms | 90.0ms |
| 90 / 3 | 60.008 | 290.493s | ≤50ms | 51.573ms | 51.4ms |
| bj / 1 | 60.020 | 290.544s | ≤50ms | 73.050ms | 72.7ms |
| bj / 2 | 60.011 | 290.585s | ≤50ms | 102.321ms | 102.2ms |
| bj / 3 | 60.005 | 290.596s | ≤50ms | 69.077ms | 98.6ms |

全部真实解码 no_output/errors、上传拒绝及日志队列 overruns 为0，没有数秒新图空档。九段汇总 [JSON](20261012_decoder_recovery_acceptance.json)。这不代替主线三段十分钟、复杂NAT、规模并发或混传长期稳定性。

## 所有 >100ms 间隔分类

- direct_2：起始后164ms交付137.3ms，首次GPU上传86.336ms；没有对应>100ms呈现，属于启动/首显窗口。
- direct_3：交付114.4ms/呈现114.246ms，7059缺4个data片，7065修复参考等待17.273ms；源恢复生成10.993ms、队列35µs、12片本地接受4.529ms，相邻五秒max_send5.116ms。属于到达延迟/缺片及有界参考修复，未延长至秒级，具体物理丢包节点未证明。[逐帧证据](20261012_decoder_recovery_acceptance_direct.md)。
- bj_2：03:37:51.948 encoder2349/wire2221，源2348→2349时间16.044ms、队列22µs、发送本地接受47µs，客户端两帧交付102.2ms。2349首片相对前帧最后片迟到99.467ms，7片覆盖完整且首→末2.733ms，Feed11µs，参考已就绪；呈现102.321ms/上传年龄149µs。短延迟发生在完整帧到达之前，不是本地组帧/解码/显示额外等待。本段没有同PID TCP ESTATS，不能称为已证明TCP HOL/重传或网卡问题。

## 交付、配置与清理

本机 Client408717B231B55A13E4BEB6787774E1ACCD99DBCE33FD794ECB497B43CD93919A、Panel8ACEB454AB5A9382AA3EDF184E2D840386EB2CCDC2E3455432F7F1A34EEEB91F，最终构建/dist逐项SHA相等；client3.3.81 development dist的40文件清单验证通过。新集成测试只重建test_sdk_decoder_factory，不改变产品身份。编译18 jobs、Release；未运行全仓release build。

90用完整Cloud Node3.3.97候选 decoder-output-recovery-20261012 安装，D:\112233下暂存；Setup56D2ECAF6FC1A37D7631A69615B41AD57754B225189C9F752DE587B617BAD293，manifestA5497FDF8F474A5CD2BC53E8B441A3B365C6C55293E4CC45E1137534180CBD97，314文件SHA全部验证/ServiceRunning/Windows会话ID1、2、3保留。NodeClient0C9B2695…、Render6F9A2D19…完整身份见 [安装记录](20261012_decoder_output_recovery_installation.json)。本轮没有修改Relay，因此复用90 Windows/BJ Linux正式服务及完整包身份。

应用恢复10000kbps/revision19，10秒宽限、其余spec保持；所有临时20000实验均已恢复。两Relay fresh/ready、disabled/desired_draining/reported_draining均false，316条业务session全部closed，本机无px_client/px_panel测试进程。90的一条基础Relay连接不等于活跃业务资源会话。无NIC/驱动/QoS/代理持久改动，未恢复被叫停的混传饱和专项。[最终健康](20261012_decoder_recovery_acceptance_final_health.json)。

日志使用既有异步有界写入，普通逐帧详情限首90秒；恢复/未完成帧保留有界64帧诊断及五秒聚合，长期正常阶段不持续逐包输出。没有因清理诊断修改已验收产品；日志耗尽/关闭回归及0 overruns证据复用。

源端先压缩再下载，压缩/原文SHA均核对：

| 源批次 | 行数 | gzip字节 | gzip SHA256 | 原文 SHA256 |
|---|---:|---:|---|---|
| direct | 38430 | 1664846 | 2909E108822522D25028DCA2FC58CFA71742C48C557E532FB1098D480858329E | 012DC44FBED8CB34C05564B65E7C957E1D10621FAEA0EEFBC37E8B7FD84D3861 |
| 90 | 38437 | 1939516 | C09BBBF30FD6E19E13A4DE27B721A0D1FCAF297C9BCFE8EABF888D30A0927AD6 | 1287999CE15AB274B89BDE7DB5C3C52B312C74EAAB6332E6946A20100911FC9E |
| BJ | 38397 | 1947102 | 00C10B3C4E6D61D06A17DFD7F3B80AE7901FC3DD0D85D271D334F714C9E6C104 | B59547DD3530EAD34F1ACC23F767B6DDD46B0C3C59B6A04C7FE939C81B555B30 |

## 边界

本轮工程验收通过，不承诺公网零抖动。真实硬件持续无输出状态没有在修复后实机复现，完整恢复动作已由真实SDK队列/可控decoder注入覆盖；底层状态仍未定因。102–114ms极少量链路到达间隔保留，没有借此称物理网络修复。长任务主线的剩余范围单列于 [推进分析](20261012_mainline_progress.md)。
