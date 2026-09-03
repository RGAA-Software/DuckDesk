# GammaRay/GoDesk 架构总览

> 2026-08-24 更新。本文是项目的模块关系与整体理解的单一入口；各专题细节见文末链接的专题文档。

## 1. 这是什么

远程桌面 + 游戏串流系统。两种产品形态共用同一套组件：

- **远程桌面**：每台被控机永远一个默认 render（屏采），panel 是本机管理端；
- **游戏串流（game-hook）**：Console 中心调度，被控机每启动一个游戏就多一个 render 进程（hook 注入采集），观看端用浏览器直达。

部署模型：**安装包即对某个 Console 授权**——Console 管理其下的机器（调度、授权、机器列表），px_auth_server 负责签发/吊销授权（只在授权链路上，不在运行面）。

## 2. 组件清单

| 组件 | 位置 | 角色 |
|---|---|---|
| px_render | `src/px_render` | 采集/编码/流媒体宿主。同一 exe 两种模式：desktop（DDA/GDI 屏采）/ game-hook（启动游戏并注入 `px_gh.dll`，帧经本机 `/ipc` 回传） |
| px_panel | `src/px_panel` | 被控端 Qt 管理 UI。管理桌面 render、把授权推给本机 service、拉起 Windows 观看客户端 |
| px_client | `src/px_client` | Windows 观看端（WS `/media`），由 panel 拉起 |
| px_service | `rust_client/px_service` | 被控机常驻服务。拉起/看管 render、执行 Console 调度（启停游戏实例）、本机控制面 WS `:20375` |
| px_osinfo | `rust_client/px_sysinfo` | 系统信息采集上报 |
| UserProxy | `rust_client/px_user_proxy` | 用户会话代理（剪贴板等），service 看管 |
| px_console_server | `rust_server/px_console_server` | 中心调度：机器列表、应用/实例管理、授权缓存下发，托管 `web/px_console` 管理前端 |
| px_auth_server | `rust_server/px_auth_server` | 授权签发/吊销，HTTPS `:30400` |
| web 观看端 | `web/px_web_client` | 浏览器观看端 SPA，由 render 自己托管在 `/web_client`，WebRTC 收流 |
| px_gh.dll | `src/px_render/hook_capture` | 注入游戏的采集 DLL（hook DXGI Present，共享纹理帧经 `/ipc` 回传 render） |

## 3. 拓扑：控制面与数据面分离

```
                        中心侧
   px_auth_server :30400 (HTTPS, 签发/吊销授权)
        ↑ Console 每小时拉自己的授权 (HMAC appkey 签名)
   px_console_server :30500 (HTTPS/WSS) + 托管 web/px_console 管理前端
        ├─ 应用 Relay：标准 RTC 的 SDP / Trickle ICE 信令
        └─ px_turn :20128 TCP/UDP；relay UDP :20200-20500
        ↑ WSS /console/service  ←—— px_service (每台被控机一条长连接,
        │                          3s 心跳带全量 app 实例状态, 断线固定 2s 重连)
        ↑ HTTPS /api/v1/app/control/* ←—— Console 管理 Web (游戏/实例启停)
        ↑ WSS /console/panel ←—— 被控端 panel

   被控机器内部 (本机明文 WS):
   px_service 监听 127.0.0.1:20375
        ↑ /service/message ←—— 每一个 render (desktop + 每个 game-hook) 1s 心跳
        ↑ ←—— panel (推授权 AuthInfo、拉起桌面 render 的 StartServer)

   数据面:
   观看端 → 可直达时 net_rtc_local；不可直达时 net_rtc + ICE(host/srflx/turn relay)
   Windows 观看端 → WS /media 直连 render
   注入的游戏 DLL → WS /ipc (仅 127.0.0.1) 推采集帧 → render
   桌面 render :20371；game render :32000-32999 (service 端口池)
```

### 方向性要点

- **控制面上 render 是 client**：反连本机 service（`:20375`）和 panel（`/panel/renderer`），利于 NAT/防火墙场景。注意 game render 没有 panel 通道（启动参数不带 `--panel_server_port`）——**panel 只管理那台唯一的桌面 render**。
- **数据面上 render 是 server**：`0.0.0.0:{port}` 同一端口承载 WS（`/media`）、HTTP（`/web_client`、信令 `/alloc/local/rtc`）、`/ipc`（仅 loopback）。
- **WS 与 WebRTC 可同时连接**，无代码级并发上限（每连接一个 router/server 进 map），仅有每连接发送队列的丢帧保护。

## 4. 游戏实例生命周期（Console 调度链路）

```
Console Web「启动」→ Console manager 按 (app_id, device_id) 找 placement
  → 复用 /console/service 长连接下发 StartAppInstance (HTTP 阻塞等结果, 25s 超时)
  → service 分配端口(32000+池)、校验游戏路径、拉起 render
  → render 拉起游戏并注入 → 观看 URL: http://{IP}:{port}/web_client/?deviceId=..&instanceId=..
停止：Console 置 Stopping → WS 下发 → service 按 pid 身份校验杀 render+游戏进程树
对账：3s 心跳带全量实例状态双向对齐；断线 15s 宽限后才空列表对账防抖动误杀
```

- **多 render 区分键**：`instance_id`（Console 全局唯一）+ `listen_port`（机器级唯一，cmdline token 精确匹配防 3200 误中 32000）；pid 仅辅助，kill 前必须重验身份防 pid 复用误杀。
- **UE 启动器（boot/view 双路径）**：UE 顶层 exe 是 bootstrap 外壳，service 解析其 `RT_RCDATA` 资源 201/202 得到真游戏路径，外壳照常启动、render 注入真游戏进程。详见 `game_hook_capture_plan.md` §10。

## 5. 关键设计决策

1. **心跳对账而非指令可信**：启动/停止有结果回执，但权威状态来自心跳全量上报 + Console 双向对账（Console 认为活但心跳没有 → Stopped；反之恢复 Running）。Console 重启有状态愈合。
2. **固定间隔无限重试**（2026-08-08 起，全项目无指数退避）：service↔Console 重连 2s、心跳 3s；render/user_proxy 重启冷却 3s；注入失败 100ms；sysinfo 重连 1s；音频致命重启 2s。
3. **同机互信**：`/ipc` 仅 loopback 不做 token 鉴权（token 管道已整体移除）；Console `force_authorize=false` 时鉴权全放行（测试用，**出包必须 true**）。
4. **失败快速可观测**：32 位游戏明确拒绝注入；游戏管理员权限 ACCESS_DENIED 明确报错并持续重试；致命错误经 callback 上报。

## 6. 逻辑会话与并发产品边界

一个 desktop Render 的正式产品模型是“**一控多观**”：只有一个 Controller，可以有多个 Observer；设备只配置“允许接管”，开启后任一 Observer 接管即获得完整主控能力并使旧主控降为观察者，关闭后不得替换当前主控。该规则同时适用于 Console 授权和无 Console 的直接 IP 访问；不携带设备 ID 的 IP 直连由 Panel 在启动客户端进程前验证本机随机密码或安全密码，并让 Render 预留一个短期 `stream_id`。子进程只使用正常连接参数连接该流，不接收密码或额外授权变量，也不重复校验密码。该路径不回填 Console 设备号、不校验 Console ticket/direct-session grant；只有 Render 在预验证阶段生成的 `stream_id` 有效，调用方自填的值无效。Console 调度的 game-hook/WebView 也可按应用配置开放观看与接管，但只经 Console，控制目标分别是游戏 IPC 与 CEF 页面。

WS、RTC、文件传输是同一逻辑会话的可靠 transport binding；传输切换或单一文件通道关闭不得误报用户离线。UDP Direct 只承载已由可靠控制面关联的音视频与媒体反馈，绝不承担认证、角色、接管、输入或会话生命周期；它目前天然只能服务一个媒体接收端，首期多观察只承诺 RTC Local。完整定义、能力矩阵和改造顺序见 `logical_session_product_definition.md`。

WS + UDP 模式中，WS 在会话准入后记录短期的首次 UDP 媒体端点关联；UDP Hello 只登记对应的 `IP:port`。关联码仅保护首次登记，之后由心跳维持端点、由 WS binding 撤销，并允许同一已关联端点完成 NAT 换端口；UDP 首帧探测失败时客户端重建 WS 媒体 binding。UDP 的丢包、端点更换和超时只影响媒体可用性，不能触发 `ClientConnected`、`ClientDisconnected` 或控制权改变。

## 7. 已知缺口（正式化前要补）

1. **授权链对 panel 的硬依赖**：service 连 Console 的地址/凭据由 panel 经本机 WS 推来——panel 不运行机器就永远离线。目标模型下应改为「安装包写入凭据，service 直连 Console」，panel 降级为可选入口。
2. **数据面零鉴权**：`/media` 只校验 stream_id 非空、`/alloc/local/rtc` 裸开——同网段知道 IP:port 就能拉流。应由 Console 签发带时效的观看 token，render 校验。
3. **标准 RTC 的异网生产门禁尚未完成**：Console 托管 Coturn，`net_rtc` 支持动态 ICE、配置热更新/ICE restart、Direct 失败后标准 RTC 回退和候选统计。本机与90号机已经通过强制 TURN UDP、受控 TURN TCP 回退、自动 Direct、全功能和重连验收；仍需两台不同公网/NAT 的真实 relay、对称 NAT、并发 allocation 和端口耗尽门禁。详见 `webrtc_rtc_acceptance_report_20260824.md`。
4. **本机控制面 :20375 无鉴权**：本机任意进程可推 AuthInfo/StartServer 让 service 拉进程，本地提权面。

## 8. 专题文档索引

- game-hook 采集/注入总纲：`game_hook_capture_plan.md`（含 §10 UE boot/view、§11 事件重放与焦点保持）
- 音频采集（PID loopback / 进程内 hook）：`game_hook_audio_capture.md`
- Console 调度状态与测试：`console_app_schedule_plan.md`、`console_app_schedule_state.md`
- 构建/部署：`../build_doc.md`、`gammaray/How_to_*.md`
- WebRTC/Coturn 配置、构建与验收：`webrtc_coturn_implementation_plan.md`
- 多用户会话、控制租约与无 Console 直连产品契约：`logical_session_product_definition.md`
