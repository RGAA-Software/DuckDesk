# Linux 升级、Relay 展示及多 Client 设置交付

2026-10-13，执行用户确认的[五步计划](20261013_next_network_development_plan.md)。上轮已验收基线 `7eaeb13a6` 已提交并 push。本轮使用本机及90；Company没有连接或使用，XR无需重复更新，Android和暂停的短顿/NIC/饱和混传专项保持原范围。

## 已完成的实现

Console在既有认证资源描述中返回独立的公开Relay名称/URL映射。Panel转交Client，统计按当前selected地址匹配名称，同时显示实际地址和RTT；未知名称用实际地址，直连/未知路径不显示上一路名称。名称只是连接描述的快照，不参与拨号、候选或授权。C++没有名称时仍序列化原Render字段，Rust节点协议和Android数据面不改；Android消费者使用JSONObject字段读取，新增外层显示元数据不改变其路径。

Console增加当前转发Mbps及预算百分比；沿用已有是否接受新候选的状态/原因，过期数据不作当前指标。预算仍为新候选筛选，不是整形、预留或平均分配。

Linux实际检查发现两项交付缺陷：`deploy.sh`只升级Backup控制配置，已有setup.complete会绕过Console数据库迁移；初始化调用私有文件接口创建空backup.env，而该接口拒绝空内容。现已写入有效注释env，并在Compose启动新服务前停止Console/Backup、运行离线迁移、原子同步Backup计划schema。权限/所有者保留，失败时不继续启动新容器。候选打包和镜像入口包含/预检新升级脚本，Linux编译明确18jobs。

## Linux候选与实测

五个Linux程序完成Release构建；41项候选SHA验证通过，PostgreSQL18.6工具链9项通过。生成 `pixels-server:1.0.56`，镜像ID `sha256:51960340e5b346b2da5844b1ec2ce0593aef3fdea92963d974c1aff8a4294f90`。

| 交付物 | SHA-256 |
| --- | --- |
| 镜像tar，56,159,744 bytes | `ee27b07f189bd805177cee3d939ea8b25d815e5181d3d1dddd4ca4b623045add` |
| Compose包，55,744,558 bytes | `989a51b043223343dc9e7b70e732565e7d5d48df311f0a56487c69813561aab9` |

交付物在 `.cache/linux-server-followup-20261013`，为fast-release候选，不是正式优化发行。使用真实新镜像、独立网络/配置卷/PG容器完成初始化，再在隔离数据库构造schema41状态，用包内升级程序升至42。Backup其他字段及Console配置/CA哈希相等，Backup权限仍10003:10003/600；重复42成功、错误Backup目标在数据库修改前拒绝。最终测试容器、网络及配置/数据卷清理确认。脚本为 `scripts/server_validation/test_console_database_linux_upgrade.py`。

这是Docker内真实Linux程序和PostgreSQL证据，**不是独立Linux Server上的Compose安装或升级**。没有指定该部署目标，没有把BJ Relay主机改成Console。

## Windows增量交付

Client、Cloud Node、Remote的Client/Panel均focused Release、显式18jobs构建，六对build-tree/dist SHA相等。90只传本批改变的程序/Web资源与清单：Console `EBC1A4C63942346B01E99FB47058277218D0323DE30491C730179E17A779F6E6`、DB工具 `F2C711F55A02EAAC5AE99AD3047D5385CC3B2EC56B3D3CD8D771F3FBD5BC1951`、Web `index-FbXcqmD7.js`；Server最终清单 `457D8BAB2FFDC8047F9614B62A41EB248FA9BBFC0572899E2AF5DC288B520932`，35项SHA通过。

90 Node Client `EEF934B09A531F5306AB89CB0A17295BCEA26A0BE46F01BDF32A423B6D32853A`、Panel `8608295A80EE7BA72DAFF338CC05766128E60DFD7C211D6A10116D0916BA6AE1`和产品元数据已更新，315项核对、清单 `E295C2EA3C759536BA431C27321BE8405FF3F3974AED5FABFC64206BA2F0365C`。旧开发dist中的Render/Service/DLL与已安装正式基线不同，且不属于本批构建修改，未把它们盲目覆盖到90。Node Service/Render、配置、schema42/Backup42保持；Panel恢复Administrator session2（实际PID68932），Console/Backup/Relay/Service Running。

本批没有新Windows Setup、签名或更新源发布，包版本和原发布者身份保留。当前运行身份由SHA表述；文件备份留在90 `D:\112233\network-followup-20261013`。

## 两路同应用实际验证

一个owned WebView应用，源为90本地动画页面；本机始终最多两Client同时运行，总共启动三个独立进程：20608、7608、70560。

1. 第一Client开启统计并保存；第二Client启动自动加载开启状态，两路都显示实际Single Server Relay＋URL。
2. 第二Client保存关闭统计，第一Client仍开启并继续出图，证明运行中窗口状态独立。
3. 关闭第二Client没有关闭第一路；新第三进程读取最后保存的关闭状态，第一Client仍保持开启。

此语义为同应用默认值最后成功保存者生效，既有窗口不即时广播更新。59–60FPS、接收字节增长仅是本批正常功能样本，不声称长期稳定性或卡顿根因结论；第三路初始画面单独观察，未作启动平稳强制要求。

最终三实例stopped、三资源closed，guest注销204、夹具应用/部署禁用、本机owned Client0；90临时页面按PID/创建时间/命令路径清理，listener0。原本批开始时的10应用和10部署（含前批禁用夹具）配置逐项相等；授权80/RDP10保持，双Relay fresh/ready/未排空、Windows默认1/BJ0。筛选后的本机日志gzip4,968 bytes，SHA `000912829b77cea029f743ffe2dee6ae2998d1ce07db37569e6e30e75ed8d6cd`；图片和详细身份见[交付证据](20261013_network_followup_delivery.json)。凭据和启动信封不进入Git证据。

## 检查与失败样本

6个Client连接CTest、三产品Client/Panel入口及相关自动检查通过；新增名称隔离/无名称原字段、实际路径对应、直连/未知/缺失名称回归通过。网页5个视图与3个带宽用例、类型/ESLint/构建通过；13个Python打包回归、shell语法、Rust格式、C++所有权/命名/150列检查通过，6个Web源/开发发布文件SHA相等。严格Clippy的既有无关告警继续保留，LinuxBackup另有既有函数转整数告警，未压制、未宣称全门禁通过。

无效夹具均保留：Docker login shell改PATH找不到cargo、未指定LinuxPROTOC、PG分离stdin无法初始化、网页慢加载时真实interval影响测试、ESLint错误cwd、MCP目标父目录不存在，以及本机焦点/遮挡/几何变化使输入守卫拒绝。分别修正命令/夹具；网页只控制测试interval时钟；本机等窗口几何稳定、核对可见点确属owned窗口后点击，没有向其他应用发送输入。Linux空env是实际项目缺陷，修复后用真实初始化和升级重新验收。初次第二Client悬浮球坐标偏差只点击测试页面，没有宣称偏好变更成功。

本阶段实现与当前可执行交付完成。剩余为独立Linux Server部署、后续Windows正式包发布及复杂NAT/规模/长稳环境验收；这些与已完成的功能证据分开记录。
