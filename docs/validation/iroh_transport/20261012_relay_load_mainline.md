# 多 Relay 负载主线推进（2026-10-12 下午）

用户将残余短卡顿调优留到今晚回归；本批推进 Windows/Linux 多 Relay 负载观测与分配核对。
第0.9节已有接入、独立文件、重连、RDP工作区及双系统交付证据继续复用。Android排除，90不做测试。

## 本批交付

Console Relay列表新增“接收 / 转发速率（Mbps）”，保留累计字节和真实连接数，补充横向滚动。
速率是近期两个受认证报告的字节差除以Console记录的报告时间差，不使用浏览器刷新间隔，
也不把它当作运营者的带宽上限或自动分配权重。离开页面后不保留历史样本，重新进入需两次报告。

计算模块 `web/px_console/src/model/relay_traffic.ts` 与业务API、页面分别维护；没有新增轮询端点、
Relay协议字段、授权系统或传输令牌。页面复用已有管理事件刷新，中文和英文走同一实现。

- 首次报告、离线/未知计数、重连代际或Console epoch改变：未知。
- 计数回退、时间倒退/零间隔、超过30秒采样间隔：取新基线，未知。
- 重复/乱序读取不刷新旧速率的有效期；两次新报告确认没有流量，才显示0。
- 不精确的JavaScript整数计数不用于计算。受影响数据未知时不伪装为容量空闲。

## 验证与交付边界

聚焦回归18项通过：计算11项、实际Table中英文与过期显示3项、已有Relay API 2项、目录键一致2项。
TypeScript检查和变更模块ESLint通过。最初Table测试漏注册TableColumn的三项失败已修正测试挂载，
没有将该夹具失败当作产品连通问题。jsdom仍报告其已有的伪元素getComputedStyle未实现提示；最终用例全部通过。

构建环境明确 `UV_THREADPOOL_SIZE=18`、`GOMAXPROCS=18`；Vitest明确 `--maxWorkers=18`。
Vite构建成功，仍有既有大型bundle提示。6个产物发布至 `output/px_console/dev/static`，
与 `web/px_console/dist` SHA逐项相同，详见 [交付清单](20261012_relay_traffic_web_delivery.json)。
这是本地开发网页交付；未覆盖远端Console、未更换Server/Relay/Node/Client安装包。

通过已有Console认证只读BJ报告约25秒，序列3141–3146，generation17/control_epoch71，fresh/ready，
0/4096连接。累计收到1,744,000,987字节、转发1,743,036,931字节保持，生产计算模块回放结果为
首次未知、后续五个区间均0Mbps。见 [原始样本](20261012_relay_traffic_live_samples.json)
及 [生产模块回放](20261012_relay_traffic_live_replay.json)。这是实际上报兼容和空闲显示证据，
不是带宽上限、并发性能或负载均衡验收。采样未启停服务、修改维护标志或施加传输负载。

## 分配链路核对及接续

Console `available_iroh_relays` 按fresh/ready/维护/连接余量筛选，按连接利用率排序。
`rust_transport/px_transport/src/relay_candidates.rs` 将集合存为BTreeMap；当前隔离iroh1.3.0
`src/socket/transports/relay/actor.rs::on_network_change` 消费网络报告的preferred_relay决定home。
因此Console候选顺序不能证明实际home按照该顺序分配，连接数比例也不能证明带宽均分。
本批只读检查这些实现，没有修改第三方源码或未经证实地调整权重。

下一开发检查点：核对多个新Endpoint的实际home、连接数和容量拒绝/释放，确认是否存在集中热点，
有具体证据再补分配策略。以前双Relay故障/维护/容量证据复用；本批90不可用，BJ之外没有新的
独立测试Relay，不伪造跨Relay性能结果，也不重新索要90权限。

今晚卡帧调优从 [Company当前报告](20261012_company_xr_video_report.md) 接续：467.233秒有效观测，
三次>100ms接收间隔共0.479653秒，占0.102658%；未证明整体系统99.9%可靠性。
无需重新安装已验证的Company/XR基线；后续仅传变化exe和必要依赖，远端日志压缩后下载。
