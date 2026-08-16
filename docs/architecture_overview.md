# GammaRay/GoDesk 架构总览

> 2026-08-09 整理。本文是项目的模块关系与整体理解的单一入口；各专题细节见文末链接的专题文档。

## 1. 这是什么

远程桌面 + 游戏串流系统。两种产品形态共用同一套组件：

- **远程桌面**：每台被控机永远一个默认 render（屏采），panel 是本机管理端；
- **游戏串流（game-hook）**：CMS 中心调度，被控机每启动一个游戏就多一个 render 进程（hook 注入采集），观看端用浏览器直达。

部署模型：**安装包即对某个 CMS 授权**——CMS 管理其下的机器（调度、授权、机器列表），px_auth_server 负责签发/吊销授权（只在授权链路上，不在运行面）。

## 2. 组件清单

| 组件 | 位置 | 角色 |
|---|---|---|
| px_render | `src/px_render` | 采集/编码/流媒体宿主。同一 exe 两种模式：desktop（DDA/GDI 屏采）/ game-hook（启动游戏并注入 `px_gh.dll`，帧经本机 `/ipc` 回传） |
| px_panel | `src/px_panel` | 被控端 Qt 管理 UI。管理桌面 render、把授权推给本机 service、拉起 Windows 观看客户端 |
| px_client | `src/px_client` | Windows 观看端（WS `/media`），由 panel 拉起 |
| px_service | `rust_client/px_service` | 被控机常驻服务。拉起/看管 render、执行 CMS 调度（启停游戏实例）、本机控制面 WS `:20375` |
| px_osinfo | `rust_client/px_sysinfo` | 系统信息采集上报 |
| UserProxy | `rust_client/px_user_proxy` | 用户会话代理（剪贴板等），service 看管 |
| px_cms_server | `rust_server/px_cms_server` | 中心调度：机器列表、应用/实例管理、授权缓存下发，托管 `web/px_cms` 管理前端 |
| px_auth_server | `rust_server/px_auth_server` | 授权签发/吊销，HTTPS `:30400` |
| web 观看端 | `web/px_web_client` | 浏览器观看端 SPA，由 render 自己托管在 `/web_client`，WebRTC 收流 |
| px_gh.dll | `src/px_render/hook_capture` | 注入游戏的采集 DLL（hook DXGI Present，共享纹理帧经 `/ipc` 回传 render） |

## 3. 拓扑：控制面与数据面分离

```
                        中心侧
   px_auth_server :30400 (HTTPS, 签发/吊销授权)
        ↑ CMS 每小时拉自己的授权 (HMAC appkey 签名)
   px_cms_server :30500 (HTTPS/WSS) + 托管 web/px_cms 管理前端
        ↑ WSS /cms/service  ←—— px_service (每台被控机一条长连接,
        │                          3s 心跳带全量 app 实例状态, 断线固定 2s 重连)
        ↑ HTTPS /api/v1/app/control/* ←—— CMS 管理 Web (游戏/实例启停)
        ↑ WSS /cms/panel ←—— 被控端 panel

   被控机器内部 (本机明文 WS):
   px_service 监听 127.0.0.1:20375
        ↑ /service/message ←—— 每一个 render (desktop + 每个 game-hook) 1s 心跳
        ↑ ←—— panel (推授权 AuthInfo、拉起桌面 render 的 StartServer)

   数据面 (不过 CMS):
   观看端浏览器 → http://{被控IP}:{render端口}/web_client → WebRTC 直连 render
   Windows 观看端 → WS /media 直连 render
   注入的游戏 DLL → WS /ipc (仅 127.0.0.1) 推采集帧 → render
   桌面 render :20371；game render :32000-32999 (service 端口池)
```

### 方向性要点

- **控制面上 render 是 client**：反连本机 service（`:20375`）和 panel（`/panel/renderer`），利于 NAT/防火墙场景。注意 game render 没有 panel 通道（启动参数不带 `--panel_server_port`）——**panel 只管理那台唯一的桌面 render**。
- **数据面上 render 是 server**：`0.0.0.0:{port}` 同一端口承载 WS（`/media`）、HTTP（`/web_client`、信令 `/alloc/local/rtc`）、`/ipc`（仅 loopback）。
- **WS 与 WebRTC 可同时连接**，无代码级并发上限（每连接一个 router/server 进 map），仅有每连接发送队列的丢帧保护。

## 4. 游戏实例生命周期（CMS 调度链路）

```
CMS Web「启动」→ CMS manager 按 (app_id, device_id) 找 placement
  → 复用 /cms/service 长连接下发 StartAppInstance (HTTP 阻塞等结果, 25s 超时)
  → service 分配端口(32000+池)、校验游戏路径、拉起 render
  → render 拉起游戏并注入 → 观看 URL: http://{IP}:{port}/web_client/?deviceId=..&instanceId=..
停止：CMS 置 Stopping → WS 下发 → service 按 pid 身份校验杀 render+游戏进程树
对账：3s 心跳带全量实例状态双向对齐；断线 15s 宽限后才空列表对账防抖动误杀
```

- **多 render 区分键**：`instance_id`（CMS 全局唯一）+ `listen_port`（机器级唯一，cmdline token 精确匹配防 3200 误中 32000）；pid 仅辅助，kill 前必须重验身份防 pid 复用误杀。
- **UE 启动器（boot/view 双路径）**：UE 顶层 exe 是 bootstrap 外壳，service 解析其 `RT_RCDATA` 资源 201/202 得到真游戏路径，外壳照常启动、render 注入真游戏进程。详见 `game_hook_capture_plan.md` §10。

## 5. 关键设计决策

1. **心跳对账而非指令可信**：启动/停止有结果回执，但权威状态来自心跳全量上报 + CMS 双向对账（CMS 认为活但心跳没有 → Stopped；反之恢复 Running）。CMS 重启有状态愈合。
2. **固定间隔无限重试**（2026-08-08 起，全项目无指数退避）：service↔CMS 重连 2s、心跳 3s；render/user_proxy 重启冷却 3s；注入失败 100ms；sysinfo 重连 1s；音频致命重启 2s。
3. **同机互信**：`/ipc` 仅 loopback 不做 token 鉴权（token 管道已整体移除）；CMS `force_authorize=false` 时鉴权全放行（测试用，**出包必须 true**）。
4. **失败快速可观测**：32 位游戏明确拒绝注入；游戏管理员权限 ACCESS_DENIED 明确报错并持续重试；致命错误经 callback 上报。

## 6. 已知缺口（正式化前要补）

1. **授权链对 panel 的硬依赖**：service 连 CMS 的地址/凭据由 panel 经本机 WS 推来——panel 不运行机器就永远离线。目标模型下应改为「安装包写入凭据，service 直连 CMS」，panel 降级为可选入口。
2. **数据面零鉴权**：`/media` 只校验 stream_id 非空、`/alloc/local/rtc` 裸开——同网段知道 IP:port 就能拉流。应由 CMS 签发带时效的观看 token，render 校验。
3. **观看端可达性假设直连**：WebRTC 无 STUN/TURN，跨 NAT 不通；cms relay 存在但 game render 场景未走。
4. **本机控制面 :20375 无鉴权**：本机任意进程可推 AuthInfo/StartServer 让 service 拉进程，本地提权面。

## 7. 专题文档索引

- game-hook 采集/注入总纲：`game_hook_capture_plan.md`（含 §10 UE boot/view、§11 事件重放与焦点保持）
- 音频采集（PID loopback / 进程内 hook）：`game_hook_audio_capture.md`
- CMS 调度状态与测试：`cms_app_schedule_plan.md`、`cms_app_schedule_state.md`
- 构建/部署：`../build_doc.md`、`gammaray/How_to_*.md`
