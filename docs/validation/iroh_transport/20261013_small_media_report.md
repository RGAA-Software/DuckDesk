# 小规模 Native 视频与双 Relay 维护

2026-10-13 10:10–10:20，本机最多两路、Company 一路；源均为 90。复用现有运行包，未编译、安装或修改原应用配置。此次完成正常视频负载下的排空、新连接分配和清理验证；不称长期稳定性或音视频完整验收通过。

创建独立测试 GameHook（4000kbps、容量1）及 WebView（3000kbps、容量2），总配置上限10Mbps。WebView 访问90的 `127.0.0.1:48777/validation.html` 动画，排除公网网页加载因素。页面标识含历史 XR 字样，本批实际源是90。原应用spec最终逐项比对保持一致，RDP容量10及80路授权保持。

| 接收端／内容 | 显示采样时长 | 加权显示FPS | 最大显示间隔 | 超过100ms的统计窗口 |
|---|---:|---:|---:|---:|
| 本机 GameHook301 | 490.8秒 | 59.154 | 314.6ms | 1/98 |
| 本机 WebView302／90 | 315.7秒 | 59.943 | 48.9ms | 0/63 |
| Company WebView303／90 | 501.0秒 | 59.906 | 512.0ms | 2/100 |
| 本机 WebView304／BJ | 135.2秒 | 59.564 | 472.6ms | 2/27 |

数值来自`video.present phase=window`的frames/elapsed_us，不是网页自己报告的动画FPS。用户本批确认刚连接时不稳定正常：Company首窗口512ms、本机304首窗口473ms属于启动阶段，单独记录，不作为稳态验收强制条件；此外Company10:16:11窗口150ms、本机30410:18:10窗口398ms、Game10:18:14窗口315ms保留为运行期异常。窗口内最大值不能推导卡顿占总时长比例、99.9%稳定性或根因。截图、窗口枚举、输入操作与样本重叠，不将时间重叠当因果；没有重启旧NIC、抓包或饱和混传调查。

初始三路实际selected Relay均为90。排空90 revision25、reported_draining=true时，旧Game/Company资源继续connected、画面计数推进。只关闭本机302，确认其资源closed、实例stopped后准备304；新源home与实际selected均为BJ，显示持续推进。恢复90 revision26／reported=false后旧两路与304保持connected，Windows/BJ两候选恢复。Company前后截图page frame20374→30225，均显示60FPS动画。这里验证维护排空；没有停止Relay进程，不当硬故障恢复证据。

90初始Endpoint计数7，排空替换后Windows5/BJ4，包含默认Render及两端端点，不是业务流数或平均分流。初始333.35秒90平均转发6.512Mbps；双Relay稳定23.67秒窗口90 5.469Mbps、BJ 1.694Mbps，合计7.163Mbps。计数包含控制及端点流量，仅为实测区间速率，不当Relay最大容量；配置10Mbps不要求实际恰好10Mbps。

输入检查只向owned本机304窗口发送三次鼠标移动，Client记录`sent=true`，画面Pointer有更新；最终坐标受并行实际鼠标活动影响，没有宣称精确坐标往返全部通过。音频配置开启，Game源侧持续Opus约44–46kbps、音频数据报约87–90kbps；动画页无声音，客户端本批无音频交付统计，没有听音/录音或解码输出证据，因此音频端到端保留待办。

采样客户端日志无`reconnect=true`、可靠发送error非零；源侧停止302及最终关闭期间出现四个send_window失败窗口（合计5次failed），保留原文，不称全程零错误。前台画面持续与资源状态共同构成维护功能证据，短显示异常仍保留。

结束后4个owned实例全部stopped、4资源closed、guest注销；本机/Company Client均0。测试应用及部署各2个已禁用，保留审计身份；动画页任务确认cancelled，端口48777监听0；90仅保留默认Render PID13524，Relay恢复Windows1/BJ0。未删除Windows用户/profile或会话。Company900秒守护任务因主动提前清理返回退出失败，不计900秒长稳通过；精确PID/路径/.NET创建时间清理任务exit0。

夹具问题保留：首次读取baseline后误取顶层kind报KeyError；第一次创建应用错误包装spec返回400，修正为裸ApplicationSpec后成功；一次PowerShell内嵌Python引号错误未执行启动；首次文件读取lines+max_bytes被参数校验拒绝；首次源日志路径不存在，改读Public运行日志；禁用测试应用后guest读取实例403，改用禁用前已确认完整终态的快照再注销。上述错误不归为网络失败。

Company日志先压缩338551bytes/SHA4300F13B…、源日志1943335bytes/SHA1E1037E5…再下载，两端SHA一致；本机gzip684518bytes/SHA606D44E4…。完整原文保留在`.cache/small-media-20261013`和对应远端开发目录，不提交含会话材料的原始日志。摘要、异常原文及完整哈希见[分析](20261013_small_media_log_analysis.json)，业务/Relay时序见[观察](20261013_small_media_observations.json)，清理见[配置清理](20261013_small_media_cleanup.json)、[进程清理](20261013_small_media_process_cleanup.json)。

本机Client C39C17CC… build-tree/dist SHA相同；Company Client FE8B8257…、90 Service6E3C56A6…/RenderC276FAB5…/Console8DAC…沿用。下一项补少量可控输入与音频端到端证据，继续保持本机≤2、Company≤1；真实NAT、规模和长稳证据仍不足，Android及已暂停专项不启动。
