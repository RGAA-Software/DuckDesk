# 晚间短卡帧定位（2026-10-12）

用户明确开始此前延后的晚间调查，Company、90、XR 恢复可用。Android、NIC/驱动和饱和混传压力仍不在本轮范围。
既有安装、双 Relay 分配与保留连接证据复用；没有重建产品或替换媒体二进制。

## XR → BJ → Company

已完成 600.251514 秒正式采样。118 个完整五秒呈现窗口覆盖 591.283508 秒、35470 张新图，
加权 59.988144 FPS，最大完整窗口呈现间隔 64.158ms。正式片段接收/呈现没有 >100ms 事件，
真实解码 no_output/errors 为0，日志 overruns 为0，源端 busy/failed 帧为0，最长应用提交13.223ms。
包含480次对 owned Client 窗口的指针消息；系统鼠标位置未改。消息队列接受不是每条消息被游戏消费的证明。

配置上限10000kbps，实际视频数据报平均1.481Mbps、最大五秒窗口1.807Mbps；不是10Mbps吞吐压力验收。
本地环回HTTP动画已加载，避免把外部网页加载时间当成传输停顿。Client FE8B8257…、Render6F9A2D19…、ServiceA06AF7FE…保持已交付身份。
实际两端选择BJ，没有以强制Relay样本冒充UDP直连。

BJ在正确容器网络命名空间采128字节TCP头；最终采集912286包、kernel drop0。
XR实际socket112.27.225.224:45950，Company39.71.45.66:50982；四个方向均被捕获。
两个业务socket对应方向均没有 >=50ms 的序列缺口或SACK等待、没有零窗口。
ss最后状态：BJ→Company bytes_retrans17073/retrans19，BJ→XR139/retrans2；重传计数并未对应 >100ms 视频停顿。
因此不能仅凭“存在重传”“reord_seen 很高”就认定卡帧根因。分析器不把重叠序列直接命名为物理丢包；TLS包头不能标识具体视频帧。

ss实际间隔约65ms（50ms等待加查询），10020份样本，查询中位13.802ms、最大40.175ms。
前一启动采集与正式采集有重叠，观测开销及非恒定50ms间隔明确保留。
Company相对本机的墙钟界限约-10.7～-9.5秒，BJ约+7.93～+8.02秒；不逐帧强配跨机器绝对时间。

证据：`20261012_evening_company_xr_bj_analysis.json`、`*_video-result.json`、两端gzip与原文SHA、
`*_final_capture_identity.json`、`*_tcp_analysis.json`、`*_tcp_summary.json`及压缩ss采样。
两份大PCAP保留在本地`.cache/evening-video-20261012/`及BJ原采集目录，SHA写入索引，不将82MB头记录加入Git。

## 采样工具修正与失败记录

首次派生用户进程的父命令为interrupted，未算性能结果。第二次45秒脚本判定“No current presented video”，
真实日志却持续约60FPS：Windows打开文件的LastWriteTime可能尚未更新，筛选跳过了活动日志。
已移除文件修改时间门槛，按日志行时间筛选；同时读取安装目录和公共可写日志目录，保留SHA、单调采样时长及 owned PID收束。
直接保持用户命令运行至样本结束，正式600秒完成。连接失败现在立即记录具体稳定错误码，不盲等45秒。

新TCP分析器支持Ethernet/Linux SLL/SLL2、序号回绕、SACK及截断拒绝。
已用已知200ms序列缺口和SACK缺口（跨32位回绕、三种头格式）及截断捕获验证，2项通过。
没有修改iroh registry、vendor、系统时钟、QoS、网卡或驱动。

## 上游比较与结论边界

只读核对Sunshine `3cba9baebac882b336be3ebe129ee612cb189853` 的 `src/stream.cpp` 逐帧/跨帧pacing，
以及Moonlight common core `e95feaf4951b8dc774671a5d6a1c31d76d78e3ac` 的RtpVideoQueue FEC/缺块恢复与VideoDepacketizer。
本项目已有跨帧有界pacing、FEC与参考恢复，不能靠重写相同机制来宣称解决TCP到达前等待。
iroh-relay1.3.0源码已确认两端TCP_NODELAY开启、发送有显式flush，不按猜测增加Nagle补丁。

本轮XR/BJ/Company目前未复现此前151–165ms短顿，不能说已定因或已修复。此前三次源API接受几十微秒、接收分片134–146ms的证据仍有效。
## 90 GameHook → 90 Relay → Company

完成300.302386秒正式片段；完整窗口295.581899秒、17729张新图，加权59.979992FPS，
最大呈现间隔80.047ms。正式片段没有>100ms接收/呈现事件、解码无输出/错误或日志overruns，源端busy/failed0。
实际视频数据报平均6.009Mbps、最高五秒窗口6.119Mbps，配置10000kbps未改；最长应用提交24.407ms。
480次owned窗口拖动消息包含左键按下/释放，OS鼠标未移动；实际连接始终90 Relay，没有把该路径称为P2P。
这轮比XR动画的1.481Mbps更接近实际游戏负载，仍不是20Mbps、饱和混传或所有游戏验收。

首轮游戏客户端在admission阶段明确拒绝、没有媒体，未算300秒样本；管理会话已自动closed。
创建资源后启动流程分成多轮工具/说明，现场时间间隔超过既有会话准入窗口。
把创建、上传启动参数和启动客户端放在同一连续工具执行内后正常接入。
没有放宽授权、新增transport token或扩展鉴权验收。首次失败的Client gzip和结果单列为`*_initial_*`。
源日志首次下载发生在压缩命令仍运行时、文件尚未生成，返回确认failed；随后确认结果文件生成再下载，两个SHA通过。
源ownership选择也纠正为4613根进程，排除WebView的CEF子进程；未杀任何额外进程。

两个正式样本合计约15分钟，没有复现>100ms卡帧。这支持本轮场景稳定，**尚不支持“此前151–165ms短顿已修复”**。
证据归档后，下一未完成项是在能复现旧事件的场景取得同段帧/链路证据；不靠降低诊断阈值、补猜测代码或重跑已通过功能宣称完成。

## UDP直连与用户确认的实际拖动

用户确认拖动的是远程游戏/网页内容，不是窗口标题栏。追加同一个90 GameHook源→Company的普通UDP直连，
客户端和源均实际选择`10.0.0.90:4613`对应LAN路径，不能冒充公网复杂NAT验收。
300.124242秒，完整窗口290.515196秒/17415张新图，59.945229FPS，最大42.457ms，>100ms0、decode no_output/errors0、overruns0。
包含480次真实OS鼠标位置、左键按住的内容区拖动，约7.88秒；操作结束恢复cursor/focus/DPI。这个8秒练习不是整段持续拖动。
实际视频数据报平均7.267Mbps、最大五秒7.988Mbps，10000kbps配置不变；源busy/failed0。
前一Windows消息练习源日志实际见hook-inner mouse n=1、200、400，不能只把API成功当作游戏消费全部480条。

直连准备阶段一次命令转义错误导致准备脚本未生成、传入旧的已结束资源启动参数；Client拒绝，未算样本。
随后把准备程序写入实际Python文件、检查exit_code，并把创建/上传/启动连贯执行，正式片段完成。
没有新建授权系统或围绕令牌追加验收。失败归为采样流程失误，未当作UDP媒体丢帧。

## 持续发送摘要的实现与交付

具体缺口：旧600秒XR片段frame34046在约第九分钟出现422.192ms接收停顿，
源普通预测帧的逐帧提交日志却在90秒后停止。五秒最大值无法关联该帧。
保留原首次90秒细节，在Sender内新增最多64个纯值提交摘要，记录encoder/wire帧号、API接受首/末时间、排队/总耗时、QUIC快照。
没有编码内容/分片payload、无新增异步回调。反馈到达/生成间隔或恢复的完整帧确认进度>=100ms时输出，最多每秒一次。
持续正常状态只维护有界内存；Stop清空，停止后的完成回调不恢复历史。API接受时间不称NIC发包时间。

61项媒体/生命周期检查通过，其中2项新历史状态测试覆盖晚间600秒时间值、64界限、分流、冷却、重复Clear；
新增真实loopback发送/延迟确认集成触发`diag_tx_history`与`diag_tx_recent`，并确认Stop不关闭可靠连接。
注入120ms确认等待是诊断功能验证，不是公网卡帧修复效果。ownership/naming门禁、TCP解析2项检查与PowerShell解析通过。
所有C++编译显式18jobs、Release；仅请求Render/媒体test目标，没有全仓release build、版本提升、npm或Rust重建。

CloudNode development Render9275B4E0…与Remote9EC1EEF8…已同步各自dist并SHA相等；正式族聚焦RenderC276FAB5…与官方stage相等，314载荷核对。
已有完整Setup基线上，仅上传约30.10MB Render+清单ZIP到90/XR，源与目标SHA相等，先VerifyOnly再备份/指定服务停止/发布/全部清单核对/启动。
仅90的已核对PID12264/创建时间默认Render为替换exe停止，没有按进程名/路径扫游戏；该默认Render后续恢复。
原ServiceA06AF7FE…、Company ClientFE8B8257…、节点自定义TOML55904D03…/38363D78…不变，Windows登录90 ID2/XR ID1保留。
也核对到上次Service增量后两侧product-manifest还保留旧Service SHA；本次同步修正元数据为实际A06，运行Service没有再换。
product-manifest与sha256sums的314载荷均重新匹配。不是新Setup安装验收；旧候选Setup保持原身份。
交付/备份/上传及本机dist证据为`20261012_evening_send_diagnostics_{delivery,rollout,local_publication}.json`、`20261012_evening_installed_identities.json`。

新Render实际启动后再测150.104051秒UDP LAN直连，超过90秒后真实拖动480位置，
完整140.275764秒59.283227FPS、最大42.669ms，无>100ms/decode错误/overruns，源busy/failed0；实际平均7.468Mbps。
这个150秒后检验小于前面的300/600秒，也不是补齐长稳，不能据短样本变化断言诊断没有成本。
新增异常摘要未因健康片段强行触发；完整触发动作由上述真实loopback集成覆盖。

本轮正式媒体合计约22.5分钟未复现>100ms旧事件，仍不宣告旧问题定因/消除。
最终335业务resource sessions全部closed，无未结束实例；90/XR节点fresh/ready、generation464/23、epoch72，两Relayfresh；
临时XR页面/本地BJ限制已撤回，测试app revision6/deployment8 disabled，Company owned Client均结束。
Android、NIC/驱动、限速和饱和混传未启动。

用户后续决定：当前未复现，暂置为**阶段性进展**，先不继续处理。保留已交付诊断及本轮有效样本，根因仍未确定，
不宣告彻底修复；实际复现或用户明确要求继续时再利用后段历史关联等待位置。此调查不作为Windows/Linux网络主线的阻塞条件。
