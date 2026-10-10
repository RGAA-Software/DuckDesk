# iroh 长任务执行状态

目标：完成 `docs/iroh_transport_execution_plan_20261008.md` 的 P0–P7 开发与验收。
用户已明确要求持续执行全部阶段。90 工作区为 `D:\112233`，不向桌面复制文件。

## 最新实机接续：2026-10-11，Windows 应用 iroh

### 当前主线交付：M4 Linux Relay 升级入口（2026-10-11）

- 前序传输/媒体/多 Relay 实现、归档和证据已提交并推送 `97d81fd1d`；提交前核对 iroh 隔离副本 84 文件及补丁 SHA，活动源码差异空白检查通过。原始归档/上游/日志不做格式改写。
- 完整 Linux Relay 镜像包新增 `deploy.py`：部署前验证包文件、加载镜像身份及运行文件 SHA；沿用显式指定的 Compose 项目名和外部配置目录升级，避免包目录变化生成第二个服务；升级后核对容器及运行 SHA。不覆盖配置、不执行 `compose down`，Console fresh/ready 仍须独立确认。
- 八项针对性测试在 Windows 与 WSL Python 3.8 均通过；真实 Docker Compose 5.5.1 解析通过，验证包含空格的外部路径、只读挂载和自定义 TCP/UDP 主机端口。Bash 语法及差异空白检查通过。说明与边界见 `20261011_relay_delivery.md`。
- 本批只变更交付脚本/文档，未修改 C++/Rust 运行产物，未重启 90/BJ、未部署新包、未追加混传或故障测试。安装身份和持久配置沿用上一实机批次；本机模拟不算新的安装/升级实测通过。
- **下一步：M4 新入口的完整 Linux 镜像包升级验收**，使用 BJ 现有 Compose 项目/配置和完整包核对运行身份、Console 就绪及一次连接；复用 Windows Setup 既有证据并补交付清单。M3 抖动/NAT/长期稳定性仍保留，Android 最后，旧数据面暂不退役。

### 当前执行决定：结束本轮测试，提交基线并回归主线（2026-10-11）

- 用户要求本轮测试到此为止，提交并 push，继续主线。下方独立进程测试已完成清理，不追加混传、抓包、网卡或故障注入测试。
- 保留已通过的 Windows/Linux Relay、双 Relay 恢复/维护/容量及 Windows/RDP 连接证据。M3 的 direct 短抖动、真实 NAT 打洞、长期稳定性仍未完成，不因转入下一批而标为通过。
- **当前下一步：M4 可独立开展的 Windows/Linux 交付收尾**，检查完整包部署/升级入口和文档，修复实际缺口；不重新安装已通过的旧包、不重新登记 BJ。Android 保持最后，仍有消费者的旧数据面继续保留。
- 当前安装身份、临时授权恢复及无活动测试进程结论沿用下方独立进程批次；本次未更改远端运行程序或持久配置。

### 当前实测：独立进程 / 独立连接并发文件与视频（2026-10-11）

- 用户明确重新允许本轮测试：视频与文件各一个进程。正式采用游戏 Render 4613 + 同机桌面文件 Render 4601，两个 `px_client` / 资源会话 / QUIC endpoint 独立，双方日志证明文件进程不收视频、视频进程不传文件。direct PID 77280/49752，IPv4 UDP 61055/63196；Relay PID 86740/46244，各自 TCP 46821/46945 连接 90:4605。
- direct：前测 60.0–60.1 FPS / 0 次 >100ms；并发文件 55.5–59.2 FPS / 2 次（110.1、145.3ms，均在下载方向）；后测 59.9–60.1 FPS / 0 次。64 MiB 上传 12.452 秒、下载 10.977 秒，SHA 一致。
- Relay：前后约 60 FPS，并发文件 59.8–60.0 FPS，最大 60.2ms、0 次 >100ms；64 MiB 上传 16.569 秒、下载 18.190 秒，SHA 一致。两端无新 NDIS 重置、无断开；两路径吞吐不同，不当作等负载因果实验或长期稳定性通过。
- 首次同游戏类别的第二路被 starter 每类一路拒绝，随后使用正常桌面文件入口。修正自动验收错误等待首帧、窗口先消费清理结果两处测试入口问题；旧误报超时证据保留，不计网络失败。Client/内置 Client 聚焦 Release 已同步 dist，SHA `20261011_independent_processes_build.json`；90 未重装、Render SHA 仍 `785DDEBB55324033DE8B6227411347F1E78EB8036E2D3F32A1E10A74DB767222`。
- 已结束测试：248 会话无活动连接，无本机测试 Client，90 仅桌面 Render 12556；远端测试文件删除，临时桌面访问授权已恢复空列表，两 Relay fresh/ready。详见 `20261011_independent_processes.md` 及同名前缀证据。
- **下一步**：以独立进程/连接为正式并发测试拓扑，定位 direct 下载时两条连接共享链路/端点的排队或丢包及视频恢复；不能把问题仅归为同连接拥塞窗口，也不能由本轮未重置宣布网卡根因排除。保持发送策略未改，本轮有界测试已完成，不自动追加压测。

### 当前本机交付：混传发送/回执/断开诊断（2026-10-11）

- 响应用户“先看看实现，打 log 找原因”，静态核对 Sunshine / iroh / noq 发送实现；本批没有重启公网测试或远程部署。发现视频 DATAGRAM 在 QUIC 组包先于 STREAM，不能直接归因为文件优先级压住视频；仍共享拥塞窗口/底层队列。Windows 分段卸载是待查候选，未证实实际启用或导致重置。
- 新增可靠发送排队/写入汇总、无回执时仍可观察的文件进展停滞、视频首写前排队/单次 datagram 写入耗时/应用批次、异常关闭原因及连接快照。常规五秒汇总，视频慢帧每秒至多一条，不逐包打印。保持发送和恢复策略不变。
- Client、Cloud Node 内置 Client、Render 聚焦 Release 构建并同步 development dist，SHA 身份 `20261011_send_diagnostics_build.json`。两组本机测试 36+4 项通过，日志 `20261011_send_diagnostics_tests.log`；不作为公网混传改善证据。实现对比、字段口径和限制见 `20261011_send_diagnostics.md`。
- 90 未安装本轮诊断版本，仍为上一完整包；没有驱动、网卡或持久配置更改。**根因仍未确定，不再把 NDIS 重置等同于网卡硬件故障。** 下一步需用户重新允许实机测试后，对齐新增队列日志和系统重置/双端包时间；此前保持停止联网压测。

### 新驱动复测：用户已要求停止测试（2026-10-11）

- 用户安装后核实本机 Realtek 驱动已为 10.68.815.2023 / oem154.inf，1 Gbps。Pixels 程序与 90 安装保持上一批基线；环境及身份 `20261011_driver_updated_environment_before.json` / `client_identity.json`（后者同前缀）。
- direct 64 MiB 往返 25.455 秒、SHA 通过；四个完整五秒窗口 43.3–58.2 FPS，全日志 8 次 >100 ms、最大 154.5 ms。参考恢复 1 次、源提交停顿 1 次、提交后至完整接收延迟 6 次；不是短抖动已解决或只剩极少异常的证据。
- 随后 Relay 混传时，00:31:05.830 本机再次记录 NDIS 10400：硬件停止响应驱动命令、重新初始化后的第 1 次重置；测试最终 FILE_TRANSFER_TIMEOUT（91.014 秒）。**能确认重置事件，尚不能确认硬件损坏或排除 Pixels 发送负载触发驱动/卸载交互问题。** 用户反馈其他应用正常，这是后续触发条件调查的重要约束；不能把重置直接解释为与本产品无关。
- 用户明确“已经断开了，别测试了”，已停止新增测试；原测试器超时退出并完成会话清理，当前无测试 Client/Python，241 资源会话无活动连接，仅 90 桌面 Render 12556，两 Relay fresh。失败上传残片先核对源文件前缀 SHA 后删除。未开展计划中的纯视频后测，不把未执行项目计通过。证据 `20261011_driver_updated_{mixed_direct,mixed_relay,stopped,after}.json` 及对应日志。
- **后续约束**：用户未重新授权前不继续联网压测。优先只读分析复现负载、发送突发/队列与网卡驱动交互；不要直接宣布网卡硬件故障，也不要无证据修改驱动高级设置。恢复测试需要用户后续明确继续。

### 最新定位：双端 UDP 成批缺包、RTT 候选撤回、本机网卡再次重置（2026-10-11）

- 后续用户要求下载新网卡驱动：本机为 Windows 10 x64 / B550M AORUS ELITE / `PCI\VEN_10EC&DEV_8168&SUBSYS_E0001458&REV_16`。已从技嘉该主板官方支持页下载并解包 10.68.815.2023 至 `D:\Downloads\Realtek-LAN-20261011\DriverFiles\setup.exe`；INF 精确硬件 ID 匹配，Setup/驱动 CAT 签名有效。**仅下载，尚未安装，当前仍是 10.43.723.2020**；证据 `20261011_realtek_driver_download.json`。Realtek 通用下载页另列更新版本，不能将该主板 OEM 包称为全网最新。
- direct 64 MiB 往返 SHA 通过、24.137 秒，55.1–59.3 FPS，3 次 >100 ms / 最大 169.9 ms。两端全程 Pktmon 无 ETW 丢失，UDP 加密负载指纹匹配：90 发 91546 包、对端缺 1746（1.91%）；本机发 64531 包、对端缺 1743（2.70%）。各端 NIC/TCPIP 指纹集合相同，所有收到的包都有发送匹配，确认捕获点之间缺包，未定位具体物理设备。
- 首次修复 frame=290 请求后 4.944 ms 提交，其附近 17 ms 窗口发出的 18 包全未出现在对端；第二次修复 frame=297 才恢复，参考等待 137.452 ms。加密包不能直接还原帧身份，不把时间关联当作逐包帧映射。另一首次修复窗口无线上包，发送排队仍需区分。详见 `20261011_paired_udp.md`、analysis/nic/capture_identity。
- RTT 感知恢复重试候选三组测试通过，但两轮 direct 仍 3/6 次 >100 ms、最大 205.2/195.2 ms，没有证明整体改善，已归档 `backup/iroh_rtt_recovery_rejected_20261011/` 并逐文件 SHA 恢复原源码。开发 Client/内置 Client 重新构建同步 dist、SHA 一致；恢复后三组 CTest 通过，身份 `20261011_udp_baseline_build.json`。**当前不含 RTT 重试试改**。
- 随后强制 Relay 实际走 90，00:13:00 本机网络丢失，两个 Relay 和 Console 同时不可达；00:13:09 固定 8 秒业务重连耗尽；00:13:13.608 本机 NDIS 10400 报 Realtek 硬件不响应、开始第 17 次重置；约 00:13:23 承载才恢复。文件测试 89.685 秒超时失败，不能当作通过或正常 UDP 短抖动。90 没有 NDIS 重置，服务正常。原始证据 `20261011_disconnect_*`。
- 90 未重装，仍为上一批完整帧身份包，Render SHA `785DDEBB55324033DE8B6227411347F1E78EB8036E2D3F32A1E10A74DB767222`，仅桌面 PID 12556，三服务 Running。239 会话无活动连接，两 Relay fresh/ready；本机测试 Client 和远端自有文件已清理。失败上传残片先与源文件前缀 SHA 核对再删。两端 Pktmon 停止/过滤器为空；无驱动、网卡、宽限期、重连预算或持久配置变更。
- **精确下一步**：单独调查本机网卡重置，并用不同网卡/端点证据区分较早短丢包是否同源；梳理客户端重连预算与配置宽限期关系。媒体主线依双端证据定位发送突发/排队，不再盲调恢复定时器或重复自适应额度实验。不能由 00:13 重置倒推 00:03 全部丢包同源。M3 性能、NAT/长期稳定性、Windows/Linux 收束、Android 最后仍未完成。

### 最新定位：direct 混传的参考恢复等待与发送准入背压（2026-10-10）

- 新增 Client 参考等待及恢复请求写入诊断；修正统计口径：原视频间隔是交付解码器的帧间隔，完整帧也可能因缺失参考而不能交付。当次组帧函数执行耗时不等于等待分片的总时间。
- 三轮实际均为 direct `39.71.45.66:4613`：纯视频前测约 40 秒 59.9–60.5 FPS、最大 51.2 ms；64 MiB 不可压缩往返 32.663 秒且 SHA 通过，42.3–58.1 FPS、23 次 >100 ms/最大 299.9 ms；纯视频后测启动一次 156.7 ms，稳定阶段 59.9–60.2 FPS、最大 80.7 ms，无参考恢复。独立实例近似操作，不宣称完全受控因果试验。
- 23 次事件中 3 次关联参考恢复、11 次源提交间隔至少 80 ms、9 次提交后至完整接收延迟。184.3 ms 实例中 172.891 ms 等待参考，9 个完整帧不能交付；Render 首个修复帧在请求后 9.209 ms 提交，但未形成客户端可交付帧，约 100 ms 重试后的修复帧才恢复。本地请求写入 69/58 微秒不等于远端接收时间；具体丢失位置未确认。
- frame=1275 源提交间隔 236.066 ms，前有 216.969 ms 捕获准入背压，恢复后约 7 ms 提交；不能归为游戏/GPU 编码慢。余下 9 次需区分链路与端点队列，不能因组帧调用为微秒级就排除等待分片。
- 本批不改恢复/文件/QUIC 策略。修正旧测试 fixture 缺少 PXQ2 回执后，三组聚焦 CTest 全通过。本机 Client、Cloud Node 内置 Client 已构建同步 dist 且 SHA 一致，身份 `20261010_recovery_diagnostics_build.json`；90 保持上一批完整 Setup，Render SHA `785DDEBB55324033DE8B6227411347F1E78EB8036E2D3F32A1E10A74DB767222`。
- 清理后 235 个资源会话无活动连接，无测试 Client/远端文件，三服务 Running，仅桌面 Render 12556；90/BJ fresh/ready、未维护。23:27 至 23:37:25 本机无 NDIS 重置。未改持久配置，未启动本轮抓包。详见 `20261010_recovery_diagnostics.md` 及同名前缀 JSON/log。
- **精确下一步**：双端 UDP/端点队列定位首个修复帧未交付与剩余 9 次延迟，再评估基于 RTT/新帧进展的恢复重试及文件发送节奏；不重试已撤回的自适应额度，不重复安装已通过身份。M3 性能未通过，NAT/长期稳定性、Windows/Linux 收束、Android 最后继续保留。

### 当前交付：GameHook 缓存/真实帧编号冲突修复（2026-10-10）

- 复用上批日志确认 Relay 四次 133/184/266/167 ms 提交间隔，分别有 117/167/249/151 ms 捕获准入背压；恢复后 4–6 ms 即提交，同期编码任务最大 9 ms。不能再把这些间隔直接归为游戏或编码器停顿。
- direct 启动时缓存回放和下一真实帧均为 frame=152；客户端参考检查将重复非关键帧判为异常，且服务端随后收到恢复请求。按 Sunshine 同一编码入口递增身份的方式，GameHook 在串行编码线程内统一分配每 monitor 的帧号；不把此缺陷解释成全部启动等待/Relay 抖动。
- 三项新增序列/真实客户端参考判定测试通过，encoded_video_delivery/iroh_transport/iroh_frontend CTest 通过；外部环境用例跳过不计实机通过。旧源码保留于 `backup/iroh_hook_frame_identity_20261010/`。
- **90 已完整 Setup 安装** `iroh-hook-frame-identity-20261010`、3.3.97，314 文件 SHA 一致，服务 Running；Render SHA `785DDEBB55324033DE8B6227411347F1E78EB8036E2D3F32A1E10A74DB767222`。本机 Render build/dist SHA `C1EA2243DC209F9E6F90D1F46A1D09DC8EA44CDC89E9555E0D9B7DA2B895CF5B`；Client 未改。完整身份见 `20261010_hook_frame_identity_{build,installation}.json`。
- 真实 GameHook/64 MiB 不可压缩往返均 SHA 通过：direct 27.047 秒、51.2–58.8 FPS、4 次 >100 ms/最大 198.9 ms；90 Relay 42.061 秒、55.5–60.4 FPS、7 次/170.4 ms。direct 首帧已为 152 → 153，不再重复；缓存至真实帧仍约 130 ms，不能宣称修复首屏等待。Relay 采样无 IDR/RFI 请求；direct 后续仍有丢包计数增长、RFI 和准入背压，性能未整体通过。
- 清理后 232 个资源会话无活动连接，无测试 Client/远端文件；三服务 Running，仅桌面 Render 12556，90/BJ fresh/ready/未维护；本机 23:10 起无新 NDIS 10400。保留固定文件回执额度，未改驱动/网卡/持久网络配置；未启动新的抓包。详见 `20261010_hook_frame_identity.md` 及同名前缀 JSON/log。
- **精确下一步**：保留帧身份修复，继续针对 direct 混传丢包/参考恢复与捕获背压定位，首屏等待和稳定期分开统计；不重试已撤回自适应文件额度，不把 TCP Relay 重传等待当组帧问题。NAT/长期稳定性、Windows/Linux 收束、Android 最后仍未完成。

### 最新定位与恢复：网卡重置、TCP 重传已确认，自适应实验撤回（2026-10-10）

- 固定额度新版 A 轮发生长中断：本机 NDIS 10400 在 22:30:03 明确报告 Realtek Gaming GbE 硬件停止响应驱动并重置（本次初始化以来第 16 次）；当前驱动 10.43.723.2020。Client Relay 发送超时、Console 刷新超时、8 秒再准入窗口耗尽，64 MiB 文件测试失败。独立记录，不与正常短抖动混算；未修改网卡/驱动。失败上传残片已核对源文件前缀 SHA 后清理。
- B 轮无新 NDIS 重置，64 MiB 往返 SHA 通过、46.321 秒、51.9–60.4 FPS、18 次 >100 ms、最大 149.2 ms。两端完整 multi-file TCPIP 包头有五条媒体事件关联缺口/重复 ACK/相同序列重传；最差帧关联 ACK 停滞 81.124 ms、30 次重复 ACK、90 发同段三次。不是把全部 18 次都归为重传。证据 `20261010_credit_capture_*`，远端抓包两个非空片段已一并分析，Pktmon 已停且过滤器清空。
- 服务器同时记录捕获受额度阻塞 83–100 ms：视频预算缩至约 32 KiB，文件仍固定 256 KiB。注意 `source_gap_us` 是发送端封包提交间隔，可能包含传输背压，不能直接解释成游戏渲染停顿。
- 曾实现并完整安装自适应候选：回执延迟增长 50 ms 时缩到 128 KiB，恢复后放开。26 项测试通过、3 项外部用例跳过；但两轮实机仍有 78 次/519 ms、22 次/178.3 ms 间隔，64 MiB 往返 63.236/56.257 秒。文件 SHA 均通过，但未证明性能改善，不保留这项调参。另一次 Console 只读状态查询超时单独记录为 harness 失败，未算通过；之后仅为只读查询加有限重试，两个完成轮均未重试。
- 实验源码/测试已归档 `backup/iroh_adaptive_credit_rejected_20261010/`，活动源码恢复固定回执额度。还原旧文件时间戳曾导致增量构建漏编依赖、测试布局不一致；触发受影响目标重新编译后原 24 项测试通过，未发布失败构建。无网卡驱动、持久网络配置或应用宽限期修改。
- **恢复已交付**：90 已通过新鲜完整 Setup 安装 `iroh-credit-baseline-20261010`，3.3.97、314 文件 SHA 一致；Render SHA `83EE1980EDEA6620DA8C0155E5A99598DCC321F6E476D785A378CBAB1FC491FF`。本机 Client/Render/内置 Client build 与 dist 哈希一致。身份 `20261010_credit_baseline_{build,installation}.json`；当前安装不含自适应策略。
- 恢复后 64 MiB 不可压缩往返均 SHA 通过：Relay 37.607 秒、48.8–60.3 FPS、8 次 >100 ms、最大 277.1 ms；direct 25.525 秒、56.9–60.2 FPS、一次 169.5 ms（发送端提交间隔 133.1 ms）。这些是帧交付统计，性能仍未整体通过；不能把候选与恢复的不同网络轮次当成完全受控因果试验。
- 无活动资源会话、自有 Client/实例或远端测试残片；三服务 Running，仅桌面 Render 19200；90/BJ fresh/ready/未维护；两端 Pktmon 已停、过滤器为空。22:33 起到清理时无新 NDIS 10400。未改驱动、网卡设置、持久配置或宽限期。详见 `20261010_adaptive_credit.md`、summary、两端 capture、NDIS 和恢复清理证据。
- **精确下一步**：保持固定回执背压，不重复这项无收益的自适应实验。M3 对余下发送端提交停顿继续按捕获准入/编码耗时区分；将本机网卡重置单列为环境故障，稳定性测试须注明是否复现。Relay TCP 重传的顺序等待不能靠增大组帧缓存消除；默认仍优先直连。NAT/长期稳定性缺失证据、Windows/Linux 收束、Android/旧栈退役仍未完成，不扩展无关业务验收。

### 当前交付：文件接收回执背压（2026-10-10，已实装，残余抖动保留）

- 已修复本地 QUIC 写入即释放额度的缺口：文件累计未确认负载限制为 256 KiB，正常两个 120 KiB 块；最大合法消息 1 MiB 可单独发送。对端分发后通过控制流回执释放额度，输入/控制/数据报不共用文件额度。
- 通道 framing 升到 PXQ2，Windows 两端同步更新；Android 旧数据面保持，不增加认证/token。原源码及未提交修改已归档 `backup/iroh_file_receipts_20261010/`。
- 三组 Release CTest 通过，24 项通过、3 项依赖外部 Relay 环境而跳过；不能把跳过计为通过。新用例覆盖回执、双向进度、分段超时、文件满额时输入/媒体继续、回调内关闭及重复关闭。
- 本机开发 Client、Cloud Node 内置 Client/Render 已同步 dist 且 SHA 一致。90 已用完整候选 `iroh-file-credit-20261010` Setup 安装，3.3.97、314 文件校验通过，替代下方 return-priority 包。Render SHA `952F3EFE0794FDCBCB3ECE660C048AE815174CF5525ED94BD065269FE0DDF84A`；完整身份 `20261010_file_credit_{build,installation}.json`。
- 同样 64 MiB 不可压缩文件往返均 SHA 通过：Relay 前测 43 次 >100 ms、最大 429 ms；后测 13 次/290 ms，复测 0 次/90.7 ms、59.0–60.4 FPS。后测往返 47.413/38.640 秒（前测 44.951 秒），采样 RTT 最大 115.950/62.013 ms（前测 435.955 ms）。结果有波动，不宣称所有抖动已消除。
- direct 后测 25.711 秒、57.4–59.0 FPS、一次 199.1 ms 间隔，其中源帧间隔 163.3 ms。首次 Relay 后测最差 290 ms 也含 83.3 ms 源间隔，组帧仅 10 微秒。新回执在两端持续推进；不把这些新间隔逐一冒称为本批抓包确认的 TCP 重传。
- 新额度机制短故障检查通过：90 Relay 实际停约 3.275 秒后恢复，同一 Client 画面间隔 3.856 秒，文件继续并 SHA 一致，仅一次业务准入；恢复末窗口 58.3 FPS、最大 53 ms。不是长期/所有故障恢复通过。
- 清理后无活动资源会话、测试文件或自有 Client/实例；三服务 Running，仅桌面 Render 24392；90/BJ fresh/ready/未维护。无持久配置更改。证据 `20261010_file_credit.md`、summary、各轮 JSON/log 和 render_cleanup。
- **精确下一步**：M3 继续区分残余源帧停顿、Relay TCP 重传等待和剩余排队；本批文件额度/短故障功能已通过，不重复安装或整套容量检查。再补 NAT/长期稳定性缺失证据，Windows/Linux 收束后 Android/旧栈退役。不能把额度修改称为消除 TCP 重传等待。

### 最新定位：TCP 重传阻塞已由双端包头确认，真实混传仍有积压（2026-10-10）

- **确认一类根因**：强制 90 Relay 时，Client 缺失 TCP 前序字节、收到后续字节并重复 ACK，90 重发相同序列号后媒体恢复。例：本地 21:29:47.072 的 110.870 ms 帧间隔，缺失序列 781103239，90 两次发送间隔 140.512 ms；Client 对该缺口 ACK 停滞至少 63.878 ms，组帧仅 8 微秒。C 轮有 14 条帧事件关联到重传/ACK 缺口，不把它们等同于 14 个独立故障。见 `20261010_paired_tcp.md` 和 correlation JSON。
- **修正测试强度解释**：此前 1.5 GiB 文件使用重复内容，文件模块压缩后网络流量很小；已有文件正确性和实际画面统计仍有效，但不是 1.5 GiB 线上流量/饱和负载证据。本批新增不可压缩内容对照。
- 64 MiB 实际文件往返均 SHA 通过：自动选中 direct，29.239 秒、46.8–60.3 FPS、最大间隔 289.8 ms、5 次 >100 ms（启动异常保留，不能称 UDP 全程无抖动）；强制 Relay 实走 90，44.951 秒、20.2–59.6 FPS、最大 429.0 ms、43 次 >100 ms。Relay 混传出现数百 ms RTT、约 10 MiB cwnd；流优先级不足以控制已进入承载的积压，性能未通过。
- A 轮可压缩文件通过；B 轮不可压缩 256 MiB 上传完成但下载超过测试程序 90 秒时限，不能记为往返通过；C 轮因 B 的同名残留文件等待覆盖确认、实际没有 bulk 文件传输，仍出现媒体抖动，作为媒体/TCP 定位证据，不算混传通过。残留文件已按 SHA 核对后删除，随后 64 MiB 用独立文件名通过。
- A/B 环形捕获虽报无 ETW 事件丢失，方向上存在早期片段缺失，不能用于全程丢包结论；C 改 multi-file 捕获，从握手到关闭完整覆盖，按 TCPIP 组件和同一序列号/ACK 对齐双端，不直接相减两机墙钟。TCP 层捕获证明承载阻塞，不冒称定位了具体路由器/网卡的物理丢包位置。
- 本批未改产品代码/安装/持久配置。90 仍为 `iroh-return-priority-20261010` 完整包，Render SHA 已复核；仅桌面 Render 21528，三服务 Running。90/BJ fresh/ready/未维护；测试 Client/实例/文件已清理，两端 Pktmon 已停、过滤器清空。
- **精确下一步：实现混传时后台可靠流的有界发送背压/节奏，避免文件把共享承载堆积到数百毫秒；用本批 64 MiB 不可压缩场景对照。** 控制/输入不能跟文件一起被全局限速饿死；保留自动直连优先。TCP Relay 的重传等待是承载限制，不通过扩大组帧缓存掩盖，也不承诺消除所有弱网停顿。M3、NAT 缺失项、长期稳定性、Android/旧栈退役继续保留。

### 当前批次：回程优先级与固定 Relay 对照（2026-10-10，已实装，残余抖动未关闭）

- `Channel::Accept` 已按业务通道设置回程发送优先级，与 Open 对称；真实 QUIC 测试读取底层优先级并检查五类通道双向消息。增加 Client/Render 所选 Relay/IP、拥塞窗口和数据报缓冲观测，不改组帧缓冲和 QUIC 常数。
- 聚焦 transport/frontend 测试通过。首次动态候选用例因测试 URL 未带规范化尾斜杠而不匹配，修正测试输入后通过，保留失败记录。可读命名检查通过。本机 Client/Render build 与 dist 哈希一致。
- 90 已通过完整候选 `iroh-return-priority-20261010` 安装，3.3.97、314 文件校验、服务 Running；身份见 `20261010_return_priority_{build,installation}.json`，替代下面 driver-queue 包。
- 固定 90 的 1.5 GiB 混传：61.080 秒、59.9–60.6 FPS、最大间隔 95 ms；固定 BJ 的 512 MiB：22.984 秒、59.4–60.9 FPS、最大 122.8 ms、两次 >100 ms。512 MiB 固定 90 最大 173.4 ms 主要来自启动源帧间隔 166.6 ms。文件往返 SHA 全部一致。
- 双候选 512 MiB 仍有最大 254.6 ms、八次 >100 ms，Client/Render 实际均走 90。稳定配置后的 1.5 GiB 路由跟踪轮：69.238 秒、59.4–60.3 FPS、最大 246.9 ms、两次 >100 ms；Client 没有 Relay 切换。异常帧源间隔约 17 ms、组帧 8/14 微秒，Render 向 iroh 提交帧的周期最大耗时仍为微秒级。问题位于提交后至交付前，尚未区分 Relay TCP 排队/重传与端点调度。
- 随后 TCP 包头捕获轮 61.624 秒、60.0–60.4 FPS、最大 64.4 ms，无 >100 ms；抓包没有复现异常，不能用这轮排除重传或宣称根因已解决。保留 25,481 个包的捕获身份及分析；未改 QUIC 参数或扩充媒体缓冲。详见 `20261010_return_priority.md`、summary 和各轮原始日志。
- 两 Relay fresh/ready、维护标志已恢复为 false；90 三服务 Running、仅桌面 Render 21528，自有 Client/实例/远端测试文件已清理。Pktmon 已停止且临时过滤器已删除，机器持久配置无变化。本机 Client、Cloud Node 内置 Client 和 Render 均已发布并核对 build/dist SHA。
- **精确下一步：在异常轮同步捕获 Client 与 Relay/Render TCP 包头及任务排队耗时，按帧序列区分 TCP 队头阻塞与进程调度，再做针对性 pacing/背压修改。** 回程优先级、路径观测、完整安装和固定路径功能无需重做；性能未通过，NAT、长期稳定性、Android/退役仍未完成。

### 最新验证：持续混传复现 Relay 路径抖动，短故障可恢复（2026-10-10）

- 本批只做网络验证和定位，产品代码/安装未改；90 仍是下方 `iroh-driver-queue-20261010`，Render SHA 已复核。
- 相同 1.5 GiB 上传/下载：实际 direct 约 60 秒，58.3–60.1 FPS、最大间隔 149.6 ms、2 次 >100 ms；第一轮 Relay 61 秒，27.9–57.3 FPS、最大 628.3 ms、45 次 >100 ms。文件 SHA 均一致；不把文件成功算性能通过。
- 无文件 Relay 也出现最大 214.5 ms；另一轮 Relay 混传约 60 FPS、最大 53.3 ms。现象依赖实际路径/时间条件，尚未定位具体 Relay 或队列根因。之前驱动导致的秒级控制回调阻塞未复现，组帧调用只有微秒级，部分异常帧在进入组帧前已延迟。
- 真实停止 90 Relay 服务约 3.28 秒后恢复：同一 Client 总画面间隔 5.486 秒，自动继续视频和 1.5 GiB 文件往返并校验通过；恢复后仍有抖动。最初防火墙注入未产生中断，已单独标为无效故障证据，不能算恢复通过。
- 90 三服务 Running、仅桌面 Render 15760，测试 Client/实例/远端大文件和临时防火墙规则已清理，持久配置未改。详见 `20261010_sustained_mixed.md` 与 summary/原始日志。
- **精确下一步：补可靠流接受端的回程优先级、记录具体 Relay TCP 对端及发送排队，再分别固定 90/BJ 路径复测相同负载，按证据修复 pacing/背压。** 不先扩大组帧缓冲或盲改 QUIC 参数。M3 性能未通过；真实 NAT、长期稳定性、Android/退役仍未完成。

### 最新交付：解除设备驱动对网络控制队列的阻塞（2026-10-10，已实装）

- 混传真实 GameHook + 128 MiB 文件发现新缺陷：Hello 同步调用 ViGEm，失败前两次等待约一秒；控制/输入排队最大 1.366 秒。文件内容校验、画面连通通过，不等于响应性能通过。
- 已将网络入口的手柄操作改为独立有界队列，取消/断开/销毁使用现有生命周期规则；不调整媒体或 QUIC 常数。新增慢驱动与异步关闭测试通过；开发 Render 已同步且 SHA 一致。
- 完整候选 `iroh-driver-queue-20261010` 已在 90 通过 Setup 安装，3.3.97、314 文件哈希一致。取代下方旧 GameHook clipboard 安装身份；本机开发 Render build/dist SHA 一致。
- 后测真实游戏强制 Relay，128 MiB 文件往返内容一致，7.457 秒；五秒窗口 60.5 FPS、最大交付间隔 26.9 ms、无 >100 ms。ViGEm 仍失败但在独立线程，同期无达到 50 ms 阈值的网络排队/回调日志，原秒级阻塞未再出现。不是端到端输入时延或长期稳定性结论。
- 自有 Client/实例和远端测试文件已清理，三服务 Running，仅桌面 Render 15760；90/BJ Relay 仍正常。详见 `20261010_driver_queue.md` 及安装/前后测 JSON/log。
- **精确下一步：接续 M3，补持续动态画面与可靠流混传稳定性、直连/中继缺失路径和恢复证据；真实 NAT 未覆盖项明确保留。** 跨宿主容量/新增端点功能已补齐，不重复 BJ 登记、容量整套测试或无关录制验收；带宽均衡、长期稳定性、Android/旧栈退役仍未完成。

### 最新交付：跨宿主容量与新端点接入（2026-10-10）

- 90 Windows / BJ Linux 正式 Relay 实测：满额后释放备用 BJ，515 ms 恢复；释放原拒绝节点 90，4364 ms 恢复。各有 6,291,520 bytes 可靠回显一致，数据报 118/120、111/120。
- 新增可复用 `allocation-self-test`：四个端点同时使用双候选、不固定 home，依次 BJ/90/90/BJ，425/473/362/1161 ms 接入，每台两连接，已有健康 home 保持。数据报 115/120，可靠回显一致。Release 探针构建及真实运行通过，产品运行代码未改、没有重新安装。
- 不把上述结果称为带宽均衡、真实 NAT、十机器扩容或长期媒体性能通过；突发混传最大数据报往返 302–485 ms 和丢报均保留。代码核对确认 Console 只筛选/排序候选，iroh 决定 home，尚无端点预约均衡保证。
- 预检 201 个资源会话无活动连接；测试上限临时为 2，完成后两台原配置逐字节/哈希恢复、上限 4096、ready/fresh；90 节点服务 Running。详见 `20261010_cross_relay_capacity.md` 及对应 JSON/log。
- 本批后续混传已发现并修复上方驱动阻塞，后续动作以上方最新交付为准。M2 带宽/负载分配、长期稳定性及 NAT 缺失项保留。

### 范围决定：网络主线纠偏（2026-10-10，执行进度以上方最新交付为准）

- 用户要求重新核对整体流程，避免扩展为全产品验收。总计划 0.0 节和根 AGENTS.md 已同步约束；本批仅修订计划，没有安装、重启或新增实机验证。
- **当前阶段：M2 核心功能已交付、仍有容量/分配余项，接续 M3 网络闭环。** M1 Windows/Linux 正式 Relay 已运行；M4 最终交付收束、M5 Android/旧栈退役尚未完成。
- **精确下一步：** 核对当前 Render Endpoint 分配代码和既有证据 → 用 90/BJ 补跨宿主新增端点分配、容量拒绝重选/释放恢复 → 视频数据报与可靠流并发的背压/响应 → 补直连/中继/取消/断线恢复及 NAT 缺失项 → Windows/Linux 交付收束 → Android → 旧数据面归档退役。
- 双向 Relay 恢复、动态候选、维护排空、文件传输、GameHook 文本已有结果直接复用；未改动不重复整套验证。十实例为单机功能结果，真实跨宿主容量/负载分配和长期稳定性不冒称完成。
- **移出本轮必做项：** 独立录制/播放、普通桌面/WebView/RDP 全量剪贴板格式和其他独立 UI 验收。只有具体传输修改影响这些路径或出现回归证据才补聚焦检查。下方各批“下一步”是历史记录，不再作为当前执行入口。
- 当前安装身份保持 GameHook 批：90 Cloud Node 3.3.97 `game-clipboard-vdd-20261010`；Server/90/BJ Relay 沿用已验证安装。后续不重做 BJ 登记或完整安装，除非运行产物发生变化或当前安装检查发现问题。

### 已交付：GameHook 双向文本剪贴板（2026-10-10）

- 用户随后明确要求 GameHook 也支持剪贴板，覆盖下方暂时排除的决定。已更新根 AGENTS.md。
- 游戏实例不再依赖仅连接桌面 Render 的 user-proxy，复用既有 Windows clipboard 平台并通过原授权通道收发文本；抑制回声、限制 1 MiB、日志只记方向/大小/结果。普通桌面、WebView、RDP 不改变。
- 新组件三项测试及 Render 异步生命周期测试通过；开发 Render 已发布且 build/dist SHA 一致。90 已通过完整 Setup 安装 `game-clipboard-vdd-20261010`，314 个文件校验一致、服务 Running。
- 实机游戏连接完成中英文双向文本往返，4.722 秒；Render 日志确认 37 bytes 写入成功、38 bytes 回传。两端剪贴板已恢复，测试 Client/游戏实例已清理。结果 `20261010_game_clipboard_live.json`，安装和排障说明 `20261010_game_clipboard.md`。
- 首个 Setup 返回 1603：90 的 32 位 PowerShell 无法启动，虚拟显示驱动检查误判失败。安装/卸载检查已改为显式 64 位 PowerShell，新完整 Setup 成功；旧候选按哈希清单归档。
- 当时拟接续普通桌面/WebView/RDP 剪贴板及界面录制验证；此安排已由顶部网络主线纠偏覆盖，不再作为必做门槛。M3 仍未整体完成。

### 接续检查：文件传输通过，游戏剪贴板排除（2026-10-10）

- 实际 Windows Client 经强制 iroh Relay 完成 8,388,864 bytes 上传、取消重试、下载及远端文件清理，源/下载 SHA-256 一致。结果 `20261010_file_transfer_live.json`。第一轮脚本漏传 iroh 配置导致超时，保留 `20261010_file_transfer_harness_failure.*`；第二轮业务已成功但不足五秒未出现周期路径统计，保留 `20261010_file_transfer_short_metric_failure.json`。最终脚本核对强制 Relay 配置和 iroh accepted，不把短测试中未产生的路径统计称为实测。
- 游戏剪贴板测试发现其 Render 收到消息后因 user-proxy 未连接而丢弃；远端随后观察到测试文字不能证明游戏通道回传成功。本地和远端测试剪贴板均已恢复，Client/实例已清理。未修改生产代码或安装新包。
- **用户明确决定游戏不需要剪贴板**：取消拟议的游戏剪贴板实现，游戏剪贴板不再是验收项；这份失败是范围调整前的诊断记录，不继续修复。普通桌面文本、WebView 文本复制粘贴、RDP 多格式剪贴板继续保留。
- **接续**：普通桌面/WebView/RDP 剪贴板及界面录制尚未完成本轮 iroh 实机检查；刚才未执行录制操作，不能标为通过。运行安装身份仍为下方 relay-capacity 包。

### 当前交付：Relay 容量拒绝后的候选切换（2026-10-10，已实装）

- 正式 `px_relay` 双实例、每台上限两连接，已复现容量释放后仍停留在拒绝节点、超过五秒启动等待的缺陷。首轮通过不能证明稳定，第二轮失败证据 `20261010_relay_capacity_before_2.{json,log}` 保留。
- 使用固定 iroh 的公开 `RelayStatus::auth_denied_reason()` 检测拒绝，不匹配原因字符串、不增添认证。项目层临时跳过被拒绝的候选；保留最后一个可重试候选，健康连接不被恢复探测重新选路。不可用时到期重试，Console 撤下候选仍优先。
- 修复后备用节点恢复 476ms（最终构建复核 231ms）、原拒绝节点释放后恢复 4168ms，均有 6,291,520 bytes 可靠流及 120/120 数据报。单机正式 Relay 进程验证，不称跨宿主容量验收。健康连接保持；Console 撤下替代候选时重新激活剩余配置候选，避免全部被本地抑制。
- PostgreSQL Relay 节点 4/4，新增十节点满额/离线/禁用/过期/维护/释放筛选；Rust 6/6、frontend/transport CTest、动态候选两例通过。持续视频 SDK 故障恢复 8.666 秒。Console 排序不等于 iroh 均匀分配，不宣称负载均衡完成。
- **90 完整安装**：Cloud Node 3.3.97 候选 `iroh-relay-capacity-20261010`，314 文件核验；Setup `E5497A340DB2F9A3DF37593ED66DDFD9149A743E28C67E56A74BE7948D2BE9D8`，product-manifest `72B4B2B208E16F586091C670B2DA9188DF39E86113E37D352BA9F84D261631BE`。本机开发产物已同步 dist 并核对 SHA。Server/两 Relay 沿用下方安装，未重复安装。
- **真实游戏/RDP 通过**：强制 Relay 游戏八个五秒交付窗口约 60 FPS、最大间隔 56.3ms；不是显示/长期性能结论。RDP 有可靠流图像并显示原工作区；断开后原记事本 PID 16496、Session 2、启动时间不变。见 [本批说明](20261010_relay_capacity.md) 和 `20261010_relay_capacity_live.json`。
- **清理完成**：自有 Client/实例/会话已退出；三服务 Running、仅桌面 Render 48640，90/BJ generation 81/7、ready/fresh/未维护，持久配置 SHA 不变、机器 trace 为空。
- **接续**：不重复本批安装/BJ 登记。跨宿主容量、负载分配及长期混合负载仍待补；接续 M3 Windows/RDP 附属功能，Android 最后，P0–P7 未整体完成。

### 当前交付：动态 Relay 候选与维护排空（2026-10-10，已实装）

- Console Report → Service → Render 心跳已接入候选数组更新；Client 每五秒使用原会话凭据刷新端点地址及候选。候选变更不重启桌面 Render、不创建新资源会话，空数组明确撤下全部候选，启动配置的校验保持。
- 健康重试与候选更新共用受控状态，避免重新加入已撤下的 Relay。仅热更新 Relay URL/QAD 候选；CA、绑定地址、直连策略不在热更新范围。
- **90/BJ 实机通过**：Client 5.062 秒收到 BJ 单候选、Render 15.252 秒上报 BJ home；原实例/会话/EndpointId 不变、无重新准入。健康旧转发连接保留，不宣称旧视频已强制迁移。新建游戏仅使用 BJ 候选，成功连接解码；取消维护后在运行 Client 自动恢复双候选。详见 [本批说明](20261010_dynamic_relays.md) 与 `20261010_dynamic_relay_real_game_retry.json`。
- 首次立即重开旧会话遇到 `409 connection_retiring`，保留失败报告 `20261010_dynamic_relay_real_game.json`；改为明确结束测试实例后验证新分配。维护传播阶段有两次 >100ms 交付间隔、最大 126.4ms，不能称零抖动；该占用保护和呈现性能仍不能由本次功能结果代替验收。
- 本地两/十个 TLS Relay 的动态添加、候选替换、持有可靠流/数据报及新 peer 测试通过。十实例仅单机功能检查，不是多机容量验收。最终 SDK 故障恢复 8.688 秒；Rust 五项、C++ frontend/transport CTest 通过。
- **当前完整安装**：Cloud Node 3.3.97 候选 `iroh-dynamic-relays-20261010`，314 文件核验；Setup `1D6E823B90A292028F61D047C13E290B2E41866B40F1EC21E5FC3809F9679D33`。Server 1.0.55，35 文件核验；Setup `620806E4CE183075AD7A6758D5C5F841AC2381875807593D2AE48E2412BC9891`。替代下方 fast-recovery 安装。本机开发 Client/Render/Service 已同步 dist 并核对哈希，完整身份 `20261010_dynamic_relay_installation.json`。
- **清理完成**：90 三服务 Running，仅桌面 Render 54096；90/BJ fresh、维护已取消，generation 81/7、epoch 66。测试实例/会话/Client/探针已退出，三份持久配置 SHA 不变。D 盘只迁移三份自有旧候选并逐文件核验，记录 `20261010_dynamic_relays_space_archive*.json`。
- **接续**：不重做本批安装、动态候选或 BJ 登记。正式 Relay 容量/均衡、长期混合负载仍未完成；按 M2 剩余项及 M3 Windows/RDP 附属功能推进，Android 最后，P0–P7 尚未整体完成。

### 当前交付：缩短等待及真实双向 Relay 恢复（2026-10-10，已实装）

- **90→BJ 实测从此前约 30 秒缩短到 8.028 秒。** 随后恢复 90、停 BJ，同一个 Client 再次重新准入并解码；EndpointId、游戏实例 `9b508329-8d85-4043-b37e-bacfd0921cb0`、资源会话 `483f920d-202f-4c9a-adfe-0b9daeebe082` 全程不变。两个重拨握手分别 29ms/7ms，恢复后有约 60 FPS 视频。详见 [本批说明](20261010_fast_recovery.md) 和 `20261010_fast_recovery_real_roundtrip.json`。
- 反向切换的协调标记落后于实际停服并在恢复后才写入，因此原 0.008 秒读数 **不代表故障恢复耗时**，正式报告将反向耗时置空。反向功能由第二次 accepted、新视频及解码、BJ 容器仍停止、Console 原会话 connected 共同确认。不据此宣称瞬时恢复。
- 原因是 iroh 1.3.0 默认三十秒 QUIC idle timeout。项目通过公开配置改为 **八秒无应答超时、一秒连接心跳**，不再改上游源码，不以没有视频判断断线。应用退出宽限期仍由 Console 配置；既有业务凭据、授权及八秒重拨预算保持。实际恢复时长仍受选路和 QUIC 重传影响，不承诺所有网络下八秒内完成。
- 同一持续视频本地场景改动前 30.641 秒、新版 8.625 秒，十五秒检查先失败后通过；报告 `20261010_fast_recovery_{before,after}.json`。无应用流量十六秒仍可双向可靠读写和传数据报；Rust 三项测试通过。短停 Relay 1013ms 保持原 QUIC，可靠往返 175 次、数据报 1156/1201，最大数据报间隔 1339ms；普通 frontend/transport CTest 通过。
- **90 当前完整包**：`iroh-fast-recovery-20261010` Cloud Node 3.3.97，Setup 安装后 314 文件核验。Setup SHA `48250CB472AF04FF0DBFAE14FE1BAFB35262C332D3DA448906E6B205B9F568AE`，manifest `DAED10C7A0222C44C32600950CC6BD5396104CD27F7138A7384353842BE18B96`，安装 Render `88F0C6861E0D8B86935E9842E65A531751A82C55D97A56038A1F877456EFE5AE`。下方队列修复候选已被此包替代，不重复安装旧候选。
- 本机 Client 开发 build/dist SHA `EF89D54D8835AED2AF8A20E6391A6C7960CA5AAD45F2542567F5C02695558348`，Render 开发 build/dist `B7768B075BC93161D1CE3E1B10E7E304B10CDC875A7EB31FF7589877120F2DBC`，Cloud Node Client 开发副本也已发布核验。完整身份见 `20261010_fast_recovery_installation.json`。原 iroh 隔离补丁 84 文件/补丁哈希复核通过。
- **清理完成**：自有 Client/游戏实例/会话和本地探针已退出；Windows Relay generation=80、BJ generation=6，均 ready/fresh；90 三服务 Running、仅桌面 Render 41648。机器 trace 为空，console.env/relay.env/iroh-relay.json SHA 保持此前值，Console epoch=65。
- **下一步从此处继续**：动态 Relay 候选更新及维护排空，之后按 M3 做 Windows/RDP 附属功能接续；不要重做本批故障恢复或 BJ 登记。真实双向连接恢复已通过，尚不代表连接内无缝迁移、十 Relay 扩展或长期稳定性完成。Android 最后，M2/P0–P7 未整体完成。

### 当前交付：持续视频下的双 Relay 故障恢复（2026-10-10，90→BJ 实机通过）

- **本批修复已实装。** 停掉 90 Relay 并保持离线，原游戏 Client 经 BJ 重新准入并恢复解码，29.928 秒恢复，同一 EndpointId、资源会话和游戏实例。恢复后 5 秒窗口 304 帧、60.7 FPS、最大间隔 30.1ms、0 次 >100ms、0 IDR/RFI；QUIC 重拨本身 28ms。不能将一个窗口称为长期性能验收，也不是无缝迁移。证据 [实机结果](20261010_relay_queue_real_game.json)。
- **原因及改动：** 持续视频填满断线 Relay 的发送队列，公共发送循环等待该队列，连健康 BJ 握手也被阻塞。固定 iroh 1.3.0 在每次拨号重置三秒清队列计时器，快速失败及退避可持续不消费。用户明确批准隔离最小补丁：断线拨号/退避消费不可达数据报，可靠载荷由 QUIC 重传。原始 registry 和参考源码未改。
- `rust_transport/vendor/iroh-1.3.0` 来自原始发布 crate，仅 actor.rs 变化；`patches/iroh/manifest.json` 固定原 crate、上游 revision、全部 84 文件及补丁哈希，验证脚本通过。实现与证据见 [定位记录](20261010_endpoint_refresh_queue_diagnosis.md)。
- Console 已支持使用原 frontend 凭据刷新当前 EndpointAddr，Client native/RDP 重拨保持端点身份、会话、修订号和额度；没有新增票据或认证系统。现有重拨八秒总预算保持，尚需等待旧 QUIC 约三十秒超时才进入重拨。
- **前后对照保留：** 补丁前八次真实故障失败，另一次诊断准备中止；原汇总 `20261010_endpoint_refresh_real_attempts.json`。空载本地用例曾通过但未覆盖故障；补上持续 9000-byte/16ms 视频后 75 秒失败，`20261010_dual_relay_business_loaded_failure.xml`。补丁后 auto/relay_only 与双方 relay_only 分别 30.926/32.654 秒通过，旧 Relay 全程离线，业务重新准入且收到视频。
- 当前 90 **Cloud Node** 为完整 `iroh-relay-queue-20261010` Setup 3.3.97，314 文件核验：Setup `D994ED3D634FFF9018022BBA749153A52226674182BB8C41EB22697D9A43DBF3`，manifest `FAD8AC72D7A5F35BD6D09D25EA0D2B2BB8F62C026DBEA199E644118BAC97DD99`，运行 Render `62B59B1BF6794501BF5F5D017929484A479EC1D5106F63BCBDA2873165FB3841`。此前 endpoint-refresh/endpoint-trace 安装均被替代。只用完整 Setup，没有手工替换运行 EXE。
- 当前 90 **Server** 为完整 1.0.55 Setup（35 文件核验），含地址刷新和共享 TLS ACL 修复：Setup `DE3386BECDA6554FE5C70776660DBA01DDD25D729E7D7342176651AB49B0ACEE`，manifest `402C39F29B4AFFBCEC0C421BABB5CE137641AC31A0CA2D5CC5F9E7D81978A26F`。Console epoch=65，持久配置未改。
- 本机 Client 开发 build/dist SHA `AED508F11BD35FE76C7B2DFD4F45D017CFC126C312423EA9CF5A24914F4E2072`，Render 开发 build/dist SHA `7915D06C2F4D9A77FB6165E744EFFE0EEC860EAFC290A7BB51BC211A439CEC8F`。完整身份见 `20261010_relay_queue_installation.json`。
- 普通 frontend/transport CTest 与两个 Rust 生命周期测试通过。PostgreSQL sessions 首轮 19/20 为既有 RDP 并发锁超时，未改参数复测 20/20；保留两次报告，不声称锁问题已修复。
- **清理完成：** 自有游戏实例、会话和 Client 退出；90 Relay 恢复，90/BJ ready/fresh（generation 79/5），三个服务 Running，仅桌面 Render 53288。诊断机器环境和服务 Environment 已恢复为空，trace 默认关闭。两个公共 Relay 配置保留，不回退为单 Relay。
- **下一步从此处继续：** 缩短故障恢复等待（当前约三十秒 QUIC 超时），再推进双向切换/动态候选及维护排空；不要重做本批安装或 BJ 登记。M2 和 P0–P7 未整体完成，Android 最后。

### 双 Relay 接续（2026-10-10，home 重选已修，业务地址刷新待接通）

- 90 已用现有 Server 1.0.55 安装中的 Relay 切换到 iroh，复用原登记 ID；不是再次安装或复制二进制。Windows Relay 与 BJ Linux Relay 均已 ready/fresh，TCP/UDP 4605。90 严格 TLS 探针可靠流 6291520 bytes 通过，数据报 107/120，最大 RTT 315326us；只认定功能连通。
- 90 持久 `iroh-relay.json` SHA `415C64CB9FDC508068DBE6F43035D8F3FB6B57B2BAE9EBBCFFD33BD2AD283EA8`，relay.env SHA `3DCBB74703D4843427E7C92ADDBC2CB8FFE1E60B98ABB05B7420AAEB172BDBB1`。首次启动因服务账户缺少 JSON/证书读取权限失败并回滚，随后补精确只读 ACL 后成功；安装脚本升级时保留共享证书权限仍需修正。
- Console 已保留 BJ CA 并加入 90 Relay CA，当前 console.env SHA **`872CAC22E6D6B4849D7A314BC7F2311D349D3B7960E75CCD57E530F007B26C8D`**，epoch=64；不要恢复前批 BJ 单 Relay 配置。原配置已备份 `console-before-two-relays-20261010.env`。
- 双候选探针确认初始路径 90 Relay；中断 90 约 10 秒并恢复，持有的 QUIC 连接未在测试窗口内恢复，BJ 未出现切换流量。失败证据 `.cache/two-relay-failover.log` / `two-relay-failover-error.log`。两服务已恢复；不能称双 Relay 自动切换通过。
- 已查明锁定 iroh 1.3.0 的 HTTPS 增量探测可能为空，`network_change()` 仅触发 OS 网络检测也未解决。新的多候选健康任务在 home 持续失联后经公共 candidate-update API 触发完整探测，不修改上游、不改变端点身份，关闭/析构取消任务。
- 本机停当前 Relay 后重选，取最新地址重连通过（断线至重选 4589ms）；实际 90→BJ 通过（4995ms、6291520 bytes 可靠流、120/120 数据报、最大 RTT 546904us）。**原 QUIC 连接迁移仍未通过；本次验证明确重新取地址、重新建立连接。** Client 固定 EndpointAddr 重拨尚未接通刷新，不得称应用自动恢复完成。
- 修正 Server 安装脚本共享 TLS 文件 ACL，8 个安装测试通过；2 个 Rust 生命周期测试和 C++ iroh_transport/iroh_frontend 通过。Client/Render 开发 dist 已发布，40/277 产物核验。Client build/dist SHA `4794AC73E204ABB592F6FEC537B911AF96B912F2FB065654B7E278920812DC20`，Render `B4ED85B048A67FE5691CF9C7C334199977240862E96C783150A13C3B6685E078`。
- **部署边界：** 90 Cloud Node 仍为前批完整 Setup；本批健康任务尚未安装 90，安装脚本改动也尚未用新 Server Setup 验收。没有手工替换运行 EXE。两 Relay 已恢复 ready/fresh，90 三服务 Running，测试探针退出、未启动云应用。当前两个候选配置保留，epoch=64、90 generation=69。
- **下一步从此处继续：** 接通 Client 在原资源会话内刷新 EndpointAddr / 再准入，验证真实应用故障恢复，再推进动态候选/维护排空及完整包安装。Android 最后，P0–P7 未完成。详情 [双 Relay 重选记录](20261010_dual_relay_reselection.md)，不要重复登记、切换或前批安装。

### BJ WebView 首帧恢复、桌面 Console 接入修复（2026-10-10，本批已交付）

- 沿主线补 BJ WebView/普通桌面，没有重复 Relay 登记或恢复旧 Console 配置。
- WebView 首测实例 `5b799633-840a-4e04-8b64-344ba45aa52c` 已授权且输入可发送，但 80 秒无客户端视频：唯一 readiness 帧早于客户端接入，后续源端 0 FPS。修正为接入/刷新时补发 Render 自己持有的 GPU/软件缓存，随后请求 CEF 重绘，新增 activity/frame_replay 日志。预改文件完整归档 `backup/webview_first_viewer_20261010`。
- 完整候选 `iroh-webview-viewer-20261010` 已 Setup 安装至 90，314 文件验证。后测实例 `5da6e9a8-2a37-4af3-b5c1-fdcd5ba4cdf5`：授权后 69ms 收到重放 frame 2，两张截图均有真实网页及视角变化；15 窗口、4222 帧，实际 iroh-relay，0 IDR/RFI。加载期最长 943ms（源端相应间隔 922ms），全程 14 次 >100ms；只认定首帧/画面功能恢复，不认定公网性能全部通过。此次网页也正常加载完成，不能把加载改善全部归于补帧。
- 普通桌面首测未接入，真实原因是 Render 共用 admission 仅对云应用识别 Console 会话；桌面进入密码分支，和 Panel 免密码启动参数不匹配。已有 Service/Console 原本支持 desktop grant，无须新增协议或认证。新增真实 QUIC 回归在旧实现返回 `INVALID_ARGUMENT`，修正后 direct/私有 Relay 各通过 17 个 frontend 用例及 grant 检查。
- 第一个桌面候选已安装（314 文件）但仍未出图：Render 初始化又把桌面设备 ID 填入应用实例 ID，导致 desktop grant 设备匹配、实例不匹配。已修正桌面实例为空，保留游戏/WebView/RDP 实例语义；空实例在设置刷新后保留的生命周期回归通过。测试已改为必须见到 Client accepted 和解码帧，不再只根据 Console connected 状态判成功。
- **最终交付**：完整候选 `iroh-desktop-identity-20261010`（含 WebView 补帧）已 Setup 安装 90，3.3.97、314 文件一致、服务 Running；安装 Render SHA `936B82F78F2B7D2CD21FBBA4A780CED6F8C0621892424D3F1607D4E7FFF48AD4`，开发 build/dist SHA `E61F921031BEFD0EBCD779382A6CE5535E0EBD8F2DBAAB32256B7DB92BF82D3C`。
- 桌面后测 session `1706bd23-81b4-4146-bb8f-dd17622ee595` 实际 accepted、收到并解码 frame 4，截图显示 90 桌面，随后约 30 秒仍收到画面，日志 `path=relay`。静态桌面有 27 秒无新增帧，源端同为 27 秒，无参考恢复请求；这是按画面变化采集的功能验证，不能用 0.4 FPS 窗口当动态画面性能结果。原始证据 `20261010_bj_desktop_results.json`。
- 测试临时设备 ACL 已恢复 `users=[] groups=[]`。ACL 修改会使旧管理员会话失效，清理脚本已增加重新登录，避免第一次恢复后 403 导致结果未落盘。后续桌面测试仍只使用并清理自己的授权/Client/会话。
- 清理完成：测试 Client/云实例已退出、临时 ACL 恢复，90 仅桌面 Render 48064、三服务 Running、QoS 0。Console 持久配置 SHA 仍为 `03352CEEAB08C7B89A342505D398629A6847757A617E19478849BBB567DD4438`，BJ ready/fresh、空闲连接 1；90 Relay 仍是旧协议。
- **下一步从此处继续**：90 Relay iroh 统一与双 Relay 选择/更新/故障恢复；Android 最后。勿重复本批安装或 BJ 登记。详细记录 [WebView 与桌面交付](20261010_bj_webview_desktop.md)。M1/P0–P7 未整体完成。

### BJ 恢复额度修复已交付（2026-10-10，残余短抖动及 Windows 抓包盲区保留）

- 基线实例 `b2e715bf-ea98-4079-ae3e-88f2b6c6cd4a` 已退出：16 窗口 4315 帧，35.9–61.0 FPS，最大 761ms、25 次 >100ms，0 IDR/RFI。90/BJ 抓包交叉证实双向 TCP 重传等待；本机 NIC 抓包缺少大部分入站流量，不能算完整三点证据，下一轮补全采集层级。
- 发现可复现的独立放大因素：正向延迟恢复至 19–41ms 后，额度仍因固定一秒冷却停留在 32KiB，反向反馈迟到时再次暂停采集。新增回归先失败，改为收到确认的正向恢复即恢复正常额度，不清除债务，不改 250ms 探测或 QUIC pacing。再次正向拥塞仍缩窗。
- Direct 与私有 TLS Relay 各 36 个媒体用例、16 个 frontend 用例通过。完整 Cloud Node 候选 `iroh-bj-recovery-20261010` 已 Setup 安装到 90，3.3.97、314 项哈希一致；开发 dist 三个变化 EXE 已同步核对。
- 同游戏后测实例 `8c42ed2f-ce00-44b9-9fb8-719d74d99ecd`：16 窗口 4814 帧，59.5–60.8 FPS，最大 125.6ms、1 次 >100ms、0 IDR/RFI，在线没有 >=80ms 采集暂停。后测链路重传也明显更少，不能将全部改善归因于代码；固定条件回归证明额度恢复缺陷已修正，不宣称公网卡顿完全消除。
- 残余 frame 3407 源端 16.355ms、接收 125.581ms、组帧 4us；附近 BJ 下行持续发送。Windows 全组件 pktmon 仍缺少大量入站字节，无法区分最后一段网络接收和客户端调度，不能用缺包现象证明链路丢包。后续需更换接收侧观测，不重复同样采集。
- 已清理两轮自有实例/Client 和三端抓包，90 桌面 Render 41916、三服务 Running、QoS 0；BJ ready/fresh、空闲连接 1、容器 restart=0。完整记录 [本批修复及边界](20261010_bj_forward_recovery.md)。下一主线仍为 BJ WebView/桌面、90 Relay iroh 统一与双 Relay 切换，Android 最后；P0–P7 未整体完成。
- 上一候选已逐文件校验后可逆移至 C 盘（319 文件），记录 `20261010_recovery_tcp_candidate_archive.json`。三点原始数据在 `C:/Users/chess/AppData/Local/PixelsRelayBuild/bj-threepoint-before`；90 文件仍在 `D:\112233`。Console 持久配置不回退。

### 当前定位结果：BJ 中继 TCP 按序阻塞（2026-10-10，仅诊断，未改参数）

- 用户要求先定位。直接读取上一批日志并补一次真实游戏 + BJ eth0 包头/容器 TCP 队列采样，没有重做安装、登记或切换配置。
- 本轮复现更差结果：最大交付间隔 513.0ms，15 窗口 51.1–60.1 FPS，6 次 >100ms，0 IDR/0 RFI；实例 `f1223d26-1ea6-4d3c-9f0d-af740ef2446f`，实际路径 iroh-relay。失败性能记录保留。
- **已定位机制：** 90→BJ 某 TCP 序号缺口持续 315.917ms，BJ 的 SACK 已确认后续字节但不能越过前序缺口；BJ→本机有重传、累计 ACK 停滞 387.877ms、Send-Q 184926 bytes、内核重传 26755 bytes。对应时段客户端出现 331ms/513ms 空洞，组帧调用仅 11us/7us。
- 这次强制 Relay 的数据报实际走 TLS/WebSocket/TCP，UDP 4605 是 QAD。不是 UDP 组帧计算卡了几百毫秒；也不能只凭 API 叫 datagram 就认定该 Relay 路径没有 TCP 队头阻塞。尚未定位具体物理丢包/迟到设备。
- 后续在途额度收缩又造成约 250ms 的源端采集暂停，属于会影响恢复节奏的第二环节。没有直接取消背压、修改 IDR 或猜测性改参数。iroh Nagle/flush、Sunshine pacing、Moonlight UDP/FEC 已只读核对并记录 revision。
- 227639 包、抓包内核丢弃 0；原始 pcap/ss 保存在 C 盘和 BJ 专用目录，SHA、序号交叉检查、两端日志及详细边界见 [定位报告](20261010_bj_stutter_diagnosis.md)。测试实例/Client、tcpdump/采样均已退出，90 仅桌面 Render 54168，BJ 容器 running / restart=0，Console 持久配置未变化。
- **下一步从此处继续：** 追两条已确定的 TCP 路径及发送 pacing/额度恢复，不重复安装登记，不将诊断当修复。随后补 BJ WebView/桌面、统一 90 Relay 和双 Relay 切换，Android 最后。

### 当前执行入口：实际 Console 升级及 BJ 接管（2026-10-10，已完成；BJ 游戏接通，短抖动未通过）

- 用户确认开始，并要求持续更新记录，避免重复工作。已完成的独立 BJ TCP Relay / UDP QAD 和隔离数据库管理测试保留，不重复从原型阶段开始。
- 本批顺序：核对 90 当前安装 → 构建完整 Server 候选（含 migration 0039）→ Setup 覆盖升级及逐文件核验 → 登记 BJ 并切为 Console 管理 → 验证在线/排空/地址下发。
- Pixels MCP 已连接 90：设备 428358431，Windows x86_64，SYSTEM。开发和安装文件只放 `D:\112233`。本机 D 盘余量低，新的 Rust 构建和 Server 候选放 C 盘专用目录。
- 已用完整 Setup 将 90 从 1.0.54 升至 1.0.55，35 项安装文件哈希一致，Console/Relay Running；schema `current=39 target=39`。迁移备份位于运行数据 `database-upgrades/0d0ffef855364c67a36d4a93ff37b3a9`。包证据 `20261010_bj_console_package.json`。
- BJ 已登记且切为 `console_managed:true`，ID `3314f150-27be-42cb-ac2e-bf3e0e68b3c7`；首次上报为维护状态，解除后已确认实际 `reported_draining=false`。Console 重启后也自动重新认证，上报新 control_epoch=63。
- **当前持久配置已变化，不再恢复旧的禁用 iroh 配置：** Console 启用自动 P2P/Relay 策略，仅配置 BJ CA，Relay 地址由数据库下发。配置 SHA `03352CEEAB08C7B89A342505D398629A6847757A617E19478849BBB567DD4438`；旧配置保留 `config/console-before-bj-20261010.env`。
- 90 Service 已真实收到配置：03:32:07 UTC 日志 `iroh_configured=true`、generation=334。桌面 Render 自动重启为 PID 54168，BJ 上报一个空闲 Relay 连接。原 90 Relay 仍按旧协议工作，尚不是双 iroh 池。
- 受管理 BJ 再测可靠流/数据报通过（120/120、6,291,520 bytes），最大往返 961936us，见 `20261010_bj_managed_probe.log`；混合突发延迟问题仍保留。真实游戏使用已有脚本验证，未重复之前的 TCP 对照。
- **游戏实际结果已完成：** 本机 Client → BJ → 90 GameHook，实际路径日志为 iroh-relay，两张无弹窗遮挡的截图显示视角变化。17 个五秒窗口、5044 帧，49.8–60.8 交付 FPS、最大间隔 258.3ms、7 次 >100ms、0 IDR/0 RFI；无人工 QoS 限速。业务接通通过，短抖动/长期性能未通过，不能与之前 UDP direct 的成绩混淆。原始结果和汇总分别 `20261010_bj_game_results.json`、`20261010_bj_game_measurements.json`。
- 测试自有实例 `a95a6557-fc75-4ea7-9003-63ac9335b4f0` 和 Client 已退出，90 只剩桌面 Render 54168，Console/Relay/px_service 均 Running、QoS 0，持久 iroh 配置保留。接续详见 [实际接入报告](20261010_bj_console_integration.md)，不要重做完整升级或再次登记 BJ。
- 下次抖动排查有现成帧证据：frame 1864 源端间隔 16.933ms、接收间隔 258.161ms、组帧调用 21us；frame 1502 另有额度收缩后的采集暂停。源/收两端选定日志已保存，按帧号关联，不用不同机器墙钟直接相减。完整任务尚未完成。
- 下一步：沿上述帧证据定位 BJ 短抖动、补 WebView/桌面验证 → 90 Relay 统一 iroh → 双 Relay 选择/动态更新/故障恢复 → Windows/Linux 交付收束 → Android 最后；未完成步骤不得标为通过。

### 正式 Relay 管理与 BJ-200M 公网部署（2026-10-10）

- Console 已接入 iroh Relay 的真实连接/流量、QAD 端口、维护排空和管理断连处理；节点认证/重连下发数据库中在线、新鲜、未排空且有容量的地址。复用已有登记凭据，无新增传输票据。
- Windows/Linux Relay 聚焦测试、隔离 PostgreSQL 管理连接及存储用例、Console Web 类型检查/测试/构建通过；Windows 二进制和 Web 开发输出已同步并核对 SHA。
- BJ-200M（49.232.233.61）已通过完整镜像和 Compose 包安装，目录 `/opt/pixels/relay-bj-200m`，TCP/UDP 4605。用户放行云安全组后，严格 TLS 的公网强制 Relay 测试通过：120/120 数据报、6,291,520 bytes 可靠流，实际路径为 BJ Relay。
- UDP QAD 另行实测通过：严格校验证书后取得公网映射地址，连接及关闭全过程 238ms；见 `20261010_bj_qad_probe.log`，该数字不是媒体 RTT。
- 混合突发最大数据报往返 **1.303 秒**，保留为未解决性能观测；不能当作真实游戏流畅度通过。两测试端均在本机 Windows，本轮未修改 90。
- BJ 当前明确为 `console_managed:false` 独立验证配置，尚未接入真实 Console。M1 后续需升级 Console 完整包和 schema 0039，再接管 BJ、验证实际应用；运行中地址热更新、多 Relay、Windows 完整安装仍有余项，Android 最后。详见 [本批记录](20261010_bj_relay_management.md)。

### 正式 Relay 双系统首批开发（2026-10-10）

- Android 已按用户要求放到最后；正式 Relay 必须同时支持 Windows/Linux，用户正在准备额外服务器。
- 正式 `px_relay` 已接入 iroh 1.3.0 数据面、TLS/QAD 配置、精确连接容量、内部排空、流量日志和有界退出；增加 Linux SIGTERM，复用 Windows Service 停止入口。
- Windows / WSL Ubuntu 20.04 各 14 项 Release 测试通过；正式开发副本的可靠流和数据报强制中继已实测，Linux SIGTERM 正常退出。两系统产物与构建输出 SHA 一致。
- 最终记录 Windows 120/120、Linux 117/120 数据报；另保留 Windows 119/120。可靠流全部逐字节校验通过。混合突发有上游队列满，未作性能通过结论。详见 [本批报告](20261010_formal_relay.md)。
- M1 尚未整体完成：下一批接 Console 管理上报与地址分发，再接完整产品包和多 Relay。现阶段未安装系统服务或容器、未修改 90、未提交/push；测试进程已停止。

### 整体规划接续（2026-10-10，Android 顺序按最新用户指示调整）

- 当前执行顺序见 [总计划第 0 节](../../iroh_transport_execution_plan_20261008.md)：M0 基线收束 → M1 正式单 Relay → M2 多 Relay → M3 Windows/RDP 整体验收 → M4 Windows/Linux 产品交付 → M5 Android 最后接入及旧栈退役。
- UDP 稳定性验证贯穿主线；正常网络下重复长停顿/新增回归先修复，2Mbps 无法维持原画质 60fps 不阻挡核心开发。原 P0–P7 清单保留，未勾选不等同于没有代码，也不等同于已验收。
- 下一开发批次为 M1.1：核对并接入 `rust_server/px_relay_server` 的 iroh 数据面、现有 Console 管理、配置和打包。现有临时 `px_relay_probe` 不作为正式服务交付。
- 此规划检查点当时未改生产代码或运行构建；其后的正式 Relay 开发见上方新检查点。90 最近真实实测及环境清理仍以下方 10:18 为准。

### 10:18 接续检查点（连续恢复冷却修复与真实 TCP 对照已完成）

- 三个先失败再通过的回归：恢复帧已成功提交后仍被服务端 500ms 冷却挡住；有效帧恢复后客户端遗留上一轮 1s IDR 冷却；iroh 描述覆盖明确 force_tcp。现按实际恢复进度清除冷却，保留无进展时重复请求合并。参考 Sunshine/Moonlight 本地只读源码，详细证据 `20261010_recovery_tcp.md`。
- 完整候选 `iroh-recovery-tcp-20261010` 已 Setup 安装到 90，3.3.97、314 项哈希一致、服务 Running；开发 dist 四对运行文件 SHA 一致。Direct 4 个 CTest、私有 TLS Relay 2 个 CTest 通过，Client 构建附带检查通过。
- 修正测试脚本：只发 WM 鼠标消息会被 SDL 实际光标位置影响，首轮退出弹框挡住输入，不能算拖动验收。该轮含 219ms/1003.6ms 的不利记录仍保留。有效对照改为真实光标定位并核对 sent=true 与两帧视角变化。
- 同一游戏：有效 UDP direct 全程 58.6–60.2 FPS、最大 140.1ms（启动源端空洞 134.7ms）、1 次 >100ms；预热后最大 83.2ms、0 次 >100ms。3 次 RFI 生成修复帧分别 1.9/7.1/1.7ms，其中连续两次仅隔 145ms，新逻辑已及时受理。
- 强制 TCP 实测确认为现有直连 WebSocket/TCP：全程 57.4–60.0 FPS、最大 174ms、5 次 >100ms；预热后最大 163ms、4 次 >100ms。切 TCP 并没有完全消除抖动。本轮串行对照不是可重复相同丢包轨迹；统计为解码前交付，不是显示呈现 FPS。
- 本轮正常拖动未再复现原 UDP 150–220ms 停顿，但非拖动首轮仍有一秒正向积压，不能宣称所有网络场景完成。若复现应沿发送/接收帧时间、OS/网卡包进一步定位；不要盲目缩短 IDR 常数。P0–P7 其他余项仍未完成。
- 清理完成：原 Console 配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 恢复，临时 Relay PID 50260 停止、QoS 0、桌面 Render PID 44988、TLS HTTPS 200。iroh 测试配置当前未启用；90 保留新完整安装。工作区 D:\112233。未提交/push。

### 09:42 接续检查点（反馈误判的两个采集门槛修复，直连丢包恢复仍未通过）

- 用户本轮聚焦正常直连短抖动，2Mbps 降级不作为当前阻塞。定位并修复两处缺陷：确认消息返回时间不再作为正向视频拥塞信号；去掉有字节/帧容量时仍因约 200ms 反馈年龄暂停采集的重复门槛。最大 64 个未确认帧、字节额度、硬发送队列和 250ms 稀疏探测保留。没有新协议字段或认证。两处均先复现失败回归，再修复通过。
- 最终候选 `iroh-feedback-age-20261010` 已通过完整 Setup 安装到 90，3.3.97、314 项哈希一致、服务 Running；本机开发 dist 四对运行文件 SHA 一致。35 个媒体用例及 frontend 目标在 direct/私有 TLS Relay 均通过。见 `20261010_forward_delay.md`、`20261010_feedback_age_delivery.json`。
- 首候选长测确认反馈方向延迟不再错误缩窗，但短测又发现剩余反馈年龄闸门导致 181ms 源端空洞，因此修正并交付第二候选。所有失败记录保留，未只取正常窗口作为通过证据。
- 最终不限速 direct 游戏：47.8–60.3 交付 FPS、最大 528.4ms、11 次 >100ms、1 IDR/45 RFI；26/33 正常窗口约 60 FPS、最大 44.2ms。在线发送仍约 60 FPS、busy/failed/flight_limited 为 0，没有此前反馈误判造成的 >=80ms 采集暂停或 >=100ms 编码空洞。**整体直连流畅度未通过**：另有 QUIC 丢包计数增加和参考链恢复停顿；不是 QoS 限速实验。见 `20261010_feedback_age_measurements.json`。
- 下一处具体排查对象：`IrohSession::AllowVideoRecovery` 的 RFI/IDR 共用 500ms 准入与接收端 1s IDR 重试。应先补充请求接受/拒绝/编码恢复帧的关联日志，再按 Sunshine 请求合并、Moonlight RFI/IDR 参考处理；当前没有足够证据把全部停顿归于该门槛，尚未修改它。第三方只读。
- **清理已完成：** 原 Console 配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 恢复，临时 Relay PID 54032 停止，QoS 0，仅桌面 Render PID 49308，TLS 校验 HTTPS 200。临时 iroh 配置未启用，下一轮需重新启用并记录身份。90 工作区仍 `D:\112233`。
- P0–P7 余项未完成，本轮未提交/push；D 盘约余 1GB。前两个候选已逐文件 SHA 验证后可逆移动到 C 盘专属开发归档，记录为 `20261010_recovery_candidate_archive.json`、`20261010_forward_candidate_archive.json`。

### 01:34 接续检查点（编码后额度拒绝修复，Relay 弱网长停顿本轮未复现）

- 按用户要求只处理两个画面问题，WebView 页面加载不作为根本传输问题。软在途额度改为只在编码前控制采集，完整编码帧跨额度仍发送并计入欠账，避免破坏预测参考链；本地硬队列和断链/超时约束保留。旧行为失败的实际 QUIC 回归已保存。
- 当完整帧确认延迟比最小值增长超过 100ms 时，按已确认字节吞吐收缩在途额度；一秒健康反馈后恢复普通容量。低码率静止场景不会直接缩窗。32 个媒体用例及 frontend 目标在 direct 和私有 TLS Relay 均通过。详见 `20261010_frame_admission.md`。
- 最终完整候选 `iroh-flight-recovery-20261010` 已通过 Setup 安装到 90，版本 3.3.97、314 项哈希一致、服务 Running；本机开发 dist 四对哈希再次核验一致。见 `20261010_flight_recovery_delivery.json`。
- 游戏 direct：52.9–60.2 交付 FPS，最大 220ms、2 次超过 100ms、0 IDR/2 RFI。游戏 Relay 含 2Mbps/20秒限速：36.4–61.1 FPS，最大 203.4ms、0 IDR/0 RFI；解除后的完整窗口 59.3–60.1 FPS、最大 66.6ms，自动恢复并持续观察约 100 秒。正常负载实际视频数据报约 4.4Mbps，确实高于限速。
- **仍存在短抖动：** direct 反馈 RTT 上升导致缩窗/暂停采集；限速突变初期仍有 639.740ms 相对传输积压。因此编码后额度拒绝已修复、秒级交付停顿本轮未复现，但不能宣称所有抖动和输入延迟消失。详见原始日志、截图和 `20261010_flight_recovery_measurements.json`；统计为参考链检查后交付，非显示呈现 FPS。
- **清理完成：** 原 Console 配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 已恢复，临时 Relay PID 46056 停止，QoS 0，仅桌面 Render PID 43664，TLS 校验的 HTTPS 200。临时 iroh 配置未启用；下一轮需重新启用并记录身份。90 开发文件只在 `D:\112233`。
- P5 多 Relay 管理、Android、旧栈退役等长任务余项未完成；本轮没有提交/push。D 盘约余 1.2GB，本轮旧候选已按完整哈希清单可逆移动到 C 盘专属开发归档，见归档记录。

### 00:33 接续检查点（小帧数量门槛已修正，Relay 仍未通过）

- 新增源端时间戳/完整组帧相对时间诊断和 QUIC 缓冲/cwnd 观测。真实 direct 证据：8 个小帧仅 26400 字节、QUIC 待发缓冲为空，却因独立帧数额度停采；失败回归已复现。现去掉 8–36 帧闸门，保留 64 条历史、256KiB 字节额度、停收年龄和稀疏探测。不是解除全部背压。
- 完整候选 `iroh-byte-credit-20261010` 已通过 Setup 安装到 90，版本 3.3.97、314 项哈希一致；开发 dist 四对运行文件哈希一致。聚焦直连 2 项、TLS Relay 2 项通过。详见 `20261010_pipeline_timing.md` 和 `20261010_byte_credit_delivery.json`。
- 游戏 direct 本轮 59.6–60.3 FPS、最大交付间隔 60ms、无 >100ms；WebView Relay 仍有最大 2841.6ms 交付停顿、5 IDR/22 RFI，不能算完成或无回归。编码帧参考链放行后的 FPS 与完整组帧计数是不同统计，详见报告口径修正。
- **下一处已证实缺陷：** `CanEncodeVideo` 用新增 0 字节准入，`SendVideo` 编码后用真实字节复查并丢弃预测帧。WebView 一个窗口 offered=301、flight_limited=82、completed=219；客户端仍在完整组帧，却因参考链断裂停交付。下一步统一编码准入/发送保留，避免编码后额度拒绝破坏参考链，按高运动量和弱网复测。另有源端输出 388–1008ms 空洞及底层相对传输等待，不能全归为组帧或 UDP。
- 最终有效 2Mbps/20秒 Relay 测试最大交付空洞 1328.9ms，解除后恢复约 60 FPS，仍有短抖动；弱网未通过。前两次 QoS 时机不完整已单独记录，未当作有效恢复验收。证据 `20261010_byte_credit_qos_policy.json`、`20261010_pipeline_measurements.json`。
- **清理已完成：** Console 原配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 恢复，临时 Relay PID 53380 停止，QoS=0，仅桌面 Render PID 54076，验证 TLS 的 HTTPS 返回 200。当前临时 iroh 配置未启用；下一轮须重新启用并记录新 PID。90 开发文件仍仅在 `D:\112233`。
- P5 多 Relay 产品管理、Android、旧栈退役仍未完成；本轮未提交/push。

### 22:37 接续检查点（反馈通道与进展判断修复已交付，Relay 短抖动仍未通过）

- 按用户要求核对 Sunshine 和 Moonlight 本地源码并记录 HEAD。先加日志复现，再按 Moonlight 可替代状态的非可靠发送原则，将累计完整帧反馈改为 40 字节 iroh 数据报，IDR/RFI 仍可靠；现有准入和 view 权限不变，没有新 token/ticket。单独更换反馈通道仍卡，不能把可靠流重传说成唯一原因。
- 失败回归确认 `VideoFlightWindow` 缺陷：新确认已释放容量，但旧帧年龄仍触发暂停。现已依据有效完整帧进展判断停收，重复/乱序报告不刷新进展。现场确认后 3–11ms 恢复，而旧逻辑还要等探测。聚焦直连 2 项、私有 TLS Relay 2 项通过，修改前失败证据保留。见 `20261009_feedback_trace.md`。
- 当前 90 完整安装候选 `iroh-feedback-progress-20261009`，版本 3.3.97，314 项哈希一致、服务 Running；开发 dist 的 4 对运行文件哈希一致。安装证据 `20261009_progress_delivery.json`，Setup SHA `BF579A74AFE285BD65DF5851C8F8A9FAD7799127185A2B7409A2F353D81A8838`。
- direct 游戏最大间隔本轮 153.8ms（同轮原行为 265.4ms）；WebView Relay 加载后仍 272.8ms，期间码率/画质明显回落；2Mbps/20秒最大 380.0ms、最低窗口 17.2 FPS，解除后约 51–58 FPS，未恢复稳定 60 FPS。没有秒级停顿，但**Relay 高画质和弱网流畅度仍不能标为通过**。单轮对比不是严格基准。见 `20261009_progress_measurements.json`。
- WebView 首次再次发生 WebGPU `Instance dropped in popErrorScope`，失败截图/日志保留；一次重启场景成功，输入响应截图保留。页面问题与传输分开跟进，不算 UDP 失败，不以静止场景帧率判定通过。
- 下一处明确排查对象：尚未确认的帧数上限仍满时，Relay 中同帧号的入/出队、QUIC 在途/排队及接收进展。已释放容量的年龄错误修复，不意味着积压本身消除。对照 Sunshine pacing 和 Moonlight 有界接收/恢复，不继续盲调码率下限。
- **清理完成**：原 Console 配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 恢复，临时 Relay PID 46984 已停止，QoS 0，仅桌面 Render PID 45116；Console Running，TLS 验证的 HTTPS 200。见 `20261009_progress_cleanup.json`。下轮须重新启用测试配置并记录新 PID，开发文件仍只放 `D:\112233`。
- P5 多 Relay 产品管理、Android、旧栈退役尚未完成；本轮未提交/push。D 盘仅余约 460MiB；上次打包空间失败后通过可逆 NTFS 压缩已有候选输入副本重试成功，未删除源码/日志/安装包。

### 20:00 接续检查点（码率恢复与分阶段诊断交付，高画质短抖动未完成）

- 普通短压力不再直接连续减半码率；持续拥塞逐级降低，停收仍快速响应，健康反馈逐步恢复。接收码率改为 500ms 累计估计，完整帧额度仍每 50ms 确认。没有新增 token/ticket。见 `20261009_quality_stability.md`。
- WebView 绘制、编码输出和 SDK 接收均增加带帧号的间隔日志；修正共享纹理 NT 打开成功时的逐帧误报警。直连 4 项、私有 Relay 2 项聚焦测试通过；开发 dist 的 4 对运行产物哈希一致。
- 当前 90 完整安装候选为 `iroh-quality-stability-20261009`，版本 3.3.97，314 项文件哈希一致、服务 Running。见 `20261009_quality_delivery.json` 和 `20261009_quality_development_hashes.json`。
- 真实 direct 游戏多数约 60 FPS，但范围 47.0–60.3、最大间隔 305.1ms。游戏 Relay 2Mbps/20秒最大 282.9ms，限速中约 30–43 FPS，解除后恢复约 60 FPS和更高码率。无秒级长停顿，但两者最大间隔高于前轮，**高画质流畅度不能标为通过**。
- WebView 首轮发生页面 WebGPU 初始化错误，失败证据已保留；重启后场景成功加载。约一秒启动间隔与源页面绘制一致；加载后仍有 226.7ms 间隔、约 40–60 FPS。在线目标码率能恢复到 14.95Mbps，仍有拥塞回落，下一步定位反馈及时性和编码准入造成的 100–300ms 短抖动。不得把页面 60 FPS 等同于实际接收始终 60 FPS。
- **清理完成**：原 Console 配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 恢复，临时 Relay PID 39472 停止，QoS 为 0，仅桌面 Render PID 54180。见 `20261009_quality_cleanup.json`；下轮需重新启用临时测试配置并记录新 PID。90 工作区仍是 `D:\112233`。
- 多 Relay 产品管理、Android、旧栈退役尚未完成；本轮未提交/push。

### 18:45 接续检查点（在途窗口、编码前准入已交付；游戏弱网长停顿本轮未复现）

- 完成音频零填充压缩还原（保留原 FEC）、实际编码/数据报码率日志、50ms 完整收帧反馈与远端在途额度；低码率仍拥塞时编码前降帧并逐步恢复。没有新增 token、ticket 或认证系统。实现与限制见 `20261009_media_flight_budget.md`。
- 首版编码后限流在正常直连触发 1580.4ms 参考恢复停顿，已补上编码前准入：缺额度时跳过采集，不继续编码依赖帧；Hook 共享纹理仍消费并释放互斥。第二版真实 QUIC/Relay 测试验证暂停、确认、恢复不打断参考链。
- 当前完整安装候选 `iroh-capture-credit-20261009`，90 的 314 项哈希一致、服务 Running；开发 Client/Cloud Node EXE/DLL 已同步 dist 并核对 SHA。直连/私有 Relay 聚焦回归通过。见 `20261009_capture_credit_delivery.json`、`20261009_capture_credit_development_hashes.json`。
- 最终游戏直连多数约 60 FPS，最大收帧间隔 150.5ms。2Mbps/20秒 Relay 全程最大 184.0ms（限速相关窗口 162.1ms），相较前轮 2140.1ms 明显缩短，后续恢复约 60 FPS，测量窗口无 RFI/IDR。启动阶段仍有 36.5–47.7 FPS 自适应波动；单轮实测不当作严格性能基准。
- WebView Relay 场景成功加载且镜头变化；启动间隔 1017.8ms，加载后多数约 60 FPS，仍有 266.0ms/136.1ms 抖动。下一项媒体工作：启动/RTT 突增时的码率与画质稳定性、WebView 加载后短抖动；不能宣称所有卡顿已解决。原始数据见 `20261009_capture_credit_measurements.json` 与同前缀证据。
- **清理完成**：Console 原配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 恢复，临时 Relay PID 53728 停止，QoS 为 0，仅桌面 Render PID 51580。下轮实测须重新启用测试配置并记录新 PID；90 开发文件仍只放 `D:\112233`。
- P5 多 Relay 产品管理、Android、旧栈退役仍未完成；本轮未提交/push。

### 17:50 接续检查点（完整收帧反馈已交付，弱网长停顿仍未通过）

- 已完成本轮两项：恢复请求合并与完整收帧进度反馈。发送失败不直接触发 IDR，部分帧丢失等待完整后续帧；静止缺帧保留稀疏兜底。客户端每 250ms 经现有控制流上报累计完整帧/字节；服务端依据真实接收进度降码率。没有新增短期 token 或第二套认证。
- 聚焦直连、私有 Relay、组帧与恢复回归通过。开发 Client/Cloud Node EXE 与 Render RTC DLL 已同步 dist 并核对 SHA。完整候选 `iroh-receive-feedback-20261009` 安装到 90，314 项校验一致、服务 Running。说明与证据见 `20261009_receive_feedback.md`、`20261009_feedback_delivery.json`。
- 正常游戏直连 59.8–60.2 FPS，最大完整收帧间隔 65.5ms；WebView Relay 已加载场景，多数约 60 FPS，启动 952.2ms、随后 363.3ms，仍有 120–184ms 短抖动。
- 2Mbps/20秒最大间隔由上一轮 5354.4ms 降到本轮 2140.1ms；解除限速后约 60 FPS，最大窗口 61.6 FPS。单轮对比不能当作严格基准，弱网仍失败。反馈已及时降目标码率，但 RTT 仍到 6.39 秒、实际接收编码码率部分时段高于目标；下一步处理在途数据/帧年龄与编码器实际输出，不能宣布全部解决。
- **清理完成**：Console 原配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8` 已恢复，临时 Relay PID 8580 停止，QoS 为 0，仅桌面 Render PID 43080。当前临时 iroh 测试配置未启用。下次实测需重新启用并记录新 PID。90 工作区仍为 `D:\112233`。
- P5 多 Relay 产品管理、Android、旧栈退役仍未完成；未提交/push。

### 17:04 接续检查点（本轮产物和清理已核对，长任务未完成）

- 当前代码使用 noq 1.3.0 默认 Cubic，保留有界视频队列、整帧发送期限、64KiB 数据报缓冲。开发 Client/Cloud Node EXE 已同步 dist 并核对哈希；90 安装的是完整 `iroh-cubic-frame-queue-20261009`，314 项一致，服务 Running，见 `20261009_cubic_delivery.json`。
- Cubic 首次 WebView 的页面 WebGPU 初始化报 `Instance dropped in popErrorScope` 和后续 TypeError，场景未运行。这是源页面失败轮次，不作为传输性能通过；证据 `20261009_cubic_webview_page_failure.json/.png/.log`。
- 重启 WebView 后场景已加载。接收多数窗口约 60 FPS，最后窗口最大间隔约 36–45ms；加载阶段 969.8ms、紧随其后 334.3ms，其余有 114–180ms 短抖动。证据 `20261009_cubic_webview_loaded.json/.png` 与 `20261009_cubic_webview_loaded_server.log`。截图出现本地退出确认，后续鼠标被 UI 捕获，因此不宣称完成全程快速拖动输入验证；动画画面接收证据有效。
- 游戏 2Mbps/20秒仍失败，最大收帧间隔 5354.4ms，限速中 RTT 升到秒级，解除时出现一个 103 FPS 窗口，表明积压数据集中到达，不能作为高帧率成功。解除后恢复约 60 FPS。见 `20261009_cubic_game_2mbps.json` 与 `20261009_cubic_game_server.log`。QoS 远端时间 `17:02:06.908`–`17:02:26.938`，已撤销。
- 正常 Relay WebView 相比 BBRv3 的长停顿轮次明显改善，但单次 A/B 不能证明唯一原因，弱网媒体恢复不标为通过。下一步优先处理恢复请求时机及 Relay 中积压帧的延迟约束，不继续盲调码率下限。只读参考 Moonlight 完整帧后请求恢复、Sunshine 批发送/pacing、RustDesk 帧消费反馈。
- **已清理测试环境**：Console 恢复原配置 SHA `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8`，服务 Running；临时 Relay PID 41504 已停止；Pixels-Iroh QoS 数量 0；仅剩桌面 Render 一个进程。见 `20261009_cubic_cleanup.json`。当前 Console 没有启用临时 iroh Relay 配置；下次实测需重新启用测试配置并记录新 Relay PID。
- 三个用户指定的本地参考仓库路径及 HEAD 已记录在 AGENTS.md 和计划中；RustDesk 以 `D:/GoCloud/rustdesk` 为准。P5 多 Relay 产品管理、Android、旧栈退役仍未完成，未提交/push。

### 16:53 接续检查点（历史）

Cubic 直连/Relay 聚焦回归均通过：`20261009_cubic_direct.xml`、`20261009_cubic_relay.xml`。Rust 探针见
`20261009_cubic_transport_probe.json`，开发产物/dist 一致见 `20261009_cubic_development_hashes.json`。
完整候选 `iroh-cubic-frame-queue-20261009` 已上传（operation `d8c0e4d5-f10a-40f9-8811-d046939c671f`），安装进行中
（operation `e2f46006-228b-463d-a60d-85d30407df8b`，task `45388f81-2ad2-4e71-8048-919afe9b33e4`）。
Setup SHA `6E42F1861479D611206BA7AB3624C80FF203902E79C0DD098A0A740AF50CDC18`，manifest SHA
`02685CB11AA68DDB9B9B4D73DEFDC97494B5F34813A3FB785D50E8B708AD27CB`。此处尚无 Cubic 实机性能结论。

- frame-queue 的 WebView 私有 Relay 仍失败：已加载船/海面 3D 页面，编码约 60 FPS，但接收出现 13–60 FPS 波动及 2769.2ms 最大间隔。见 `20261009_frame_queue_webview_relay.json/.png` 与 `20261009_frame_queue_webview_server.log`。页面内 60 计数不是客户端显示帧率。本轮没有施加限速。
- 低 RTT 下仍有 250/500ms 发送期限超时，开始隔离拥塞控制因素：撤销强制 BBRv3，使用固定 noq 1.3.0 自带默认 Cubic，保留 64KiB 数据报缓冲和 4 帧/4MiB 队列。尚不把 BBRv3 判定为唯一原因。
- 相同本机传输探针，BBRv3 的先前一次最大数据报 RTT 为 2273963us，Cubic 两次为 5677us、8981us；均校验 6291520 可靠字节和 120 个数据报。自动选择的本机虚拟网卡/MTU 不同，因此这是定位线索，不是严格性能基准。Cubic 完整实机候选正在构建，尚未安装。
- 90 仍安装 frame-queue 候选；Relay PID 41504 与 Console 测试配置启用。所有本轮客户端/应用测试已由自身 finally 关闭，QoS 已移除。后续先验证 Cubic 下 WebView 正常网络，再判断是否继续修改媒体恢复策略。

### 16:48 接续检查点（历史）

- frame-queue 完整候选已安装，314 项校验一致、服务 Running，见 `20261009_frame_queue_delivery.json`。2Mbps 限速 20 秒仍失败：最大收帧间隔 3930.4ms；解除后连续约 60 FPS、窗口最大间隔 37–76ms。见 `20261009_frame_queue_game_2mbps.json` 与 `20261009_frame_queue_server.log`。
- 本轮实际 QoS 时间为远端 `16:44:48.345`–`16:45:08.376`，策略已由 finally 移除；远端与本机时钟有偏差，不混用两端时间计算延迟。
- 自动选路实际为 direct，约 75 秒拖动中大多数窗口约 60 FPS，最大间隔通常 30–70ms；启动出现 152.9ms，一窗口 54.6 FPS/197.3ms，不声称全程无抖动。见 `20261009_frame_queue_game_direct.json` 与 `20261009_frame_queue_direct_server.log`。新队列已消除此前 direct 的数秒停顿复现，本轮无需再以2Mbps结果替代正常网络结论。
- WebView 私有 Relay 正在补测加载后的画面。接下来的媒体改进线索是参考 Moonlight：组帧完整性和恢复请求时机协调；当前实现对部分帧的丢失报告会立即触发 RFI，发送失败还会直接请求 IDR，这些行为在带宽下降时可能继续增加压力。尚未修改该部分，不把推断写为已验证原因。
- Relay PID 41504、Console 测试配置仍启用；本轮结束需恢复并核对原配置后停止该 Relay。P5、Android、P7 未完成。

### 16:43 接续检查点（历史）

- path-budget 完整安装通过，但降低 Relay 视频 FEC 仍未解决 2Mbps 卡顿，最大收帧间隔 11137.1ms。见 `20261009_path_budget_game_2mbps.json`、`20261009_path_budget_server.log`，不标为性能通过。
- 发送端原来仅容纳一帧，关键帧发送期间会直接丢掉后续参考帧；单分片 20ms 超时还早于整帧期限。现改为包含在途帧共 4 帧、4MiB 上限的小队列，保留逐帧期限和被拒帧序号；QUIC 负责包 pacing，数据报等待使用整帧剩余期限。Stop 排空队列且回调在锁外执行。
- 新增队列超限、期限、可靠流并行、丢帧参考恢复、回调内 Stop 与重复停止检查。直连和私有 Relay 两套回归通过，见 `20261009_frame_queue_direct.xml`、`20261009_frame_queue_relay.xml`。三个开发 EXE/dist 哈希一致，见 `20261009_frame_queue_development_hashes.json`。
- 新完整候选 `iroh-frame-queue-20261009` 正在上传到 90，尚未安装/实测。Setup SHA-256 `19F0722352F5D5B27276307E8672AA20A92D326C15CEDF71BF591F0EEF5B3ACF`；product-manifest SHA-256 `F3545D61D07E29B9CF951E8CE0E9ABF0180196030643E2D3C596EF3FD60FF117`。上传 operation `af047ca3-46a8-4d94-a79e-176d76e3e5b9`。
- 用户更新的 RustDesk 路径 `D:/GoCloud/rustdesk` 已记录到 AGENTS.md、开发计划和设计文档，三个参考仓库及 Moonlight media-core HEAD 已核对，均只读。Moonlight `VideoDepacketizer.c` 在完整后续帧到达后再请求恢复，避免拥塞中重复 IDR；这是后续对照线索，不是当前产品已验证行为。
- 90 Relay PID 41504 与测试 Console 配置仍启用；16:37 检查仅桌面 Render 运行，未残留 Pixels-Iroh QoS。结束仍需恢复原配置、停止该自有 Relay。

### 16:22 接续检查点（历史）

- `iroh-sequenced-backpressure-20261009` 完整安装成功，见 `20261009_sequenced_delivery.json`；三个开发 EXE/dist 哈希一致，见 `20261009_sequenced_development_hashes.json`。外层恢复绕过已经消除：限速期间 IDR 每 5 秒约 6–9 帧，实际编码约 0.4Mbps。但 2Mbps 收帧仍有 8115.3ms 长停顿，未通过。见 `20261009_sequenced_game_2mbps.json` 与 server.log。
- 小帧每帧仍至少附加一个 1100 字节视频冗余分片，60 FPS 本身就约 0.53Mbps，加上固定填充音频/封装形成与编码码率无关的下限。已按当前路径调整：可靠 TCP Relay 路径视频不再加 FEC，直连保持既有 20%；固定分片尺寸不变，路径每 500ms 采样。原 packetizer/receiver 已支持逐帧 FEC 数量变化，不新增协议或备用传输。
- 同时避免单个调度抖动导致码率减半：每个 500ms 采样区间至少 3 帧且 10% 被丢弃才认定持续背压。先前正常路径因单次 busy 反复降到最低码率的行为不保留。新候选 path-budget 正在编译，尚未验证/安装。
- Relay PID 41504 与测试 Console 配置仍启用，2Mbps QoS 已撤销；实机 RDP 断线恢复证据仍有效。P5 多 Relay 产品管理、Android 与旧栈退役尚未完成。

直连与自有回环 TLS Relay 的媒体/frontend 两套回归均通过，见 `20261009_path_budget_direct.xml`、`20261009_path_budget_relay.xml`。回环 Relay 由 finally 清理；这不是 90 性能通过证据。

### 16:12 接续检查点（历史）

- `iroh-backpressure-recovery-20261009` 完整安装通过，见 `20261009_backpressure_delivery.json`，但 2Mbps 场景仍失败。目标已降到 250kbps，实际编码仍约 3Mbps；证据 `20261009_backpressure_game_2mbps.json`、同名前缀 server.log。不能以目标码率下降冒充拥塞恢复。
- 找到外层恢复绕过：`EncodedVideoFanout::DrainVideo` 将 `PublishNativeEncodedVideo` 的 false 交给 `VideoBacklog::Complete`，后者直接请求 IDR。IrohTransport 之前把所有连接忙碌返回 false，虽已为这些帧分配传输序号，仍被误判为组包前丢失，导致传输层限频失效。现明确 SubmitVideo 的 true 是“已交给有序号的传输处理”，每连接忙碌/异步失败由自身恢复管理；真正校验失败仍 false。新增真实 QUIC 连续超速提交回归通过，见 `20261009_sequenced_backpressure.xml`。发送窗口新增 IDR 数量，待新候选实测核对。
- 之前自动选路确认 direct，但同样存在上述恢复风暴，见 `20261009_lowrate_game_direct.json` 和 server.log。新候选将分别复测 direct/Relay，尚不标为性能通过。
- 当前 90 安装的是 backpressure 候选；sequenced-backpressure 候选正在构建。Relay PID 41504、Console 测试配置仍启用，QoS 已撤销。

### 16:02 接续检查点（历史）

- `iroh-lowrate-recovery-20261009` 完整安装到 90，314 项一致，开发 Render/dist 一致，见 `20261009_lowrate_delivery.json`。仅降低编码下限未解决拥塞：限速时可降到约 3–4 FPS，RTT 已约 7ms 但每 5 秒有 200 多帧因背压被丢弃，编码仍约 3Mbps。完整证据见 `20261009_lowrate_game_2mbps.json`、`20261009_lowrate_game_server.log`。该测试 QoS 已撤销。
- 已根据证据修正 RTT 单指标判断：IrohSession 把发送失败/忙碌反馈给 VideoRateControl；即使 RTT 低仍降低编码目标。参考恢复请求在 NVENC 参考窗失效时可能退化为 IDR，因此 RFI 与显式 IDR/发送失败共用每显示器 500ms 限频。低 RTT 持续背压、混合 RFI/IDR 请求合并及稍后恢复回归通过，见 `20261009_backpressure_recovery.xml`。新包正在构建，尚未实机验证。
- WebView 完整已加载结果见 `20261009_recovery_webview_loaded.json/.png` 与 server.log：启动最大间隔 1344.5ms，后半段约 59–60 FPS，多数最大间隔 35–86ms。保留启动背压问题，不将后段平稳表述为全程无卡顿。
- 临时 Relay 仍为 PID 41504，Console 测试配置仍启用；结束需恢复配置并停止此自有进程。RDP 实机完全断线恢复的成功记录不变。

### 15:52 接续检查点（历史）

- `iroh-reconnect-recovery-final-20261009` 完整安装到 90，314 项哈希一致，服务 Running，见 `20261009_reconnect_recovery_delivery.json`。四套聚焦回归通过，见 `20261009_reconnect_recovery_direct.xml`；开发运行产物已同步并核对哈希。
- 新版 2D 游戏 2Mbps/20 秒限速最大收帧间隔 2541.5ms，解除后恢复约 60 FPS，最后窗口最大间隔 30.9ms。比上一版 28224ms 改善，但限速中仍明显卡顿，不标为性能通过。证据 `20261009_recovery_game_2mbps.json` 与 `20261009_recovery_game_server.log`。观察到 1Mbps 编码下限叠加恢复帧/FEC/填充后仍持续拥塞，已降至 250kbps；新增持续拥塞与更低用户上限回归通过，正在生成下一完整候选。
- RDP 真正断线后的自动恢复已在 90 验证：脚本停止自有 Relay 32 秒，客户端记录 `network_lost`，随后 `accepted reconnect=true`、新的 RDP 帧进度和桌面截图。原记事本 PID 16496、Session 2、启动 UTC 2026-10-02T01:21:24.8049191Z 均不变。见 `20261009_rdp_business_reconnect.json`、同名 PNG 与 operations JSON。首轮人工恢复太晚的失败记录保留，不混入成功报告。
- 当前测试 Relay PID **41504**，由定时恢复操作 `f7b2a2a2-dc65-4786-a281-17dfb7a7bbcc` 创建；此前 45636、8104 均已停止。Console 测试配置仍启用，结束时先恢复原配置再停止此准确路径/PID。2Mbps QoS 已由 finally 撤销。
- WebView 首次请求在建立 Console TCP 连接时超时，没有启动应用；重试已加载 3D 水面/船只场景并拖动，仍在收集完整结果。不能把页面未加载的轮次当作媒体通过。

### 15:26 接续检查点（历史）

- Pixels MCP 恢复，设备码 428358431。`iroh-bounded-bbr-20261009` 完整安装成功，314 项 SHA-256 一致，见 `20261009_bounded_delivery.json`。本机三个开发 EXE/dist 一致，见 `20261009_bounded_development_hashes.json`。该版本包含 BBRv3、64KiB 发送缓冲、5ms 单数据报等待和 NVENC 快速降码率，但**不包含随后正在编译的自动重接入**。
- 2Mbps 实测失败：`20261009_bounded_game_2mbps.json` 中最大停顿 28224ms。服务端精确日志 `20261009_bounded_keyframe_storm_server.log` 显示某些 5 秒窗口 301 次发送全部失败，每次失败触发关键帧；目标 1Mbps 时实际编码仍约 3.8Mbps。已有带宽反馈无法抵消关键帧风暴。已修改发送失败/接收恢复共用每显示器 500ms 关键帧限频；数据报等待 20ms、IDR 500ms 有界发送、最小 FEC 从每块 2 个改为 1 个。当前正在构建验证，尚未安装或宣称解决。
- 新增真正完全断线测试：`run_iroh_business_reconnect.py` 停掉自己拥有的回环私有 Relay，等待 QUIC 超时再恢复。初次失败证明 noq 的显式 Close 覆盖原网络故障原因；现于 C++ Connection 清理前保留网络超时/重置判定，主动关闭和拒绝仍不重试。修正后测试通过：断线约 30003ms，Relay 恢复后约 1028ms 完成业务重接入和 IDR 视频接收。见 `20261009_business_reconnect_reason_fixed.json/.xml`；原失败证据保留。
- IrohDialer 复用一个 Endpoint/原有业务授权，在网络故障后有界重试；旧适配器回调排空后切换代次。工作线程通过现有 RAII 延后回收，避免回调内退出与旧适配器停止交叉等待。SDK 音视频任务过滤旧代次；RDP 重置本地 bridge/protocol 并过滤旧回调，保留远端 Windows 工作区。RDP 新改动尚待客户端构建与 90 实测。
- 90 临时 Relay PID 45636，原运行操作 `f80f2b6d-6c6f-4fde-8c05-91b3893a2f59`，task `1dd14b2d-3f10-4e4d-bd20-f8cf31a9db4b`。Console 测试配置已再次启用，配置 SHA 为 `B3DBA7513F7F3D7A28BBF01564B4EECB6A2042D99B0CCC97271787E4B3C3C0DA`。结束前须恢复原配置再停止此 Relay。20 秒 QoS 操作 `81527cc5-13ba-4d4c-8fae-f392cd13de56` 成功且 finally 已撤销，需最终核对零策略。

### 15:00 接续检查点（历史）

- `iroh-adaptive-inplace-20261009` 和随后 `iroh-adaptive-cbr-20261009` 已分别完整安装到 90，现为后者，314 项哈希核对成功，详见对应 delivery JSON。开发 Render 构建/dist 一致。原位调整有效，但 WebView 高复杂度画面在目标约 2Mbps 时仍输出约 7Mbps，已查到原生 CBR 的 max QP 36 限制，改为 H.264 合法上限 51；不修改默认 WebRTC 配置。
- `20261009_inplace_webview_relay.json` 为加载成功的 WebView 测试；另一轮主页面 75 秒内未完成加载，见 `20261009_inplace_webview_no_load.json`。尚无 console 错误证明页面空白的根因，不能把页面未加载当作媒体卡顿。
- CBR 新候选的 2D 游戏实测见 `20261009_cbr_game_2mbps.json`：针对拥有的测试 Relay 施加 2Mbps/20 秒 QoS，编码目标降至 1Mbps，实际约 1.2Mbps，随后接收约 60 FPS。但中段仍有 9015ms 停顿；且限速前 RTT 已到 1234ms，不能把全部卡顿归因于限速。此项仍未通过。QoS 已删除且 ActiveStore 验证为零，测试应用清理后只剩常规桌面 Render。
- 正在验证后续改动：使用 pinned noq 1.3.0 的 BBRv3，QUIC 媒体待发送队列 64KiB，有界 5ms 等待而非静默淘汰旧分片；NVENC 降码率最短等待从 2 秒缩到 200ms，升码率仍保持节制。当前只完成编译检查，尚未交付/实测，不宣称修复卡顿。
- 构建期间已恢复 Console 原配置 SHA-256 `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8`，服务 Running，停止拥有的临时 Relay 41532。恢复操作 `f91e00b4-cfc3-4d65-950c-0235d3d91e2f` 成功，后续实测需重新显式启用。
- 完全断线后的业务重接入、多个 Relay、Android 和旧栈退役尚未完成，继续优先核心传输和卡顿修复；不增加传输 token 或第二套认证。

### 14:32 接续检查点（历史）

- `iroh-adaptive-spaced-20261009` 在 90 完整安装成功，314 项哈希一致，见 `20261009_adaptive_delivery.json`。加载成功的 WebView Relay 在启动热身后约 59.4–60.3 FPS、最大帧间隔 62.5ms；截图显示 3D 场景及客户端接收/解码 60 FPS，见 `20261009_adaptive_webview_loaded.png`。心脏游戏约 59.6–60.3 FPS，最大间隔 77.3ms。该轮 Console TCP 在清理时短暂超时，第三个应用未执行；保留 `20261009_adaptive_relay_loaded.json`，客户端已退出，90 已无测试 Render。
- WebView 另两轮为低变化或低源帧率。截图 `20261009_webview_page_no_scene.png` 明确显示页面控件正常但 3D 场景空白、页面自身 0 FPS；服务端约 2.2 FPS、组帧无丢失。这是页面初始化问题，尚未确定 JS/资源/GPU 根因，不能把这些轮次用作媒体性能通过证据。
- 2D 游戏实测中降码率/恢复日志生效，见 `20261009_adaptive_game_encoder.log`；接收后段约 60 FPS，但前段仍有 109–572ms 间隔，码率曾降至 1Mbps，故不标为完整性能通过。检查发现调码率重建 NVENC，现新增编码线程专用原位更新能力，复用已有串行 NVENC 重配置，保留参考链；不支持的编码器仍走原有重建路径。
- 新增 WebView 有界 console warning/error 日志，用于追查空白 3D 场景；来源仅记录 host/path，不记录来源 URL 查询串。每次主页面加载最多 20 条。
- 两次只针对拥有的 `D:\112233\...\px_relay_probe.exe` 的 8Mbps/20 秒 QoS 限速均已自动撤销，已确认 ActiveStore 无测试策略。因当时实际源码率低于限速，不能将其当作充分拥塞验收。
- 新候选 `iroh-adaptive-inplace-20261009` 已构建、校验，开发 Render 构建/dist 哈希一致；传输三套回归、架构与采集管线两套回归通过。正在上传完整 Setup，操作 `923b011b-b88c-42ce-ad27-2c60fba5d08a`。详见 `20261009_adaptive_inplace_delivery.json`；90 仍是上一候选。
- 临时 Relay 41532 与 Console 测试配置仍启用。完全断线业务重接入尚未实现，仍是下一核心项。

### 14:15 接续检查点（历史）

- 已补齐上轮 WebView 长停顿的精确服务端窗口：Render 仍约 60 FPS、发送无失败，Relay RTT 从约 13ms 上升到 864ms；客户端同窗口 RTT 约 721ms、最大帧间隔 1146ms。见 `20261009_webview_stall_server.json`。证据指向路径排队/等待，尚不能仅凭这些指标认定具体 TCP 丢包位置。
- 新增逐连接视频码率反馈：每 500ms 观察 RTT 相对路径基线的增长，拥塞时降码率；稳定 5 秒后逐档恢复。每条连接使用独立的原始上限，共享编码取最小预算。恢复不按每次采样微调，避免原生编码器频繁重建。
- 聚焦直连三套回归通过；更新后媒体 8 项通过。私有 Relay 媒体 8 项、frontend 11 项通过，另一个首次连接用例超过 5 秒期限、单独重跑通过，保留原失败和重跑报告，不宣称已消除该偶发现象。初次 Relay 脚本的 PowerShell 证书字符串序列化错误也已修正，不是产品代码错误。
- 新候选 `iroh-adaptive-spaced-20261009` 已完整构建、314 项验证。开发 Render 已同步 dist 并核对 SHA-256。90 上传完成，完整 Setup 正在安装，原操作 `3924a6be-ea4d-4489-a619-a859bddced80`，必须查询原操作，不重复安装。交付元信息 `20261009_adaptive_delivery.json`。
- 90 临时 Relay PID 41532（完整路径在 D:\112233），操作 `772584bf-e80b-46c2-87b1-3a5e3990d312`；Console 已显式启用测试配置。收尾必须恢复原配置哈希，然后停止此测试 Relay。当前尚未做新候选快速拖动实测。
- 完全断线的业务重接入尚未实现；已检查现有一次性 IrohDialer、旧连接释放和 RDP 客户端首次 Open 假设。后续需复用现有资源会话授权，按连接代次屏蔽旧回调，并重建 RDP 前端协议连接，保留 Windows 工作区。

### 13:49 接续检查点（历史）

- `iroh-budget-ready-20261009` 已完整安装成功：Node 3.3.97、314 项哈希一致，详见 `20261009_budget_ready_delivery.json`。
- WebView 直连热身后约 58.5–60 FPS，最后七个 5 秒窗口均无 >100ms 间隔；强制 Relay 重测多数窗口约 59–60 FPS，但末尾仍出现 1146ms 间隔与 721ms RTT，性能验收尚未完成。两款游戏 Relay 全窗口约 59.9–60.1 FPS、最大间隔 <50ms。保存 `20261009_budget_webview_auto.json`、`20261009_budget_native_relay.json` 和服务端对应采样。
- 另一次 WebView Relay 实测源端仅约 2.2 FPS、低码率且组帧零丢失，单独保留 `20261009_budget_webview_relay_low_source.json`；未认定为传输丢包或已查明页面原因。第二轮截图见加载后的 3D 场景，接收约 59 FPS、解码约 57 FPS，不将页面自身 FPS 当显示 FPS。
- 新端点就绪重复检查三次分别 1.720/1.370/0.753 秒，全部连接成功；这轮长测试也均在约 0.6–1.2 秒就绪。测试实例、会话与访客均已清理。
- 临时 Console 配置已恢复到原 SHA-256 `0EECD9304D4E32478576500261FB385CC680968B8EE7D5FBD13BB3F9C02CCBA8`，Console/Service 均 Running，原测试 Relay PID 28420 经完整路径核对后停止，仅余常规桌面 Render。当前运行配置不再依赖临时 Relay；后续测试需重新显式启用测试环境。
- 已补入原来遗漏的 iroh 双向语音数据报与 Render 回复路由，复用现有通话确认与 Opus 队列。真实 QUIC 回归通过（媒体 6、SDK 4、frontend 12）；私有 HTTPS Relay 的双向语音用例通过，包含 MTU 拒绝、音频权限撤销和主动退出后拒绝发送。证据 `20261009_voice_direct.xml`、`20261009_voice_relay.xml`；这是传输回归，尚非真人麦克风通话验收。
- 语音版本 Client 两个产品及 Render 已 Release 构建并发布到各自开发 dist，三个 EXE 构建/dist SHA-256 一致，记录 `20261009_voice_development_delivery.json`。90 仍为上面的预算/就绪完整候选；没有手工替换其安装目录 EXE。额外本机语音测试 Relay 已按 PID/完整路径停止。
- 新增可复现 Relay 恢复测试 `scripts/tests/run_iroh_relay_recovery.py`：仅重启该脚本自己的回环 HTTPS Relay，停机 1011ms，同一 QUIC 连接上的可靠流/数据报均自行恢复，可靠流最大间隔 1089ms、数据报 1042ms，1198/1198 数据报返回。证据 `20261009_relay_recovery.json`。这证明短暂路径中断可恢复，不代表完全断线后的业务重接入或跨物理网卡迁移已经实现；测试进程均由 finally 清理。

### 13:33 接续检查点（历史）

- 定时精度与实际路径统计版本已安装，314 项哈希匹配，见 `20261009_timer_path_delivery.json`。23 个本机真实 QUIC 用例通过；跨机器私有 Relay 媒体 5 个、frontend 11 个通过。原停止用例的 2 秒假设不符合 iroh 1.3 正常关闭最多约 3 秒的排空行为，已修正为 5 秒有界上限，原失败报告保留。
- WebView 已由两端路径统计确认选择 direct；热身后有发送窗口 300 帧、busy=0、failed=0，但 QUIC 发送端仍有丢包，不能将定时修复视为完整根因。证据 `20261009_timer_webview_auto.json`。
- 进一步修复两处接入遗漏：`EffectiveVideoBitrate` 未为 iroh 预留 FEC/音频/协议开销；Service 的端点就绪此前只随 15 秒遥测发送，超过 Panel 12 秒等待，实测出现 `transport_not_ready`。现复用媒体预算，并在 1 秒命令轮询中检测端点快照变化，触发已有报告及时发送。
- 新候选 `iroh-budget-ready-20261009` 已完成构建与 314 项验证。Service 聚焦回归通过、frontend 11 项通过，本地 Render/Service 构建与开发 dist 哈希匹配。完整 Setup 已上传，安装操作 `23c9e556-628f-40ae-8a43-48fe93341026` 正在执行，原 task `c51d4ae9-969e-41a2-a516-18913589db1e`；必须查询原任务，不重复安装。包哈希和交付信息见 `20261009_budget_ready_delivery.json`。
- Pixels MCP 曾中断、随后命令恢复而上传提示缺少桌面端保存凭据；SSH 此时也认证失败（大小写已按 Administrator）。用户已在 Pixels 桌面连接，上传恢复。未修改机器密码或防火墙。短暂本地安装包 HTTP 服务试验不可达，已停止。
- RDP 自动路径及强制 Relay 两轮真实连接完成，看到旧桌面/记事本画面并有 RDP 帧发布。断开后旧记事本仍为 PID 16496、Session 2、启动时间 2026-10-02T01:21:24.8049191Z。证据 `20261009_rdp_live.json`；这是功能与工作区保留验证，不是 RDP 帧率验收。
- 下一步：确认此次完整安装结果，重复启动检查地址就绪延迟，复测 WebView 自动路径/强制 Relay 并采集服务端发送日志。测试 Relay PID 28420 仍在 90 的 D:\112233\iroh-native-validation-20261009；配置仍启用它，结束时须先恢复 Console 原配置，再停止本次 Relay。原 MCP Relay 操作历史在连接工具恢复后不可查询，但已按 PID 与完整路径核对它仍在运行。

- Server 1.0.54 和 Cloud Node 3.3.97 新候选均通过完整 Setup 安装到 90；Server 35 项、Node 314 项清单验证成功，服务 Running。Pixels MCP 在 Server 安装成功后出现 Transport closed；经 `.env` 的 SSH 检查没有待执行 Node 安装且旧清单仍在，随后通过 SSH 完成 Node 安装。
- 显式启用私有测试 Relay 4618 配置，原始 Console 环境备份位于 D:\112233\iroh-native-validation-20261009。不得在停止测试 Relay 后保留对此测试服务的配置依赖。
- WebView 与两个 GameHook 应用均通过实际 Console 会话和 iroh 准入，自动路径与强制 Relay 各完成一轮输入/视频测试，测试会话均清理。WebView 截图确认加载完成并显示 3D 画面。接收统计是编码帧接收速率，不是屏幕显示帧率。
- 自动路径首轮：2dadventure 约 60 FPS；心脏演示多数窗口约 59–60 FPS，但存在 565ms 长间隔；WebView 热身后约 51–58 FPS，启动最大间隔 923ms。自动策略尚不能证明已选择直连。
- 强制 Relay 首轮：两个游戏多数窗口约 60 FPS；WebView 约 23–67 FPS，存在 1142ms 间隔与集中补到帧。没有将连接成功等同于性能验收通过。原始统计见 `20261009_native_auto_baseline.json`、`20261009_native_relay_baseline.json`。
- 排查发现旧 UDP 模块独占 `timeBeginPeriod(1)`，显式 iroh 禁用旧模块后缺少自身的 Windows pacing 定时精度申请。正在补入 RAII 定时精度管理、实际路径/QUIC 统计和发送端 busy/失败/耗时统计，完成构建后复测；尚不能认定它解释了全部 WebView Relay 抖动。
- RDP 实机、故障恢复、多 Relay 管理、Android 与旧栈归档仍待完成。下方为此前检查点，安装状态以本节为准。

## 最新接续：2026-10-09，Console 网络配置与启动就绪

- Console 配置 `PIXELS_CONSOLE_IROH_CONFIGURATION` 接收与端点相同的公开 JSON：`{"relays":[{"url":"https://relay.example.test:4605","qad_port":4605}],"ca_certificates_pem":[]}`。仅显式配置启用；`{}` 表示私网直连。固定 bind 地址仍由 Render 根据实际端口选择。
- 配置经已有节点认证响应下发；Service 通过子进程环境传给桌面、游戏、WebView、RDP Render，不写入 Panel 的启动偏好或拼接为命令行。启用时清除旧 Relay 启动凭据；断开 Console 后保留网络配置，避免重连时无故重启桌面。
- 配置的节点尚未报告 iroh 地址时，Console 返回 `transport_not_ready`，不签发描述或推进会话版本；Panel 在同一预留会话上进行有期限的等待，避免误走已关闭的旧 UDP 通道。
- Node protocol 6 项、Console 配置 1 项、Service iroh 2 项及节点认证配置接收 1 项通过。PostgreSQL 会话 20 项通过，证据 `test-results/server_validation/pg-20261009-123016-926d8c94/report.json`。
- Release 开发构建、Panel 回归与真实 HTTPS 11 场景通过；6 个 EXE 构建/dist 哈希一致，见 `20261009_configuration_delivery.json`。Client 完整开发 dist 40 项检查通过；Cloud Node 发布器核验 277 项，但开发目录缺少既有 RDP 主机 DLL，不将它当完整可安装包。90 当前 Server 1.0.53、Cloud Node 3.3.97 未改变；准备 Server 1.0.54 与 Cloud Node 完整候选 Setup 后再验证，暂存路径 D:\112233。当前正式 Relay 数据面尚未替换，本阶段实测使用独立私有 iroh Relay。
- Server 1.0.54 候选 Setup 已构建并上传到 D:\112233\iroh-native-validation-20261009，35 个清单文件与上传 SHA-256 验证通过。iroh publication 库构建完成；Cloud Node 完整候选正在 official Release 树构建，未安装。包暂存信息见 `20261009_candidate_staging.json`。
- 新增每流 5 秒接收统计（编码帧接收 FPS、最大间隔、>100ms 间隔数、IDR/RFI 请求），不将其称为显示 FPS；SDK 4 项真实 QUIC 回归通过。
- 配置切换前完整保留有关分支于 `backup/iroh_console_configuration_20261009/`。没有新增传输 token、ticket 或第二套认证。

## 上一检查点：2026-10-09，Windows 启动入口与地址上报链

P0–P7 仍在执行，尚未切换生产默认传输，90 未安装本检查点的新构建。

- 文件流满队列会向 SDK/Render 返回 Busy，并在可写或关闭时通知调用者；重试不会静默丢块。控制流不受文件排队阻塞，真实 QUIC 背压测试通过。
- Render registry 已组合 IrohTransport，路由编码视频、音频、控制、文件和 RDP；权限更新作用到现有连接。配置 iroh 时不同时创建旧 Native UDP/Relay socket；WS 保留 IPC/Direct Host WebRTC 职责。
- NetClient 已接入异步拨号、取消、现有准入、媒体交付和关闭；不在 QUIC 握手后提前报告业务连接成功。真实 QUIC 测试覆盖 NetClient、Render module、准入拒绝、回调内关闭及文件流。
- Console 客户端 DTO → 应用/设备连接 → Panel 启动信封 → Windows Client/RDP 的 iroh 参数链已接通。Panel 对受 Console 授权且带 iroh 描述的目标跳过直连 HTTP 前置探测，由实际 QUIC 连接完成准入；启动对话框仍等待 Client 回执。强制 Relay 不要求旧 Relay 票据。
- 统计页增加 iroh/QUIC 和 RDP/iroh 标识，中文/英文目录一致；没有把未知实际路径标成直连或 Relay。
- Render 心跳上报实际 EndpointAddr 与去掉本地 bind_address 的端点配置。Service 只报告最近 5 秒且 IPC 仍在线的端点，应用地址关联本次 instance_id/launch_id；断连、过期或无效替换会清除地址。旧 IPC 断开不再误删新连接登记。
- 新增 PostgreSQL 0038 迁移保存当前节点代际/控制代际的完整地址快照。Console 下发前匹配实例和当前启动批次；Web Client 保留 Direct Host WebRTC，Android 待接入。iroh 描述存在时不签发旧 Relay 票据，未引入新 token、ticket 或认证系统。
- 直连 CTest 已通过：媒体 5、SDK 4、frontend 11、Client 启动参数 22、统计/中英文 5、Panel 75。强制私有 HTTPS Relay 的媒体 5 + frontend 11 也通过。证据：`20261009_wiring_direct.xml`、`20261009_composition_relay.xml`。
- 并行编译时曾出现大帧交付与关闭耗时失败，保留 `20261009_composition_relay_concurrent_build_failure.xml`；独立复测通过，尚不认定根因已解释，媒体等待断言已补充交付/失败计数。
- Node protocol 5 个测试通过；Service 心跳派发及 iroh 地址生命周期测试通过；Console runtime Release check 通过。数据库会话 20 项回归通过（pg-20261009-120136-d8410064）；6 个 EXE 构建/dist 哈希一致，发布器同时核验资源，详见 `20261009_wiring_delivery.json`。最终 Service 构建发现拆分模块未登记，已补入 main.rs 并重新构建发布。

### 接着完成

1. 数据库实例地址回归和开发 dist 交付已完成；当前代码入口仍需要显式 Render iroh 配置。
2. 接通 Service/Console 的私有 Relay 配置和运行时启动配置，去除仍在旧链路中的额外直连假设；将完整候选 Setup 暂存于 90 的 D:\112233 后安装，验证真实游戏、WebView、桌面和 RDP。
3. 实际路径/恢复状态与故障日志、语音、全功能文件/剪贴板、网络切换/重连、多 Relay 管理、Android 和旧栈归档仍未完成。合成 QUIC 用例不能代替真实 60 FPS 与故障恢复验收。

## 历史状态：2026-10-09，iroh 媒体与 Relay 业务链路

本节覆盖下方历史检查点。P0–P7 整体仍未完成，生产默认连接入口仍未迁移。

- 新增共享 `MediaDatagramSender/Receiver`，复用现有视频 FEC、组帧、参考恢复和 Opus 缓冲；Direct/Relay 固定使用 1100 字节分片。一次只接受一帧发送工作，250ms 期限，异步短批次发送；音频不排在视频批次之后。
- 拒绝排队的视频帧仍消耗传输帧序号，使接收端能发现参考链缺口。真实 QUIC 测试证明发送端背压丢帧会请求恢复，不会将缺口误判成连续帧。
- Render `IrohSession` 与 SDK `IrohConnection` 已接入媒体适配层。参考失效/关键帧请求用现有控制可靠流上的类型化 protobuf 消息送回编码事件；没有新增授权凭据。端到端测试覆盖音视频交付、丢帧、RFI 请求返回 Render、kind 5 修复帧保留前驱索引。
- SDK 的部分初始化失败、重复停止与回调停止已检查；失败会关闭已准入的 QUIC 连接。
- 修复实际发现的 Relay 接入缺陷：Rust 探针等待 Relay 上线，但 FFI 原先立即返回端点，可能公布缺少 Relay 地址的 EndpointAddr。FFI 现在在调用者给出的超时内等待已配置私有 Relay 上线。修复前强制 Relay 测试全部连接失败；修复后媒体 5 个、frontend 8 个用例全部通过，包括 RDP 往返。
- 直连验证：iroh_transport 3 个、iroh_media_datagrams 5 个、iroh_frontend 8 个、udp_paced_batch 3 个、iroh_sdk_connection 3 个用例通过。强制本机私有 HTTPS Relay 另通过 13 个用例。它们是实际 QUIC/Relay 业务收发测试，不代表实际游戏 60 FPS 或跨机器媒体验收完成。
- 原 UDP pacing 文件移到 `src/px_transport/paced_media_batch.h` 复用，原 UDP 行为未改；旧完整内容归档 `backup/shared_media_pacing_20261009/`。
- Client / Render focused Release 已发布开发 dist，Client 40 个、Cloud Node 277 个发布产物校验通过；构建树与 dist 的 EXE SHA-256 一致。详见 `20261009_media_delivery.json`。本轮没有新安装 90；90 仍保留 `20261009_delivery.json` 中的完整 Setup 安装。
- 临时本机 Relay 已停止。无新增短期 token、票据或第二套认证。

### 下一步执行顺序

1. 在 Render registry 组合 IrohServer/IrohSession，接入实际媒体、控制、文件和 RDP 路由。
2. 补齐可靠队列的调用者背压结果：当前 MessageSession 有 4MiB/流限制，但 SDK 旧文件发送入口只按消息数量判忙、调用 void PostBinaryMessage；生产接入前要让满队列返回 Busy 与可写通知，不能静默丢掉文件块。
3. Service/Console 上报和下发实际 EndpointAddr、私有 Relay 集合与端口；NetClient 接入拨号、取消和现有业务准入流程。目前这些入口尚未切换。
4. 完成真实游戏/WebView/RDP 流程、网络切换/重连、多 Relay、Android，最后归档退出旧 Native/Relay 栈。

## 历史状态：2026-10-09，业务通道与私有 QAD 实测

本节覆盖下方历史检查点中的“待构建、待安装、未配置 QAD”等状态。P0–P7 整体仍未完成。

- UDP 异步 pacing 修复已通过完整候选 Setup 安装到 90，版本 3.3.97，314 个文件哈希匹配，服务 Running。安装与旧一版开发产物证据：`20261009_delivery.json`。
- 已完成实际 WebView / GameHook 测试，测试实例和 Client 均已清理。游戏服务端日志证明鼠标动作到达；WebView 加载画面已截图检查。后段客户端游戏 58.6–60.1 FPS、WebView 57.7–59.6 FPS，后段均无超过 100ms 的接收帧间隔；启动阶段仍有长间隔，WebView 最长约 1 秒。不能宣称所有场景恒定 60 FPS 或问题已全部消失。样本见 `20261009_live_media.json`，服务端摘录见 `20261009_live_render_excerpt.log`。
- `MessageSession` 为 Client/Render 共享的独立可靠流收发实现；每流有界发送，接收业务回调串行。SDK 使用薄适配器。旧实现完整归档到 `backup/iroh_shared_channel_session_20261009/`。
- `IrohSession` 已完成业务消息、绑定来源、流量统计与 RDP loopback 桥接。连接事件使用 iroh source ID；客户端不能靠正文更换应用 stream；RDP 只处理 RDP 包和心跳，第二个前端（包括相同逻辑身份）被拒绝。复用原授权和 RdpTcpBridge，不创建新传输凭据，不注销 Windows 会话。
- 真实 QUIC CTest：iroh_transport 3 个、iroh_sdk_connection 2 个、iroh_frontend 7 个用例通过。包括独立输入/文件流、回调内停止、错误通道拒绝、RDP 二进制往返与单次释放。最新 Client/Render 开发 dist 及构建树哈希见 `20261009_business_delivery.json`；Client 40 个、Cloud Node 277 个产物发布校验通过。
- 私有 Relay 现支持 HTTPS 与 QAD；端点配置改为 `relays: [{url, qad_port}]`，不再隐式探测默认 QAD 端口。`ca_certificates_pem` 可接收运营者正常 TLS CA，测试只使用独立临时证书，未修改系统或产品信任库。旧原型配置归档到 `backup/iroh_private_relay_qad_20261009/`。
- 跨机器 QAD 已发现 90 公网 UDP 地址，测试最终选择 `ip:39.71.45.66:60485`，可靠流 6291520 字节校验通过；数据报 116/120，最大 RTT 163652us。强制私有 HTTPS Relay 可靠流也通过，数据报 113/120，最大 RTT 418559us。数据报与批量文件并发；这些结果证明路径可用，不代表媒体性能验收通过。证据 `20261009_private_relay_qad.json`。
- 临时本机 Relay 已停止；90 Relay MCP 操作已确认 cancelled，两次 echo 探针均 succeeded。开发文件仍全部在 D:\112233。

### 接下来直接执行

1. 接通媒体 datagram 的 MTU/FEC/pacing 与现有组帧/恢复逻辑，避免文件压力下的视频积压；复用现有采集/编码，不另写媒体栈。
2. 在 Render registry 组合 IrohServer/IrohSession，并接入媒体、控制、文件、RDP 路由；目前仍未由生产 registry 创建。
3. Service/Console 下发实际 EndpointAddr、私有 Relay 候选和端口，NetClient 选择 iroh；目前默认入口仍是旧协议。
4. 然后完成真实用户流程、故障恢复、多 Relay、Android 及旧栈归档。当前不得宣称 Native 已整体迁移；不增加短期 token 验收项。

## 历史检查点（以下记录保留执行过程，以最新状态为准）

## 当前检查点

- P0 进行中：已记录 `baseline.json`，保留原有启动回执修改，持久保存 `initial_media_diagnosis.json`。
- P1.1 已实现：组帧→SDK→解码队列保留结构化参考信息；解码输入拒收与已接收但暂未输出分开处理。
- P1.2 已实现每流 RFI 合并、100ms 有界重试、500ms 未恢复升级 IDR，成功交付取消恢复；当前固定时间策略不宣称 RTT 自适应。发送端 pacing 和实机拖动对照待完成。
- P2 已开始：新增独立 rust_transport 工作区，锁定 iroh 1.3.0；Minimal 私有端点、可靠流及数据报原型已构建运行。
- P3–P7 未完成。按最新用户要求先完成功能，不引入短期 token、额外票据或重复认证。
- 用户更新设备码为 `428358431`；Pixels MCP 已成功连接 WIN-RASS8RC6V3H，Windows x86_64、SYSTEM、原生执行上下文。工作区 D:\112233；未修改运行中产品。

## 已完成准备

当前待替换分支及受影响解码器的 15 个文件完整归档到 `backup/media_recovery_delivery_20261008/`，包含原始状态与 SHA-256。
未改变服务器协议，优先采用 SDK 内部拥有生命周期的帧交付类型，将本地组帧参考信息送到解码端。
Client 已完成 focused Release 构建并同步开发 dist；没有安装远端产品或修改网络配置。

## 当前验证证据

- CTest：sdk_source_sets、udp_media_failure、sdk_stream_helper、encoded_video_delivery、upstream_media_reference 全部通过；添加恢复策略后重跑 encoded_video_delivery / udp_media_failure 通过。
- build_cpp_client.bat client 8 成功并发布开发产物。px_client.exe 构建与 dist SHA-256 同为 `1868A3CF2A1191F3C208CA5546E4F39A2ACE69FD256538D2D99A5A0EF57F7EA4`。日志 `.cache/media-recovery-client-build.log`。
- iroh 原型本机真实 QUIC 连接：可靠流校验 6291520 字节、数据报发送/返回 120/120、最大数据报容量 1162、总耗时 2104ms。这里只验证基本功能，不作为 60FPS 媒体性能结论。
- SSH 预检证据：`.cache/iroh-remote-preflight.json`。
- iroh 私有 Relay 本机功能：可靠流 6291520 字节校验通过，数据报 120/120，路径确认为 relay。
- 跨机器：本机连接 90 上 D:\112233\px_relay_probe.exe 的临时 4618 端口，通过可靠流 6291520 字节校验。修正迟到回复统计后，数据报 94/120，最大往返 656767us（同时传输文件）；仅证明连通，媒体性能尚不合格。
- 90 上报 10.0.0.90 内网地址，当前无 Relay 的跨机直连超时；不得称为 NAT 打洞通过。
- Rust 静态库与 C++ RAII 包装已实现并成功链接；CTest iroh_transport 通过真实连接、可靠流、半关闭、数据报、关闭中断接收和三次重复启停。
- C++ 业务通道已实现：控制、输入、剪贴板、文件、RDP 分别使用独立有优先级的可靠流；长度分帧上限 1 MiB；部分头/部分正文超时后保留读状态。真实流测试通过。
- SDK `connection/iroh_connection.*` 已接入核心构建：接管已准入连接，可靠消息收发、有界发送队列、数据报收发、弱引用回调及取消。`iroh_sdk_connection` 真实连接测试通过，包括从接收回调 Stop；NetClient 默认选择分支尚未切换。
- 新的 `network/frontend_admission.*` 提取 Render 原有身份/会话准入供 iroh 复用；没有改变授权协议。net_ws 构建成功，ws_frontend_admission 与 rdp_route_close 均通过。原 ws_server.* 全文已归档到 backup/iroh_frontend_admission_20261008。
- SDK 接入后的 Client 再次 focused Release 发布，当前 px_client.exe 构建/dist SHA-256 均为 `C2571FB44B52E6CB104C065ECADE0B89B42E5F5EA566043AC8C1A5D0586226DC`。Render 开发构建/发布成功，构建 `cmake/src/px_render/px_render.exe` 与 dist SHA-256 均为 `D42C85FD032A07071FFC4F3E9FFC73BC03FA2CEEFE18057A79D43760BCD5DD63`。日志 `.cache/iroh-render-delivery.log`。
- 构建入口 scripts_build/build_transport.ps1 成功。静态库链接已通过；当前端点/流接口为专用 C++ 工作线程上的同步 ABI，没有跨语言回调，也没有新增认证票据。

## 早期原型下一步（历史，已由上方接续检查点取代）

1. 在已验证的 Rust/C++ 传输接口和业务通道上推进 Client/Render 实际接入。90:4618 临时 Relay 已通过原 MCP 任务取消，终态 confirmed cancelled；本机测试 Relay 也已退出。
2. 使用已发布 Client 做游戏/WebView 实机拖动回归，按证据处理剩余发送调度问题。
3. 接入 Client/Render/Console 的实际业务连接；复用现有授权，不增加短期 transport token。

P1 本地修复已交付开发 dist，实机拖动回归仍待完成；iroh 尚未接入产品。不得声称整个迁移已完成。

## 早期原型接续入口（历史）

- `rust_transport/px_transport`：仅私有基础设施的端点构建；`px_transport_ffi`：端点/连接/可靠流/数据报同步 ABI，Rust 持有 Runtime；`px_transport_probe`：独立连通探针和临时 Relay。
- `src/px_transport/transport.*` / `transport_handles.h`：立即 RAII 接管 Rust 句柄，所有跨线程调用需持有共享 owner。`channel.*`：PXQ 版本 1 通道头，独立流类型、消息分帧、超时续读。
- focused C++：`scripts_build/build_cpp_tests.bat client test_iroh_transport`；CTest `iroh_transport` 已通过（3 个行为测试）。Rust：`scripts_build/build_transport.ps1`。不要在测试 Relay 正占用构建树 EXE 时重链接该 EXE。
- SDK 接入口为 `sdk_connection_params.h` / `sdk_net_client.cpp`，当前仍是原有 UDP+WS。Render 公共准入已移动到 `network/frontend_admission.*`；其服务参数仍是 WsTransport 的业务依赖，后续可由新 Iroh 模块组合复用。逻辑会话最终绑定、配置发送和 RDP 桥仍在 `network/ws/ws_server.cpp` / `ws_stream_router.*`，不能把 QUIC 握手直接当业务准入。
- `px_common::Thread` 已有同线程退出转交 `PxAsyncRuntime::DeferJoin`，适合在必要的阻塞传输工作线程上复用；不要自己引入裸 this 捕获或同线程 join。
- 当前用户桌面仍在操作 Pixels Agent Bridge，避免连续坐标点击干扰用户；实机媒体验证可优先经 API 启动本次拥有的测试会话。
- 没有提交或 push 本轮修改。保留原有 Panel 启动回执改动。
- 下一项具体实现：Render 的 Iroh 模块与 registry 路由接入，复用 frontend_admission；Console/Node 上报 EndpointAddr、Client 获取目标地址后走新 SDK 分支。当前这些生产连接入口尚未改变，不得把基础库测试当作游戏/WebView 已迁移。
- 原型 Relay 目前只有 HTTP/WebSocket 转发，尚未开启带 TLS 的 QAD UDP 地址发现服务；跨 NAT direct 切换仍需在私有 Relay 配置中实现明确的 QAD 端口。不要额外添加短期 Relay token。

## 2026-10-09 接续实现（构建/远端验证进行中）

- 新增 `network/iroh/iroh_server.*` 与 `iroh_frontend.*`：真实 QUIC 接入、现有 frontend/password 授权、逻辑会话占用、现有业务续租和单次断线释放。没有新增传输凭据。
- 控制流交换现有参数和结构化结果；返回授权的通道列表，输入、剪贴板、文件、RDP 使用独立可靠流。RDP 仅协商 control + rdp，不接入宿主桌面通道。
- SDK IrohConnection 按消息类型路由至独立流，分别维护有界发送队列；接收回调仍串行，避免改变 SDK 回调线程约定。停止关闭整个 QUIC 会话。
- 修正监听器的超时：QUIC 握手允许 10 秒，断线检测另用 250ms 定时循环；不能用短轮询间隔取消高延迟 Relay 握手。
- CTest `iroh_frontend`、`iroh_sdk_connection` 验证了真实授权接入、控制/输入/文件/数据报、占用拒绝和回调内停止。`iroh_transport`、`rdp_route_close`、`application_idle_lifecycle` 通过。`encoded_video_delivery` 在 Client 构建树通过；Cloud Node 构建树没有生成该测试 EXE，未将其计为通过。
- P1 新增实际修复：原 UDP 在 socket executor 中逐帧 yield 忙等 pacing，整个等待会阻塞反馈及音频。新 `UdpPacedBatch` 用异步 timer 续发短批次，保留原 packetization/FEC/速率和单批次容量；失败/取消完成一次。原始文件完整保存在 `backup/udp_async_pacing_20261009/`。
- `udp_paced_batch` 的 3 个测试通过：大帧发送间隙处理控制事件、批次间取消、executor 销毁释放剩余任务。实际快速拖动画面仍待 90 新包安装后对照。
- 当前新增 Render 接入口尚未由 module registry 选择；Console/Node EndpointAddr 下发与 NetClient 默认分支未接入，不得将这些测试当作产品已迁移。接入 registry 时需给 IrohFrontend 的连接事件注入 iroh source ID；目前认证服务组合自 WsTransport，不能把业务回复误送回 WS。
- 正在运行 focused Client/Render 开发构建和 `build_cpp_render_package.bat cloud_node udp-async-pacing-20261009` 完整候选包刷新。完成前不声明新产物已交付或远端安装成功。日志 `.cache/iroh-channels-client-delivery.log`、`.cache/udp-async-pacing-render-delivery.log`、`.cache/udp-async-pacing-package.log`。
- Pixels MCP 428358431 已重新连接；90 当前仅有桌面 Render、Service、Panel，未发现运行中的云应用实例。远端仍为既有安装，尚未替换。
