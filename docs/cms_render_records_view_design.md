# CMS Web 查看 Render 录像 — 设计方案

> 状态：已评审（v1 决策已锁定，见 §1.1 决策记录）
> 范围：CMS Web 页查看 render 端录屏文件（`C:\Users\Public\Pixels\px_render_records`）
> 核心决策：**不做全量上传归档**，panel 作为录像的稳定出口；同网段直连，上层网段按需代理。

---

## 1. 背景与目标

render 端录屏已落地：录像文件落在本机 `C:\Users\Public\Pixels\px_render_records\`（`rec_{monitor}_{YYYYMMDD}_{HH.MM.SS}.mp4`，1GB/段滚动，24 个文件上限）。目标是在 **CMS Web** 上按设备查看这些录像（列表 + 播放 + 下载）。

两种部署拓扑：

1. **同网段**：CMS 与所有 render（及其所在机器）在同一个局域网，可直接访问。
2. **上层网段**：CMS 在上层/中心网段，管理大量分散的 render，设备在 NAT/防火墙后，CMS 无法主动直连。

### 1.1 决策记录（评审后锁定）

| 决策点 | 结论 | 理由 |
|---|---|---|
| CMS web 部署协议 | **局域网场景 CMS 整体走 HTTP** | HTTPS CMS 页面内嵌 `http://` 内容（panel 录像、render 托管的 web client iframe）会被浏览器混合内容拦截；现有 VideoWall 嵌 `http://设备IP` web client 已踩此坑。给每台设备 IP 签 SAN 证书运维成本过高 |
| HTTP 部署的功能损失 | 可接受 | WebRTC 远控（RTCPeerConnection/DataChannel）不受 secure context 限制，web client 本就由 render 以 HTTP 托管；损失的仅：麦克风上行（getUserMedia）、`navigator.clipboard` 剪贴板读写、文件传输 SHA-256 校验（已有降级，不阻断传输）。注：`localhost`/`127.0.0.1` 访问仍算 secure context，本机操作不受限 |
| cms_server 改造 | 加 HTTP 监听配置开关 | 当前 `cms_server.rs` 只绑 rustls（证书缺失直接拒启动），HTTP 明文口仅 `/ping`。需加配置项使主服务可绑纯 HTTP |
| CMS 反代路由 | **v1 不做** | HTTP 下同网段直连无混合内容问题；反代唯一不可替代的场景是"web 与 panel 不同网段、但 CMS 同时够得到两边"，出现时再加 `/api/v1/record/proxy`（流式透传 Range） |
| 拓扑 2 首播延迟 | 接受为已知限制 | 首播延迟 = 整段回传时间（1GB 分段在百兆网约 1 分钟级）；web UI 必须显式展示"回传中 x%"，后续可优化为流式上行边下边播 |
| 录制中文件过滤 | **sidecar 标记**，不用 mtime 启发式 | 滚动写盘间隙可能超过阈值，mtime 判定会把 moov 未写的文件误判为可播（黑屏）。录制中写 `xxx.mp4.recording` 标记文件，写完删除；列表/回传只暴露无标记文件 |
| 回传幂等键 | `文件名+大小+mtime` | 1GB 文件算 md5 需数秒且阻塞上传，不值得 |
| panel `/records` 鉴权 | **强制开启**，cms 签发短时效 ticket（HMAC,§5.3） | 20369 在局域网裸露，录像内容敏感；`<video>` 无法带 Authorization 头，裸口令放 URL 会进浏览器历史/日志 |
| panel IP 下发 | panel 握手时上报本机网卡 IP 列表（`panel_lan_ips` 数组） | 长连接来源 IP 在跨路由/NAT 场景会拿到网关地址，不可靠 |
| 编码兼容性 | **已确认（任务 0 完成）**：默认会话 = H264/yuv420p + Opus 48k stereo in MP4，Chrome/Edge 可播（Chromium 官方支持 MP4 容器 + Opus 编码，见 `rd_settings.h:34` 默认 kH264、编码由启动参数 `--encoder_format` 决定且默认 h264）；仅"全彩模式"（Qt 客户端手动开启，默认关）会产生 H265/yuv444 不可播录像 | 录制直接 remux 编码产物（`media_recorder_plugin.cpp:147`）。v1 策略：列表按编码标记，H265 文件"仅可下载"；Safari 对 Opus-in-MP4 支持差，本功能目标浏览器限定 Chrome/Edge；联调时用真实录像实测出声（§9.3） |

## 2. 为什么出口是 panel，而不是 render

| 维度 | render | panel（出口） |
|---|---|---|
| 进程性质 | 随时会死（崩溃/被重启）、**可多开** | 常驻、服务托管、单例 |
| 与 cms 连接 | **无**（render 与 cms 无长连接，也无 cms 客户端代码） | 与 cms 保持 `/cms/panel` 长连接；`cms_device_api`（px_cms_client 库）被 panel 广泛使用 |
| 本机 HTTP 服务 | net_ws 插件 20371（但随 render 生死） | **自带 asio2 `http_server`，监听 20369**（`ws_panel_server.cpp`，现挂 ws 端点，可加 HTTP 路由） |
| 录像目录访问 | 写入方 | **同机可读**（公共目录 `Public\Pixels`），render 死活不影响 |

结论：panel 常驻、单例、同机可读录像目录、与 cms 有现成长连接和 HTTP 客户端——是录像的稳定出口。render 死掉/多开不影响已录文件的查看与回传。

## 3. 现状可复用能力（代码依据，已逐条核实）

| 能力 | 位置 | 说明 |
|---|---|---|
| panel HTTP 服务器 | `src/px_panel/src/render_panel/network/ws_panel_server.cpp:123`（asio2 `http_server`，默认 20369，`px_settings.cpp:264`） | 现挂 `/panel`、`/panel/renderer` ws 端点；可加普通 HTTP 路由（列表 JSON + 文件流 + Range） |
| panel→cms 长连接 | `/cms/panel`（panel ws 客户端；cms 侧 `cms_server.rs:153`，HMAC token 鉴权 `cms_ws_token_filter.rs`） | 双向消息通道；另有 `/cms/service`（service 长连） |
| panel→cms HTTP 客户端 | `HttpClient::MakeSSL`（panel 多处使用）+ `px_cms_client` 库 | 鉴权上报模式现成；CMS 改 HTTP 后需支持明文连接 |
| cms 文件上传接口模式 | `rust_server/px_cms_server/src/update/update_handle.rs:37`（multipart → 存 `uploads/` → mongo 记录 → 返回下载路径） | **按需拉取可完全仿照** |
| cms 静态伺服 | `cms_server.rs:94` `"/uploads"` ServeDir | `uploads/` 下文件已可直接 HTTP 播放（mp4 + Range 拖拽） |
| cms 路由 | `/api/v1/record`（visit/ft 上报）、`/api/v1/update` | 新录像接口同构挂载 |
| 录像目录 | `C:\Users\Public\Pixels\px_render_records`（`media_recorder_plugin.cpp:51`） | panel/render 同机均可读 |

## 4. 总体架构

```
                    ┌─ 本机 ────────────────────────────┐
   render(易死/多开) │ 写 px_render_records(公共目录)      │
                    │ 录制中写 sidecar: xxx.mp4.recording │
                    └──────────────┬─────────────────────┘
                                   │ panel 常驻/单例/同机可读
                                   ▼
  拓扑1(同网段):  web ──HTTP──► panel:20369 /records ── 列表+文件(Range)
  拓扑2(上层网段): web ──► cms ──经 /cms/panel 隧道发指令──► panel
                        ◄── panel 按需调 cms 上传接口回传文件(临时缓存) ──► web 播放
```

- 设备本地录像**保留**，沿用本地 1GB/24 文件滚动清理；cms 侧**不做长期归档**。
- 拓扑 1 与拓扑 2 共用同一套 panel 端实现（HTTP 路由 + 回传能力），差异只在触发方。
- CMS web 按 HTTP 部署（§1.1），拓扑 1 直连无混合内容问题；**v1 不做 CMS 反代**（出现"web 与 panel 不同网段但 CMS 同时够得到两边"的部署时，再加 `/api/v1/record/proxy` 流式透传）。

## 5. 拓扑 1：同网段直连 panel

### 5.1 panel HTTP 路由（20369 新增）

| 路由 | 方法 | 说明 |
|---|---|---|
| `GET /records` | GET | 返回录像文件列表 JSON（文件名、大小、mtime、monitor 名），限 `px_render_records` 目录 |
| `GET /records/{filename}` | GET | 文件流，**支持 Range**（`<video>` 拖拽播放）；文件名做路径穿越防护 |
| `GET /records/info` | GET | 可选：目录剩余空间/总大小等 |

- **Range 需手工实现**：asio2 `http_server` 不带 Range 支持，需自行解析 Range 头、回 `206 + Content-Range + Accept-Ranges`，处理越界（`416`）与多段请求（可拒绝多段，浏览器只发单段）。这是拓扑 1 唯一的硬骨头，工作量按"小偏中"估计。
- 鉴权：**强制开启**,cms 签发短时效 ticket（HMAC,§5.3），panel 本地校验；后续可接安全密码体系。
- 录制中文件（moov 未写）不可播：以 **sidecar 标记**（`xxx.mp4.recording`）为准过滤，仅暴露已完成的段/已停止的文件。**不用 mtime 启发式**（滚动写盘间隙会误判）。

### 5.2 web 端

- CMS Web 录像页：设备列表（cms 已知设备）→ 展开录像 → 播放地址 = `http://{panel_ip}:20369/records/{file}`。
- **panel IP 来源**：panel 在 `/cms/panel` 长连接握手时上报本机网卡 IP 列表，cms 存设备表字段 `panel_lan_ips`（数组）透传前端；前端逐个尝试（通常第一个即通）。**不用长连接来源 IP**（跨路由/NAT 场景拿到的是网关地址）。
- 播放：`<video>` + Range（浏览器原生拖拽）；下载按钮分两种：**下载到本机**（浏览器直下，同 URL）与**下载到 CMS**（`POST /api/v1/record/download`,cms 服务端从 panel 拉取落盘为保留副本，见 §6.4）。

### 5.3 拓扑选择与拓扑 1 的鉴权 ticket

**web 侧自动选择拓扑**：cms 不知道浏览器的网络位置，由前端探测——拿到 `panel_lan_ips` 后对第一个 IP 发 `GET /records/info`（约 2s 超时）；通则走拓扑 1 直连，不通（或全部超时）则自动回退拓扑 2 回传流程，用户无感知。

**拓扑 1 鉴权与 `<video>` 的矛盾**：`<video>`/`<a download>` 无法携带自定义 Authorization 头，口令只能走 query string 或 cookie。v1 采用 **cms 签发的短时效 ticket**：

```
web 向 cms 请求播放地址 → cms 用与 panel 的共享口令计算
  tk = HMAC(device_id + filename + expire_ts, shared_secret)  (过期如 10 分钟)
  返回 http://{panel_ip}:20369/records/{file}?tk=...&exp=...
panel 校验 tk + exp,过期/不符一律 403
```

- 长期口令不出现在 URL/浏览器历史/日志里；ticket 泄露窗口只有分钟级。
- panel 侧校验是纯本地 HMAC 计算，无需回问 cms（设备离线查看不受影响）。
- v1 从简也可先把共享口令直接放 query（内网可接受），但 ticket 机制是推荐落地形态，工作量差异很小。

## 6. 拓扑 2：上层网段按需代理

### 6.1 流程（按需拉取 + 临时缓存，不长期存储）

```
web 请求设备 X 的录像列表/播放
  → cms 经与设备 X 的 panel 的 /cms/panel 隧道发送指令"列出录像 / 回传文件 F"
  → panel 读本地 px_render_records 响应列表 / 调 cms 上传接口回传文件 F(带 device_id + 会话令牌)
  → cms 存 uploads/records/{device_id}/ 临时目录, 写 mongo c_records(临时标记)
  → cms 静态伺服该文件给 web 播放(/uploads, Range 天然支持)
  → 临时文件 TTL 到期/磁盘阈值触发清理
```

- 隧道只传**指令和小 JSON**（列表、回传请求），大文件走 panel→cms 的普通 HTTP 上传接口（复用现有 multipart 模式），cms 内部零流式转发复杂度。
- **回传进度机制**：cms 在上传端点按 `Content-Length` 累计已收字节，写 `c_records` 进度字段；web 轮询 `list`/状态接口展示百分比，无需额外通道。
- **并发去重**：同一 `device_id + 文件名` 的 in-flight 回传在 cms 侧去重（多用户同时点播同一文件只触发一次），panel 侧按设备串行上传（§7.2）。
- **已知限制：首播延迟 = 整段回传时间**（1GB 分段在百兆网约 1 分钟级）。web UI 必须显式展示"回传中 x%"进度，避免用户误判为故障；后续可演进为流式上行边下边播（不在 v1）。

### 6.2 隧道消息（/cms/panel 通道内，建议新 protobuf 或 JSON 消息族）

| 方向 | 消息 | 说明 |
|---|---|---|
| cms→panel | `RecordListReq{device_id}` | 请求录像列表 |
| panel→cms | `RecordListResp{files[]}` | 文件名/大小/mtime/monitor |
| cms→panel | `RecordFetchReq{device_id, filename, token}` | 请求回传单个文件（token 防重放） |
| panel→cms | 回传动作：调用 `POST /api/v1/record/upload`（multipart，带 token） | 复用文件上传接口 |
| cms→panel | `RecordFetchDone{filename, ok}` | 可选：回传完成通知/错误 |

- 消息收发挂在现有 `/cms/panel` 连接处理里（panel 侧现有 cms ws 客户端；cms 侧现有 panel ws 会话管理），新增消息类型即可。
- **实现落地（已锁定）**：该通道本就是 protobuf 二进制帧（`CmsPanelMessage`,`src/px_deps/px_server_protocol/cms_panel.proto`，两端自动生成），新消息族直接扩 proto（`kRecordListReq/kRecordListResp/kRecordFetchReq/kRecordFetchDone` + `RecordFileInfo` 等，字段号 50-53）;`CmsPanelHello` 增加 `panel_lan_ips`/`panel_http_port`。未采用 JSON 包层。

### 6.3 cms 新增接口

| 接口 | 说明 |
|---|---|
| `GET /api/v1/record/access?device_id=` | 拓扑 1 入口：返回 `{device_id, panel_lan_ips[], panel_port, online}`，前端据此探测直连 |
| `GET /api/v1/record/ticket?device_id=&file=` | 签发 ticket（HMAC-SHA256，key=设备 `safety_pwd_md5`,exp=now+600s）;`file=*` 用于列表/info。播放 URL = `http://{ip}:{panel_port}/records/{file}?tk&exp` |
| `POST /api/v1/record/upload` | multipart 上传（仿 `handle_upload_update_info`）；参数 `device_id` + 一次性 `token`（隧道签发，10 分钟，验后作废）；存 `uploads/records/{device_id}/`；mongo `c_records`（进度节流更新，完成置 ready） |
| `GET /api/v1/record/list?device_id=` | 经隧道向 panel 要列表（10s 超时；离线 503/626，超时 504/627），合并 `c_records` 状态返回：`state ∈ none\|fetching\|ready\|error`,ready 时带 `url` |
| `GET /api/v1/record/fetch?device_id=&file=` | 经隧道触发回传；幂等命中直接 ready;in-flight 去重只触发一次；立即返回 `{state:"fetching"}`，前端轮询 list 看 `progress/total` |
| `POST /api/v1/record/download` | **下载到 CMS 机器**（§6.4）；两种拓扑统一入口，落盘并标记 `keep` |
| `DELETE /api/v1/record/{id}` | 删除 mongo + 磁盘文件（id = `{device_id}:{filename}` URL-encode）；不动设备侧文件 |

- **临时清理**：TTL（停止查看 24h）与 `uploads/records` 磁盘阈值（10GB）双触发，按 device_id 粒度轮询清理；**`keep=true` 的保留副本不参与自动清理**（§6.4）。
- 幂等：同 `device_id + 文件名 + 大小 + mtime` 已存在则跳过重传（**不算 md5**，1GB 文件哈希需数秒且阻塞上传）。

### 6.4 下载到 CMS（用户触发的服务端落盘与保留）

区别于"浏览器下载到操作者电脑"，此功能把录像**保存到 CMS 服务器本机**，供集中留存/分发：

| 拓扑 | 数据路径 |
|---|---|
| 拓扑 1（同网段） | cms 服务端作为 HTTP 客户端直连 `http://{panel_ip}:20369/records/{file}`（带口令），流式落盘，不经浏览器 |
| 拓扑 2（上层网段） | 复用 §6.1 隧道回传链路；落盘后将该记录从临时缓存转为保留副本 |

- **存放**：与临时缓存同目录 `uploads/records/{device_id}/`，mongo `c_records` 记录加 `keep=true` 标记。
- **保留语义**：`keep=true` 的记录**豁免 TTL 与磁盘阈值自动清理**，只能由 web 手动 `DELETE`（它们是用户有意的留存）；临时缓存（`keep=false`）清理策略不变。
- **幂等**：同 §6.3 幂等键命中且已 `keep=true` → 直接返回成功；命中临时缓存 → 仅翻标记，不重传。
- **进度**：下载为服务端异步任务，`GET /api/v1/record/list` 返回每项的下载状态（未下载/下载中 x%/已保留），web 轮询刷新。
- **设备侧文件不动**：下载是"拷一份到 CMS"，设备本地录像保留并沿用滚动清理（§7.3）。

## 7. 关键设计点（通用）

1. **只有已完成的文件可看/可传**：以 sidecar 标记（`xxx.mp4.recording` 存在即录制中）为准，不用 mtime 启发式。
2. **panel 上传失败重试**：panel 侧按设备串行上传 + 限速（避免挤爆 cms 带宽）；失败指数退避重试，不丢任务（本地待传清单）。
3. **不删除本地文件**：cms 只是"临时查看缓存"（+ 用户触发的 `keep` 保留副本，§6.4），本地保留并沿用滚动清理；若未来要"归档中心"，加全量上传开关即可（同接口）。
4. **安全**：panel `/records` 强制 ticket 鉴权（§5.3，同网段暴露）；回传 token 短时效 + 绑定会话；上传接口验 appkey+device_id。
5. **路径防护**：文件名只允许 `[A-Za-z0-9_.-]` 白名单，杜绝目录穿越。
6. **多 render 机器**：panel 单例保证同一台机器只有一个出口；多开 render 写入同一公共目录，列表天然聚合。
7. **设备下线**：拓扑 2 设备离线时列表/回传失败，web 明确提示（文件在设备本地，无法代理查看）。

## 8. 实施计划与工作量预估

| # | 模块 | 内容 | 量级 | 依赖 |
|---|---|---|---|---|
| 0 | ~~编码兼容性验证~~（**已完成**） | 代码走读确认：默认 H264/yuv420p + Opus 48k in MP4，Chrome/Edge 可播；H265 仅全彩模式产生（默认关）。联调时用真实录像实测出声（§9.3） | — | 已做 |
| 1 | cms_server HTTP 开关 | 主服务可配置绑纯 HTTP（现状仅 rustls + HTTP `/ping`）；配置项 `ssl_enable = false` 时走明文监听 | 小 | 无 |
| 2 | 录制 sidecar 标记 | media_recorder 写/删 `xxx.mp4.recording` | 小 | 无 |
| 3 | panel HTTP 路由 | `/records` 列表 + 文件流 + **手工 Range** + ticket 校验（§5.3）+ 路径防护 + sidecar 过滤 | 小偏中 | 2 |
| 4 | panel 上报 `panel_lan_ips` | `/cms/panel` 握手携带本机网卡 IP 列表；cms 存设备表 | 小 | 无 |
| 5 | panel 隧道消息 | /cms/panel 新增消息族 + 回传触发（串行 + 限速 + 退避重试） | 小 | 3 |
| 6 | cms rust | record/upload（仿 update_router）+ list/fetch/download 路由 + ticket 签发 + 回传进度透出 + 并发去重 + 临时清理（豁免 keep）+ mongo `c_records`（含 `keep`/进度字段） | 中 | 5 |
| 7 | web/px_cms | 录像页（设备→拓扑自动探测→列表→播放→下载到本机/**下载到 CMS**→回传进度与下载状态展示；H265 文件标记"仅可下载"） | 中 | 4（拓扑1）、6（拓扑2） |
| 8 | 联调 | 拓扑 1 同网段直连 + 拓扑 2 按需回传，按 §9 测试矩阵执行 | 小 | 7 |

## 9. 测试计划

### 9.1 单元测试（panel 侧，仿 `px_media_record_new/tests` 的 gtest 模式）

| 用例 | 验证点 |
|---|---|
| Range 解析 | 单段正常（`bytes=0-`、`bytes=100-199`）、越界回 `416`、多段请求拒绝（`416` 或忽略）、`bytes=-500` 尾段 |
| 路径防护 | `../`、`..\\`、绝对路径、非白名单字符（中文/空格/分号）一律 400；白名单内正常 |
| sidecar 过滤 | 造 `a.mp4` + `a.mp4.recording` → 列表不含 `a.mp4`；删 sidecar 后出现 |
| 鉴权 | 无 ticket/错 ticket/过期 ticket 一律 403；正确 ticket 200；HMAC 校验纯本地（不回问 cms） |
| 列表字段 | 文件名/大小/mtime/monitor/编码格式（供 web 标记"仅可下载"）解析正确（`rec_{monitor}_{date}_{time}.mp4` 含下划线 monitor 名） |

### 9.2 panel 路由单机集成测试（curl，不等 web）

- 本机起 panel，`px_render_records` 放测试 mp4：
  - `curl -H 口令 http://127.0.0.1:20369/records` 列表正确
  - `curl -r 0-1023 .../records/a.mp4` → `206` + 1KB；`-r 999999999-` → `416`
  - 录制中文件（手工造 `.recording`）不出现在列表、不可下载
  - `curl .../records/../../etc/passwd` → 400

### 9.3 拓扑 1 端到端（双机：本机 CMS + 10.0.0.90 设备）

- **测试视频预制（不手工录屏）**：`tests/gen_test_videos.sh` 用 ffmpeg（testsrc2 彩条 + 正弦音轨，h264/aac/yuv420p/+faststart）生成 `tests/test_videos/render/` 与 `tests/test_videos/client/` 两组样例，命名与 `px_media_record_new` 一致（`rec_{monitor}_{YYYYMMDD}_{HH.MM.SS}.mp4`），每组各含一个 `.recording` sidecar 模拟录制中文件；部署时直接拷贝到 10.0.0.90（administrator）的 `C:\Users\Public\Pixels\px_render_records` / `px_client_records`
- 10.0.0.90 部署 panel + render（render 无需实际录屏）
- CMS web（HTTP 部署）→ 设备 → 录像列表 → `<video>` 播放 + **拖拽进度条**（验证 Range 生效，DevTools Network 应见 206）
- **真实录像播放验证（关键）**：预制视频是 h264/aac 不够，须取一段真实 render 录制文件放入 `px_render_records`，确认浏览器可播；若实际为 H265 则验证列表"仅可下载"标记与下载流程，并回报任务 0 的决策结果
- **拓扑自动选择**：直连可达时走拓扑 1；断开局域网连通性（防火墙拦 20369 入方向）后前端自动回退拓扑 2，无报错弹窗
- 下载到本机：浏览器直下，落盘文件 md5 与设备上一致
- **下载到 CMS**:`POST /api/v1/record/download` 后文件落盘 `uploads/records/{device_id}/`(md5 与设备一致)、`c_records` 标记 `keep=true`；重复下载幂等（不重传仅翻标记）；list 接口可见下载状态流转（下载中→已保留）
- 验证 `panel_lan_ips` 透传：设备多网卡时下发的列表正确
- **浏览器控制台无混合内容告警**（HTTP 部署的回归点）

### 9.4 拓扑 2 端到端（CMS 与设备隔离直连）

- 设备防火墙阻断 CMS→panel 方向（模拟上层网段），仅允许 panel→CMS 出方向
- web 请求列表 → 隧道指令 → 列表回显
- web 点播放 → 回传触发 → **UI 显示"回传中 x%"** → 完成后可播可拖拽（走 cms `/uploads`）
- 幂等：同一文件重复点播不重复回传（`文件名+大小+mtime` 命中缓存）
- **下载到 CMS（拓扑 2)**：走同一回传链路，落盘后标记 `keep=true`;TTL 到期/磁盘阈值触发清理时**保留副本不被删**，手动 DELETE 后才消失
- 清理：TTL 到期 / 写超 10GB 阈值文件后，临时文件（`keep=false`）被删
- 设备离线：列表/回传失败，web 提示文案正确
- 录制中文件：拓扑 2 同样不可回传（sidecar 过滤在 panel 侧统一生效）

### 9.5 回归

- render 崩溃/多开时，panel 列表与下载不受影响（出口在 panel）
- CMS 改 HTTP 后：登录、设备列表、WebSocket（`ws://` 自适应）、VideoWall 嵌 web client（混合内容消除，顺手修掉现有问题）回归通过

## 10. 明确不做（后续可选）

- **全量自动上传归档**（设备离线也能看）：作为独立开关，接口同源，按需开启。
- **录制中实时预览**（流式/分段播放进行中文件）：涉及 moov/fMP4 改造，复杂度高，暂不做。
- **对象存储**：规模化后演进方向，v1 不上。
- **CMS 反代路由**（`/api/v1/record/proxy` 流式透传）：仅在出现"web 与 panel 不同网段、CMS 同时够得到两边"的部署时追加。
- **拓扑 2 流式上行边下边播**：消除首播延迟的演进方向，v1 以进度展示兜底。

## 11. 已确认项（原待确认，评审后关闭）

1. 临时缓存 TTL / 磁盘阈值：**TTL 24h + 10GB 阈值双触发**（§6.3）。
2. panel `/records` 鉴权：**强制开启，cms 签发短时效 ticket（HMAC）**，口令不放 URL（§5.3）。
3. 拓扑 2 下载：与播放同源，随播放实现，不单独做。
4. panel IP 透传：字段 `panel_lan_ips`（数组），panel 握手时主动上报本机网卡 IP（§5.2）。

## 12. 双机联调记录（2026-08-17,本机 CMS 10.0.0.16 + 设备 10.0.0.90)

### 12.1 联调拓扑与环境

- CMS 跑本机（HTTPS,`output/px_cms`,force_authorize=false）；设备 10.0.0.90 全新安装 `Pixels_3.3.42_Setup.exe`（装到 `C:\Program Files\PixelsRender`)。
- 设备 CMS 配置通过 leveldb 注入工具 `tests/sp_put`(panel 设置持久化在 `px_data\pixels.dat` / `panel_companion.dat`)：`cms_server_host/port`、`relay_server_host/port`、`key_auth_appkey`、`device_safety_pwd`(md5)。注入前必须停 px_service 并杀净 px_panel，否则 leveldb LOCK 被占。
- 远程运维通道：SMB(`\\10.0.0.90\C$`)+ schtasks 以 SYSTEM 远程执行（WinRM 需 TrustedHosts，未用）。
- 注意：px_service 注册了失败自动重启（3s),`sc stop` 也会被拉回；要真停需先 `sc config start= disabled`。

### 12.2 联调发现并修复的缺陷（已随代码提交）

1. **ticket 双重 MD5(panel 侧 bug)**:`records_http_handler.cpp` 用 `MakeRecordsTicketKey(GetDeviceSecurityPwd())` 再 hash 一次，而 panel 存的 `device_safety_pwd` 本身就是 md5,CMS 直接用 `safety_pwd_md5` 当 HMAC key → 所有 ticket 校验必然 403。修复：handler 直接用存储值；两侧各加同一组 pinned HMAC 向量测试（`cross_side_pinned_vector` / `CrossSidePinnedVector`）锁定字节级兼容。
2. **panel /records 缺 CORS 头**:CMS web(https 源）跨源 `fetch` 探测/列表被浏览器拦截，前端误判直连不通而回退拓扑 2。修复：三个 handler 统一加 `Access-Control-Allow-Origin: *`（无自定义头，无 preflight;`<video>` 本就不受 CORS 限）。

### 12.3 测试矩阵执行结果

§9.1/9.2 单测与 curl 集成：全绿（ticket 12 例、rust 4 例、catalog/transfer 等此前已过）。

§9.3 拓扑 1（同网段）:
- 设备上线注册（`/cms/panel` + relay `server_/ft_server_`)、`panel_lan_ips=["10.0.0.90"]` 透传 ✓
- ticket 签发 + 直连列表 / Range(206 首段/尾段、416 越界）、路径穿越 400、录制中文件 403(不出列表也不可下载）✓
- 浏览器 E2E(`scripts/cdp_records_e2e.mjs`,headless Chrome)：列表渲染、直连播放、拖拽 seek（见 206)、h264+**Opus** 样例可播、控制台零报错 ✓
- 浏览器直下 md5 与设备一致 ✓
- 下载到 CMS：fresh→ready、`keep=true`、md5 一致、重复调用幂等（仅翻标记）✓；DELETE 生效 ✓

§9.4 拓扑 2（断 20369 模拟隔离；90 防火墙原为全关，临时开启+6 分钟自动恢复保险）:
- 自动回退"经 CMS 回传"徽标，无报错弹窗 ✓
- 点播触发回传 → 完成后自动开播（`/uploads/records/...`,206)✓
- 幂等：已回传文件重复点播即刻可播，不重传 ✓
- 设备离线（用 panel 未连的设备 990405157)：页面提示"设备离线，无法查看录像" ✓

§9.5 回归：render 不在时 panel 列表/下载不受影响（出口在 panel)✓

未覆盖（遗留）:TTL 24h / 10GB 阈值清理未实测（时间成本）；多网卡 `panel_lan_ips` 仅单网卡验证；真实 render 录制文件未验（用同形态预制样例代替）。

### 12.4 已知遗留（后续工作）

- ~~**HTTP 部署的前置**:panel 连 CMS 长连硬编码 `wss`~~ → 已完成，见 §13(2026-08-18 全 HTTP 化）。
- ticket TTL 600s，超长视频跨过期点拖拽会 403（前端需续期或重取）。
- 直连拉取落盘 mtime 记 0（幂等键精度略低）；CMS 重启后 `c_records` 可能残留 fetching 态。

## 13. 全 HTTP 化改造（2026-08-18,ssl_enable 开关贯通四端）

### 13.1 目标与设计

CMS 以 `ssl_enable=false` 纯 HTTP 运行时，panel / px_service / px_client 三端原先硬编码的 `wss://`/`https://` 必须能跟随切换，浏览器不再需要 `--allow-running-insecure-content`。

开关源头是 CMS 配置 `px_cms.toml` 的 `ssl_enable`（缺省 true，向后兼容旧 HTTPS 部署），经 access 广播下发，逐端持久化：

- **CMS(rust)**:`CmsServerConfig` 新增 `srv_ssl_enable`(serde 缺省 true，旧 access 串无此字段时视为 HTTPS);`get_server_config` 从 `ssl_enable` 填充。`cms_server.rs` 本就有 `ssl_enable=false` 的纯 HTTP 监听分支。
- **panel(C++)**:PxSettings 新键 `cms_ssl_enable`（缺省 true)+ `IsCmsSslEnabled()`;access 解析链（`cms_access_info_parser.cpp` → `CmsSrvConfig::srv_ssl_enable_` → `cms_scanner` → `st_network.cpp::Save`）落盘；`PxCmsClient` 重构为抽象接口 + `PxCmsClientImpl<ClientType>` 模板（`asio2::wss_client`/`asio2::ws_client`)，工厂按开关实例化，开关变化触发长连重建；约 30 处 `HttpClient::MakeSSL` 收口为 `PxSettings::MakeCmsHttpClient` / `px_cms::MakeCmsHttpClient` / `ProfileApi`(px_deps 两库用包级 atomic 布尔 + setter 注入，panel `Load()` 时同步）。
- **proto**:`MsgAuthInfo` 新增 `bool cms_ssl = 12`(proto3 缺省 false，注入工具需显式传 true,`inject_service_auth.mjs` 已加 `--cms-ssl` 参数）。
- **px_client(C++)**:`--cms_ssl` 命令行（panel `running_stream_manager` 启动时透传）;`CtCmsClient` 同样模板化按开关选 ws/wss。
- **px_service(rust)**:`cms_client.rs` 按 `cms_ssl` 选 `connect_async`(ws）或 `connect_async_tls_with_config`(wss，保留 NoCertVerifier);`auth_info_to_json` 带 `cms_ssl` 字段。

relay 两端本来就是明文，web 端已协议自适应，均不需要改。

### 13.2 验证结果

- 单测：`test_access_decrypt` 新增三组断言（旧串缺省 true / 显式 false / 显式 true)✓;`test_records_ticket` 12/12 ✓;rust `px_cms_server` access_info 3 例、`px_service` 33/33 ✓。
- 部署：本机 CMS `ssl_enable=false` 重启，日志 `http.listening on 0.0.0.0:30500 (ssl_enable=false)`;90 侧重拷新 `px_service.exe`/`px_panel.exe` + `sp_put` 注入 `cms_ssl_enable=false`(`tests/_redeploy_service_90.bat` 新增、`_redeploy_panel_90.bat`、`_cfg3_90.bat` + `cfg_panel3_90.bat` 带杀 panel 重试）。
- 90 重连：CMS 日志确认 relay 与 `/cms/panel` 明文 ws 握手来自 10.0.0.90 ✓。
- E2E(HTTP，无 `--ignore-certificate-errors`/`--allow-running-insecure-content`):§9.3 拓扑 1 全项（列表/ticket/206/拖拽/控制台零报错）✓；离线提示（990405157)✓;CMS web 冒烟（`scripts/cdp_cms_smoke.mjs`：设备列表/设备监控/资源总览渲染正常、控制台零报错）✓。

### 13.3 遗留

- CMS → px_auth_server 的授权拉取仍是 `https://127.0.0.1:30400`(auth server 侧未动，独立部署时再议）。
- `px_common_new/tests/test_http.cpp` 手动测试仍直连 MakeSSL(127.0.0.1)，未加开关。

### 13.4 端口收口（2026-08-18)

原 `cms_port - 1`(30499）的 ping-only 明文监听已删除，`/ping` 由主端口统一提供（主路由本就有该路由）。panel 侧 `CmsDeviceApi::Ping` / `CanPingServer` 本来就打主端口，无需改动；CMS 控制台（egui)"打开"按钮由写死 `http://ip:30499` 改为按 `ssl_enable`/`cms_port` 拼主端口 URL。验证：`30500/ping` 200、30499 连接拒绝、90 明文 ws 重连正常。

### 13.5 部署形态约定（2026-08-18 确认）

**内网/自托管部署（当前形态，推荐默认）**：全链路明文 HTTP/WS——CMS `ssl_enable=false`(30500 单端口承载 HTTP/WS/REST/`/ping`/静态资源），设备端 `cms_ssl_enable=false`，浏览器无需任何特殊 flag，无混合内容问题。render 侧本来就是明文（web client 托管 20371、panel 录像 20369 均为 plain HTTP)，无需改造。

**公网部署：nginx 终结 TLS，后端零改动**。nginx 反代 30500 时必须透传 WebSocket 升级头（`/cms/panel`、`/cms/website` 等长连），并放宽超时与上传体积：

```nginx
server {
    listen 443 ssl;
    server_name cms.example.com;
    ssl_certificate     /etc/nginx/certs/cert.pem;
    ssl_certificate_key /etc/nginx/certs/key.pem;
    client_max_body_size 1g;        # 对齐后端 1GB 限制(录像/安装包上传)

    location / {
        proxy_pass http://127.0.0.1:30500;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;      # WS 必需
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_read_timeout 3600s;                    # WS 长连保活
    }
}
```

公网部署的配套约束：

1. **设备端开关**：设备 `cms_ssl_enable=true`（缺省值）以 wss 连 nginx 443；所有 wss 客户端均为 `verify_none`/`NoCertVerifier`，自签证书可连，CA 证书则浏览器零警告。
2. **relay(30502）也是 WS**，过中继的设备需要第二个 server 块单独反代。
3. **UDP 广播（30501）不过公网**，设备靠粘贴 access 串接入（公网场景本来如此）。
4. **混合内容的根治靠"不直连设备"**:https 页面内嵌 `http://设备IP:20369/20371` 仍会被浏览器拦，因此公网形态下录像走拓扑 2（经 CMS 回传，URL 为同源 `/uploads/records/...`)、远程桌面走 relay 而非直连 web client——页面内不出现任何 `http://设备IP` 内容，nginx 一层即完整解决。LAN 直连模式仅适用于内网。
5. **WebRTC 媒体面**(DTLS-SRTP）与 nginx 无关；P2P 打洞失败时需 TURN 或全 relay，属另一层部署问题。

TLS 能力在代码里是双向保留的：CMS 拨回 `ssl_enable=true` + 设备重新下发 access 串即回到 HTTPS/WSS 直连模式（此时浏览器对自签证书有警告、且混合内容问题回归，仅建议配合受信证书使用）。
