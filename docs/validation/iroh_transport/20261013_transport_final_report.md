# Windows/Linux网络主线追加改动的完整包收尾

本批把RDP多用户池、Native声音输出、接入终止及此前媒体诊断改动汇总进正式优化的完整Windows候选。90按Server→Cloud Node覆盖安装，Company再覆盖Client；三次Setup均exit0，实际载荷逐项SHA通过。四类安装后接入有真实输出，测试资源和临时访问授权已清理。第0.9节Windows/Linux主线及本次追加交付已收尾；现存性能与环境限制继续保留。

运行源码基线为`9beb4f59508369a6f25e809a9067e9a14b3ca870`。本批工具改动仅为Server构建显式18jobs、通用安装核验支持Client/Remote，以及单路Native启动和压缩采样脚本；没有新传输协议、授权机制或短期票据。全部编译使用Release，正式树`PX_FAST_RELEASE=OFF`，不调用`build_official.bat`、不升产品版本，也不把正式载荷复制到开发dist。

## 完整候选与实际安装

候选标识`transport-final-20261013-9beb4f595`。完整路径、安装包/清单/运行文件SHA及构建树对照见[交付身份](20261013_transport_final_delivery.json)；原始安装回执见[安装记录](20261013_transport_final_installation.json)。

| 产品 | 版本 | Setup大小 | 载荷文件 | 本批安装 |
| --- | --- | --- | --- | --- |
| Server | 1.0.56 | 13,730,926字节 | 35 | 90 |
| Cloud Node | 3.3.97 | 278,551,685字节 | 314 | 90 |
| Client | 3.3.81 | 47,963,273字节 | 41 | Company |
| Remote | 3.3.79 | 92,189,126字节 | 76 | 仅生成完整候选，未单独安装 |

Server候选位于`build_official/private_server/candidates/transport-final-20261013-9beb4f595/`；其它产品位于各自`official/candidates/<候选标识>/installer/<版本>/`。Setup为既定无Authenticode签名产品；本批未发布更新源或GitHub发行版。

- 90当前Console SHA `10119C3F…`、Windows Relay `2583B875…`；schema预检`current=41 target=41`，没有再次迁移数据库。Console、Relay及原Backup服务运行。
- 90当前Service `4CE0F74B…`、Render `FEE6ED1F…`、Client `EBFD48B7…`；Company实际Client `4CF79FFA…`。这些是正式优化身份，区别于之前的开发增量身份；安装与本批实际启动前核对一致。
- Console/Relay环境、iroh Relay配置、workspace密钥、数据库owner连接文件及80路许可证的SHA保持不变。节点TOML由旧默认模板`55904D03…`更新为包内`38363D78…`，增加默认空iroh Relay候选过滤项和说明，默认端口/范围不变；持久Console绑定、设备/节点身份及Administrator会话2保留。
- 原RDP用户会话3–17仍保留为Disc，504复用原slot1用户`pxrdp_2c3153bd5fff41`及会话3。没有删除账号/profile或logoff。原9应用spec、9部署容量/GPU/禁用配置逐项相等；RDP容量10、许可80保持。
- Linux BJ沿用已完成的完整镜像/Compose升级、两主机恢复及维护证据，不因这次Windows消费者收尾重新安装；XR服务也不更新。Remote同一传输/Client实现已构建核验，其新Setup没有单独安装，不能将共享路径验收称为本批Remote独立安装。

## 安装后的有界连接

Company每次仅启动一个Client，完成并清理后才进行下一种模式。准备和启动在同一编排脚本内连续执行，没有在取得连接信息后等待另一轮人工操作。数字来自同一接收端日志；两端墙钟不直接相减。

| 样本 | 实际路径 | 输出与范围 |
| --- | --- | --- |
| Game502 | Relay，path2 | 45秒保持，计入关闭等待的50.108秒呈现窗口；正常窗口45.090秒/59.259FPS，最大33.879ms；wasapi、48k双声道打开及3840字节首次PCM |
| Web503 | UDP直连，path1 | 30秒保持及启动/关闭等待；正常窗口40.068秒/59.898FPS，最大54.630ms；6秒后真实Windows鼠标点击，随后设备打开/PCM提交 |
| RDP504 | 可靠Relay，path2/kind5 | 30秒保持，published53，display_channel=true；同用户、同Windows会话复用 |
| Desktop505 | UDP直连，path1 | 30秒保持，26.321秒统计窗口实际呈现203帧；静态桌面更新稀疏，仅验证接入/出图，不作60FPS或运动流畅性证明 |

Game首个窗口为59.39FPS/33.135ms；Web首个窗口为53.99FPS/534.088ms，启动间隔按用户决定单列，不当稳态强制条件。这里的“正常窗口”仅排除首个呈现窗口，并非无限期稳定性统计。四路日志均无hello发送失败、ADMISSION_REJECTED、业务重连或可靠发送错误。Game/Web声音是实际播放提交证据；不宣称本批再次做了FFT或主观听音，前批频率验收继续复用。

RDP/普通桌面为静态内容，日志保留4.894秒/5.235秒呈现间隔，不能据此声称持续视频卡顿或达到60FPS。RDP还保留FreeRDP初始化MD4/RC4及编译选项警告；该样本实际SSPI连接与图形已工作，没有把警告藏作“零错误”，也未擅自修改第三方。

正式Client树3个CTest目标、30个GTest用例通过：hello就绪/断开/失败生命周期、SDL音频初始化/释放/失败重试、启动描述与中英文提示。PowerShell解析与修改范围检查通过。已完成文件往返/独立进程生命周期、双Relay维护、正常多路和基本真实双NAT证据复用，不重跑录制、全量剪贴板、混传饱和或网卡专项。

每路客户端日志先在Company压缩再下载，90源日志也先压缩，5份压缩文件双端SHA相等。统计与校验见[分析](20261013_transport_final_analysis.json)，原始摘要见`20261013_transport_final_sample502`–`sample505.json`、[源端记录](20261013_transport_final_source.json)。

## 无效样本、清理与主线范围

- 501准备脚本误把非RDP响应中的`rdp:null`当对象，产生已准备资源但未启动Client；该样本关闭/停止后以502重测，不计通过。未再次把它误判为网络故障。
- 初次PowerShell收集将Get-Content附带的PSObject属性序列化为大对象，修正为纯字符串后采样/压缩通过；原命令已确认完成，没有未知任务重放。
- 前端注销使同账号管理会话失效，首次恢复访问授权出现403；正常重新登录后继续完成，仅撤销本批临时增加的用户授权，回读与原值相同。
- 最终4 owned实例stopped、5资源closed（包含无效501），guest/user注销，两测试应用/部署恢复禁用，启动信封删除；Company Client0，页面任务confirmed cancelled/48777 listener0，90仅默认Render。两节点fresh/ready，90 generation470、XR26、epoch74；两Relayfresh/ready/未排空，Windows默认Endpoint1/BJ0。见[清理](20261013_transport_final_cleanup.json)、[健康与配置](20261013_transport_final_after.json)。

| 第0.9节 | 当前结论 |
| --- | --- |
| A Windows/Linux Relay交付 | 已有两系统完整安装、Linux升级入口和恢复证据；本批Windows Server Setup追加覆盖通过 |
| B 独立文件/媒体 | 已交付并验收；本批复用，未改文件协议或入口 |
| C 恢复/宽限协作 | 已交付；首次接入失败原因及终止后hello修复已闭环，正式候选聚焦回归通过 |
| D 多Relay/实际路径 | 动态候选、故障/维护、负载可见性、多路分配及基本双NAT已验证；本批path1/2实际接入继续通过 |
| E 完整包与集成 | Server→Node→Client完整覆盖及四类网络消费者输出通过，新增Runtime与包身份一致 |
| F 提交/记录 | 本报告和状态/计划更新随代码提交push，安装身份、限制及证据可追溯 |

Windows/Linux本次主线不再自动扩展成附属业务验收。**复杂NAT/CGNAT、更多UDP受限环境、规模容量上限和长稳仍有证据缺口**，不宣称99.9%或所有网络条件下零卡顿。先前短顿调查仍为阶段性进展，未定因/未承诺消除；按用户决定暂缓主动复现，NIC/饱和混传不恢复。Android完全排除，继续保留它以及Direct Host WebRTC仍消费的数据路径。后续推进应单独选择上述未覆盖范围或用户新需求，不重复已通过主线。
