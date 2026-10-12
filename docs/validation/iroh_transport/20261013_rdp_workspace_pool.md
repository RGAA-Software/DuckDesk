# RDP 多用户工作区交付

2026-10-13。用户要求同一RDP应用在一台机器上使用不同Windows用户并发启动，当前部署容量为20。
已实现并增量发布；真实Windows连接确认两路独立桌面，20slot完整分配/占用/复用通过数据库回归。
**尚未完成20路真实Windows桌面同时连接的验收，不能把配置容量或数据库测试当作该验收。**

## 实现与使用

- schema41将工作区唯一键改为`(application_id,node_id,workspace_slot)`，容量1–64。
  原工作区/历史实例保留slot1，账号、SID、凭证和加密AAD保持。新实例分配最小空闲slot，
  reserved/starting/running/stopping及未确认结束实例均占用；确切结束后复用该slot的持久用户。
- 每slot只允许一个活动实例和一个控制前端。Console为实例下发该slot的凭证，拒绝跨工作区SID确认。
  Service只拦截同一workspace的重复启动，不再拦截同应用同节点的其他用户。RDP仍使用原生编码和iroh可靠流。
- Console Web允许编辑RDP容量，取消前端强制1路和总览中的虚假单实例限制。
  Panel正常启动遇到已有实例忙时只申请一次新实例；不抢占、不为旁观/禁止访问创建桌面，不无限重试。
- 普通停止关闭Render/proxy/连接，保留Windows账号、profile和断开的登录会话；没有logoff、账号删除或会话内应用清理。
  工作区仍不按访问者永久绑定；先后获准访问同一空闲slot会进入同一个持久用户桌面。

原80路PXLIC2授权、节点64实例上限、GameHook/WebView容量20继续使用。90的RDP部署
`e1db8006-1ab8-4044-bafb-980636831c21`已保存capacity20/revision2并重新上报ready。
参见[容量回读](20261013_rdp_pool_configuration.json)和[最终API](20261013_rdp_pool_api_final.json)。

## 验证边界

100项Rust/Web聚焦检查通过：工作区7、资源会话21、部署7、实例19、Service29、Web14、Console配置3。
工作区新增用例真实并发预留20slot，21路拒绝；不同账号/凭证、跨slot拒绝、Stopping不释放、结束后复用身份均核对。
SQLx缓存由PrepareQueries生成，不手工改JSON；首次实例夹具缺少已有协议字段的编译失败保留，补齐夹具后19项通过。
PostgreSQL批次为`pg-20261012-232126-0a52bb37`、`pg-20261012-232253-6b9a636a`、
`pg-20261012-233357-827dff1c`、`pg-20261012-233903-67dca618`，隔离容器/卷已清理。

三种development Panel的product/lifecycle/localization CTest通过，新忙状态分配回归覆盖只申请一次、旁观/禁止不申请和再次忙终止。
C++所有权/命名、rustfmt和PowerShell解析通过。编译均Release并显式18jobs，不运行全量release入口。
development Client/CloudNode/Remote的Panel与dist逐一SHA相等；Console8DAC及6个Web文件已同步本地开发输出。

Company installed Client FE8保持，通过现有资源会话启动两个独立子进程，90实际Windows用户
`pxrdp_2c3153bd5fff41`/`pxrdp_b0f36cef23c348`分别进入session3/4；各自explorer身份不同。
Client两个RDP图形线程实际发布114/99帧，可靠流kind5正常，非模拟/仅进程存活判断。
该任务原定420秒，但约143秒后MCP执行任务interrupted，不能称完整420秒长稳通过；之后5路启动请求因用户执行上下文失效未获接受。
最初两批夹具的PowerShell JSON数组包装错误只打开错误启动窗口，已修正，不计真实RDP样本。
见[两用户原始证据](20261013_rdp_pool_native_two_users.json)及[Client尾段/最终状态](20261013_rdp_pool_final_health.json)。

最终11个owned实例全部stopped、11个owned资源会话closed、guest已注销，Company无owned Client遗留。
原Windows管理员session2保持；RDP session3/4为Disc，既有账号SID/credential_revision/密文摘要完全保持。
数据库现有7个持久工作区，未登录的后续slot也保留账号。见[身份核对](20261013_rdp_pool_identity_preservation.json)。

## 增量交付与失败保留

原完整Setup基线之上只传变更exe、Web和两种清单，没有新Setup安装或版本发布声明。
90工作目录`D:\112233`，XR为`D:\uploadsoft\rd\pixels`，Company为`D:\112233`；没有Desktop暂存。

|运行组件|最终SHA-256|
|---|---|
|90 Console|8DACF64F6F6CD753A0663B4E83BDE251A881FC268C106682CD88418F3B634842|
|90/XR Service|6E3C56A6FB7F05B1AA41A9B6288CC7BC9877287781CB6E5623E2357D471B912B|
|90/XR CloudNode Panel|C63B173E2F169D5A5C966AEA2086E7C793C844715D621E053D87A1A30A8B508F|
|Company Client Panel|17A16E24E4B6DDCE37393860F3AB694DD22129DEEDFDB0EE63180B408EA2A3D7|
|90/XR Render，未改|C276FAB5F8263CBB32C70D0087C785C941127996FCE27C0F21A61F09C49D8931|
|Company Client，未改|FE8B82574E133D765D2FA2E7838370F2AEC761E0FCA8363DF9C701DB96B92466|

两节点TOML保持55904D03…/38363D78…；Relay、管理员桌面未改。Console/Backup/Relay/两Node服务最终Running。
Server35载荷、两Node314载荷及各产品清单、Company41载荷核对。
最终Server manifest970741C8…，90 inventory710A3F3A…，XR81F8303A…，CompanyF3AE7D4D…。
首次包、后续ZIP源/目标SHA以及操作/备份目录见[原计划](20261012_rdp_pool_delivery.json)、
[后续包](20261013_rdp_pool_followup_delivery.json)及[上线结果](20261013_rdp_pool_followup_rollout.json)。

schema40→41前已用pg_dump保存private备份，SHA95F702AF…，4,129,540bytes。
Backup计划schema同步41，不回滚数据库或换回schema40旧Console。
**本批初次Console包错误地将Web移到static根目录，造成Console约23:41–23:59不能启动。**
补充启动/config字段日志后明确定位static_index不存在，恢复既有static/console路径并全SHA后服务恢复。
不是网络或RDP逻辑故障，也不称零停机。配置解析器支持单/双引号，早期手工诊断对单引号JSON的误读已纠正。
维护的迁移发布器增加static/console/index预检，未来错误包必须在停服务和迁移前拒绝。

90发布Panel先遇到PowerShell Stop-Process NullReference，恢复服务后看门狗又启动旧Panel导致文件占用。
两次失败均确认终态及未发布，随后以PID/完整路径/创建时间核对后用.NET进程句柄退出，保持Node停止直至发布完成，再启动服务。
维护发布器已替换该Stop-Process边界。Company VerifyOnly首次被脚本执行策略拒绝，使用仓库标准的进程级ExecutionPolicy后通过；未改系统执行策略。
本地完整collect因缺update-root参数/磁盘空间不足未发布，改为只同步变化Panel和清单并核对现存全部载荷；不伪称完整包新建成功。
删除PG编译缓存的请求被自动审批策略拒绝，未删除。原始实现19文件在`backup/rdp_workspace_pool_20261012`只读参考归档。

下一主线是实际多路客户端/多Relay分配与正常负载验收；RDP20路Windows实机并发仍需单独有效样本。
短顿保持用户确认的阶段性进展，不重启NIC/饱和混传调查；Android排除，不新增短期transport token或第二套认证。
