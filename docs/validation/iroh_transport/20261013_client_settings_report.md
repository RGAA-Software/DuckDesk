# 客户端设置持久化与实际连接状态

用户要求继续连接状态与多 Relay 主线，同时明确客户端设置应写入已有数据库。客户端部分已实现并发布到三个产品的开发 dist；Relay 带宽候选筛选另见 [本批报告](20261013_relay_bandwidth_report.md)。

## 设置与目标身份

沿用当前 Public/Pixels 产品目录下的 `px_data/app.<identity>.dat` LevelDB，不增加第二份配置存储。统计显示、声音、语言、主题、全屏、Native 帧率和支持的分辨率均保存并在下次启动恢复；悬浮球位置继续保留。缺失或损坏的数据使用启动默认值。保存失败既记日志，也通过已有中英文 Toast 提示。

云应用原 `remote_device_id` 实际为每次运行实例，不能作为持久设置的唯一目标。Panel 的 Native/RDP 启动信封现在携带稳定 `application_id`；数据库身份按产品、Console、应用（桌面使用设备）、RDP 账号区分。不会改变网络上的实例、资源会话或设备身份，也不读入退休 Qt 配置。新应用实例、更换部署节点仍共享该应用偏好；不同应用和 RDP 工作区分别保存。

数据库只在读写期间打开。相邻 `.ui.lock` 文件以 Win32 独占文件句柄串行化访问，RAII 先关闭 LevelDB、再释放文件句柄；不在整个 Client 生命周期持有 LevelDB 锁，且不依赖 Windows session 内的命名互斥量。真实独立子进程同时读写与关闭重开通过，保留其它偏好和悬浮球位置。

帧率/分辨率在连接就绪后恢复；Native 只采用当前上报支持的分辨率，失效选择记录 `resolution_unavailable`。观察者不自动发送这些控制请求。RDP 仍使用原生协议，不显示 Native 帧率控制；不会自动启动语音通话、录制或其它业务操作。

## 实际路径

SDK 从当前 QUIC 连接读取 selected path、地址与 RTT，Client 统计页每秒展示实际直连/UDP或中继/TLS、对端/中继地址和连接状态。RDP 同样可以查询，无需等待视频数据报。没有样本显示未知/`--`，不把强制策略或候选列表当作已选路径。

连接恢复与业务结束分开；最终拒绝原因使用已有中英文目录。关闭后不继续报告旧连接。RDP 统计查询与关闭时安全持有网络对象，并保留反复启动/停止及并发查询回归。这里展示实际 Relay URL，尚未提供 Console 中的友好 Relay 名称映射。

拨号和统计共用 `UsesClientIrohTransport` 判断，明确选择 TCP 时不会因为信封仍包含 iroh 描述而误标为 iroh；强制 Relay 仍按当前拨号优先级选择。相关启动配置与统计目标最终两项 CTest 通过。

## 验证与开发交付

- `scripts_build/build_cpp_client_connection_tests.bat client 18`：6 个 CTest 目标通过，覆盖数据库重开、独立进程共同目标、非法值、应用稳定身份、统计中英文、真实 loopback QUIC 查询与关闭、RDP/声音生命周期。子进程辅助用例在普通父进程中跳过，只在隔离子进程中执行。
- `build_cpp_product_panel_tests.bat client 18`：Panel 产品目标通过。
- Client、Cloud Node、Remote 的 Client 与 Panel 均使用 focused Release/18 jobs 构建，发布入口完成各自 dist/开发清单更新及无 Qt 检查；6 个 exe 的 build-tree 与 dist SHA 相同，见 [运行身份](20261013_client_settings_delivery.json)。
- C++ 所有权、可读命名及 diff 检查通过。未执行 release-only 全仓构建、Setup 安装、远端覆盖或视频/NIC压力测试。90、Company、XR、BJ 的安装身份沿用此前记录。

本批工作树尚未提交。客户端改动未改变 Relay 协议和 Android；同批 Console 的 schema42/带宽配置与候选筛选有独立报告。客户端偏好已具备数据库回归证据，真实产品界面重启验收尚未执行；本地开发构建不代表远端安装已更新。
