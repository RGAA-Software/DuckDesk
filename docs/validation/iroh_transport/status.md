# iroh 长任务执行状态

当前目标（2026-10-11）：按 `docs/iroh_transport_execution_plan_20261008.md` 第 0.9 节，连续完成 Windows/Linux iroh 网络开发与交付，**本次不做 Android**。
早期 P0–P7/Android 总路线保留为历史；Android 迁移及依赖它的全局旧栈退役不属于本次完成条件。
90 工作区为 `D:\112233`，不向桌面复制文件。

## 最新实机接续：2026-10-11，Windows 应用 iroh

### 连续实施：回程反馈额度修复（2026-10-11）

- 2026-10-12 14:35 Company完整新Client3.3.81已安装：Setup181F0006…/manifestFE24F4FC…/41文件全SHA/ClientA714EFA4…/hy会话1保持；Pixels Yun与AgentBridge保留。Company→BJ→XR实际接入后，MCP用户命令在467.233秒interrupted，Client已结束，不能称600秒通过；450.868秒完整呈现窗口59.867FPS，3次151.219/163.047/165.387ms接收(新图max165.167)，源总提交43/41/34µs，分片跨度134.399/146.203/0.369ms。正确容器TCP见XR腿回程重传与入站乱序，Company视频出口未报告bytes_retrans；BJ+7.610～8.132秒/Company−11.360～−9.908秒墙钟界限已测，不能逐帧强因果归因。两端源压缩/gzip原文双SHA通过，报告20261012_company_xr_video_report.md，未改组帧/iroh vendor/NIC/驱动/UDP。另修实际诊断缺口：Client安装目录日志不可写时回退既有公共运行目录；18jobs dev/official focused构建与devdist SHA一致/所有权门禁，已有基线后只传变化px_client.exe+两清单，最终ClientFE8B8257…/manifest7D198893…/41文件核对。脚本误用旧路径hash失败、只监测公共路径而实际仍写本地的假失败保留；最终仅日志子目录临时拒写，验证permission denied→公共日志→BJ实际60.359秒通过/Client39816退出，临时规则已撤回。最终所有owned资源/guest/实例收束；XR localBJ配置按哈希恢复38363D78…/ServiceRunning/Render0/页面取消confirmed监听0，owned app revision4/deploy6 disabled，安装保留，原应用/全局Relay/90未动。下一项是源→Relay及回程反馈等待的具体关联与未完整长稳片段，先复用已有结果，不重开NIC/饱和混传；Android排除。此前f62a6f2eb已push，本批日志修复及新完整基线/有效片段/失败证据已提交并push bebe65184，最终Company41文件复核/Client0/临时deny0确认。

- 2026-10-12 13:52 Company新对端准备：原机器只有Pixels Yun3.1.46及独立AgentBridge1.2.87，无新版Client/px_service，原安装未改。独立D:\112233\xr-relay-video-receiver-20261012复用20个相同文件，仅传22个变化文件zip43,877,601bytes/13.448秒=3.263MB/s(26.102Mbps)，42文件SHA核对但仅开发运行目录、不能称新版包安装。第一次脚本用ReadAllText缺少FileShareReadWrite失败已保存/Client47592清理；修正后Client41924实际ADMISSION_REJECTED且用户关闭，并非缺Vulkan(正确fallbackD3D11)，会话/实例/guest清理。用户指出Company未安装新版，已18jobs官方聚焦构建Client/Panel并collect/verify/create完整Client3.3.81候选company-relay-video-20261012，Setup181F0006…/47,952,642bytes、manifestFE24F4FC…/41文件、ClientA714EFA4…，MCP上传SHA同并正用完整Setup安装新基线，不卸载Pixels Yun/AgentBridge。后续仅变更exe/必要依赖。XR测试应用临时恢复revision3/deployment5、localBJ选择87C9…/Servicegeneration7，页面owned83296943…运行；本机不再收视频，90不参与。第一次503发生在配置上报尚未ready，ready后新会话成功，未当产品连通验收；正在核对安装/修正前端授权启动脚本，并将在容器命名空间采TCP。上一收束f62a6f2eb已push；下一项安装41哈希、使用installed Client正确Admission、600秒实际新图/源gzip/清理，暂未通过。

- 2026-10-12 13:33 第二轮未通过600秒：13:23:00–01复现139.646/185.864ms，frame2504源三片提交24µs/总38µs而接收分片跨度104.602ms，frame2530源总33µs/接收跨度28µs但完整帧间隔185.864ms。持续诊断有效，仍不能直接归因外层TCP或NIC。随后本机13:24:24 NDIS10400报告Realtek硬件无响应并重置，DNS1014/HTTP0同步，Client断开/XR宽限10秒退出；本轮失败保留，不继续NIC/驱动/混传压力，亦不以此解释全部短抖动。BJ宿主ss采样因Docker网络命名空间错误为空，无效且不称无重传；下一批用容器PID nsenter。源12988行gzip22503446…/原文37864C7B…双SHA核对，Client gzip95DB1453…/原文E93F7808…核对。owned Client63520退出、resource关闭、guest删除、Render0、页面取消confirmed/listener0；仅owned app revision2/deployment revision4 disabled，XR ServiceRunning/安装保留，Relay选择TOML按87C9→38363D78哈希守卫恢复默认(身份JSON已存)，临时明文node配置删除；全局Relay/90未动。参考三仓HEAD重读/iroh84文件及批准patch核验/C++ownership通过。下一步Company独立目录接收XR/BJ真实视频，保持旧Panel，补正确容器TCP日志；本批开发与失败/通过证据收束，Android排除。

- 2026-10-12 13:22 XR→BJ→本机第一段真实600.008秒完成，正式完整591.358秒窗口35466新帧/59.974FPS；末段frame34046有422.192ms接收/422.293ms新图间隔，不能称零卡顿。启动frame4源911.866ms/接收858.903ms在正式窗口之前，分开保留。源端对应284/284全部接受、maxsend93µs/maxqueue35µs，反馈新鲜而帧进度暂停；Feed5µs只是单次CPU耗时，不能当完整组帧跨度，亦未证明NIC/TCP根因。Client与源gzip/原文双SHA核对，owned Client31532已退出、资源关闭、guest删除、grace20.32秒stopped。新增持续首/末分片时间诊断，媒体测试通过，18jobs Release Client build/dist SHA7B72C204…一致(40文件dist刷新)；第二段13:22:23正式开始，owned Client63520/instanceadafb32f…/resource377f2e4e…，sourceRender6F9…保持，selectedBJ，轻量BJ TCP只读采样并行，不动90/NIC/UDP。页面旧4f6e操作自然succeeded，新owned1f63d82b…运行至约14:00；下一项完成第二段、源gzip双SHA、关联分片/TCP、清理owned与更新结论。MCP完整278.6MB上传均速0.398MB/s/3.186Mbps，和BJ43–50Mbps可靠流不同。用户新授权整包基线后只传变化exe/必要依赖，已写AGENTS，不重复整包传输；本次仅本机日志变化不需远传。

- 2026-10-12 12:38 新候选node-relay-selection-fixed完整覆盖XR成功：Setup58011B21…/manifest967259D7…/314文件全SHA核对/ServiceRunning/Administrator1保持，实际Service636C688E…(target/stage/development-dist/official-dist/installed全部匹配)，Render6F9A2D19…/NodeClient0C9B2695…不变。先前EE9候选误用旧official-stage EE2Service导致新TOML解析不兼容/安装隐藏MessageBox，执行240s超时，owned Setup50080路径核对后终止；失败候选记录保留，未称通过。Rust focused入口新增official目标/发布staging，NSIS服务错误尊重Silent；13项分发回归及7 NodeCore通过。新278631442-byte完整Setup从已核对原56D2候选+4133960-byte差量重建，两端全包SHA58011B21…相等后用完整Setup安装，无手工runtime替换。XR专属配置iroh_relay_urls=[BJ]，原38363D78…/新87C9D9DB…已记录(安装314核对后唯一operator配置变化)，global两Relay不变；Node74884567…generation5/fresh/ready/deployment revision3 ready，GPU8d726d…(PCI39)保持。owned旧页面取消终态confirmed，新页面4f6e…启动/源marker12:38:16；10分钟正在启动实际Client，不能提前计BJ样本。下一项actual selectedBJ+600秒新图/两端源gzip双SHA/owned清理/结果分类与提交，90不再参与，Android排除。

- 2026-10-12 实现节点局部Relay候选选择：network.iroh_relay_urls可选HTTPS源，仅过滤Console授权集合/不引入新Relay；认证与每次Reported更新均经过同一选择，空交集不回退其它Relay，默认[]保持完整集合。NodeCore7项聚焦回归通过(含撤下/重新加入、URL规范化、重复和凭据/歧义拒绝)；18jobs Release Service开发与official focused构建成功，build/stage/development-dist/official-cargo SHA同636C688E72F5FB135A7E7C194344345049449CA7E9DE5397C23DB6FF3D4526C0，development277文件manifest刷新；新完整official-dist314SHA验证。未改C++媒体或iroh vendor/全局Relay；旧XR6F9Render/EE2Service仍运行，准备完整新候选node-relay-selection-20261012再覆盖安装，不能手工换exe。新Rust构建入口默认/显式18jobs，文档已补配置。十分钟仍待新包部署和实际selectedBJ确认，测试页面旧owned操作37b2…取消中，随后重启以保证采样完整窗口。

- 2026-10-12 XR包安装/首帧与路径检查：完整Node3.3.97已装XR/314SHA一致/ServiceRunning/Administrator1保持，Render6F9A2D19…/NodeClient0C9B2695…/ServiceEE2CFCC7…。上传278626254 bytes/699.743s=0.398MB/s(3.185Mbps)，记录20261012_xr_node_setup_upload_speed.json；首次无目录0字节失败保留。新Node74884567…fresh/ready，四GPU省略key被唯一匹配门禁拒绝，绑定PCI168/b84…后网页HTTP200/21ms但CEF共享纹理不能打开/20s无首帧；改为实际PCI39/NVMLGPU0 stable8d726d…后成功出图，非网络卡顿修复。随后Client明确Relay/BJ候选但实际selected仍90Relay，因为源端home90来自全局候选；短样本立即失败清理/15.08s宽限停止，不能当BJ十分钟。两处失败及日志已分文件保留，未使用90机器命令/应用；源端Relay90的短连接诚实标为错误路径。本批尚未完成10分钟，需解决仅XR源端选择BJ、不得修改全局Relay/UDP网卡；下一步检查并实现最小节点局部候选限制，再完整包交付和真正BJ样本。

- 2026-10-12 XR真实Relay视频批次进行中：90不参与。MCP核对Company只有Intel630/旧Panel16936、XR仅本批Client3.3.81/4×RTX4090/Administrator会话1，没有CloudNode；选择XR作为新源、本机Client接收。完整Node3.3.97候选Setup56D2ECAF…/manifestA5497FDF…(314文件)上传至D:\uploadsoft\rd\pixels\relay-video-20261012，首次父目录缺失失败/0字节已确认，创建目录后新ID fb7e33b4…重试；约0.30MB/s(MCP上传，不是Relay吞吐)，尚未安装。installer互斥需只卸载前批owned Client(D25A6043…/9E1E2E54…全身份守卫)再装完整Node，安装脚本已准备。Console新owned设备f16deb06…/node74884567…/app7edfe1e0…/deploymentb89b6da7…独立登记，测试WebView仅本机环回48777动画页，应用10000kbps/宽限10秒，capacity1；原应用和两Relay配置未动。唯一下一步上传完成核对SHA→完整安装/314文件及会话/Service→现有DPAPI Nodecontrol配置→核对实际BJ路径→10分钟真实新图/两端日志；安装前不算视频验收，Android排除。

- 2026-10-12 XR/BJ速度批次完成（90已停止使用）：本机→BJ→XR可靠20秒43.267Mbps/108855296字节核对，反向约50Mbps达到本次发送上限/124993536字节；两个方向数据报1/4/10Mbps顺序测，10Mbps各11353/11353且9.988/9.990Mbps，queue_failures/重复/旧phase残留均0，最大到达间隔49/111ms；不等于单向延迟、视频FPS、Relay最大容量或长期零抖动。首次每包timer实际供给仅~1Mbps的速率无效样本已保留，helper改为实际时间信用/最多32包短批次，产品媒体发送器未改；18jobs Release最终helper91E796B5…本机/XR SHA相同，批准vendor84文件核验。XR全部owned文件现D:\uploadsoft\rd\pixels\nat-acceptance-20261012，旧目录不存在/临时文件同处；安装Client3.3.81/41文件/9E1E2E54…保持。自然BJ自动策略1199/1199+58可靠消息补入core_progress；Company双向跨网络Relay→IP结果已补，90/Company仅LAN。先前游戏样本实为公开IP4613，82窗口18.632FPS/9.94–23.93，不能当Relay媒体；包装器deadline失败保留，Client日志源gzip及双SHA核对，90停用后不再采源日志。XR/本机owned进程0/启动凭据0，319业务sessionsclosed/未结束实例摘要[]，双Relay原配置保持；停止UDP代理原文已归档backup/iroh_stopped_udp_fixture_20261012。报告20261012_nat_and_xr_relay_report.md，身份与清理JSON完整。下一未完成项是更长正常会话及真实并发/热点/规模和未覆盖NAT证据，需使用非90对端；弱网不强求60FPS，Android排除。

- 2026-10-12 最新约束：停止使用90(428358431)测试，用户另有用途；不再启动90应用、探针或做安装/网络策略变化。XR(947158375)的本次owned测试目录已验证绝对路径后整体移到D:\uploadsoft\rd\pixels\nat-acceptance-20261012，旧暂存目录不存在，Client进程0；安装产品与运行时日志仍保留原位置。后续只从本机/Company到XR测BJ Relay实际吞吐、可靠流与数据报性能；不把弱网XR的60FPS设为通过门槛，不重开UDP屏蔽/NIC/饱和混传。已有默认低流量XR两路private Relay往返已通过，但并非速度/媒体验收。XR完整Client3.3.81/41文件Setup BFC20A5D…/manifest D25A6043…/Client9E1E2E54…安装核对完成。原真实应用样本2显示画面但selected公开IP4613且12–30FPS，不能计Relay视频；launch wrapper到期后finally关闭子进程，失败保留。下一项：本机→BJ→XR单通道吞吐测试；90源日志后续不再通过新测试采集。

- 2026-10-12 用户最新范围：停止人为UDP屏蔽，使用XRServer03(947158375)自然网络验证Relay，不将其误当第三台Relay部署主机。XR已MCP验证Windows20348/SYSTEM，172.17.80.25/24→172.17.80.1，D盘可用；低流量helper暂存D:\112233\nat-acceptance-20261012，无产品/服务/NIC改动。已补反向Company→本机600/600 datagram+30可靠消息、90→Company LAN601/601+30，真实selected10.0.0.90与跨网络publicIP分别记录。此前临时Firewall规则因profiles全关闭不生效，规则已删/数量0；单边代理case3及应用case1没有建立UDP、零实际丢包，只作Relay正常样本，不能算回退。MCP中断case2旧命令已原ID确认failed无peer超时，无残留进程。WFP仅查阅官方接口，未实现/安装过滤；强制处理停止。317业务sessionsclosed、90/BJfresh/ready原标志，Client408717B2…/90Node3.3.97/10000revision19不变，Company旧Panel保持。应用case1源11295行gzip470551，压缩5B1A6915…/原文6350DFC4…双SHA已核对。唯一下一步XR↔90/本机默认自动策略实际路径及低流量可靠/不可靠往返验收；NAT类型/长期规模仍只按实际证据报告，Android排除。


- 2026-10-12 NAT新对端批次：Pixels MCP90(428358431)和Company(559038160)均可命令/上传；90=10.0.0.90、Company=10.0.0.16且双向ARP/同网关MAC50-98-B8-7F-D2-7D，二者直连只能证明LAN。本机192.168.31.6另一个网络，Company↔本机的首次30秒实际双端映射：Company10.0.0.16:62485→39.71.45.66:62485，本机192.168.31.6:53024→39.82.198.244:53024。初始selected90 Relay，1秒内双方selected IP，601/601个128-byte datagram、30个8-byte reliable消息校验通过，Endpoint/peer身份全程保持；不是媒体FPS或复杂CGNAT全矩阵验收。新低流量px_nat_probe仅helper新增，18jobs focused Release编译，binary097F43A8…两端上传哈希一致；工作区均D:\112233\nat-acceptance-20261012，无产品安装/服务或网卡改动，Company现有旧Panel16936保持。旧Client408717B2…/90Node3.3.97候选及应用10000/revision19身份不变。证据20261012_nat_topology_inventory.json、20261012_nat_probe_identity.json及nat_company_local_auto_1_{client,server}.log/address.json。下一未完成项：反向发起及90/Company LAN对照、单helper UDP不可用后的自动Relay回退/恢复，然后长稳/容量缺口按原范围推进，Android排除。


- 2026-10-12 04:05 统一卡帧任务S1–S4本轮工程验收通过：direct/90/BJ各3×5分钟共45分钟，59.963–60.020FPS，完整约290秒窗口P99≤50ms；无实际no_output/errors/上传拒绝/日志overrun/秒级新图空档。全部>100ms分类：direct启动137.3ms、direct到达/缺片114.4ms（参考修复17.273ms）、BJ到达102.2ms（source16.044ms/queue22µs/send47µs/收齐2.733ms/Feed11µs/呈现102.321ms）；不作NIC或TCP重传无证据归因。BJ源38397行gzip1947102，压缩00C10B3C…/原文B59547DD…双SHA核对。补真实SDK接收/视频队列decoder注入集成回归，04:00:26.854 frame6观察持续无输出Release、一次IDR、完整配置门禁/第二decoder重新产图，11项SDK通过11.02s；仅测试改动，产品无新变化，修复后物理GPU分支未复现的边界保留。Client408717B2…/Panel8ACEB454…最终build/dist相等，client development40文件验证；90完整Node3.3.97/314文件候选Setup56D2ECAF…/manifestA5497FDF…/Render6F9A2D19…/NodeClient0C9B2695…运行。应用10000/revision19/10秒宽限，其余spec及双Relay原disabled/draining配置保持；316业务sessions全closed，本机无测试Client/Panel。报告20261012_decoder_recovery_acceptance.md/JSON及20261012_mainline_progress.md。主线第0.9节Windows/Linux A–F功能原已交付，本轮补媒体性能；下一未完成检查点为真实NAT/三段十分钟及长期稳定性/规模容量证据缺口，Android仍排除，停止的NIC/饱和混传不重开。实现及失败/通过证据已提交并push master：5fe4b8ae8（远端从c449357a3推进）；本轮工程验收和主线分析完成。


- 03:29 S3/强制90 Relay三段五分钟通过：60.017/59.963/60.008FPS，完整290.562/290.509/290.493秒P99≤50ms，0次>100ms交付/呈现、无实际解码无输出/错误/上传拒绝/数秒空档。每段实际selected Relay https://39.71.45.66:4605/，connected/480输入/宽限停止，原desired_draining及disabled标志finally恢复；同PID TCP统计三份CSV已gzip存档，不能从零字段反推物理无丢包。源38437行gzip1939516，压缩C09BBBF3…/原文1287999C…双SHA核对。产品和10000/revision19没有新修改，本机Client408717B2…/90完整包NodeClient0C9B2695…/Render6F9A2D19…保持。现在BJ三段五分钟（source collect bj-1）运行；只剩这条正常路径与最终证据/交付收尾，本任务仍未结项，新无输出分支实机未触发的限制仍保留，不称已观察该分支。

- 03:10 S3/direct三段五分钟指标通过：60.016/59.991/59.998FPS，完整有效交付290.496/290.552/290.573秒、P99≤50ms均通过，无真实解码无输出/错误/上传拒绝/数秒空档。初90秒数值P99 31.748/31.568/31.204ms单列。第二段初显后164ms发生137.3ms交付（首次GPU上传86.336ms、无对应长呈现），第三段114.4ms迟到伴7059缺片/7065修复17.273ms；源7065请求后10.993ms生成、准入35µs/全部12片本地接受4.529ms，相邻窗口max_send5.116ms/max_queue80µs，未将迟到延长成数秒，具体链路节点不作网卡归因。详细分类20261012_decoder_recovery_acceptance_direct.md。源38430行gzip1664846，压缩2909E108…/原文012DC44F…双SHA验证；全部connected/480输入/各实例宽限退出。SDK生命周期套件在样本后18 jobs构建通过10.75秒；产品无新修改、预算10000/revision19不变。本机Client408717B2…，90Render6F9A2D19…/已装314文件完整包；安装结果CED1D924…SHA验证。现在运行强制90 Relay三段五分钟并采同PID TCP ESTATS（source collect90-1）；双Relay标志每段finally恢复，尚未完成S3/S4，下一步90异常分类/源压缩核对，再BJ。

- 02:50 decoder-output-recovery候选完整安装90成功：Setup56D2ECAF6FC1A37D7631A69615B41AD57754B225189C9F752DE587B617BAD293，manifestA5497FDF…，3.3.97/314文件全SHA验证，ServiceRunning，Windows会话1/2/3保留。工作区D:\112233\decoder-output-recovery-20261012；Render6F9A2D19…未变，NodeClient0C9B2695…包含新SDK恢复，本机developmentClient408717B2…/Panel8ACEB454…构建/dist匹配。新增3项恢复回归及encoded_video_delivery通过，所有权/命名通过。修复后第6/7段短丢包未再发生数秒空档（最大呈现167.681/162.878ms），但均未触发no_output持续状态，不能声称观察了实机重建分支；源码策略及限制详见20261012_decoder_output_recovery.md。第7段60×40ms、1718报文丢失/路径127.0.0.1:58908；源gzip8F1C8BE4…/原文BF344CB0…核对；应用已恢复10000/revision19/其余spec保持。307个业务session均closed/双Relay fresh原标志。正在S3三条路径各3×5分钟正常采样，首先direct batch（source collect Case direct-1）；只有指标及每个异常分类通过才进入S4结项和主线进度分析。

- 02:33 第5段频繁丢包已复现并定位旧长空档：75秒owned UDP测试一直走127.0.0.1:61647，实际丢包与窗口数见20261012_controlled_loss_proxy_5.json（不再被路径迁移绕过）。02:30:51.138真实解码frames93/attempts271/no_output178/errors0，02:30:56.149为frames239/attempts300/no_output61/errors0；连续239次已接受输入但没有图像，02:30:52.177呈现gap4264365us。完整帧交付最大120.3ms，上传无拒绝，54.78FPS不通过。这次有直接产出证据，确认持续无输出的解码状态延长了网络丢包后的停顿；不是显示线程4秒阻塞。源11332行gzip666034，压缩545DA734…/原文DAAC03E7…双SHA核对；临时revision14/20000已恢复revision15/10000，connected并停止owned流程。正在实施DecoderOutputProgress：持续接受≥4输入且100ms无图像才重建解码器并立即请求带配置IDR；单次EAGAIN和网络无输入不触发，门禁在请求后限频，按monitor及重建/断线清理状态。下一步18 jobs编译/测试及dist哈希，同一故障再次验收；不能在修复实测前宣称成功，S3/S4仍未通过。

- 02:29 decoder_output/direct第4段原20000kbps五分钟完成：60.00FPS、0次交付/新图呈现>100ms、初90秒P99 32.261ms，无真实解码无输出/错误/上传拒绝，未复现旧4秒空档。resource0e03ed89…/instanceb43125aa…/Client46612，connected/480输入/宽限11.82秒停止；revision12临时20已恢复10000/revision13，全部其他spec保持。源12798行gzip582986，压缩8A648F96…/原文791A56F0…双SHA核对。继续第5段75秒频繁短丢包诊断：仅owned启动描述本地副本bind_address=127.0.0.1:0，避免自动迁移绕过proxy；不改机器/产品持久地址或业务鉴权。实际selected127.0.0.1:61647前三次采样持续，暂未结束，不算验收通过。下一步核对实际丢包计数和真实产出/完整恢复分片，必要时再定位旧长空档，S3/S4未通过。

- 2026-10-12 02:21 接续：paired owned UDP 故障第2段确实丢159个报文，最大交付202.1ms/呈现203.931ms，59.66FPS，真实解码无无输出/错误/上传拒绝，未复现4秒空档；源gzip 0E8544E5…/原文7A1A83C7…核对。第3段尝试频繁短丢包，但路径在约第10秒从127.0.0.1:50974迁移39.71.45.66:4613，proxy只转发6203下行/3352上行、实际丢包0；因此60.06FPS/0次>100ms只能算正常对照，不能作为故障验收。源gzip6EFE3C28…/原文A35F21E7…双SHA核对；临时20000/revision10已恢复10000/revision11，其余spec保持，connected/宽限14.58秒退出。身份仍Client11C05AB3…/Render6F9A2D19…，未编译/改网卡或Relay。下一步直接以原20000预算做一段五分钟真实产出诊断（probe4自动恢复10000，读预览后启动），复现旧4秒故障并核对解码产出/上传/呈现阶段；S3/S4尚未通过。

- 有界20Mbps产出诊断120秒通过但未复现旧问题：0次>100ms/60.03FPS、真实产出=attempts/no_output0/errors0，已恢复应用10000kbps/revision9（临时20为revision8）。首次配置切换立即start得到503且未创建实例，已留失败记录/revision6→7恢复；第二次读预览等待deployment_revision_mismatch消失后启动，不修业务模块。源12643行gzip977834bytes，压缩EB84FEAE…/原文1835BF28…双SHA核对。随后仅owned连接的本地UDP转发器75秒实测：实际selected Ip127.0.0.1:58107，53010下行/28265上行，三次40ms窗口丢>=900bytes的13/17/12个加密报文，确实触发三次RFI，参考等待44.975/27.593/18.281ms，真实解码产出均有/无上传拒绝，未复现4秒空档；一次109.1ms交付对应第一次人为40ms丢包，不算常态性能门槛。退出宽限14.52秒后proxy记录WSAECONNRESET（UDP上游端口关闭通知），测试已connected完成/worker及双socket关闭；后续工具明确记录这种通知并继续，不静默当成功。源11201行gzip493461bytes，压缩73FEAB4A…/原文F89AD5A3…SHA核对。身份Client11C05AB3…/90 Render6F9A2D19…/两Relay配置保持，无新编译或NIC更改。下一步同隔离连接成对40ms丢包、第二次针对首个恢复帧，核对恢复帧再次丢失和实际画面产出；S3/S4仍未通过，不能将未复现冒称4秒已修复。

- decoder_output direct/10000kbps五分钟完成（01:45:45.254–01:50:45.255）：本机Client11C05AB385077168FADDD6C0E8F9AED6D77E3849D6751E001B13F34BE1552362/Panel8ACEB454…build/dist核对，90仍stutter-unified完整包Render6F9A2D19…。真实解码frames=attempts/无输出和错误0，未有上传拒绝；59.95FPS、两次交付151.3/105.7ms及对应呈现，未复现长显示空档。connected/480输入/宽限11.23秒停止；初90秒P99 33.113ms，完整聚合以summary为准。源12778行gzip549140bytes，压缩9155FAC6…/原文2D3CCDA9…双SHA核对。这是10Mbps对照，不能冒称20Mbps故障已修复，当前仅本机增加解码统计/上传拒绝日志，Node自带Client尚未补诊断。下一步有限120秒20Mbps复现4秒空档并自动恢复原10000spec，借真实产出与上传判定环节；不改网卡/双Relay，不重开饱和混传。S3/S4未完成。

- stutter_unified direct第一段（01:34:16.387–01:39:16.387）失败：同候选0759A9A0…/Render6F9A2D19…/原20000kbps，5次交付176.6/164.7/213.5/171.9/155.3ms；另两次实际新帧呈现4.336/4.283秒空档，窗口flags0且无相应UI调度/原生阻塞或swapchain_acquire事件。聚合呈现56.74FPS不通过，完整290.517秒16916交付间隔中142个>50ms（0.84%，完整区P99≤50ms）；初90秒P99 53.053ms单列，不能混用。原生caption924ms未导致对应显示暂停。核对ThunderSdk发现旧LAT-decode.frames其实统计Decode调用（无输出TRError(0)也计数），先前“解码正常”结论无真实产出证据，正式修正；新增真实产出/attempts/no_output/errors及有界上传拒绝诊断，18 jobs本机发布中。源13474行gzip616177bytes，压缩4A3CBB2E…/原文BC522D9D…双SHA核对，connected/宽限11.43秒退出。按用户90约10Mbps方向，测试应用8ebdbd8e…从20000改10000/revision4→5，其余spec及access_revision完全保持，记录20261012_stutter_application_budget.json；这是持久的单应用配置变化，未改全局、WebView或网卡。下一步同10Mbps直连诊断实际解码产出与上传，不盲目扩大缓冲、不将网络诊断计数当帧显示；S3/S4未完成。

- stutter-unified完整安装90成功：Setup710967A1…/manifest34936783…、Cloud Node3.3.97/314文件SHA核验/Service Running，Windows用户会话1/2/3及工作区保持。已安装Render6F9A2D19758F386303BFC378C7F07D285DFBD8A57AE9CA29A1C98643B4A97D5E、NodeClient20F6088A1FCD6EEB4A9C1A38647D8674FE67CD70383C5C284DFF8191F589878F、NodePanel4FA73690…、ServiceEE2CFCC7…；本机Client0759A9A0…/Panel8ACEB454…开发dist/SHA相符。安装任务17513182…succeeded/0，结果下载SHA7D82318E…核对。297业务会话全部closed/双Relay fresh/原标记保持，应用20000kbps。当前开始最终候选direct第一段五分钟（series stutter_unified），完整源gzip与双SHA沿用；不与编译/传输重叠。原4秒显示空档仍无已证明原因，不能仅凭SUBOPTIMAL修复结项，接续三路径稳定性及窗口状态证据。S3/S4未完成。

- stutter-unified完整候选已18 jobs串行构建：五份development build/dist SHA相符，本机Client0759A9A0F5CD9DF4F4BD250A0D9D8AFEAE254F64BAA271C859CB0D07E6361C76/Panel8ACEB454…；Setup710967A1F2B84E262B40D3BA82A53636B857E429DBB48AB40D7EF50127355AC2、manifest34936783784ACA5C0DBEA49BBEE57AE0278F33D3818B71B698C5B9168E7E383D，候选Render6F9A2D19…与recovery-audit相同（发布配置无媒体算法变更）。包含原生移动/缩放持续绘制、SUBOPTIMAL图像消费修复及接收五秒完整聚合>50ms计数；三UI回归/所有权及命名通过。安装前297业务会话全部closed，双Relay fresh/原标记保持；应用20000kbps不变。正在上传D:\112233\stutter-unified-20261012，尚未完整安装或通过S3/S4。下一步安装后最终运行身份与三路径验收；不重复未改媒体模块已通过58项。

- recovery_audit 90 Relay第二段五分钟完成（01:17:40.681–01:22:40.694）：59.99FPS、3次交付129.2/103.5/200.4ms；对应TCP重组队列6221/6221/31480bytes、重复ACK3566→3622/3622→3669/4932→4992，应用接收队列均0/统计API错误0，source RTP间隔17.355/16.366/16.644ms，组帧11/23/15us及上传年龄146/640/151us。这三处明确为TCP连续字节到达前等待，不是组帧/显示额外暂停；第四呈现100.724ms对应交付99.838ms（边界），没有4秒独立显示空档。480输入/connected/宽限12.35秒停止/原双Relay标记恢复；初90秒P99 26.260ms，overruns0。源12805行gzip491557bytes，压缩BE5E2E74…/原文2E24E7CD…SHA核对；Client仍29A13FD1…/90 Render6F9A2D19…/应用20000kbps不变。只读固定ImGui SDL3 Vulkan示例明确SUBOPTIMAL仍须提交已取得图像，项目原提前return形成实现缺陷，现修复并加入有界重建原因日志；不冒称已证明它导致旧4秒空档。18 jobs Client/Panel已发布/标题栏及三UI回归通过，准备全部运行身份与最终候选同步。接续完整包及同候选三路径/窗口验证，S3/S4未完成。

- recovery_audit direct第二段五分钟完成（01:10:04.840–01:15:04.841）：Client29A13FD16156D433E4601D59E970CFCA024FE8AFE2E4DAEF3401B60F228A79EA/Panel8ACEB454… build/dist SHA相符，90 Render仍6F9A2D19…完整诊断包。0次>100ms交付/呈现、新帧聚合59.98FPS、初始详细区P99 41.770ms、overruns0；480游戏输入/connected/宽限12.33秒停止。窗口flags仅启动hidden→visible，未复现4秒空档，不能宣称额外窗口状态日志修好了它。四分钟内丢片后首张恢复10177：源request_age13.306ms、发送本地11.202ms、客户端23片数据齐后Feed22us/参考等待56.613ms恢复，全部28片收齐，首次RFI本地29us。源12827行gzip1021244bytes，压缩C6633992…/原文FD2019F9…双SHA核对。应用20000kbps/双Relay持久标记不变；接续同候选90 Relay正常五分钟及显示空档状态定位。S3/S4未完成，保留前段失败，不用单段通过结项。

- recovery_audit direct90秒完成（00:59:09.255–01:00:39.262）：ClientE4F04795…/90 Render6F9A2D19…、实际Ip39.71:4613，480游戏输入加0.94秒原生caption拖动/connected/宽限11.76秒停止。SC_MOVE|HTCAPTION调用919ms期间对应呈现窗口最大44.720ms，证实live-resize已避免该窗口操作停止绘制。整段仍未通过：2次交付185.7/113.8ms，另4.280秒纯呈现空档，接收和解码正常；没有WM_SIZE记录或慢UI调用对应，缺窗口状态，不能宣称是最小化。含显示空档聚合57.10FPS，P99早期交付32.414ms，overruns0。wire3924/encoder4114只看到4/5data+1parity中的4片，缺data4及parity，RFI即时发出，恢复帧4120在12.207ms生成、客户端参考等待19.030ms后恢复，不能把113.8ms全归于本地组帧。源日志12586行gzip590482bytes，压缩0AFABF11…/原文131A51A2…双SHA核对。新增SDL实际visibility flags及UI迭代间隔诊断，18 jobs本机发布中；尚未更新完整Node包，源Render身份保持。接续4秒显示空档的实际窗口状态与五分钟恢复帧证据，S3/S4未完成。

- live-resize本机Client/Panel已18 jobs构建、dist发布/SHA相符：ClientE4F047959CA4DC208FD08D44252B4AD266401F4B926E2CBF447A1F13EDE7FAE9，Panel0E2610E88523A79F2406824B16F5ACE12C9934AD1A6C8CE5856681C374EA08FA。回调线程过滤、重入、派发中注销/销毁、三次注册生命周期及呈现计数、标题栏最终三项通过；原全局SendInput标题栏失败和复测均保留，改owned消息后稳定通过。90保持recovery-audit完整安装Render6F9A2D19…，Node自带Client尚为诊断包，未安装live-resize产品包。90秒direct（series recovery_audit/segment1）正在进行有限原生标题拖动；已确认message274 parameter61458=SC_MOVE|HTCAPTION、161 parameter2，原生调用919ms但对应呈现窗口最大间隔44.720ms，视频没有随调用停止，尚不代替整段/长稳验收。应用20000kbps/双Relay/网卡配置未改，没有编译或大传输与采样并行。下一步结束采样、90源gzip双SHA核对后做恢复帧全程对齐；S3/S4未通过。

- recovery-audit完整候选已安装90：Cloud Node3.3.97/314文件核验/Service Running，Windows三组会话及ID保留。Setup45BF296958845895FDF59E225CFC1A678C8DC147CC2511FEE24A09DBD3CCD3E2、manifestD5B606330EDE0F8B2587A5A31A42C7F0D3FC4A21615D4E6B2BDBAFAB3E521318、安装Render6F9A2D19758F386303BFC378C7F07D285DFBD8A57AE9CA29A1C98643B4A97D5E；本机诊断ClientBD3DF0FA…/PanelA1E4A93A…，五development build/dist SHA相符。第一次上传因远端目录未建立确认失败，安装脚本未找到而未运行Setup；建D:\112233\recovery-audit-20261012后从原失败操作续传成功，Setup完整安装succeeded/0。安装结果下载SHA00BB76F8…核对，294业务会话closed/双Relay fresh。另根据只读SDL_OnWindowLiveResizeUpdate核对具体窗口机制，项目新增UI线程live-resize expose重绘及弱引用/RAII注销，正在18 jobs专项构建，尚未发布/实测；诊断Node安装不包含该显示修复。应用20000kbps/双Relay及网卡配置保持。下一步显示回调安全回归及本机Client发布后，在直连正常视频中追踪恢复帧并做有限原生拖动检查；S3/S4未通过。

- 2026-10-12 用户授权统一实施直到验收成功，完成后再分析主线推进。当前补齐90秒以后的恢复帧证据：发送端非预测帧保留offer/逐片本地接受，接收端有界64帧元数据/分片覆盖，完整正常帧在初始诊断期后不逐帧输出；恢复/未完成帧给出编码号、kind、缺失data片、完成/交付结果，first shard缺失时不编造身份。每20ms最多清理一次历史；新增三项覆盖四分钟后恢复/重复片、缺描述头后FEC身份补齐、容量边界/重复shutdown。58媒体及标题栏回归通过；标题栏增加wParam和WM_SIZE状态定位显示空档。仅诊断改动，18 jobs串行构建development五运行文件及recovery-audit完整Node候选，尚未安装/宣称性能修复。原Render93183AF1…/应用20000kbps/双Relay配置不变。下一步完整Setup部署后核对首张恢复帧交付，S3/S4未完成。

- 2026-10-12 UDP direct第二段完成（00:15:04.583–00:20:04.589）：两端实际Ip39.71.45.66:4613/39.82.198.244:58649，480输入/connected/宽限11.87秒停止，294业务会话全部closed/双Relay fresh及原标记保持。普通接收59个五秒窗口平均59.70FPS，7次>100ms交付（120.1/101.2/136.0/251.7/193.7/162.9/108.0ms），本段失败，不能沿用首段0次宣称已稳定。另有4.250秒纯呈现空档，同窗接收/解码仍60FPS，无慢UI调用对应；可能涉及最小化跳过绘制但缺窗口状态日志，尚未定因。含该空档聚合呈现58.84FPS，不能作为UDP接收FPS；log_overruns=0。最大251.7ms中参考等待135.682ms、首次RFI立即发出，源恢复帧15699在28.396ms生成，但客户端后来收到15705才恢复；该链仍需核对恢复帧本身的交付，不盲改RFI定时器。源日志12849行5939844bytes先gzip928892bytes，压缩A0B3B147…/原文4CFA82F6…SHA均核对。身份仍Render93183AF1…/本机ClientA32733C0…，应用20000kbps/网络配置不变，无编译、采样或传输残留。证据20261011_direct_continuity_direct_2_*及20261012_direct_continuity_render_direct_2.log.gz；下一未完成项为对齐本段两类停顿和15699恢复帧传输，不继续重复跑一段期待偶然通过，S3/S4未完成。

- 2026-10-12 接续汇报：UDP direct 最新 final_loss 五分钟为 59.98FPS、0 次 >100ms 交付/呈现空档；上一 media_logging direct 五分钟仍有 8 次/max241.7ms，失败证据保留。直连改善但三段长稳门槛未通过，不把 Relay TCP 故障混算为 direct。当前应用仍 20000kbps，无新增带宽/网卡/Relay 持久配置变更，后续编译固定最多18 jobs。

- tcp_path 90 Relay 五分钟（23:57:11.283–次日00:02:11.294）完成：480输入/connected/宽限11.34秒/双Relay标记恢复，59.68FPS，1次129.156ms交付空档、3次呈现空档。frame9203源RTP间隔16.655ms/组帧25us；客户端同一时钟42.261秒TCP重组队列38384bytes、应用接收队列0、重复ACK727→739→770，42.369重组队列清空，明确为TCP连续字节交付前等待。源主要连接同期快速重传13→14/重传bytes增加，跨机时钟估计+6965±123ms，只作关联，不冒称逐包精确因果或网卡归因。两端主要连接2800/2975有效ESTATS样本、API错误0；临时EnableCollection均恢复。部分零计数可能不支持，不能推导无丢包。两次独立原生消息WM_SYSCOMMAND(274)/WM_NCLBUTTONDOWN(161)嵌套688/889ms解释纯显示空档，尚未记录wParam，不宣称具体系统命令已证明。源日志gzip850396bytes/源CSVgzip68855bytes先压缩下载，压缩及原文SHA均核对。无采集/编译仍运行。

- 当前身份：90仍final-loss完整Cloud Node3.3.97/314文件，Setup A298C0E9…/Render93183AF1…；本机native-message development Client A32733C0…/PanelDDD320AD…，build/dist SHA一致。新增只读TCP统计/原生消息诊断，无新媒体算法交付。下一未完成项：对照Sunshine及只读iroh Relay实现检查发送批次/节奏，并继续当前候选UDP direct有效长段；S3/S4未完成，不以最近低故障时段掩盖此前90 Relay88次失败。

- native-message诊断Client/Panel已经18 jobs focused构建与dist发布：Client SHA A32733C073353200053351354C18D5ADB5ECDAD5E8C0400ED30356AA93809368，Panel DDD320AD811F5D150C70FC4B280BBAFBA08142A40963C013ADBD3BF11DF9F446，build/dist均相符，标题栏回归通过/所有权与命名检查通过。无新Node安装，Render仍93183AF1…，未开启Rust trace；正执行90秒普通90 Relay视频定位半秒原生事件等待，源日志仍在90 gzip后下载。只新增耗时日志，尚未宣称修复。

- Relay actor只读诊断一分钟（23:37:42.290–23:38:42.298）完成：相同final-loss Node/20000kbps，实际90 Relay，480输入/connected/宽限11.18秒/双Relay标记恢复；0次>100ms视频交付，但3次纯呈现空档504.715/487.786/506.419ms，阶段日志确认SDL事件等待实际503.113/470.540/505.415ms，而请求等待仅16ms；composition402–662us/draw604–1024us，不能归为网络或Vulkan绘制等待。34,259条actor消息/5.15MB trace，trace同步写入故仅诊断、不计稳定性验收；源9135行先gzip604229bytes，压缩CC03DB34…/原文15C27C1C…SHA均核对。新增项目WndProc范围的慢原生消息日志，正在18 jobs focused Client/Panel构建及标题栏回归；Node完整安装仍A298C0E9…/Render93183AF1…未变。下一步核对SDL事件泵内的具体Windows消息调用，再继续Relay数据到达前等待；S3/S4均未完成。

- 接收头记录有效性已纠正：final_loss 90第二段SACK分析显式标注receive_data_observed=false，不能以0 SACK推出无丢包；三个极小HTTP探测也只有Tx。新的memory抓包命令被自动审批拒绝，仅返回blocked by policy，未执行；继续用现有捕获和应用内日志，不改变网卡/驱动/VPN。90当前10.0.0.90，Render到本机公网Relay连接经39.71.45.66，Relay看到Render源39.71.45.66及Client源39.82.198.244；这证明公网回环路径存在，尚未证明它导致卡顿。

- final_loss 90 Relay第二段（23:03:45.902–23:08:45.909）失败：480输入/connected/宽限11.97秒/双Relay原标记恢复；88次>100ms交付/max274.6ms、87呈现空档、平均呈现57.22FPS、初90秒P99 88.634ms、log_overruns=0。两端Snapshot均Relay90。同步TCP4605/96字节NIC记录31445包/ETW无lost，但解析确认仅见本机→90，**没有接收方向**；TCP ACK号实际前进约103MB，不能把0个SACK报告当作双向捕获或无丢包证据。pktmon已停/过滤器清除，ETL/pcapng在.cache；正在用IP过滤的少量HTTP只读请求检查采集点/过滤行为，不改网卡或驱动、不做压力。源12692行gzip453869bytes，压缩8FCBC79F…/原文7D8F4A0B…SHA均核对。当前候选/应用20000kbps不变，S3未通过，接续真正双向TCP证据和源端转运定位。

- final_loss BJ强制Relay第一段完成（22:54:42.260–22:59:42.261）：实际49.232.233.61:4605/480输入/connected/宽限11.25秒/双Relay原标记恢复，0次>100ms交付或呈现，新帧呈现59.98FPS、初90秒P99 39.540ms、log_overruns=0；首次upload89.082ms，无运行中纯UI停顿。源12792行先gzip520809bytes，压缩278118CF…/原文E170F4CC…SHA核对。三路径各首段完成，尚不是三段长稳；90五次101–137ms转运空档下一段加入本机TCP4605/96字节被动NIC头记录，核对SACK与空档，网卡/驱动/预算不变，不重开压力专项。候选及运行身份同上。

- final_loss 90强制Relay第一段完成（22:44:41.626–22:49:41.626）：480输入/connected/宽限12.31秒/双Relay原标记恢复；5次>100ms交付和对应呈现，max136.8ms，新帧呈现59.91FPS、初90秒P99 24.350ms，log_overruns=0，无RFI。五次source RTP gap16–17ms、Feed20–40us、upload_age<1ms，均为分片到达前转运等待；首例wire2579整帧17片在76us内集中到达，此段没有TCP头记录，不能把旧SACK证据当作这五次的物理丢包定论。首次UI upload1963.540ms单独保留，不掩入常态FPS。源日志12895行/5825491bytes先gzip895276bytes，压缩C1F271C0…及原文EF836AEB…SHA均核对；同final-loss安装/应用20000kbps。接续同候选BJ Relay，S3尚未完成。

- final_loss direct第一段五分钟完成（22:36:30.650–22:41:30.651）：480输入/connected/宽限12.37秒停止，实际Ip39.71.45.66:4613（源选39.82.198.244:54545），0次>100ms交付或呈现空档，新帧呈现59.98FPS、初90秒P99 32.151ms，log_overruns=0；唯一UI慢操作首次upload154.902ms。没有RFI，不能证明丢包恢复改善或用不同公网时段宣称因果。源日志12815行/4761506bytes先gzip597013bytes，压缩SHA D77340E3…与原文8FFBB6FB…均核对；本段通过但三段/路径门槛尚未达标，90/Client安装身份同上，应用20000kbps/两个Relay未改。接续90强制Relay第一段。

- final-loss完整安装90成功：Cloud Node3.3.97/314文件SHA核对/Service Running，三组Windows会话及ID保持；Setup A298C0E9…/manifest91B3FC51…，安装Render93183AF144C7F7702040E8DB013539AD9D6BDC842C4C914934B2C895BE00B76A，NodeClient512782ED…/Panel76875724…/ServiceEE2CFCC7…未变。local Client A1278F07…，开发五文件SHA相符。首段五分钟direct（series final_loss）开始，包含480拖动输入；全部编译/上传/安装完成，当前无编译或大型传输。Setup完整安装任务c5fbc88f… succeeded/0，结果文件下载SHA675DDD7F…核对；来源日志待段结束后在90gzip/双SHA收集。应用20000kbps/Relay配置未变，S3仍未通过。

- final-loss构建完成：23交付/恢复、Moonlight oracle、55媒体通过，命名/所有权通过1454变更行；五份development build/dist SHA相符，local Client A1278F07BBD8BABF1F9432798CD02EE60FFBD53AC0EEDAFC2A20730B25C48A43，NodeRender E50135C5…/NodeClient E953BA74…，两Panel未变。完整候选3.3.97/314文件，Setup SHA A298C0E967AF36943DECE83E2BCB52A00FFE094A654D2C2A67545A1F263E1D65，manifest SHA91B3FC5171C4F9E34743AC6DDC04A15631224F9E2C50224A8BCD12206B67ECA9；正在上传D:\112233\final-loss-20261011，90仍media-logging旧完整安装。所有编译明确18 jobs；没有并行性能采样。安装前286业务会话closed/两个Relay fresh/未排空，应用20000kbps不变；后续完整Setup安装再做direct。

- 真实到达记录回放5218帧/29不完整块，未发现下一帧之后仍能救回的旧块，暂排除扩组帧队列。对比当前Moonlight core e95feaf…确认项目在不可恢复后仍等待下一完整帧才发RFI，两例多等15.095/15.552ms；新增独立final_frame_loss覆盖预测telemetry省略最终通知的遗漏，保留FEC预测可恢复行为。两处旧失败及FEC测试配置初失败已保存；交付/恢复23项通过，正在核对upstream oracle。尚未交付或实网宣称修复，90仍media-logging完整安装/Client E03B514D…，双Relay/应用20000kbps不变。后续编译明确18 jobs已写AGENTS及当前helpers，gzip双SHA收集不变。接续focused构建及真实direct对照。

- `media_logging` direct五分钟第二段完成（21:58:15.488–22:03:15.489）：480输入、connected/宽限11.83秒停止；8次>100ms交付/max241.7ms，新帧API呈现59.08FPS，初90秒P99 45.059ms，log_overruns=0。首次上传153.312ms（启动阶段），另UI wait79.259ms；没有再现444ms纯显示冻结。头几十秒接收45–51FPS，后多数窗口60FPS；源端实际编码约7.8Mbps/媒体包约10.3Mbps，并非达到配置13Mbps编码上限，发生RTT增长和连续QUIC loss后自适应降至4.26Mbps，再恢复。不能把时间上的恢复冒称为日志修复的效果。源码/安装身份同上批，双Relay配置不变；源日志12629行gzip878753bytes，压缩及原文SHA均核对，证据prefix20261011_media_logging_direct_2与render_direct_2。继续对齐八次空档的未发送等待/丢片/参考修复；S3仍未完成。

- `media-logging-20261011`完整安装90成功：Cloud Node3.3.97/314文件SHA核对、Service Running、三组Windows会话/ID均保持。Setup SHADE1A1E13DC054480437BC249CE1238073D7B2E1490DEA20260FCD18C97937E00，manifest SHA5E7340B279645EFDD60C4E63FCAD68A1919E749BBDFFA97AC5B02CAC15E28FF1，安装Render SHA581E0B9BEDBC7598DFB24B16E908A4F658FAD697C7A7E1E35D2326A458A4632F，NodeClient SHAC4322A5F…/Panel SHA76875724…/Service EE2CFCC7…未变。五份development build/dist SHA相符，local Client E03B514D3DBE10C6F29D375C083333468187E46641FF94F4F9B40EAA434CD452，包含clipboard/snapshot/upload/toolbar慢阶段日志。3日志阻塞/排空/重启、55媒体、19交付/恢复通过；安装前285业务会话closed/双Relay fresh，应用20000kbps/Relay软件/网卡配置不变。当前direct第二段五分钟（series media_logging）开始，全部编译与包传输已完成；不与性能采样重叠。日志后续在90先gzip再下载。当前仍未S3通过，不把日志风险修复等同于原始链路问题解决。

- 媒体日志阻塞复现与候选修复：实际Logger注入阻塞sink时，旧同步写入导致producer在100ms检查点仍未返回；Client/Render改用spdlog已有有界8192异步非阻塞队列，文件worker停住且16384条日志将队列填满时，producer仍在500ms门槛内完成（队列溢出计数非零）。三项回归包括旧行为、队列饱和、shutdown排空和三次重启均通过，命名/所有权通过。五秒源端/路径/新帧呈现记录加入log_overruns，不能将丢诊断视作完整证据。仅媒体进程启用异步模式，其余日志保持原初始化；原文件及未提交内容已归档backup/media_logging_20261011。当前进行focused Release development dist及完整media-logging候选构建，尚未安装90/实测，不能据此认定之前444ms的真实根因。

- BJ `relay_repair` segment2完成（21:17:26.984–21:22:26.985）：480输入、connected、宽限12.31秒停止/双Relay原标记恢复；26次>100ms交付/max242.3ms，聚合新帧API呈现59.04FPS、初90秒P99 45.704ms，仍未达到稳定性门槛。UI阶段慢记录只有首次纹理上传78.917ms，本段未复现此前444ms纯显示空档，不能宣称其已修复。源端日志12741行先gzip后下载501101bytes，压缩SHA9A4AF746…/解压原文SHA4A35EE1D…均核对。接续核实共享同步日志写入/每条fflush能否阻塞媒体线程；这是具体实现风险，尚未作为实网停顿根因。

- `relay_repair` 五分钟90强制Relay结果失败（21:02:17.330–21:07:17.341）：480拖动输入、保持connected、113次>100ms交付/max576.6ms、新帧API呈现55.26FPS，初90秒交付P99 79.014ms。短分片误发RFI已消除，不能因此宣称性能通过。Client仍为46F83A86…，90仍为relay-probe完整安装，应用20000kbps未变。末段仅TCP4605/96字节NIC头记录中，主连接有44个接收SACK序号缺口，最长补齐约258ms；存在与视频空档重合的明确TCP等待，尚不能指认物理设备或解释全部停顿。pktmon已停止/专属过滤器移除，无WPR/Rust trace运行；双Relay原标记恢复，owned实例按宽限11.89秒停止。接续BJ对照（segment2）用于定位已观察到的纯UI停顿，远端收集器现已改为先gzip并返回双SHA再下载；不重复相同90性能失败。

- Relay分片短暂停顿修复已通过19项交付/恢复、55项媒体测试；按实际selected path每250ms更新部分帧idle恢复策略，Relay保留真实参考失效和4秒静态watchdog、direct保留100ms；路径恢复切换回归通过。local Client已同步dist/hash46F83A86053894A60805BCA045409D40ADF56EA16C2EF97F7620F5AB5D00723A，90保持relay-probe完整安装身份，双Relay软件/应用20000kbps不变。本次五分钟90强制Relay（series relay_repair）开始，含UI慢阶段记录；没有并发编译、WPR、pktmon或Rust trace，阶段修复尚未宣称实机通过。

- `relay-probe-20261011`完整安装90成功：Cloud Node3.3.97/314文件SHA/Service Running、Windows会话保持。Setup SHA7631CBC63AEB82AE29086FDE945740836BF72ABC05CCE231244546E37B911FAB，manifest SHA5D1B5E0BA132062FF5E701B93241DBAAABD2FF5847D91BD7B7E58418E1AE5B8A，Render SHA525EEA34BE49E067BB94CF8781D8E913441B062689D9227C024A543E672EE0E9。安装前283会话closed/双Relay fresh。源码又推进了有证据关联的修复但尚未交付：90 Relay 615ms空档的ETW接收线程最大Ready等待69us、正常10–13ms轮询；解码线程614903us等待与接收空档一致，非该事件CPU饥饿。另确认partial idle 100ms的RFI invalid723在原723完整/参考有效到达前83ms误发，源端后生成740恢复帧24955字节；42次恢复请求可能放大队列。正在取消Relay短分片停顿的猜测性修复，保留实际参考缺失与4秒watchdog，direct100ms机制保留，另加路径切换回归。随后实测本机UI阶段日志及90 Relay。

- 同候选五分钟BJ Relay第一段完成（20:35:47.630–20:40:47.631）：真实BJ路径/480拖动输入、交付2次>100ms(max117.1ms)、初90秒P99 25.228ms、聚合新帧呈现59.92FPS。另1次纯显示443.650ms空档，同窗接收60FPS/max27.1ms、解码max1085us，不能归因网络。已加UI wait/events/composition/draw和纹理upload慢调用日志，local Client已增量构建同步dist并SHA相符9f854727efddf0f19234e0afed1bcb785379e31f8087541de0c2126629bfd3c6；节点仍为原partial-repair-final候选。复现Relay队列恢复后两秒25%码率探测、短新压力仍再次探测的旧失败保留；统一近期loss/queue/backpressure恢复为5秒后10%探测，健康高RTT/配置上限变化保留正常恢复，55媒体测试通过。新源码尚未部署；接续UI阻塞定位和更新节点后Relay对照。ETW无lost事件，但CPU.Light没有DPC/ISR，不能据此排除驱动停顿。

- 最终候选五分钟90 Relay第一段完成（20:27:15.813–20:32:15.814，480拖动输入）：路径真实选择https://39.71.45.66:4605/，153次>100ms交付/呈现空档，交付max615.2ms、初90秒P99 91.879ms、聚合新帧呈现53.60FPS，明确失败。源端持续出现前向增长150–482ms、QUIC队列空但捕获暂停117–250ms，以及码率2.13–13Mbps来回变化；新增repair_credit已实触发，未解决余下路径问题。CPU.Light临时跟踪在20:28后开始（第一批失败先于跟踪），20:32:32开始收尾、20:35前完成，WPR已恢复无记录；后处理ETL暂不与下一段视频抢CPU。双Relay维护标记恢复；当前BJ Relay第一段对照将使用同一候选与20000kbps应用设置。

- 最终候选五分钟direct第一段完成（20:19:27.580–20:24:27.581，480拖动输入）：新帧API呈现聚合59.90FPS，初90秒交付P99 31.090ms；有效窗口1次268.1ms交付空档、6次111–222ms呈现空档，尚未通过。客户端同时出现ReceiveDatagram等待267828us（设置10ms超时）、控制派发排队264274us、解码max162763us；无参考丢失/恢复请求，源端同段五秒提交max5110us且QUIC tx_lost=0，反馈生成313826us/到达316031us。不能归结纯UDP或网卡，也不能把多线程同时迟到当作已证实全系统停顿；下一段90 Relay正在采样并加本机CPU.Light调度跟踪。源端时间比客户端约快6.7秒，按编码frame/反馈对齐，不能直接按相同墙钟截取。

- 追加五分钟有效采样：direct有4次>100ms，max379.7ms，初段交付P99 38.544ms；源端末次暂停261ms/165ms，QUIC缓存已空但真实前向进度落后200–272ms且额度缩到101200字节，仍需处理。90 Relay有57次交付>100ms、max470.2ms，初段P99 51.479ms，性能不通过，实际选择90路径证据保留在源端send_path；这段开始时尚无可见窗口，未执行拖动，不算拖动覆盖。
- 新增低开销实际呈现统计：只在新视频纹理经DXGI/Vulkan接受呈现后计帧，旧纹理重绘不计；5秒聚合与>100ms慢呈现记录，测试覆盖失败/重复/替换上传/停顿。90 Relay有效聚合呈现45.30FPS，健康接收约60FPS窗口呈现39–55FPS，查明UI固定绘制后再等待16ms、解码不唤醒UI的问题。参考Moonlight Qt帧就绪SDL事件，已加弱引用/合并通知，Client development dist SHA与build tree相符 `E5D575CDF0762BAEA945F886792703491FE312B90F04D131AC73F73E1E925C91`；唤醒/销毁回归通过，当前做针对性呈现对照，不宣称实网已通过。
- iroh路径接口核对：默认把IP列Primary、Relay列Backup，跨类不按RTT择优；公开custom-selector仅在路径生命周期时触发，不能仅加selector就声称运行中自适应切换。未改第三方路径策略。两个Relay维护标记已恢复，Machine trace为空、无pktmon运行；节点仍为loss-feedback完整安装身份。
- 唤醒后针对性90 Relay一分钟：拖动480条输入执行、交付P99 24.358ms、无>100ms交付或呈现空档，聚合新帧呈现60.10FPS；不是长稳通过。复现“已有参考、最后一帧只有部分分片、无后续完整帧”时旧策略迟迟不发RFI，旧失败已保存。修复为最后有效部分分片停止进展100ms后请求一次RFI；持续接片不触发、FEC补齐取消、无进展重试仍稀疏，未知初始参考保留原watchdog。Source对同一恢复请求仅放行一次额外捕获，仍检查QUIC未发送额度、两帧流水线、历史/字节债务和deadline，不给正常帧无限额度。
- 已通过17项Encoded delivery/recovery、53项媒体测试，呈现/弱唤醒回归通过；命名/所有权检查通过。初次媒体三项失败保留：取消测试短信用在Start前已过期；反馈测试只等待任意报告却断言完整确认；大帧本地deadline在该批602ms越限。修复前两项测试的确定性条件，构建负载结束后复测媒体通过。新增持续700ms正常接片测试发现了过早IDR升级，已将升级起点改为部分分片最后进展。早期partial-repair候选没有安装，最终候选partial-repair-final正在构建，不作为已交付身份。
- 最终候选`partial-repair-final-20261011`已完整安装90：Cloud Node3.3.97/314文件SHA核验/Service Running，Windows会话保留。Setup SHA `464F94BE2EC808376E3A2C78BF078B83EB7726A151DB5817C4DFD3E3937AB133`、Render SHA `52353D6D1BEABFC68C9E327C240D7B896FD34A55D34E3240708D26075571D95E`、manifest SHA `001C907D4A8C10F327C79C20ABF4608C0D1F372333D21BC6907A11A7197664F0`。六development运行文件SHA相符，Client dist `08CEE246E2DB28F57D20A06EF395AC410B81064C8C25D61372DC862454840D81`。安装前280会话均closed/双Relay fresh，应用20000kbps及Relay软件不变。当前执行第一段五分钟direct含实际呈现；未宣称卡帧解决。

- 用户再次要求连续完成卡帧任务，后续实施不以阶段汇报作为停止点。当前仍为 Windows/Linux 媒体传输范围，继续使用90工作区 `D:\112233`。
- 已新增并保留旧实现失败的确定性复现：前向接收50ms内前进，但报告额外迟到210ms、实际未确认271700字节时，固定256KiB额度错误暂停捕获。新实现按应用预算、基线RTT和实测回程延迟计算健康额度，最多1MiB/64帧；重复旧状态不能放宽，真实前向延迟仍收紧32–256KiB，QUIC64KiB缓存和按预算未发送准入保持原值。
- 50项媒体测试通过，命名/所有权检查通过687行变更；已构建并完整安装 `feedback-lag-20261011`，Cloud Node 3.3.97/314文件SHA核验、Service Running，Windows会话保持。Setup SHA `1B847091FBC91A8DB9DB1B445836D1ACF5E5B0D6CC8AB2583F8DDE5F167BB312`，Render SHA `00080A1D7A8A0B901D7C47AF33BDAE9BBBDFD978328A595F9EE481B4ED997411`；六份development dist与build tree SHA相符。仍未宣称实网卡帧修复。
- 临时诊断：首批Service Environment未传给CreateEnvironmentBlock(false)的用户Render，已恢复；TCP定位批改用两个Machine诊断变量，原空值备份并已恢复。只抓TCP4605/96字节头，pktmon已停、专属过滤器已移除。应用预算、网卡和Relay软件未变。
- 原20Mbps一分钟实测：direct49.47FPS/P99122.38ms/max338.561ms/34次>100，出现丢片与RFI恢复，性能未通过；90 Relay59.87FPS/P9927.026ms/max253.616ms/1次>100。Relay源间隔17.451ms/客户端分片206.529ms，Relay actor接收空档191.696ms，组帧21µs。结果全部保留，不用重跑好结果替代失败。
- TCP诊断批的一分钟有效窗口没有>100ms完整帧空档；日志收集期间连接仍在，窗口结束后出现5次完整帧空档与TCP序号缺口/后到数据。这些是定位证据，不计入一分钟性能失败或成功。会话与宽限停止完成。已回放direct前两个500ms真实采样（74/0→564/100→856/109，RTT11083→92586→83070µs）：旧控制在连续丢包且RTT回落时仍维持13Mbps，复现失败已保存。按500ms增量识别连续丢包，逐步降码率，近期丢包后5秒再做10%恢复探测；孤立丢包、静态场景、历史计数/重置/切换Relay不降码率。53媒体测试通过；命名/所有权检查通过780行。已完整安装loss-feedback候选，Cloud Node3.3.97/314文件核验/Service Running，Setup SHA `C75F3D5B2530F5646D29429B33792179E1CB5E53A53D4519C7C2F63E0E297E5A`，Render SHA `25968F176012CC4FA42926EBC343F6B472B9CC9114CC9EF0BB403FC1405B856E`；六development运行文件SHA相符。当前接续：原20Mbps/无Rust trace的五分钟直连采样正在执行（含8秒快速拖动），随后实际选择90/BJ Relay。未宣称性能通过。

### 当前实施：清空在途帧后的拥塞状态重置（2026-10-11）

- 用户要求立即实施；旧自动目标暂停不阻止当前已授权开发/安装/验证。已回放 Relay 反馈：encoder 473 的确认释放五帧、pending=0 时，旧 forward delay 仍为 135332µs；下一批仍沿用旧缩小额度，造成新的准入等待。
- 已先保留旧实现失败的复现测试（新批仍受 32768 额度和 300000µs 历史延迟影响），再修改 `VideoFlightWindow::Observe`：只在真实累计确认清空全部 pending 时恢复正常有界额度、重建时序基线并清空该批吞吐采样；部分确认/不前进反馈不清债务，第二次真实拥塞仍收紧额度。
- 47 项媒体测试通过，命名/所有权检查通过。首次全测两个新/调整用例未满足 250ms 采样周期或已到稀疏探测期，已按有效时间线修正并保留初始结果；不得把探测准入当成无限额度。
- 已完整安装 `flight-drain-20261011` 到 90，Cloud Node 3.3.97/314 文件核验、Service Running；Setup SHA `924909B2C6BB8958EED522FDCA31D3D4EFFB91936B56E23F46D03C773FACEAAE`，Render SHA `4BD3D2A46997FB3F9A7DF98BCA33B7B0362627DC40421159AE29420BB7315216`。六份 development dist SHA 相符。Server/两 Relay/20000kbps 应用配置未变。
- 各一分钟游戏完整帧交付：direct 59.30FPS/P99 45.578ms/max 130.440ms，两次 >100ms；强制 90 Relay 59.22FPS/P99 51.329ms/max 512.826ms，一次 >100ms。Relay 客户端日志轮转已恢复首段 1129 条，同路径首段缺失的初始统计作废。**尚未性能通过**，没有旧实现同环境对照，不能宣称改进。两个短 WebView 连接/宽限停止成功，非持续 60FPS 源。
- direct 现存空档：反馈到达间隔 211/227ms、生成约 50ms；正常 262144 额度被真实未确认字节 271700/265100 消耗，捕获暂停，组帧 22/24µs。Relay wire 1437：源端 8.290ms 内本地提交，但接收分片跨 390.913ms，组帧 68µs；没有 receive_worker 超时记录，仍需区分 iroh 内部与 Relay 链路。不能归咎网卡或把本地提交当实际发送。
- 清理后 272 会话均 closed，两 Relay fresh/ready，90 只剩桌面 Render 27448/session 2，Windows 会话保留。完整结果见 `20261011_flight_drain.md`。
- 接续预算对照：仅临时将测试游戏 Console 预算20000→10000，同版本各一分钟 direct 59.88FPS/P99 37.247ms/max89.439ms、90 Relay60.02FPS/P99 44.500ms/max59.963ms，两段均无>100ms；源端确认10Mbps预算/19200字节队列。已恢复原20000、revision4，274会话均closed/两Relay fresh。首次配置同步期间503、后续预览eligible后成功的记录已保留。不同公网时段不能建立唯一因果，更不能替代长稳/显示验收。
- **下一步**：回放 direct 正常额度被迟到反馈消耗的边界，并对齐 Relay wire 1437 的实际发送/接收/转发，定位 391ms 分片空档；不先扩大额度，不重做已通过的安装/媒体测试。

### 当前长任务：直到卡帧问题解决（2026-10-11）

- 用户明确要求规划一个长任务直到解决卡帧。当前入口为 `docs/video_stutter_resolution_task_20261011.md`：S1 可重复时间线/复现 → S2 发送与反馈额外暂停修复 → S3 direct/90 Relay/BJ Relay 实际验收 → S4 诊断清理与交付。Android、独立业务、短期 token、已叫停网卡/饱和混传专项仍排除。
- 不以单次短测或本轮队列上限完成结项。目标持续更新 60FPS 源，在足够带宽下无未解释的反复 >100ms 停顿，并验证受控阻塞解除后没有本地额外积压；三路径分段稳定性门槛详见新计划，均未宣告通过。
- wire 270–290 中清空全部 pending 后历史拥塞残留的复现与修复已完成，当前实机候选及下一步以上方最新实施批次为准；显示链与剩余实网停顿尚未结项。
- 目标工具状态：旧 P0–P7 目标仍 paused，创建本次目标返回 unfinished goal；新自动目标未激活，旧目标未冒标完成。该工具状态不阻止当前已授权的开发、安装和验证，实施按本页最新批次继续。

### 当前：QUIC 未发送队列准入（2026-10-11）

- 接续 ACK 停顿证据，新增按应用媒体预算约 16ms 的未发送队列额度（最小 4 片、最大 32KiB），在每批视频分片提交前检查 QUIC 可用缓存并通过 1ms 异步定时器等待；不更改拥塞控制或第三方源码。编码准入改为最多两帧流水线，并检查未发送队列余量；已编码帧不任意丢片，现有帧 deadline 和接收端参考恢复保留。
- 本地 45 项媒体测试及 UDP failure、iroh Frontend 通过；新增 160ms 模拟阻塞验证队列界限、控制任务可执行、分片顺序保留、等待超时/停止只完成一次，实际 QUIC 端点验证 64KiB 缓存契约和编码准入。命名/所有权检查通过。
- 修改前 20 秒游戏基线已保存（原 20000kbps 配置），接收窗口约 60FPS/最大间隔 50.3ms，无 >100ms 停顿；不把未复现当成已修复。工作区 `D:\112233\queue-admission-20261011`，无网卡/捕获/业务配置变化。
- 已完整安装候选 `queue-admission-20261011`，Cloud Node 3.3.97/314 文件核验；Setup SHA `96B9A92EF0D81CC1A43E7FC99BAF8D9E86CDB9BDA8B0299E8776757B418E7310`，Render SHA `99B8EEDF75F50ABD661DB7728382C8DD1E9DF9C259EE99FC4F57C3696CCA63ED`。Server/两 Relay 软件及应用预算未变，六个 development dist 运行文件 SHA 相符。
- direct 游戏有效 20 秒：59.30FPS/P99 44.126ms/max 57.270ms，无 >100ms；基线 60.02FPS/P99 37.020ms/max 50.339ms。候选有 QUIC 丢包/参考恢复，已接收范围内 1206 帧全部本地提交成功；不能称为实网性能提升。WebView 连接和宽限停止通过，加载期间长间隔对应编码源输出间隔，未发现这段由提交失败/准入排队造成。
- 强制 90 Relay 有效 10 秒：54.45FPS/P99 91.306ms/max 198.132ms，五次 >100ms，**性能未通过**。wire 280 等待 146.242ms 时队列峰值 30633 字节，低于 32768 额度且完整提交；后续存在队列已空、远端接收进度额度仍暂停编码的事件。未证明是错误反馈/物理网卡，继续按上方 S1 对齐。
- 清理后 268 会话均 closed，90 只保留桌面 Render 7580/session 2，Service Running，双 Relay fresh/ready，trace 变量为空。Relay 日志发生轮转，已合并 `.1.log` 和当前文件保存，未重新跑连接。见 `20261011_queue_admission.md` 及同前缀证据。本批源码未提交/push。

### 当前：按应用配置媒体带宽（2026-10-11）

- 用户指定“每个应用分别配置”。复用 Console 应用 `launch.video.bitrate_kbps` 的持久化/下发链路，新增应用默认 10000，已有配置保持；90 当前三个游戏/WebView 应用均保存 20000kbps。Web 中英文明确每路媒体预算、预留开销和新启动实例生效。
- 移除 iroh 诊断候选的固定 6.5/9.6Mbps 上限，按应用配置派生编码/发送预算；Service 通过 `--app_media_bandwidth_kbps` 精确传递 kbps，避免整数 Mbps 取整。不同 Session 使用独立预算对象。
- 31 Web 测试、28 Service 应用测试、Render 前端配置贯通及 UDP 测试通过。媒体 41 项首次并行构建期间有一大帧 deadline 超时，停止构建负载后原样复测全部通过；初始失败与复测记录均保留。6 个 development dist 运行文件 SHA 一致，命名/所有权检查通过。
- 90 已完整更新 Server 1.0.56（35 文件），Setup SHA `26a8a0258d33ed26dd0fac24327b2e96ca1a649d292151c791c5f46e666004a8`，线上 Web 资源 SHA 与构建相同；schema/配置未改。工作区 `D:\112233\app-bandwidth-20261011`。
- 90 已完整安装最终节点候选 `app-bandwidth-final-20261011`，Cloud Node 3.3.97/314 文件核验，Setup SHA `061926482674A522DC8FB4BEC17FFAE53DFF8B5EF2C0B7EFC4A9F7D8797021F0`；Render SHA `4E339940E8EA0334076DB2714801E27922384899F32C20FA8CF984923ABC49CE`，Service SHA `EE2CFCC775356BA371766A2049E6F69769538871DDFD314AB919CE8805E11AE0`。首个 `app-bandwidth-20261011` 节点包未包含新 Service，未安装，不作为交付物。
- 游戏/WebView 各 5 秒正常连接通过：两次 Render 日志均为 total=20000000、video_wire=19200000、encoder_ceiling=13000000；游戏启动参数亦确认精确传入 20000kbps。已有应用值未修改，新建默认 10000。此为配置贯通检查，不是性能/长期稳定性结论。
- 清理后 264 会话均关闭，90 仅桌面 Render 18116/session 2，Service Running，双 Relay fresh/ready（generation 91/16，control_epoch 70）。Windows 会话保留，trace 变量为空，无抓包、网卡或 BJ 安装变更。证据见 `20261011_app_bandwidth.md` 及同前缀文件。
- **下一未完成项**：回到上次 ACK 停顿定位，评估 QUIC 视频排队额度/期限和按帧依赖恢复；不把可配置带宽当作原始回程延迟已修复。真实 NAT/规模/长期稳定性仍未验收。

### 最新离线定位：161ms 停顿的 ACK/拥塞等待链（2026-10-11）

- 用户要求先查；本批只分析已有预算候选的 trace 与 NIC 指纹，无新测试/部署/配置变化。
- 已明确 pn 89/90 等 ACK 在客户端及时构造（ACK delay 6–14µs），回程 NIC 记录相对延迟增长约 160ms；90 在途 24561 / 窗口 25909 字节，持续约 147.492ms 拥塞阻塞、两次 PTO。ACK 到达后 46µs 恢复发送，恢复帧组帧仅 48µs，形成 wire 11→12 的 161.357ms 间隔。
- 等待期间后续视频继续进入 64KiB QUIC FIFO，接近满后 `send_datagram_wait` 才向应用传播背压；可用缓存降至 2568 字节、后续帧应用排队约 70ms。该积压是可优化的恢复负担，不冒称它造成 NIC 之间的原始回程延迟。
- 本事件未发现 ACK 生成延迟、ACK 后唤醒迟滞或组帧慢；尚不能区分网卡/驱动以下/中间网络。重新核对 Sunshine 只读 UDP 发包路径，其没有这层 QUIC ACK 窗口门槛，不能照搬组帧解决此问题。
- 证据及复现：`20261011_ack_stall.md`、`20261011_ack_stall.json`、`analyze_ack_stall.py`。运行身份、262 已关闭会话和清理状态保持下节预算候选。
- **下一未完成项**：评估视频进入 QUIC 的排队时长/额度和按帧依赖恢复，避免等待确认时累积过期视频；进一步定位回程物理位置需要外侧观测/路径对照，不重复相同抓包或盲改拥塞窗口/FEC。

### 当前：系统收发边界与约 10Mbps 预算候选（2026-10-11）

- 新增一次有界纯视频双端 NIC/TCPIP Pktmon；无混传、限速注入或网卡配置改动。正常 19 秒窗口正向 27030 包有 176 包未到对端 NIC，反向 4727/4727 匹配；两机内部 NIC/TCPIP 包集合一致，无 ETW 事件丢失。匹配正向包最大相对延迟增长 202.68ms。不能指认具体物理网卡或中间设备。
- 对齐 127/135ms 两次交付停顿；后者约 90ms 的成批缺包后，参考恢复只占约 14ms。两端无接收缓存丢弃日志或新 NDIS 重置。旧脚本 20 秒后的截图/关闭阶段已排除性能统计，后续测试移除截图。
- 测试已清理：261 会话均关闭，90 只保留桌面 Render 27888，两端 Pktmon 停止且筛选器为空，90 trace 变量恢复为空；运行身份仍为此前完整诊断包。详见 `20261011_socket_boundary.md` 及 SHA/四层捕获证据。
- 用户明确 90 上行约 20Mbps、希望控制约 10Mbps。基线 UDP 负载平均约 11Mbps、1ms 最大约 40KiB，不能只限制编码平均码率；已开始跨帧共享的应用提交节奏候选，4 片/批，估算视频线速 9.6Mbps、编码上限 6.5Mbps，保留 QUIC 拥塞控制并预留 FEC/协议/音频空间。
- 候选已完成：40 项媒体 + 3 项原 UDP 发送测试通过，development dist SHA 一致；90 完整安装 Cloud Node 3.3.97/314 文件，Setup SHA `5553251DC3AD997ECF0702B8C275582C4C165E4182B6A2DBB8911BC48FAA5D61`，Render SHA `0E4458CB5F2ED398D5927F9FE99E7FFD9BB480EB0DF1A035F00DDFFA762458AF`。Server/BJ 未变。
- 无截图纯视频 direct 对比：正常 19 秒约 59.74 FPS、最大间隔 60.53ms，UDP 负载 7.84Mbps、加线层开销估算 8.41Mbps；正向 20476 包缺 15（0.073%），此前 27030 缺 176（0.65%）。1ms 最大突发 40644→19508 bytes、100ms 峰值 17.99→9.96Mbps。两轮公网条件不同，只称本轮改善，不宣称因果或长期稳定。
- 初始连接仍有 wire 11→12 的 161ms 停顿，发生在正式采样前；同时间客户端 NIC 已发送的反向包在 90 NIC 约晚 142–160ms 成批出现，同时发送侧背压。正常窗口反向也有约 214ms 延迟增长。没有硬件归因，也未发现足够分片无法组帧。
- 清理后 262 资源会话均关闭，90 仅桌面 Render 14844/session 2；双 Relay fresh/ready，两端捕获/筛选器清理，90 Machine trace 变量恢复为空。持久变化为约 10Mbps 固定视频预算候选包和本机 dist，无网卡/Relay/宽限配置变更。详见 `20261011_socket_budget.md` 及同前缀证据。
- **当前下一步**：通过正常使用复现剩余短停顿，对齐反向到达和 QUIC 发送停滞；详细诊断暂留，不重新进行饱和混传/网卡专项，不把单轮短测称为全面稳定。

### 当前：QUIC 包号边界定位（2026-10-11）

- 接续逐帧诊断，用已有底层 trace 做一次约 20 秒纯视频 direct；未修改源码/二进制/发送策略，完整安装身份仍为下节诊断包。未恢复混传、抓包或网卡专项。
- 新增证据：发送端构造的 pn 24452–24455、24472–24488 共 21 个 QUIC 包，对端接收 trace 缺失且 ACK 跳过相同范围，发送端 lost=4+17。包括 16 个视频 DATAGRAM、4 个 STREAM 和 1 个 ACK；可靠字节随后重传并确认收到。双方接收缓存丢弃日志为零，缺失不在上层组帧/FEC。
- 本轮 wire 1012 缺片、1013 等参考、1014 恢复；交付间隔约 19.5ms。四窗口 59.6–60.6 FPS/最大 66.4ms，未复现此前 118/124ms，不能称为修复。原始 trace 有同步写盘开销。
- 90 临时两个 Machine trace 变量已核对并恢复为空，未改服务环境/重启服务。本机无测试 Client，90 仅桌面 Render 27888，应用宽限停止。原始四份日志、SHA、离线重建脚本及限制在 `20261011_quic_boundary.md`。
- **下一步**：补 QUIC 构造之后的系统 socket 收发边界，区分发送栈、接收栈与链路；当前 `wrote packet` 不等于网卡发出成功。没有证据指认网卡硬件故障。上轮无新增丢包的 124ms 背压事件仍独立未解决，不盲调媒体恢复参数。

### 当前：视频逐帧诊断（2026-10-11，用户要求增加日志定位）

- 重新核对 Sunshine `3cba9bae` 的批次/FEC 耗时和 Moonlight common `e95feaf4` 的每帧 FEC/缺片状态。原参考仓库未改。
- 新增编码帧号/传输帧号映射、每片写入等待及接收次序、完整/参考等待结果、准入暂停和逐次反馈，限制每连接首 90 秒。发送/FEC/恢复策略未改。
- 37 项媒体测试通过；本机 Client、Cloud Node Client/Render development dist SHA 匹配。90 已用完整候选 `iroh-video-diagnostics-20261011` 安装 Cloud Node 3.3.97，314 文件核验。Setup SHA `0F5DB88A3582C512E89F73215CDE1FB0551A3BB4F0CA5DB655C3103D2DD07625`，运行 Render SHA `C14E2CB439E87F052E3C1A338A1B95F2DC2A530E33655B66E455519CEFAF4E4E`。工作区 `D:\112233\iroh-video-diagnostics-20261011`；Server 1.0.56/schema 40、BJ 安装和网络配置不变。
- 两轮约 20 秒纯视频完成：direct 58.2–60.4 FPS，最大交付间隔约 124ms；Relay 59.8–60.4 FPS，最大约 78ms。此前持续 29–32 FPS 本轮未复现；未改策略，不能据此称为修复。
- direct 同帧双端对齐发现：wire 522–524 的 53 个连续分片本地 QUIC 已接受、接收侧未观测到，超过 FEC 恢复能力；发送提交每帧不足 0.2ms，QUIC lost 0→61，随后六个完整帧因参考缺失未交付，恢复帧到达后结束约 118ms 停顿。另一约 124ms 停顿分片齐全，但出现发送计数停滞、缓存减少及约 40ms 写入背压。没有发现“足够分片已到却无法组帧”的实例。具体底层丢失/延迟位置仍未确认，不能归为网卡硬件故障。
- 已清理测试：259 会话均终态；node generation=435，Relay generation=90/15，control_epoch=69，均 fresh/ready。持久变化仅诊断产品包；临时首 90 秒详细日志保留，未更改发送/FEC/恢复策略或网卡设置。原始双端日志压缩保存并记录 SHA。
- 下一步：区分 QUIC 接受之后的发送排队、UDP 发送及接收交付边界，解释上述两种停顿；依据证据选择修复，不盲调恢复定时器。用户此前叫停的饱和混传/网卡专项不恢复。详细数据、限制及证据见 `20261011_video_diagnostics.md`。

### Windows/Linux 本次功能交付完成并已推送（2026-10-11）

- A：BJ 新完整 Linux 镜像部署入口升级通过，配置 SHA 未变；强制 BJ 协议检查 120/120 datagrams、6,291,520 bytes 可靠流通过。Windows Relay 随本轮新 Server 完整 Setup 安装通过。
- B：修复同设备桌面 + 文件窗口 409：新增复用既有设备授权的 `file_transfer` 角色，文件连接不占控制席/视频额度，只允许文件能力；Console/Panel/SDK/Render/管理页同步。实机两个进程同时连接、关闭媒体文件仍连接、新角色 4 KiB 往返 SHA 通过，临时 ACL 已恢复为空。旧无调用内嵌文件面板已原样归档。
- C：重拨默认从 8 秒改为 30 秒有界预算，明确区分暂时端点查询失败与会话结束；终止错误经 SDK 上报 Client 并中英文提示，阻止旧回调覆盖终止状态。不会延长应用默认 10 秒宽限、自动重启应用或注销 RDP 会话。补 Panel 启动失败资源会话释放。
- D：候选资格/容量及动态路径核对完成，复用双 Relay 故障/维护/容量证据；真实复杂 NAT、十 Relay 规模和长期稳定性未验收，不重新开启叫停专项。
- E：21 PostgreSQL 会话测试、Frontend 19 项（1 专用故障项复用既有实测）、Client 三目标、Web 6 项与构建、Server 包 8 项通过。开发产物已同步 dist 且 SHA 一致，`20261011_file_session_build.json`。
- 90 最新完整安装位于 `D:\112233\iroh-file-session-completion-20261011`：Server **1.0.56** Setup SHA `7AC4CCA6…A2B9AA3`、schema **40**、35 文件核验；Cloud Node **3.3.97** Setup SHA `16E56344…314BCA3`、314 文件核验，Render SHA `574DF82344D8620736F2FA741F53C859D976FBB3850E7DFF58129151A86BDEFB`。详见 `20261011_file_session_*installation.json`。
- 首次 Server 安装因 32 位 PowerShell 缺注册表而失败，旧服务未停；安装器改 Sysnative 64 位 PowerShell 后成功，未修改系统注册表。旧运行 Server 实为 1.0.55，失败的缓存 1.0.54 候选未安装。
- 正常 GameHook/WebView/RDP 与强制 Relay 游戏连接、画面及宽限退出通过。**画面性能未解决**：首轮 direct 游戏 34.4–42.7 FPS/约 301ms，最终短检查 29.1–32.0 FPS/约 277ms、频繁 RFI；强制 Relay 多数窗口约 60，一次 47.4 FPS/200ms。不能称为“只有少量混传抖动”或稳定 60 FPS，未确认网络/网卡/代码根因。发送策略本批未改，没有恢复饱和混传/网卡实验。
- 清理完成：257 会话均 closed，本机无测试 Client；90 仅桌面 Render 18512，RDP 及其他 Windows 登录会话保留。control_epoch=69，node generation=434，两 Relay generation=90/15，均 fresh/ready；设备 ACL `users=[]/groups=[]`。持久变化为 schema/备份 schema 和产品包更新，原 Relay 配置、网卡、宽限期保持。
- F：实现/证据已提交并推送 `bf5ebc03a`（master），工作区检查通过。详情 `20261011_windows_completion.md`；本条目随文档收束提交保存。Android 明确排除，旧消费者仍需的路径未删除。
- **后续未完成项**：从本批双路径日志继续定位 direct 画面性能，真实 NAT/规模/长稳验证；这些不冒称通过。当前只完成第 0.9 节功能交付范围，不宣称整个网络性能目标完成。

### 当前任务：Windows/Linux 一次完成，Android 排除（2026-10-11）

- 用户明确要求“android先不做，规划一个完整任务，我要一次性做完”。总计划第 0.9 节及 AGENTS.md 已同步范围，覆盖下方旧的“Android 最后接入”本次安排。
- 一个任务连续执行六段：A 双系统 Relay 交付 → B Windows 独立文件传输 → C 自动重连与宽限期 → D P2P/多 Relay 功能缺口 → E 完整包及必要集成回归 → F 提交 push、证据与文档结项。阶段完成不等待重复“继续”。
- Android 开发/验收、它依赖的旧数据面退役和已叫停的性能/网卡专项排除；已有网络消费者、文件和剪贴板证据复用。必要功能回归保留，不借整体任务自动恢复饱和混传压测。
- 本次仅更新规划，未构建、部署或启动测试。安装身份/持久配置沿用上一实机记录；当前代码基线 `a217f9331`。
- **直接下一步：A，生成完整 Linux Relay 镜像包并用 BJ 现有项目/配置完成新入口升级验收。** 后续直接衔接 B–F；缺环境如实列出，不将未验证 NAT 或未修复功能缺陷冒称完成。

### 当前规划：后续整体顺序已收敛（2026-10-11）

- 当前已推送基线为 `a217f9331`（含前序 `97d81fd1d`）。后续任务详见 [总计划 0.8](../../iroh_transport_execution_plan_20261008.md#08-后续整体执行安排2026-10-11当前顺序)。
- 顺序：BJ 完整包升级验收 → Windows 独立文件入口核对 → 断网/重连与宽限期生命周期 → NAT/多 Relay 缺口 → Windows/Linux 网络版本收束 → Android 最后接入 → 旧数据面归档退役。
- 源码核实：Panel 独立文件入口和 Client `fileTransferOnly` 模式已有；不能重新从零实现。`iroh_dialer.cpp` 业务重拨仍固定 8 秒，需要与应用退出宽限期及会话有效性一起梳理，不能仅延长一个常量。
- 本次只更新规划，未构建、部署、重启或开展实机测试；上一批运行身份和配置记录继续有效，本轮混传压测继续停止。计划列入的未来网络验证不代表现在恢复测试。
- **唯一下一步仍为 BJ 新 Linux Relay 包升级验收**，复用现有项目/配置与已通过的 Windows 安装证据。M3 抖动/NAT/长期稳定性不标完成，Android 前不移除仍有消费者的旧栈。

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
