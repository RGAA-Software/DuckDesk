# CMS Web 查看 Render 录像 — 设计方案

> 状态：待评审
> 范围：CMS Web 页查看 render 端录屏文件（`C:\Users\Public\Pixels\px_render_records`）
> 核心决策：**不做全量上传归档**，panel 作为录像的稳定出口；同网段直连，上层网段按需代理。

---

## 1. 背景与目标

render 端录屏已落地：录像文件落在本机 `C:\Users\Public\Pixels\px_render_records\`（`rec_{monitor}_{YYYYMMDD}_{HH.MM.SS}.mp4`，1GB/段滚动，24 个文件上限）。目标是在 **CMS Web** 上按设备查看这些录像（列表 + 播放 + 下载）。

两种部署拓扑：

1. **同网段**：CMS 与所有 render（及其所在机器）在同一个局域网，可直接访问。
2. **上层网段**：CMS 在上层/中心网段，管理大量分散的 render，设备在 NAT/防火墙后，CMS 无法主动直连。

## 2. 为什么出口是 panel，而不是 render

| 维度 | render | panel（出口） |
|---|---|---|
| 进程性质 | 随时会死（崩溃/被重启）、**可多开** | 常驻、服务托管、单例 |
| 与 cms 连接 | **无**（render 与 cms 无长连接，也无 cms 客户端代码） | 与 cms 保持 `/cms/panel` 长连接；`cms_device_api`（px_cms_client 库）被 panel 广泛使用 |
| 本机 HTTP 服务 | net_ws 插件 20371（但随 render 生死） | **自带 asio2 `http_server`，监听 20369**（`ws_panel_server.cpp`，现挂 ws 端点，可加 HTTP 路由） |
| 录像目录访问 | 写入方 | **同机可读**（公共目录 `Public\Pixels`），render 死活不影响 |

结论：panel 常驻、单例、同机可读录像目录、与 cms 有现成长连接和 HTTP 客户端——是录像的稳定出口。render 死掉/多开不影响已录文件的查看与回传。

## 3. 现状可复用能力（代码依据）

| 能力 | 位置 | 说明 |
|---|---|---|
| panel HTTP 服务器 | `src/px_panel/.../network/ws_panel_server.cpp`（asio2 `http_server`，20369） | 现挂 `/panel`、`/panel/renderer` ws 端点；可加普通 HTTP 路由（列表 JSON + 文件流 + Range） |
| panel→cms 长连接 | `/cms/panel`（panel ws 客户端） | 双向消息通道；另有 `/cms/service`（service 长连） |
| panel→cms HTTP 客户端 | `HttpClient::MakeSSL`（panel 多处使用）+ `px_cms_client` 库 | 鉴权上报模式现成 |
| cms 文件上传接口模式 | `rust_server/px_cms_server/src/update/update_handle.rs`（multipart → 存 `uploads/` → mongo 记录 → 返回下载路径） | **按需拉取可完全仿照** |
| cms 静态伺服 | `cms_server.rs` `"/uploads"` ServeDir | `uploads/` 下文件已可直接 HTTP 播放（mp4 + Range 拖拽） |
| cms 路由 | `/api/v1/record`（visit/ft 上报）、`/api/v1/update` | 新录像接口同构挂载 |
| 录像目录 | `C:\Users\Public\Pixels\px_render_records`（公共位置） | panel/render 同机均可读 |

## 4. 总体架构

```
                    ┌─ 本机 ────────────────────────────┐
   render(易死/多开) │ 写 px_render_records(公共目录)      │
                    └──────────────┬─────────────────────┘
                                   │ panel 常驻/单例/同机可读
                                   ▼
  拓扑1(同网段):  web ──HTTP──► panel:20369 /records ── 列表+文件(Range)
  拓扑2(上层网段): web ──► cms ──经 /cms/panel 隧道发指令──► panel
                        ◄── panel 按需调 cms 上传接口回传文件(临时缓存) ──► web 播放
```

- 设备本地录像**保留**，沿用本地 1GB/24 文件滚动清理；cms 侧**不做长期归档**。
- 拓扑 1 与拓扑 2 共用同一套 panel 端实现（HTTP 路由 + 回传能力），差异只在触发方。

## 5. 拓扑 1：同网段直连 panel

### 5.1 panel HTTP 路由（20369 新增）

| 路由 | 方法 | 说明 |
|---|---|---|
| `GET /records` | GET | 返回录像文件列表 JSON（文件名、大小、mtime、monitor 名），限 `px_render_records` 目录 |
| `GET /records/{filename}` | GET | 文件流，**支持 Range**（`<video>` 拖拽播放）；文件名做路径穿越防护 |
| `GET /records/info` | GET | 可选：目录剩余空间/总大小等 |

- 鉴权：与 ws 端点一致的口令/来源校验（同网段暴露需防局域网随意访问，v1 可用简单共享口令，后续接安全密码）。
- 录制中文件（moov 未写）不可播：列表过滤"仍在增长"的文件（或按 mtime 距现在 <N 秒过滤），仅暴露已完成的段/已停止的文件。

### 5.2 web 端

- CMS Web 录像页：设备列表（cms 已知设备）→ 展开录像 → 播放地址 = `http://{panel_ip}:20369/records/{file}`。
- **panel IP 来源**：cms 从 `/cms/panel` 长连接记录到的来源 IP 下发（同网段时即为设备内网 IP）；设备列表接口透传给前端。
- 播放：`<video>` + Range（浏览器原生拖拽）；提供下载按钮（同 URL）。

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

### 6.2 隧道消息（/cms/panel 通道内，建议新 protobuf 或 JSON 消息族）

| 方向 | 消息 | 说明 |
|---|---|---|
| cms→panel | `RecordListReq{device_id}` | 请求录像列表 |
| panel→cms | `RecordListResp{files[]}` | 文件名/大小/mtime/monitor |
| cms→panel | `RecordFetchReq{device_id, filename, token}` | 请求回传单个文件（token 防重放） |
| panel→cms | 回传动作：调用 `POST /api/v1/record/upload`（multipart，带 token） | 复用文件上传接口 |
| cms→panel | `RecordFetchDone{filename, ok}` | 可选：回传完成通知/错误 |

- 消息收发挂在现有 `/cms/panel` 连接处理里（panel 侧现有 cms ws 客户端；cms 侧现有 panel ws 会话管理），新增消息类型即可。

### 6.3 cms 新增接口

| 接口 | 说明 |
|---|---|
| `POST /api/v1/record/upload` | multipart 上传（仿 `handle_upload_update_info`）；参数 `device_id` + `token`（由隧道会话签发，短时效）；存 `uploads/records/{device_id}/`；mongo `c_records` 记录（临时标记 + TTL） |
| `GET /api/v1/record/list?device_id=` | 经隧道向 panel 要列表并回传（web 侧统一入口） |
| `GET /api/v1/record/fetch?device_id=&file=` | 经隧道触发回传，返回临时下载地址 |
| `DELETE /api/v1/record/{id}` | 删除临时文件（web 操作或 TTL 清理） |

- **临时清理**：TTL（如停止查看 N 小时后）或 `uploads/records` 磁盘阈值（如 10GB）触发删除；清理按 device_id 粒度轮询。
- 幂等：同 `device_id+filename+md5` 已存在则跳过重传。

## 7. 关键设计点（通用）

1. **只有已完成的文件可看/可传**：录制中的分段 moov 未写不可播；上传/伺服粒度 = 分段完成或录制停止。
2. **panel 上传失败重试**：panel 侧按设备串行上传 + 限速（避免挤爆 cms 带宽）；失败指数退避重试，不丢任务（本地待传清单）。
3. **不删除本地文件**：cms 只是"临时查看缓存"，本地保留并沿用滚动清理；若未来要"归档中心"，加全量上传开关即可（同接口）。
4. **安全**：panel `/records` 需鉴权（同网段暴露）；回传 token 短时效 + 绑定会话；上传接口验 appkey+device_id。
5. **路径防护**：文件名只允许 `[A-Za-z0-9_.-]` 白名单，杜绝目录穿越。
6. **多 render 机器**：panel 单例保证同一台机器只有一个出口；多开 render 写入同一公共目录，列表天然聚合。
7. **设备下线**：拓扑 2 设备离线时列表/回传失败，web 明确提示（文件在设备本地，无法代理查看）。

## 8. 工作量预估

| 模块 | 内容 | 量级 |
|---|---|---|
| panel HTTP 路由 | `/records` 列表 + 文件流 + Range + 鉴权 + 路径防护 | 小 |
| panel 隧道消息 | /cms/panel 新增消息族 + 回传触发 | 小 |
| cms rust | record/upload（仿 update_router）+ list/fetch 路由 + 临时清理 + mongo `c_records` | 中 |
| web/px_cms | 录像页（设备→列表→播放→下载） | 中 |
| 拓扑 1 联调 | 同网段直连验证 | 小 |

## 9. 明确不做（后续可选）

- **全量自动上传归档**（设备离线也能看）：作为独立开关，接口同源，按需开启。
- **录制中实时预览**（流式/分段播放进行中文件）：涉及 moov/fMP4 改造，复杂度高，暂不做。
- **对象存储**：规模化后演进方向，v1 不上。

## 10. 待确认

1. 临时缓存 TTL / 磁盘阈值默认值（建议 TTL 24h 或 10GB 阈值触发清理）。
2. panel `/records` 鉴权方式（v1 简单口令 vs 复用安全密码体系）。
3. 拓扑 2 是否也需要"下载"（与播放同源，随播放实现）。
4. 设备列表的 panel IP 透传字段命名与前端展示方式。
