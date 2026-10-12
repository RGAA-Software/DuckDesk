# Company 接收 XR / BJ Relay 实际视频

本批不使用 90。XR 的源、上传及临时文件仍在 `D:\uploadsoft\rd\pixels`。
Company 验证工作区 `D:\112233\xr-relay-video-receiver-20261012`，原开发运行目录未改。

## 上传与新版基线

Company 原来只有 Pixels Yun 3.1.46 与独立 Agent Bridge 1.2.87，没有新版 Client。
最初独立开发目录复用了 20 个哈希相同文件，22 个变化文件压缩为 43,877,601 bytes，
Pixels MCP 用时 13.448 秒，平均 **3.263 MB/s / 26.102 Mbps**。
这是 Company 的上传结果；此前 XR 完整 Node 上传平均 0.398 MB/s / 3.186 Mbps，二者不能混用。

用户指出尚未装新包后，18 jobs 官方聚焦构建 Client/Panel、collect、41 文件验证并生成完整 Client 3.3.81 Setup，
以 Setup 新装 Company，返回 0，安装后 41 文件 SHA 全匹配，用户 hy 的 console 会话 1 保持。
原 Pixels Yun、Agent Bridge 保留，未卸载，也未替换原开发目录。
安装基线 Setup `181F0006AFFF1DBD97D4C086BFDD9AD02EFB5F00AE4A1BF60731BC76CA758F9C`，
manifest `FE24F4FCD9E51B9852D76A9731B684856F05D682599998890551E77992AC0E66`，Client `A714EFA4…`。

最初日志读取脚本 FileShare 不正确的失败、独立目录接入拒绝、安装后脚本误用旧路径的失败均保留，
不作为媒体性能样本。后续及时取得并使用现有资源会话连接凭据后正常接入；没有新增鉴权系统或 transport token。
UHD630 不支持本路径的 Vulkan Video，Client 正确选择 D3D11，并能实际呈现 H.264 画面。

## 有效视频片段与 TCP 证据

实际源端及 Client 都使用 BJ Relay。实例 `fd7ef3a1-be48-494a-a376-b16f667e26d1`，
资源 `1a431354-07a3-4189-8527-dcb7700180f5`，Company Client PID48868。
应用上限 10000 kbps，页面为已加载的环回 HTTP 动画，不依赖外部网站；不强制弱网达到 60 FPS。

原计划 600 秒，MCP 用户命令在 467.233 秒时为 `interrupted`，没有正常 completion，Client 已不在。
结果文件仍是 sampling，不能据此称 600 秒完成，也不将 MCP 中断直接当作产品崩溃。
完整呈现窗口覆盖 450.868 秒，加权 **59.867 FPS**，最大新图间隔 **165.167 ms**。
三次正式接收间隔 **151.219、163.047、165.387 ms**：

- frame324：源总提交 43 µs，接收首片到收齐 134.399 ms。
- frame3769：源总提交 41 µs，接收分片跨度 146.203 ms。
- frame3770：源总提交 34 µs，包群跨度 369 µs，但完整帧间隔 165.387 ms。

这些事件不是源端本地排队或 Feed CPU 耗时造成的长等待；TCP Relay 下包群交付阶段仍有短抖动。
本机 NDIS 重置不再是本段接收条件，因此不能把所有短抖动都归因于本机网卡。

本批 TCP 采样正确进入 BJ Docker Relay 的网络命名空间。
XR 对应 `112.27.225.224:45930` 有回程 TCP retrans/bytes_retrans 与入站 rcv_ooopack；
Company `39.71.45.66:17182` 视频出口 socket 未报告 bytes_retrans。
这些是 TCP 状态实际计数；乱序、重传和 DSACK 不等于全部是真实丢包，也不能用 QUIC tx_lost=0 代替 TCP 状态。
BJ 相对本机墙钟偏差测得 +7.610～+8.132 秒，Company 相对本机 -11.360～-9.908 秒。
采样含往返与调度不确定性；已记录时钟界限，**尚不能证明每次卡帧对应哪条 TCP 缺口**，不作逐帧因果结论。

两端日志均在源端压缩后下载，gzip 和解压原文 SHA 都核验；分析 JSON 保留帧身份与源提交记录。
实际节点部署 revision/鉴权配置、第三方 iroh 实现及组帧策略没有为这轮短抖动作猜测性修改。

## 安装后的日志路径修复

原 Client 只尝试写 exe 相邻 px_logs，在普通用户无法写安装目录时缺少诊断文件。
修复为本地日志初始化失败后，使用现有 `FolderUtil` 的可写产品运行目录 `C:\Users\Public\Pixels\px_logs`，
开发 dist 可写时仍沿用原位置与有界异步媒体日志。
18 jobs Release 开发与官方聚焦构建通过，开发 runtime/dist SHA 匹配，所有权和格式门禁通过。

已有完整安装基线后，仅压缩传输变化的 **px_client.exe** 与两个哈希清单，未再次传输 Setup。
最终 Company Client `FE8B82574E133D765D2FA2E7838370F2AEC761E0FCA8363DF9C701DB96B92466`，
manifest `7D1988934E36DFB0E32D936CA08EB499B2D06A367906C832BE6A298E98B2592D`，41 文件重新核验。
旧日志完整归档于 Company 工作区；安装目录临时日志写权限撤回。

首次修复验证误以为运行用户必然不能写安装目录，脚本只监测公共目录，造成 No presented video 的假失败；
Render 实际已 connected，未当产品媒体失败。随后仅对本产品日志子目录施加临时拒绝写规则，
明确观察 permission denied → 公共目录日志 → BJ 实际新图，**60.359 秒通过**，Client 正常结束。
拒绝规则已撤回，验证不改变 NIC、UDP、驱动或其它产品文件权限。

## 清理及下一项

所有本批 Client、资源、实例、guest session 已收束；XR 页面取消终态 confirmed，Render0、监听0、ServiceRunning。
XR Relay 选择从 BJ 限制恢复原 `[]` 完整候选，配置 SHA `38363D78…`；测试 app revision4/deployment revision6 disabled。
两台新产品安装与 node 身份保留，全局 Relay 和原应用未修改，90 不参与。

网络主线剩余定位是把源→Relay交付和回程反馈的具体等待与 TCP 状态对齐，及补未完成的完整长稳片段。
复用已通过的传输功能、NAT 和 Windows/Linux交付证据，不重启停用的 NIC/饱和混传调查，不扩大录制或剪贴板验收。
Android 排除，复杂 NAT、容量/热点及长期稳定性仍按缺失证据记录，未宣布整体完成。
