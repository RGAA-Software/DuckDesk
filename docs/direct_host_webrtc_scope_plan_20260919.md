# Direct Host WebRTC 与中央媒体能力收缩计划

> 决策日期：2026-09-19。
>
> 状态：活动代码、构建与归档清理已完成聚焦实现和短测；公网跨端功能验收仍按第 8 节继续，尚未关闭 DB5。
>
> 本文同时约束 Windows Client、Web Client、Android、Render、Service、Console 后端、Console 前端、Relay、安装包和 DB0–DB5
> 验收。任一端单独修改都不能宣称本计划完成。

## 1. 最终产品决定

1. 保留现有 **Relay** 名称、协议、路由、配置和数据转发能力，不实施 Exchange Data 重命名，也不提供新旧名称兼容层。
2. Relay 不再承担 WebRTC SDP、Answer 或 Trickle ICE 的中央交换；已有非 WebRTC 数据转发行为必须保持。
3. WebRTC 只保留 **Direct Host WebRTC**：消费者直接连接资源会话描述符给出的 Render `host + port`，在该直接连接上与 Render
   完成 WebRTC 协商，媒体和数据通道也直接到 Render。
4. Direct Host WebRTC 不使用 ZLMediaKit、Coturn、STUN、TURN 或 Relay fallback。部署必须让客户端直接到达实际 Render 端口；
   不可达时返回稳定、可诊断的直连失败，不能偷偷切到其他链路。
5. ZLMediaKit 中央推流、RTMP/HLS/HTTP-FLV、Coturn/TURN 中转及其凭据、状态、端口池和安装制品退出本轮产品，完整归档到
   新的 `backup/` 批次并从活动构建、测试发现、打包和运行加载中排除。
6. 录像和文件传输不是中央直播能力，继续实施。Observer 权限语义继续保留；视频墙或多观察者观看若以后恢复，只能由多个明确的
   Direct Host WebRTC observer session 实现，不能恢复 ZLMediaKit 中转。
7. 产品仍处于开发阶段，不迁移旧配置和旧数据，不保留旧 live/TURN/中央 RTC signaling 接口、字段、路由、fallback 或转发 facade。

WebRTC 协议本身仍需要 SDP/ICE 协商。“移除 WebRTC 信令”在本文中精确表示：**移除经 Console/Relay 中转的中央信令**；Client 与
Render 在 Render 的直接端点上完成的协议协商仍然存在。

## 2. 目标数据流

```text
Console
  ├─ 身份、ACL、调度、资源会话、lease
  ├─ 节点实际上报的 Render host + port
  └─ 向已授权消费者返回 Direct Host 描述符
                         │
                         ▼
Windows/Web/Android Client ───── Direct Host WebRTC ───── Render
                                 直接协商与直接媒体

Client/Render ─────────────────── Relay ───────────────── Client/Render
                                 既有通用数据转发
                                 不参与 WebRTC 协商或回退
```

桌面 Render 当前包默认使用 4601；应用 Render 从 4613–4998 动态分配。每个实际 Render 端口在同一端口号上承载该实例需要的
TCP/WS 与 UDP。20371 不得作为默认、探测、测试或兼容回退。

## 3. 跨组件责任矩阵

| 组件 | 必须保留 | 必须移除/修改 | 短期功能门禁 |
|---|---|---|---|
| Windows Client | Direct Host WebRTC、现有 Relay 数据模式、音视频、输入、数据通道、重连 | 经 Relay 的 RTC signaling、TURN/ZLM fallback；连接描述改用实际 Render host/port | 分别验证 Direct Host 与既有 Relay；直连失败不改走其他媒体路径 |
| Web Client | 直接连接 Render 的 RTC 页面、浏览器媒体/输入/统计 | `standard_signaling` 一类 Console Relay RTC 房间流程、TURN candidate 展示和 fallback | 真浏览器完成 offer/answer、首帧、音频、输入、重连；最终 candidate 不能为 `relay` |
| Android | 当前 Console 身份、独立 CloudApplication target、Direct Host 描述符消费 | 旧设备嵌套应用、Panel 冒充、旧 endpoint fallback；不得引入中央 RTC signaling | USB 真机短测登录、云应用、直连、撤销和停止；功能完成后再统一长测 |
| Render | Direct RTC server、直接协商入口、资源会话/instance/role/lease 准入、4601 与动态端口模型 | `live_pusher`、RTMP URL/stream ID、ZLM 发布和中央 RTC signaling 适配 | 未授权协商无副作用；授权后画面/音频/输入可用；lease 撤销精确断开 |
| Windows Service | 启停 Render、回报实际 host/port、generation/sequence、命令 fencing | 不再生成或传递 ZLM live 参数、Coturn/TURN 配置 | 启动 ACK 与实际端口一致；重启、迟到回执和重复命令不产生第二实例 |
| Panel | 本机管理、Service/Render状态及现有直接连接入口 | 不再生成ZLM/TURN/中央RTC signaling参数；启动Client时只传明确Direct Host或既有Relay数据模式 | 本机启动、停止、重复启动和Client参数专项通过；不能把Panel身份用于Android或Console资源会话 |
| Console 后端 | PostgreSQL authority、ACL、资源会话、描述符、Relay 现有数据面、管理事件 | `live/`、媒体 sidecar、托管 Coturn、TURN REST credential、RTC Relay房间与旧播放 ticket | 空库启动、断库 fail-closed、描述符无 ZLM/TURN/RTC Relay字段；Relay回归通过 |
| Console 前端 | 应用/节点/会话/告警/录像/文件等管理能力 | ZLM直播入口、Coturn/TURN配置与状态、中央 RTC signaling 状态 | 中英文目录一致；不存在无后端能力的菜单；Direct Host 与 Relay 状态不混淆 |
| Auth/Desk | 新PostgreSQL许可证/站点职责 | 不增加媒体、Relay或Direct Host运行依赖 | 许可证消费者切换、撤销和站点流程按DB3独立验收 |
| Relay | 当前名称、认证、房间/路由、数据转发、反压、统计和维护语义 | 只删除 WebRTC SDP/ICE 中转分支；不得借机重命名或重写其余协议 | 既有连接、转发、重连、限流、关闭和统计专项持续通过 |
| 构建/安装包 | Console、Relay、Web资源和各产品独立制品 | `px_media.exe`、`px_turn.exe`、ZLM/FFmpeg sidecar依赖、Coturn配置/许可证 | 清单拒绝退役文件；聚焦构建通过；最终完整构建留到 DB5 制品验收 |

浏览器从 HTTPS Console 页面直接连接 Render 时，Render 必须提供与目标 hostname 匹配的可信 HTTPS/WSS 边界，或由已明确设计的
同源入口承载直接协商；不能用关闭浏览器证书校验作为产品方案。连接授权继续绑定 owner、资源会话、instance、role、lease 和节点代际，
不得退化为“知道 IP/端口即可访问”。

## 4. 描述符与安全边界

Direct Host 描述符至少携带类型化的：

- `transport = webrtc_direct`；
- `render_host` 与 `render_port`；
- `instance_id`、`resource_session_id` 与明确的 Desktop/CloudApplication/RDP target；
- `access_role`；
- 当前 frontend authorization 与 lease/hard deadline；
- 节点 generation、实例 generation 或等价的防迟到标识。

Console 只能使用节点当前 generation 上报且与实例实际启动 ACK 一致的端点。Render 在处理协商前验证当前授权，续租失败或撤销到达时按
现有硬截止解除 RTC allocation 和逻辑绑定。不得新增裸 URL ticket、设备密码 fallback、任意 caller 自填 stream ID 或仅凭 IP 的准入。

## 5. 归档与活动源码清理

实施时创建独立命名批次，例如 `backup/central_media_retirement_20260919/`，并遵守仓库归档规则：保存原路径、基础 revision、本地修改状态、
退役原因和摘要；共享文件删除分支前先归档完整的修改前内容；归档目录不参与构建、测试、打包和运行加载。

归档范围至少包含：

- Console `live/`、ZLM播放代理/状态/短期播放 ticket；
- Console `media_sidecar` 中 ZLM 与 Coturn 全部生命周期；若无其他责任则整体归档；
- `px_media.exe`、`px_turn.exe`、媒体运行目录、Coturn配置和许可证；
- Render `live_pusher`、RTMP参数和启动接线；
- Windows/Web/Android中经 Relay 交换 RTC SDP/ICE 的客户端分支；
- Console RTC/TURN配置模型、临时凭据、管理路由、前端页面和专属测试；
- 构建复制、安装清单、运维探针和端口/防火墙说明。

不得归档 `src/px_deps/px_webrtc_client`、Render Direct RTC server、Direct Host客户端实现、Console `console_relay/` 的非 RTC 数据能力、
Relay协议主体、录像或文件传输。

## 6. DB0–DB5 接续顺序

1. **范围收敛前置**：完成归档、活动依赖移除和 Relay/Direct Host双回归，修订功能守恒矩阵。
2. **DB0**：冻结 Direct Host描述符、显式 CloudApplication target、端点代际、权限及合成用例；从当前契约移除 ZLM/TURN/中央 RTC字段。
3. **DB1**：正式 `px_console.exe` 切 PostgreSQL组合根；新安装不携带媒体/TURN sidecar，断库 fail-closed。
4. **DB2**：先完成 CM-REALTIME与节点/遥测闭环，再完成命令、实际端点、Direct Host WebRTC、录像、文件、RDP和更新执行器。
5. **DB3**：Console 切新许可证协议、库外水位、额度/feature 门禁及 Auth outbox 认证接触；Service 只消费 Console control epoch，
   不复制 PXLIC 解析器，并清除未使用的旧授权依赖；清除三个产品服务的 Mongo 运行依赖。
6. **DB4**：在短期可重复测试中完成目标 Linux、异机副本、PITR、keyring/witness轮换、签名与节点/RDP恢复对账；自然周期长测后置。
7. **DB5**：全新环境先 Windows后 Android完成短期功能验收和完整制品核验，随后才进入统一长测。

ZLM直播和 Coturn/TURN不再是 DB0–DB5出口。录像、文件、Direct Host WebRTC、Relay既有数据能力及显式记录的延期能力仍是功能防丢门禁。

## 7. 开发期与最终验收

开发期每个切片只做确定性短测：单元/真实 PostgreSQL集成、聚焦编译、前端合同与生产构建、真浏览器或真机短流程、断库/重启/乱序等
故障注入。保留周期用测试时钟推进，不等待数天；完整 release-only构建不用于日常切片。

DB5短期功能出口通过后统一长测：Relay长连接、Direct Host重复建连、Windows/Android持续会话、数据库资源增长、备份/WAL/异机复制自然
周期、Render空闲升级及 Relay冗余排空。长测不再分散阻塞每个开发阶段。

## 8. 实施与验收记录

2026-09-19 已完成以下聚焦实现：

- `backup/central_media_retirement_20260919/` 保存全部退役实现及 91 个共享文件的改前快照，`MANIFEST.md` 与
  `shared_before/SNAPSHOT_SHA256.txt` 固定来源、原因和逐文件 SHA-256；归档不参与活动构建。
- Console 已移除 ZLM/Coturn sidecar、直播/视频墙/中央 RTC 路由、配置和打包；资源描述符只返回当前节点确认的 Direct Host
  `render_host + render_port`，Relay 保留既有非 WebRTC 数据协议。
- Render 只构建 `net_rtc_local`；同一实例端口承载直接协商及 UDP，中央 SDP/ICE 消息、remote RTC DLL、live pusher 和
  `wall_observer` 隐藏入口均已移除。未授权、超时或不可达均 fail-closed，不存在媒体 fallback。
- Windows Client 与 Web Client 已移除 ICE server、RestartIce、中央 signaling 和 TURN/Relay candidate fallback。Web Client
  只建立 `iceServers: []` 的 Direct Host 连接，并返回稳定的 `RTC_DIRECT_REQUIRED` / `RTC_DIRECT_UNREACHABLE` 错误。
- Rust 三个相关 workspace `cargo check`、现代 Web Client 55 项 Vitest 与 19 项语音断言、旧 Web Client 构建均通过。
- Render Direct Host 聚焦用例 3/3 通过；C++ 可读命名与所有权门禁通过。Cloud Node 的 `px_render.exe` / `px_render_rtc.dll`
  SHA-256 分别为 `F5A2268A96E013B527A5A2BE76D21FADB54400CB16C4AABC29BEC3C4B6E92FD5` /
  `CF18B22AEBED1E56CD7339F267EB6F7A05617061B9219F9B7B61DE8FC1C89DC7`；Remote 对应为
  `A2F0472E9B68E83EE583D7A98EC73717733A1E990269AFE66940DF8FFCC8BF4B` /
  `24D71E6B236E756CBFBCBD215E4C18F3447167897D2CD91632B4359313F2C362`，构建树与各自 dist 一致。
- Windows Client 聚焦构建、6 个测试程序共 28 项断言通过；`px_client.exe` 的构建树/dist SHA-256 均为
  `E625806DA98F32A9D3B339EC37D91620A6904E2602A8474D71233D57921922C1`。
- DB2 的 CM-REALTIME 已完成首个纵向切片：独立只读管理 WebSocket、首帧管理员鉴权、有界序号/游标、重启快照边界、心跳、
  固定间隔重连、授权撤销重验、管理写入与节点上报失效通知，以及相关页面自动刷新均已接通。真实节点/PG报告为
  `pg-20260919-063411-302a299c`，真实 Chromium 外部写入刷新与进程重启重连报告为 `pg-20260919-063531-2fcb06ab`；公网高频与
  断库页面专项仍未完成，因此功能矩阵保持部分迁移。
- Render 的资源通道生产者已补齐 Direct Host WebRTC 真实载荷计数：成功编码视频、成功数据通道发送和收到的数据通道载荷按实际
  connection ID 汇总，以 5 秒周期、单调 sequence 和累计字节通过 Render→Service→Console 上报，断开终态携带最后累计值。
  Cloud/Remote Render 与 RTC DLL 聚焦构建、dist 哈希和 3/3 RTC 测试均通过。
- Relay 出站媒体与文件数据的统计已落在实际异步 WebSocket 写成功点，不再把排队当成发送成功；完整写入后按活跃 room 映射真实
  connection ID 并累计原始业务载荷。真实本机 WebSocket 写入回调和 Relay 重连/所有者生命周期 2/2 通过，Cloud/Remote Render
  已重新发布且构建树/dist 哈希一致。该证据仍不覆盖公网 Relay 端到端、Relay 入站/音频、RDP/Native 完整双向计数或公网真实会话，
  这些继续保留在 DB2 出口。

尚未完成且不得被上述聚焦证据冒充：正式 Console 产品入口、真实公网 Windows/Web 首帧/音频/输入/重连/撤销、Relay 数据面真机回归、
Android CloudApplication 真机直连、安装包内容审计，以及所有 DB5 短测通过后的统一长测。
