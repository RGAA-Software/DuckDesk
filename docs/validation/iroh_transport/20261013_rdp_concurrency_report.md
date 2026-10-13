# RDP 十路实机功能与 Company 停测记录

用户将RDP目标改为最多10路，90部署现capacity10/revision3/ready；GameHook/WebView容量和正式80路授权保持。
用户随后报告Company严重卡顿，本批全部停止。Company不再承担多客户端或重负载验证，未经新指示不重开。

本批复用已交付Console8DAC/Service6E3/RenderC276/ClientFE8，没有编译、包更新、网卡变更或抓包。
实际Company→90路径日志path1，属于直连功能样本；不能计入双Relay真实业务并发或带宽验收。

已取得的证据：

- 90真实10个不同标准用户在session3–12同时Active，10个对应的资源会话connected。原slot1/2用户保留凌晨登录时间及原Windows会话。
- Company10个精确PID/创建时间记录、10个独立RDP图形线程都有published>0且display_channel=true，
  10个对应窗口均捕获1440×900 JPEG，源端没有以同一账号串接代替并发。
  一份截图人工核对实际Windows桌面，其余为逐窗口捕获及图形日志证据，不称每个桌面交互均已逐项验收。
- 容量改10后已有连接保持，第11路申请返回503/code unavailable。只读预览显示deployment_slots0、
  deployment_capacity_exhausted和rdp_workspace_busy，节点仍有54空闲slot，授权额度没有占满。
- 同一slot断开后重新分配仍是原账号；完整重连/输入验收在用户减负时中止，未宣称全部通过。
  Company卡顿使本批不能称10路流畅或长期稳定，也没有20路验收。

用户限10时扩容执行单元已立即终止；其前已接受5个额外Client和1个仅准备的源实例，随后均关闭。
账号及断开的Windows会话按产品规则保留，不logoff、不删profile；之后没有再扩大并发。
初始批量准备5源再统一启动超出无连接宽限，已改为每路准备完成就立即启动，不作为失败网络样本。
MCP user context后来失效，拒绝发生在接受前（原请求无task）；刷新后重连源已过期，该次也不计有效样本。
临时LaunchOnly实验未用于有效并发样本，完整未提交内容归档于`backup/rdp_concurrency_handoff_20261013`后撤回。

停测清理只处理本批记录的Client PID、完整路径及创建时间，没有按进程名或路径扫无关应用。
第一次紧急清理把CIM微秒精度与.NET 100ns精度直接比较，在关闭首个Client后停止；
随后统一使用.NET创建时间来源核对，关闭剩余owned Client，实际remaining_clients=[]。
对应24个owned实例全部stopped、24个资源会话closed、guest注销；Console最终业务活动0。
失败/主动结束的长驻测试命令不是完整900秒通过证据。

运行/状态证据为本目录`20261013_rdp_concurrency_{capacity10,observations,ten_windows,ten_captures,overflow,summary,final_api}.json`。
窗口JPEG在Company的`D:\112233\rdp-concurrency-20261013\ten-window-*.jpg`，路径与SHA已记录；截图为JPEG，无需额外解码改写。
源端工作仍在90的`D:\112233`，没有Desktop暂存。用户报告是接收机负载问题的直接证据，
本批没有测出具体CPU/GPU归因，不将其推断为UDP、Relay或发送端故障。

下一网络项仍为少量、分布式实际业务连接下的双Relay分配和维护保留；不得再集中打开Company多路窗口。
Android、已暂停的短顿/NIC/饱和混传调查不启动。
