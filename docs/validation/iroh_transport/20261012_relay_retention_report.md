# Relay 满员／维护时保留已有 home（2026-10-12）

本批接续多Relay分配核对，卡帧调优仍留到今晚。Windows/Linux既有交付证据复用，Android排除，90不测试。

## 问题与最终行为

此前Console `available_iroh_relays` 同时用于新端点配置和Node报告响应；Service把响应替换进
`node_control_iroh`，再通过心跳把同一集合交给已有Render。Relay满员或进入排空后从集合消失，
运行中Endpoint也会收到撤下该候选的更新。这与“停止新分配，保留健康现有连接”的维护语义不一致。
这是配置路径缺口；本批没有将其认定为Company三次短顿的原因。

现在Console认证与Node报告分别下发两组地址：

- `iroh_relays`／初始 `iroh.relays`：近期认证、就绪、未禁用、非排空且有两连接余量，供新端点。
- `iroh_retained_relays`：近期认证、就绪、未禁用的允许保留集合，满员/排空不会仅因此剔除。

Service的新启动配置始终使用新端点集合。每次Render心跳仅将该Render自己当前上报的home
与新候选合并；未知、新启动、仅IP地址或其他Render不会获得这些保留项。URL规范化匹配、
当前QAD配置更新、重复消除和32候选上限均覆盖；两组地址都受本节点的Relay URL限制。
禁用、离线、上报过期或Console撤下的地址不会保留。已有Relay硬连接准入继续执行，无新认证机制。
Service仅在两组策略实际改变时记录 `event=iroh.relay_policy` 的候选和保留数量。

涉及Console store/runtime、共享Node协议、Service控制接收与每Render心跳组合；没有改第三方库、
QUIC、媒体分片、组帧、编码或网卡。原来iroh按网络探测选择home的机制保持，不能称带宽自动均分已完成。

## 验证

网络聚焦21项：共享Node protocol6、Service8、Relay store4、Console WebSocket/PG3，全部通过。
真实WS用例确认满员与排空的Relay不出现在新候选而仍在允许保留集合，禁用后两组均空；
Service测试确认现有Render的心跳保留自己的home、新启动配置不被污染、其他Render不继承、
撤下后清除。已有RDP准入／工作区契约回归通过；这是网络回归，不新增RDP业务或帧率验收。

数据库使用一次性Linux PostgreSQL容器，Windows Console真实WebSocket，源文件验收期间不变，
所有测试容器与卷已清理。报告：`test-results/server_validation/pg-20261012-155206-0239e557/report.json`
和 `pg-20261012-155913-7b440392/report.json`，精确SHA见 [测试索引](20261012_relay_retention_tests.json)。
Service文本结果见 [聚焦日志](20261012_relay_retention_service_tests.log)。分发准备7项另通过。
批准iroh1.3.0的84文件及补丁核对通过；本批未改变Relay二进制，Windows/Linux既有Relay实机结果复用。

已有跨宿主4端点（两台各2）与容量释放证据 `20261010_cross_relay_capacity.json` 保持有效。
本批合成满员报告不等于多台物理Relay并发负载，也不等于新的实际媒体长稳通过。

## 交付

全部编译显式18 jobs，Release快速增量配置。没有运行全仓release-only构建、提高版本或要求Windows签名。
CloudNode/Remote开发Service均发布对应dist，277/43文件清单核对；Service target/stage/dist/官方候选SHA一致：
`A06AF7FE0E9F0DC7CDFBA78A443874A2855B5DA2F18CA4B770764FBE115671ED`。
Console target/output/Server候选SHA一致：`C783635BE09091D6BBA21159141AAE1D3F28C7CAAF366749010EDF804441B265`。

| 完整候选 | 文件数 | Setup SHA-256 |
|---|---:|---|
| Server1.0.56，`build_official/private_server/candidates/relay-retention-20261012` | 35 | `42FDB474D3ADE37790E59083AEEDBD705A82178562683DE61954FFEA4A7F13B6` |
| Node3.3.97，`build_official/cloud_node/official/candidates/relay-retention-fixed-20261012/installer/3.3.97` | 314 | `EE7832A7BBDEE3EFC423CD6437A164BD756A885AAF8DFC860CF65533C2BC0835` |

Server包含上一批新增速率的Console网页；其他未修改组件从已核对完整基线复用，组装后重新核对全清单。
候选均为未发布的fast-release验证产物，不将开发dist称为安装包。详细清单见 [交付身份](20261012_relay_retention_delivery.json)。

首次Node collect遗漏要求的update-root-file参数，PowerShell继续打包旧dist。该候选Service636C688E…，
与本批A06AF7FE…不一致，未上传、未用于验收；候选根目录已标记REJECTED，
[失败记录](20261012_relay_retention_rejected_candidate.json)保留。正确collect后先检查新Service哈希，
再以check=True调用打包器，在新candidate ID重建，最终314文件重新核对。

## 部署边界与下一步

**本批没有实机上线。** 当前公网Console位于90，用户仍不允许使用该机器；本批没有90命令、
安装、服务重启、应用启动或网络配置操作，也没有单独升级XR Service使其连接协议不匹配的旧Console。
Company Client FE8B8257…、XR Service636C688E…／Render6F9A2D19…及上一批已恢复配置保持；没有新远端测试资源。

新字段要求Server和Service配套版本，按Server先、各节点随后分别升级；不设计分布式激活事务或旧开发协议兼容层。
下一项是在可用Server部署窗口完成安装身份、节点重连和一次实际容量／home保留验证。
在此之前不将本批称为线上修复或十台Relay性能验收。晚间卡帧从既有有效片段及源／Relay反馈证据继续，
不重新开启已停止的NIC或饱和混传专项。
