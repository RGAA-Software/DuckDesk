# Windows/Linux 网络任务交付（2026-10-11，不含 Android）

按总计划第 0.9 节持续执行 A–F。A–E 的必要功能与当前环境安装检查已完成；F 文档和提交收束中。性能专项、真实 NAT、规模与长期稳定性不计通过。

## A：Linux Relay 完整包升级

BJ 使用新完整镜像包中的 `deploy.py` 升级成功。Compose 项目、登记身份、外部配置及配置 SHA 保持不变。
运行镜像、二进制、Console fresh/ready 和实际强制 BJ 连接见 `20261011_relay_upgrade.json`。
120/120 数据报往返、6,291,520 字节可靠数据校验通过。Windows Relay 随新 Server 1.0.56 完整 Setup 再次升级并核对运行身份。
两系统启动、停止、升级和故障查看说明在 `deploy/relay/README.md`。

## B：文件与媒体进程

正常文件入口为 Panel 设备文件传输：单独启动 `px_client --native-launch-stdin`，`mode=file-transfer`。
文件和桌面模式有独立实例锁、资源会话、Endpoint、QUIC 连接。文件模式不收视频，不启动新的游戏实例。
当前媒体工具栏没有内嵌文件入口；无调用的旧 `ClientFileTransferPanel` 已原样归档并移出构建。
归档身份见 `backup/windows_transport_completion_20261011/manifest.json`，Android 代码未退役。

重复打开相同目标和模式时激活原窗口，新子进程退出；媒体与文件模式互不占用实例锁。
Panel 等待实际连接回执，失败只终止本次子进程。此次补上远程启动失败路径的 Console 资源会话释放。
成功文件进程不登记为桌面控制进程，关闭媒体不会误关它；整个 Panel 退出仍停止它所拥有的子进程。
复用 `20261011_independent_processes.md` 的两个进程、独立连接、文件 SHA 和清理证据，不重新进行饱和混传。

实机检查发现原入口虽然分进程，却仍申请 `controller`，同设备桌面已有控制者时文件窗口返回 409。
新增 `file_transfer` 资源角色，仅支持 Panel 已授权用户访问 desktop；复用设备 ACL 和已有登录，
不占用控制席及视频许可证额度，仍要求桌面服务授权。Cloud App/Guest 不获得此角色。
Console 通道和传输登记、Panel 请求、SDK 前端参数、Render 权限收敛及管理页中英文角色同步更新。
Render 将其绑定到独立文件会话，只开放控制协议与文件通道，不开放画面、输入、音频和剪贴板。
权限刷新不能扩大文件会话权限；本批没有增加任何新认证票据。

迁移 0040 与 21 项真实 PostgreSQL 会话测试通过，SQLx 离线查询缓存由真实隔离数据库重新生成。
Frontend 追加真实双 Endpoint 测试证明控制者和文件连接共存、文件不能获得输入权限、关闭控制者文件仍存活。
本机对 90 完整安装后的实测同样通过，并完成新角色下 4 KiB 上传/下载 SHA 一致、远端测试文件删除。
证据 `20261011_windows_completion_desktop_file.json`、`20261011_windows_completion_small_file.json`；
临时设备 ACL 恢复 `users=[]/groups=[]`，不把小文件功能检查当作吞吐或混传压测。


## C：重连终止和宽限期

旧实现将端点查询暂不可用与会话失效都当作空结果；固定 8 秒耗尽后只通知断开，界面可能继续显示恢复中。
现将查询结果分为有效描述、暂时失败、终止原因。HTTP 401/403/404/410 终止既有会话，400/409 拒绝无效描述；
超时、5xx 等暂时失败有间隔重试。端点身份变化仍拒绝，沿用现有授权，不申请额外票据、不自动重启应用。

客户端业务重拨默认上限 30 秒，从初次 QUIC 连接失败或已连接 QUIC 确认网络失活后起算。
QUIC 本身的失活检测发生在此前；本地上限不延长 Console 会话有效期、Render 控制席位或应用退出宽限期。
应用宽限期继续使用 Console 已有配置（默认 10 秒），最后客户端离开时开始，有效重入取消待退出。
因此应用可能在本地重拨上限前已退出；明确会话失效时立即停止，暂未发布可用端点时仍受总预算限制。
重拨复用 Endpoint 身份及原资源会话；不抢占其他控制者、不创建替代实例。主动停止关闭 Endpoint，取消后不再回报失败。

新增 SDK 终止消息及中英文界面提示，区分重连超时、会话结束、密码错误、占用和策略拒绝。
终止后丢弃旧传输排队的非终止状态更新，避免旧帧/断开事件将界面改回“已连接”或“恢复中”。
RDP 同用此终止路径，关闭的是 Client 传输；Windows 账号、登录会话及工作区仍保留。

本地测试覆盖临时查询失败后真实 QUIC 接入、会话结束、预算耗尽、回调中停止、重复启动/停止、鉴权拒绝原因上报、
控制/文件/输入流与视频数据报、RDP 可靠流。Frontend 18 项通过，专用 Relay 长故障用例本批跳过，复用已有实测证据。
详见 `20261011_windows_completion_frontend_tests.log`、`20261011_windows_completion_client_tests.log`。

## D：多 Relay 与证据边界

Console 候选仍按已认证、当前 epoch、30 秒内上报、就绪、未维护且至少两条连接余量筛选，按占用率排序。
候选顺序不等于强制分配：iroh 会探测/选择实际路径，日志中的实际路径才是证据；不承诺平均分流或单连接带宽聚合。
健康连接不因新候选被强制拆除。复用已通过的双 Relay 故障恢复、维护、容量释放与动态候选证据。
本次没有改变发送策略、拥塞控制、分片或网卡配置。

真实两端复杂 NAT 打洞、十 Relay 规模、长期稳定性缺少验收环境/时长，未宣称通过。
前序 direct 混传 110–145ms 抖动并非当前全部问题。此次正常连接观察 direct 游戏为 34.4–42.7 FPS，
最大约 301ms；最终文件角色候选下短检查为 29.1/32.0 FPS、277.6/275.4ms、频繁参考恢复。
强制 90 Relay 游戏多数五秒窗口约 60 FPS，一次 47.4 FPS/200.2ms；WebView direct 含加载期约 1 秒间隔。
这几轮不是受控性能因果实验，发送策略本批未变，不能断言代码回归、网络/网卡根因或稳定 60 FPS。
明确保留 direct 画面性能问题，已叫停专项不因功能收束而恢复。实测样本见
`20261011_windows_completion_observations.json`、`20261011_windows_completion_live*.json`、
`20261011_file_session_game.json` 与 Render 日志；后续定位从这些现有证据接续。
Android 及其旧路径迁移单独后续执行，不影响本次 Windows/Linux 交付范围。

## E/F：产物、安装和清理

Client 与 Cloud Node development Client/Panel/Render 已聚焦 Release 构建、同步 dist 并核对 SHA；
最新身份 `20261011_file_session_build.json`，第一候选身份保留在 `20261011_windows_completion_build.json`。
Client 三个测试目标通过；Frontend 追加后 19 项通过、1 专用长 Relay 故障测试复用旧证据；
Web 定向 6 项测试及生产构建通过，Windows Server 包测试 8 项通过，PostgreSQL sessions 21 项通过。
详见对应 `*_tests.log`。不重跑已通过且本批未触及的录制、全套剪贴板验收。

90 使用 Pixels MCP，全部暂存在 `D:\112233\iroh-file-session-completion-20261011`。
先 Server 后 Cloud Node，均用完整 Setup 覆盖安装：

| 产品 | 版本 | Setup SHA-256 | 安装检查 |
|---|---|---|---|
| Server | 1.0.56 | `7AC4CCA61D9C2CBF22A4B5B61406B27CE875354E636FD8339E4B8BD1AA2B9AA3` | 35 文件校验、schema 40、Console/Relay Running |
| Cloud Node | 3.3.97 | `16E56344D40DB4D4C913845E3783BFF1B3C9FDB888148B8FF01C48405314BCA3` | 314 文件校验、服务 Running、Render `574DF823…86BDEFB` |

安装 JSON `20261011_file_session_{server,node}_installation.json` 记录完整身份。
第一候选完成 GameHook/WebView/RDP 有画面及宽限退出检查；追加角色修复后验证桌面与文件独立连接、
小文件往返及一次 GameHook 连接/宽限退出。RDP Windows 账号和会话未注销，截图为保留会话的画面。

首次 Server 候选未安装成功：32 位 NSIS 使用 `$SYSDIR` 启动 WOW64 PowerShell，90 的该组件缺注册表，
脚本执行前返回 -327680，原服务仍运行。安装/卸载入口改为 `$WINDIR\Sysnative` 显式启动 64 位 PowerShell，
随后 1.0.56 完整 Setup 安装成功。没有修补系统注册表。之前缓存脚本使用 1.0.54，实查旧运行版本为 1.0.55，
失败尝试未降级；本轮最后交付版本以 1.0.56 为准。

持久变更为 Server schema 39→40、配套备份 schema 配置和完整产品版本更新；用户 Relay 地址、设备 ACL、
网卡及应用宽限期未改。安装器自动创建升级前数据库备份，路径在 remote_final JSON。
最后 control_epoch=69、节点 generation=434、90/BJ Relay generation=90/15，均 fresh/ready；
257 资源会话全为 closed，本机无测试 Client，90 只剩桌面 Render PID 18512，三个 Windows 登录会话保留。
清理/健康证据 `20261011_file_session_final_state.json`、`20261011_file_session_remote_final.json`。

Android 未改，Android/现有 WebRTC 依赖的旧数据面继续保留。没有把 P0–P7 历史清单全部打勾；
本次收束的是第 0.9 节 Windows/Linux 功能和交付任务，未解决的画面性能与 NAT/规模/长期稳定性保持待办。
提交/push 身份在 `status.md` 的最新条目与 Git 历史中接续记录。
