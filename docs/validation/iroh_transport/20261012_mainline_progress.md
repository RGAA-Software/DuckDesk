# iroh 主线推进分析（2026-10-12，卡帧验收之后）

当前已经越过选型、原型和基本接入阶段。第0.9节 Windows/Linux A–F 功能交付原先已完成并推送；本轮追加媒体停顿修复已达到卡帧任务的有界工程验收，不能把 Android、复杂NAT、规模与长期稳定性也算完成。

## 按当前交付里程碑核对

| 主线 | 已有实现/证据 | 当前判断与边界 |
|---|---|---|
| M0/媒体基线 | 应用级kbps预算；发送准入/反馈/参考恢复；真实解码产出和新图呈现计量；持续无输出恢复；三路径各3×5分钟 | 本轮卡帧S1–S4验收通过。极少量102–114ms到达延迟和底层FFmpeg具体状态未完全定因；不承诺零抖动 |
| M1/正式Relay | 正式px_relay、现有Console管理、独立地址/TLS；90 Windows完整Server Setup、BJ Linux完整镜像/Compose及deploy.py升级；真实datagram/可靠流校验 | 已交付，不再做首个Relay原型或重装已通过包。两系统说明deploy/relay/README.md |
| M2/多Relay | 双宿主故障恢复、动态候选、维护、容量拒绝/释放；Console按fresh/epoch/ready/余量筛选，实际selected路径上报；隔离iroh最小发送队列补丁 | 核心功能通过。十实例本地功能证据不等于十台物理主机并发；带宽均分、端点热点上限和规模容灾没有全验收；单连接不做跨Relay带宽聚合 |
| M3/Windows可靠流+datagram | 桌面/GameHook/WebView共用内核；RDP可靠透传；文件独立进程/Endpoint/QUIC及file_transfer角色；Panel等待真实接入回执；重连原因/截止/取消/身份与10秒宽限 | 功能闭环已有证据，复用20261011_windows_completion.md。新增decoder集成回归通过；当前性能门槛按三段五分钟通过，原计划三段十分钟及长期故障/混传稳定性未自动通过 |
| M4/Windows/Linux产品交付 | Server1.0.56/schema40；Cloud Node3.3.97完整包314文件；Client development dist40文件/SHA；BJ真实镜像升级/配置登记身份保持 | 当前可执行交付完成。本轮Node完整候选安装和本机dist已核验，不称development dist为可安装正式Pixels发布包。Relay未改不用重复部署 |
| M5/Android与全局退役 | Android排除当前任务；仍消费的旧数据面和Direct Host WebRTC边界保留 | 未开始，不提前删共享旧实现，不借本轮通过把P0–P7历史清单全打勾 |

实现入口核对：SDK/Render共用src/px_transport，媒体分片/反馈在media_datagrams，SDK视频队列的decoder恢复位于thunder_sdk；可靠业务与RDP仍经同iroh连接内核。文件入口独立生命周期和resource role已经交付。本轮没有重做剪贴板、录制/播放、认证票据或网络驱动专项。

## 还缺的主线证据

1. **真实双端NAT打洞矩阵。** 当前公网映射直连、loopback可控代理和强制90/BJ Relay分别有证据；不等于复杂NAT/CGNAT/两端UDP不可用全部通过。先列现有拓扑和UDP可达条件，再选缺的一组补证，不能把已通过公网直连重复称打洞。
2. **更长正常路径与受控恢复稳定性。** 本轮45分钟是九段五分钟，不是每路三段十分钟，也不是多小时持续会话。补证时沿用实际新图计量/有界日志，保留所有异常；已叫停的NIC/饱和混传/抓包专项没有重新获得自动许可，不作为立刻开测的安排。
3. **容量、端点热点及多主机规模。** 已有双宿主容量释放与故障功能，尚不能给出十台Relay性能/吞吐结论；按用户后续主机环境验证真实余量和拒绝行为，不先设计复杂地域调度。
4. **Android最后迁移。** 仅在用户重新安排后进入SDK/JNI/网络生命周期/APK验收，再判断旧Native/Relay/TCP哪些路径可归档退役；当前不做。

唯一下一开发检查点是整理真实NAT/更长稳定性/容量缺失环境清单，按已授权范围补一个确实缺失的网络场景。已有Windows/Linux功能、文件角色、RDP工作区、双Relay切换不从头做；不将上述证据缺口改写成尚无核心功能。用户这次请求的“先完成卡帧再分析主线”至此完成，后续主线范围与缺口已经明确。

## 当前产品与配置

90工作区D:\112233；Cloud Node3.3.97候选decoder-output-recovery-20261012（Render6F9A2D19…/NodeClient0C9B2695…），本机Client408717B2…/Panel8ACEB454…；详细全SHA、45分钟指标和恢复回归见 [本轮验收](20261012_decoder_recovery_acceptance.md)。应用10000kbps/revision19、宽限10秒，双Relayfresh/ready/原标志，316业务会话closed。本轮没有Relay、Android或认证契约改动。

先前主线功能/交付提交bf5ebc03a已推送，状态文档基线c449357a3。本轮媒体代码、失败与通过证据、验收及本分析已提交并push master：5fe4b8ae8，远端从c449357a3推进。当前完成的是卡帧任务工程验收和主线分析，未将NAT/规模/长期稳定性或Android冒标完成。
