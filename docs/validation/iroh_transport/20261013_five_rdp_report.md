# 三路本机＋两路 Company：RDP Relay 维护验证

2026-10-13，用户重新授权本机最多3个Client、Company最多2个。本批没有超过接收端限制；
90 RDP capacity10保持，仍使用80路PXLIC2和现有资源会话授权，没有编译、安装、网卡变更或新增transport token。

## 结果

五个不同RDP用户同时接通并有图形输出。Client的Endpoint仅在测试启动参数设relay_only=true，
日志可靠通道kind5和控制通道均path2，确认使用Relay。不是直连样本，也没有改节点持久TOML。

初始五个实际源Render端点home全部是90 Relay。Console最终采到90 Relay11连接、BJ0；
该数字包含默认Render、源/接收端Endpoint连接，不能当作业务会话数或按11路视频计算。
启动时曾见BJ临时1连接，不算源端点平均分配；自然选择优先90，不能声称双Relay自动带宽均分。

临时将90 Relay排空（revision22→23），实际reported_draining=true，旧五路业务资源保持connected，
原Client PID、session_id和源Endpoint ID保持，可靠流继续收发、图形继续更新。
旧连接仍使用90，不把排空当作关闭服务，也未做杀Relay/故障重启。

为了验证新接入，先关闭本机batch203，再在确切结束后启动batch206，始终最多本机3/Company2。
新源端点home为BJ（49.232.233.61:4605），Client仍path2，双方实际Relay计数和字节继续增长。
账户仍为pxrdp_705dbd09276447，Windows session5与9:16原登录时间保留；不是新建用户或注销后重登录。
同一时间旧四路保持connected，BJ的新RDP桌面有图形输出。

将90排空恢复false（revision24），reported状态确认，现有五路仍connected；
Client看到候选列表恢复count2，旧会话未通过重新创建来伪装恢复。
两端选定的日志片段没有reconnect=true或reliable_send error非0；
这不是证明所有包零重传，也不是完整长稳。期间部分连接有tx_lost1而可靠流正常。

本地日志四个RDP图形线程（含一次替换）max published69/100/84/56，Company两线程均100，均见display_channel=true。
静态Windows桌面只在内容变化时更新，几十秒新图间隔和低video.present FPS不能直接判为卡帧。
本机仍记录52条draw_us约50–99ms的慢绘制日志；本批未分析GPU/UI瓶颈，不称五路全程流畅或60 FPS性能通过。
Company同范围慢draw日志0，但没有系统CPU/GPU连续采样，不能据此保证无负载问题。

此结果补齐小规模**真实RDP可靠流**的双Relay维护/新接入行为，
不代替GameHook/WebView音视频数据报的高运动负载、最大吞吐或长稳验收。
累计约7分钟接入/维护过程，没有重启暂停的短顿/NIC/饱和混传专项，Android排除。

## 收束和证据

本机3个Client及替换后的第3个均已退出；Company按本批PID/完整路径/.NET创建时间关闭2个Client，最终remaining_clients=[]。
6个owned实例stopped、6个资源closed、guest注销；业务活动0。
Relay最终90=1（原默认Render）、BJ=0，desired/reported draining都false，两Node fresh/ready。
管理员和RDP账号、profile及断开的Windows会话保留。没有新Setup或改变产品版本的声明。

本机开发Client SHA C39C17CC…与build-tree/dist一致；Company installed Client FE8B8257…保持。
90 Console8DAC/Service6E3/RenderC276复用已交付身份。首个本地控制脚本因stdin EOF自然退出，没有启动Client；
改成有界PTY控制后启动成功。Company日志收集时停测任务仍在进行，早期样本列2进程，
后续明确核对0；不把早期收集样本当最终清理状态。

Company在源端先gzip筛选本批日志，下载7831bytes，源/目标SHA593EDF61…一致；
本机筛选日志gzip12484bytes。筛选后原文及gzip SHA在log_analysis中，非整个原始日志的SHA。
临时脚本/日志在本机.cache、Company D:\112233\five-rdp-20261013、90仍为D:\112233，没有Desktop暂存。

证据：同目录`20261013_five_rdp_{sessions,observations,drain_on,drain_off,windows,log_analysis,receiver_cleanup,final_api}.json`。
下一项可保持3＋2或更小规模，补GameHook/WebView实际运动内容的可靠控制＋媒体数据报验证；
不因本批通过重复十路RDP或提高Company并发。
