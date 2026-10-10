# BJ Relay 接入实际 Console

日期：2026-10-10。接续 `20261010_bj_relay_management.md`，本批以实际环境闭环为目标，不重跑已完成的独立公网连通性和隔离数据库阶段。

## 完整 Server 升级

- 90 通过 Pixels MCP（428358431）操作；Windows x86_64、SYSTEM。开发和安装文件位于 `D:\112233`。
- 原 Server 1.0.54，当前完整 Setup 安装为 **1.0.55**；全部 35 项安装文件 SHA-256 与候选清单一致，Console 和 Relay 服务 Running。
- 数据库升级后预检为 `UPGRADE_READY current=39 target=39`。安装器迁移前备份：`C:\ProgramData\Pixels\Server\data\database-upgrades\0d0ffef855364c67a36d4a93ff37b3a9`。
- 候选位于本机 `C:\Users\chess\AppData\Local\PixelsRelayBuild\console-bj-20261010`，使用聚焦 fast Release 构建和现有完整包组装/Setup 脚本；未运行全量 release 入口。版本、包/清单/二进制哈希见 `20261010_bj_console_package.json`。
- 安装操作 ID `1a6b50e1-acfc-425d-9e1f-1da7f286ee4f`；安装任务 `4cf15ade-4f7e-4a97-a7bd-87f22c9bcf08` 输出校验成功，无 stderr。

## 管理与持久配置

- BJ 登记 ID `3314f150-27be-42cb-ac2e-bf3e0e68b3c7`，名称 `BJ-200M Relay`，公开地址 `49.232.233.61:4605`。登记前查询去重，已有登记凭据保存在忽略的本地配置目录；未增加短期传输认证。
- BJ 原完整容器镜像未变化，切换为 `console_managed:true`，通过 Compose 重建容器加载管理环境，信任实际 Console CA。
- 首次认证实际上报维护状态；Console 解除维护后，实际 `reported_draining=false`。重启 Console 后 BJ 自动重认证并上报 control_epoch=63，未恢复独立模式。
- Console 持久启用 `relay_only=false`，提供 BJ CA；Relay URL 来自已认证管理记录，而非静态配置。新配置 SHA `03352CEEAB08C7B89A342505D398629A6847757A617E19478849BBB567DD4438`。旧配置备份为 `C:\ProgramData\Pixels\Server\config\console-before-bj-20261010.env`，不在批次清理时恢复它。
- 90 Service 在 03:32:07 UTC 记录 `iroh_configured=true`、generation=334；桌面 Render 因配置变化自动重启为 PID 54168。BJ 上报一个空闲 Relay 连接，说明配置已进入真实节点运行流程。
- 90 原 Relay 仍按旧协议运行；即便列表有两条 ready 记录，**当前只有 BJ 是可用于 iroh 的正式 Relay**，不能称双 iroh 池或故障切换已验收。

## 实测与限制

- 配置从独立模式改为受管理模式后，重新验证数据面：120/120 数据报、6,291,520 bytes 可靠流校验成功；实际路径 BJ Relay；最大往返 961936us。原始证据 `20261010_bj_managed_probe.log`。这是配置变更后的必要复测，不覆盖上一批 1.303 秒不利结果。
- 实际管理接口已返回 BJ 在线、新鲜、未排空、QAD 4605，房间数为空。真实游戏连接时，BJ 的连接数为 3（桌面、应用 Render、Client），上传和转发计数增长；旧 90 Relay 连接数 0。快照 `20261010_bj_managed_online.json`。
- 启用配置后服务 Running 与 HTTP 就绪之间有数秒初始化窗口，第一次立即请求被拒绝，后续实际 TLS 登录/管理请求通过。未通过重复安装处理这个正常启动窗口。

## 真实游戏验证

- 使用已有有效拖动脚本 `test_iroh_recovery_tcp_live.py --relay-only`，游戏实例 `a95a6557-fc75-4ea7-9003-63ac9335b4f0`，Client PID 69028。仅本次自有实例参与测试和清理。
- 本机 Client → BJ → 90 GameHook；Client 日志实际路径为 iroh-relay，Console 上的 BJ 转发计数同步增长。两张截图显示游戏视角随拖动变化，没有退出/错误模态弹框遮挡。
- 17 个五秒窗口、5044 帧，交付 FPS 49.8–60.8，最大帧交付间隔 **258.3ms**，7 次 >100ms，0 IDR/0 RFI。统计为解码前交付，非显示 FPS；无人工限速。本轮不能作短抖动已解决的结论，尚未取得逐跳抓包证据，不能把间隔直接归因于 BJ 网络或组帧。
- 证据：`20261010_bj_game_results.json`、`20261010_bj_game_measurements.json`、`20261010_bj_game_frame_600.png`、`20261010_bj_game_frame_2400.png`。
- 自有 Client 和实例已正常清理；90 最终只剩桌面 Render 54168，Console/Relay/px_service Running，QoS 0，持久配置 SHA 未改变。不要恢复此前禁用 iroh 的配置，也不要重建 BJ 登记。
- 为避免下轮重复复现，已保存本次源端和接收端诊断：`20261010_bj_game_source_timing.log`、`20261010_bj_game_client_timing.log`。最大间隔 frame 1864 的源端相邻时间为 16933us，接收相邻时间 258161us，组帧调用 21us，说明该帧的长间隔不发生在组帧计算内。另有源端 frame 1502 的 183ms 编码空洞，与在途额度收缩后的 166651us 采集暂停对应。两端系统时钟有偏差，应按 frame ID 关联，不能直接减墙钟。下一次从这些具体帧追踪 Relay/TCP 排队及额度反馈，保留正常 60fps 窗口，勿盲调 IDR。

后续从 BJ 真实应用证据继续，补 WebView/普通桌面验证，再升级 90 Relay 的运行配置和服务打包，完成双 Relay 动态更新与切换。Android 最后；本批不宣告多 Relay、混合突发性能或全部长任务完成。
