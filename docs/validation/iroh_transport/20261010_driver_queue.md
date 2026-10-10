# 慢设备驱动阻塞网络控制消息

## 实机证据

90/BJ 跨宿主容量检查后继续网络混传。真实 GameHook Client 强制 iroh Relay，上传/下载 134,217,984 bytes 并完成 SHA-256 校验及远端文件清理。约 10 秒短测视频继续解码、输入日志 sent=true；一个五秒窗口 58.8 FPS，最大交付间隔 148.8 ms（源端对应 145.266 ms，且发生在开始上传前）。这不是完整性能通过。

Render 同期显示 `kHello` 回调耗时 1,000,660 / 1,000,048 us，随后输入和控制消息队列最长 1,366,221 us。两次长回调均与同线程 `vigem_target_add` 失败日志重合。直接原因是 ingress 同步调用 JoystickService 初始化虚拟手柄；MessageSession 的串行回调被驱动等待占住。不是仅凭数据报丢失率猜测 UDP 故障。

证据：`20261010_mixed_relay_live.{json,log}`、`20261010_mixed_relay_driver_before.log`。JSON 的 passed 表示文件与视频连通检查通过，不代表输入排队或稳定性通过。

## 修复

网络入口只将手柄 Hello/状态投递给单工作线程的有界执行器（最多 128 项），现有设备后端继续负责创建和操作。其他控制消息、鼠标、文件不在这条设备队列上等待。断开使同流待处理操作失效并与进行中的驱动调用串行清理；停止取消排队任务。弱引用避免队列延长服务生命周期；从自身工作回调 Stop 时，设备清理在当前操作释放锁后完成。

本批不修改 UDP/QUIC 超时、媒体缓冲和重传参数，不修复或重新安装机器上的 ViGEm 驱动。驱动失败仍明确记录，不让它拖住正常网络控制消息。

只读对照 Sunshine `D:/source/Sunshine` revision `3cba9baebac882b336be3ebe129ee612cb189853`：`input.cpp` 的 passthrough 将输入排队并交给 task_pool；这里复用项目自己的有界执行器和生命周期规则，没有修改外部源码。

## 验证与交付

新增慢驱动、队列满、待处理输入取消、回调中 Stop、活动调用期间释放 owner 的测试；原有重复启停等用例保留。joystick_service 和 Render execution lifecycle 两套 CTest 通过。开发 Render 已发布 dist，SHA-256 一致。

完整 Cloud Node 候选 `iroh-driver-queue-20261010` 已通过 Setup 安装到 90，版本 3.3.97，314 个文件哈希一致、服务 Running。安装身份见 `20261010_driver_queue_installation.json`，构建与开发 dist 哈希见 `20261010_driver_queue_build.json`。

同条件后测 `20261010_mixed_relay_after.{json,log}`：真实游戏、强制 iroh Relay，134,217,984 bytes 上传/下载及 SHA-256 一致，7.457 秒完成；投递 346 条输入消息。五秒统计窗口 303 帧、60.5 FPS，最大交付间隔 26.9 ms，>100 ms 为 0，IDR/RFI 为 0。这是解码前交付指标，不是显示 FPS 或端到端输入时延测量。

90 同期日志 `20261010_mixed_relay_driver_after.log`：ViGEm 失败仍存在，但在独立线程 8020；文件进度线程 3824 和媒体线程 1408 继续运行，未出现达到 50 ms 阈值的 message_dispatch 排队/回调日志。对照修复前最大排队 1.366 秒，说明本次复现的驱动阻塞已解除；不能把未记录的小于阈值延迟写成零。文件通道 accepted=1100、busy/disconnected/transport_errors=0，媒体窗口 completed=301/301、failed=0。

两张 Client 截图已检查：游戏画面正常且角色姿态变化，无连接失败覆盖；不据此宣称高动态场景或输入时延验收完成。测试实例与 Client 已结束、远端测试文件不存在，三服务 Running，仅桌面 Render 15760。90/BJ Relay 保持正常配置。

本批完成具体缺陷的修复及短测，不代表长期混传、带宽负载均衡或真实 NAT 打洞已经验收。接续仍是网络混传稳定性及直连/中继缺失路径，不扩展录制和其他独立业务功能。
