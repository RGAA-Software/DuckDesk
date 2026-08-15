# GammaRayUserProxy 实施计划

## 1. 文档目的

本文档描述在 **不删除现有代码** 的前提下，引入 Rust 进程 `GammaRayUserProxy`（crate：`px_user_proxy`），通过 **主动连接 Render `net_ws` 插件** 承接剪贴板用户态能力，并逐步替代当前 **Render ↔ Panel** 的剪贴板通道。

前置讨论结论摘要：

- Render 必须以 **SYSTEM** 身份运行，才能采集锁屏、UAC 等安全界面。
- SYSTEM 进程不宜直接操作用户剪贴板；应由 **用户身份进程** 代理。
- `net_ws` 为 Render 默认必启插件，在其上新增 WebSocket 路径 `/user-proxy` 是最小改动接入点。
- UserProxy **主动连接** Render；Render **被动接受** 本机连接。
- Panel 与 Render 之间，除剪贴板外还有其他职责（审计、插件控制、硬件信息等），本阶段 **仅迁移剪贴板**。

---

## 2. 目标与非目标

### 2.1 本阶段目标

| # | 要求 | 说明 |
|---|------|------|
| 1 | crate 位于 `rust_client/px_user_proxy`，产物名 `GammaRayUserProxy.exe` | 纳入 `rust_client/Cargo.toml` workspace |
| 2 | 注释 Render↔Panel 剪贴板交互代码 | **只注释，不删除**；保留回滚能力 |
| 3 | Proxy 永久重连 Render | 连接失败 **2 秒** 后重试，**永不停止** |
| 4 | 日志完善 | 连接、协议、剪贴板读写、重试、异常均需可追踪 |
| 5 | 日志路径与其他进程一致 | `C:\Users\Public\GoDesk\gr_logs\` |

### 2.2 本阶段非目标

- 不删除 Panel 剪贴板相关代码。
- 不一次性去掉 Panel 进程。（Service 启动链改动已在 Phase 3 完成：Service 拉起/守护 UserProxy。）
- Phase 1 文本剪贴板与 Phase 2 文件/OLE 虚拟文件流均已实现；详见 §9 Phase 2。
- 不改变 Render 必须以 SYSTEM 运行的采集模型。

---

## 3. 目标架构

### 3.0 产品形态（服务端无界面）

目标部署为 **Service + Render + UserProxy**，不依赖 Panel / Guard：

```
┌─────────────────────────────────────────────────────────────┐
│ GammaRayService (SYSTEM, Session 0)                         │
│   └─ start_desktop → GammaRayRender (SYSTEM, 用户会话)     │
│   └─ start_desktop → GammaRayUserProxy (WTS 用户令牌)       │
│   └─ 3s 监控：Render 掉线重启；Render 在但 UserProxy 掉线补拉 │
└─────────────────────────────────────────────────────────────┘

GammaRayRender (SYSTEM) ◄── ws://127.0.0.1:{port}/user-proxy ── GammaRayUserProxy (用户会话)
```

**Guard / Panel** 为现有桌面客户端配套，**不负责**拉起 UserProxy。Guard 仅守护 `GammaRay.exe`（Panel）与 `GammaRaySysInfo.exe`。

### 3.1 架构图（剪贴板路径）

```
┌─────────────────────────────────────────────────────────────┐
│ GammaRayService (SYSTEM, Session 0)                         │
│   └─ 启动 Render (SYSTEM, Session 1)                        │
│   └─ 启动 UserProxy (WTS user token，与 Render 同生共死)    │
└─────────────────────────────────────────────────────────────┘

┌──────────────────────────┐         ws://127.0.0.1:{port}/user-proxy
│ GammaRayRender (SYSTEM)  │ ◄────── 主动连接，失败 2s 重试
│   plugin: net_ws         │
│   ├─ /media              │         二进制 RpMessage (protobuf)
│   ├─ /file/transfer      │
│   └─ /user-proxy  [新增] │
└──────────────────────────┘

┌──────────────────────────┐
│ GammaRayUserProxy (hy)   │
│   剪贴板监听 / 读写       │
│   echo 防回环             │
└──────────────────────────┘

Client (hy) ──直连/Relay──► Render ──/user-proxy──► UserProxy
                              │
                              └── 不再经 Panel 做剪贴板
```

### 3.2 剪贴板数据流（迁移后）

**Client → 被控端粘贴（远端写入本机剪贴板）**

```
Client 发送 kClipboardInfo
  → Render (plugin_net_event_router)
  → [新] PostUserProxyMessage(kRpRawRenderMessage 内嵌 tc::Message)
  → UserProxy 解析并 WriteClipboard
```

**被控端复制 → Client 粘贴（本机读出发往 Client）**

```
用户 Ctrl+C
  → UserProxy WM_CLIPBOARDUPDATE / 监听
  → UserProxy 发送 kRpClipboardEvent
  → Render WsUserProxyRouter 转为 kClipboardInfo
  → 发往已连接 Client
```

---

## 4. 协议设计

复用现有 `tcrp` 协议（`src/px_deps/px_message_new/tc_render_panel_message.proto`），减少双端改动。

### 4.1 WebSocket 端点

| 项 | 值 |
|----|-----|
| URL | `ws://127.0.0.1:{network_listen_port}/user-proxy` |
| 端口 | 与 Render `network_listen_port` 相同（`net_ws` 监听端口） |
| 帧类型 | 二进制，`tcrp::RpMessage` 序列化字节 |
| 握手 | 建议要求 query `token=` 或固定 header（与 Service 生成配置一致，Phase 1 可仅校验 localhost） |

### 4.2 消息类型（剪贴板子集）

| 方向 | RpMessageType | 载荷 | 含义 |
|------|---------------|------|------|
| UserProxy → Render | `kRpHello` | `RpHello` | 握手 |
| Render → UserProxy | `kRpHelloResp` | `RpHelloResp` | 握手应答 |
| 双向 | `kRpHeartBeat` / `kRpHeartBeatResp` | 空 | 保活（可选，建议实现） |
| UserProxy → Render | `kRpClipboardEvent` | `RpClipboardInfo` | 本机剪贴板变化 |
| Render → UserProxy | `kRpRawRenderMessage` | `RpRawRenderMessage.msg` = 序列化 `tc::Message` | 多为 `kClipboardInfo` |
| UserProxy → Render | `kRpRawRenderMessage` 或专用 resp | `tc::Message` `kClipboardInfoResp` | echo 确认（与 Panel 行为对齐） |

内层 `tc::Message` 使用现有 `tc_message.pb`（C++ 已有；Rust 侧可用 `prost` 生成或 Phase 1 仅处理文本路径手工解析）。

### 4.3 连接策略

- **单连接**：新 UserProxy 连接建立时，踢掉旧连接（避免双监听剪贴板）。
- **来源校验**：`on("open")` 时拒绝非 `127.0.0.1` / `::1` 的连接。
- **Render 侧门控**：`IsUserProxyConnected()` 为 false 时，不向 Client 同步本机剪贴板 outbound（与 Panel `IsRendererConnected()` 对齐）。

---

## 5. Rust 工程：`px_user_proxy`

### 5.1 目录结构（建议）

```
rust_client/px_user_proxy/
├── Cargo.toml
└── src/
    ├── main.rs                 # 入口、参数解析、单实例
    ├── config.rs               # 路径、常量、CLI
    ├── logging.rs              # px_base::log_util 封装
    ├── render_client.rs        # WS 连接、2s 重连、收发 RpMessage
    ├── proto/                  # prost 生成或手写最小 proto
    │   └── mod.rs
    ├── clipboard/
    │   ├── mod.rs
    │   ├── win_listener.rs     # AddClipboardFormatListener + 消息泵线程
    │   ├── win_platform.rs     # OpenClipboard 读写 CF_UNICODETEXT / CF_HDROP
    │   ├── echo.rs             # 与 px_common_new/clipboard_echo 同语义
    │   └── virtual_file/       # OLE IDataObject + IStream 虚拟文件粘贴
    │       ├── coordinator.rs
    │       ├── stream.rs
    │       └── win_clipboard.rs
    └── app.rs                  # 组装：连接 + 剪贴板 + 分发
```

### 5.2 `Cargo.toml` 要点

```toml
[package]
name = "px_user_proxy"
version.workspace = true
edition.workspace = true

[[bin]]
name = "GammaRayUserProxy"
path = "src/main.rs"

[dependencies]
px_base = { path = "../../rust_base/px_base" }
tokio = { version = "1", features = ["full"] }
tokio-tungstenite = "0.27"
futures-util = "0.3"
clap = { version = "4", features = ["derive"] }
tracing = "0.1"
prost = "0.13"
anyhow = "1"
# Windows 剪贴板
windows = { version = "0.62", features = ["Win32_UI_WindowsAndMessaging", "Win32_System_DataExchange", "Win32_Foundation", "Win32_System_Ole"] }
```

### 5.3 命令行参数（建议）

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--render-host` | `127.0.0.1` | Render 地址 |
| `--render-port` | `20371` | 与 Render `network_listen_port` 一致 |
| `--path` | `/user-proxy` | WebSocket 路径 |
| `--reconnect-secs` | `2` | 失败重连间隔（固定需求：2） |

### 5.4 重连逻辑（需求 #3）

参考 `px_sysinfo/src/sys_panel_client.rs`，但必须满足：

```text
loop {
    清空旧 sender / 连接状态
    尝试 connect_async(url)
    成功 → 进入收发循环；断开 → 记录 error log
    sleep(2s)   // 无论何种失败，固定 2 秒
    // 永不 break
}
```

日志要求：

- 每次尝试连接：`INFO connect attempt url=...`
- 成功：`INFO connected to render`
- 失败：`ERROR connect failed: {err}, retry in 2s`
- 收发断开：`WARN connection closed: {reason}, retry in 2s`
- 协议解析失败：`ERROR parse RpMessage failed: ...`

### 5.5 日志（需求 #4、#5）

与 `px_service` 一致，使用 `px_base::log_util::init_log`（原 `gr_guard` 已删除，其保活日志也并入本文件）：

| 项 | 值 |
|----|-----|
| 目录 | `%PUBLIC%\GoDesk\gr_logs` → `C:\Users\Public\GoDesk\gr_logs` |
| 文件名 | `godesk_user_proxy.log` |
| 轮转 | 复用 `log_util` 默认 50MB × 5 份 |
| 控制台 | 同步输出 `INFO+`（与现有 Rust 进程一致） |

`config.rs` 示例常量：

```rust
pub const USER_PROXY_LOG_DIR: &str = "gr_logs";
pub const USER_PROXY_LOG_FILE: &str = "godesk_user_proxy.log";
pub const APP_SHARED_ROOT: &str = "GoDesk";  // 与 guard 相同
```

**建议记录的 ERROR 场景清单**

| 场景 | 级别 | 消息要点 |
|------|------|----------|
| 日志目录创建失败 | ERROR | path, os error |
| 单实例锁冲突 | ERROR | 已有实例运行 |
| WS 连接失败 | ERROR | url, error |
| WS 发送失败 | ERROR | msg_type, byte_len, error |
| protobuf 解析失败 | ERROR | len, hex_preview |
| OpenClipboard 失败 | ERROR | GetLastError, retry_count |
| SetClipboardData 失败 | ERROR | text_len, error |
| echo 抑制跳过 | INFO | suppressed outbound |
| 收到空剪贴板 | INFO | no syncable text |

---

## 6. Render C++ 改动：`net_ws` 新增 `/user-proxy`

### 6.1 新增文件

| 文件 | 职责 |
|------|------|
| `src/px_render/plugins/net_ws/ws_user_proxy_router.h` | 单连接 session、消息分发 |
| `src/px_render/plugins/net_ws/ws_user_proxy_router.cpp` | 解析 `RpMessage`；处理 `kRpClipboardEvent`；下发 `kRpRawRenderMessage` |

### 6.2 修改文件

| 文件 | 改动 |
|------|------|
| `ws_server.h` | 增加 `user_proxy_router_`、`PostUserProxyMessage()`、`IsUserProxyConnected()` |
| `ws_server.cpp` | `kUrlUserProxy = "/user-proxy"`；`AddWebsocketRouter` 增加分支；`bind_disconnect` 清理 |
| `CMakeLists.txt` | 加入 `ws_user_proxy_router.cpp` |
| `plugin_net_event_router.cpp` | Client 来 `kClipboardInfo` 时改发 UserProxy（并注释原 Panel 剪贴板转发） |
| `plugin_event_router.cpp` | 注释 `ReportRemoteClipboardResp` → Panel 路径（若改走 UserProxy） |

### 6.3 `WsUserProxyRouter` 核心行为

**`on open`**

1. 校验 `remote_address` 为 localhost。
2. 若已有连接，关闭旧 session。
3. `LOGI("user-proxy connected, fd=...")`。

**`on message`（UserProxy → Render）**

- `kRpClipboardEvent`：转 `MsgClipboardEvent` 或直接组 `tc::Message kClipboardInfo` 广播给 Client（对齐 `ws_panel_client.cpp` 原 Panel 路径）。
- `kRpHello`：回 `kRpHelloResp`。

**`PostUserProxyMessage`（Render → UserProxy）**

- 将 `kRpRawRenderMessage`（内嵌 `tc::Message` 字节）发到当前 UserProxy session。
- 无连接时 `LOGW("user-proxy not connected, drop clipboard message")`。

### 6.4 安全说明

`net_ws` 当前 `start("0.0.0.0", port)`，`/user-proxy` 会对局域网暴露。**必须在 `open` 回调拒绝非 127.0.0.1 连接**。后续可加 token 校验。

---

## 7. 注释 Render↔Panel 剪贴板代码（需求 #2）

> 原则：**只注释，不删除**；每处加统一标记：  
> `// USER_PROXY_MIGRATION: clipboard path disabled, see px_user_proxy`

### 7.1 Render 侧

| 文件 | 位置 / 内容 | 操作 |
|------|-------------|------|
| `src/px_render/plugins/plugin_net_event_router.cpp` | `PostPanelMessage(kRpRawRenderMessage)` 整段 `PostTask`（约 133–141 行） | 注释；改为仅对 `kClipboardInfo` 调 `PostUserProxyMessage` |
| `src/px_render/plugins/plugin_net_event_router.cpp` | `case kClipboardInfo` 内仅日志（可选保留日志） | 保留或补充 user-proxy 转发日志 |
| `src/px_render/plugins/plugin_event_router.cpp` | `ReportRemoteClipboardResp()` 中 `PostPanelMessage` | 注释；改发 UserProxy 或暂留 |
| `src/px_render/network/ws_panel_client.cpp` | `kRpClipboardEvent` 分支（约 238–261 行） | 整段注释 |
| `src/px_render/network/ws_panel_client.cpp` | `kRpRawRenderMessage` 若仅服务 Panel 回传剪贴板 | 评估后注释（注意该分支也用于 file transfer `data_channel`，**勿整段注释**） |

### 7.2 Panel 侧

| 文件 | 位置 / 内容 | 操作 |
|------|-------------|------|
| `src/px_panel/.../win_panel_message_loop.cpp` | `OnClipboardUpdate` / `ProcessLocalClipboardUpdate` 及 `kRpClipboardEvent` 发送 | 注释 outbound |
| `src/px_panel/.../panel_clipboard_manager.cpp` | `OnRemoteClipboardInfo` 全文 | 注释 inbound 写剪贴板 |
| `src/px_panel/.../gr_render_msg_processor.cpp` | `OnMessage` 调 `clipboard_mgr` | 注释 |
| `src/px_panel/.../network/ws_panel_server.cpp` | `kRpRawRenderMessage` → `GrRenderMsgProcessor`（约 675–683 行） | 注释 |
| `src/px_panel/.../clipboard/win/panel_cp_virtual_file.cpp` | `PostMessage2Renderer` 剪贴板文件相关 | Phase 2 再注释 |
| `src/px_panel/.../clipboard/win/panel_cp_file_stream.cpp` | `PostMessage2Renderer` | Phase 2 再注释 |

### 7.3 保留不动的 Panel↔Render 通道

以下 **不属于剪贴板**，本阶段 **不注释**：

- `WsPanelClient` 连接 `/panel/renderer`（Render 主动连 Panel）
- `kRpCommandRenderer`、`kRpHardwareInfo`、`kRpClientConnected/DisConnected`
- `kRpFileTransferBegin/End`、`kRpRelayAlive`、`kRpMonitorChanged`
- `ws_panel_client.cpp` 中 `kRpRawRenderMessage` 的 **file transfer `data_channel`** 分支

---

## 8. 构建与打包

### 8.1 Workspace

`rust_client/Cargo.toml` 增加：

```toml
members = [
    ...
    "px_user_proxy",
]
```

### 8.2 根 `CMakeLists.txt`

仿照其他 Rust 组件增加（原 `GammaRayGuard_rust`/`GammaRayGuard_stage` 已随 Guard 删除）：

```cmake
set(GR_USER_PROXY_EXE_NAME GammaRayUserProxy.exe)
# GammaRayUserProxy_rust
# GammaRayUserProxy_stage → copy 到 ${GR_PROJECT_BINARY_PATH}
```

并加入 `dist` / `collect_dist.py` 产物白名单（若需要随官方包发布）。

`collect_dist.py` 的 `SKIP_NAMES` 包含 `plugin_net_udp.dll`（`PLUGIN_NET_UDP_ENABLED=OFF` 时避免陈旧产物进包）。

### 8.3 Service 启动（**唯一**拉起方）

在 `rust_client/px_service` 中：

- `start_desktop`：`start_process_as_active_user` 拉起 Render 后，**立即**以 `start_process_as_session_user`（**仅 WTS 用户令牌**，不回退 SYSTEM 令牌）拉起 `GammaRayUserProxy.exe`，确保 UserProxy 以登录用户身份运行。
- 参数：`--render-port={与 Render network_listen_port 一致}`，从 Render args 的 `--network_listen_port=`（也兼容 `-flag=value` 与 `--flag value` 形式）解析，缺失时用默认 `20371`。
- `stop_desktop` / Service 停止：终止 Render **与** UserProxy（`is_managed_clipboard_process`）。
- `monitor_loop`（3s）：Render 缺失则整包 `start_desktop`；Render 在但 UserProxy 缺失则仅补拉 UserProxy（`should_restart_user_proxy`）。

**Guard 不拉起、不监控 UserProxy**（Panel 桌面配套专用）。

---

## 9. 实施阶段

### Phase 0：文档评审（当前）

- 评审本文档，确认协议与注释范围。
- 确认 Phase 1 仅文本剪贴板。

### Phase 1：连通性 + 文本剪贴板 MVP

1. 实现 `ws_user_proxy_router` + `/user-proxy` localhost 校验。
2. 实现 `px_user_proxy`：连接、2s 重连、hello、日志。
3. UserProxy：Win32 文本剪贴板监听 + `kRpClipboardEvent` 发送。
4. Render：Client `kClipboardInfo` → UserProxy；UserProxy 写入文本。
5. 按第 7 节注释 Panel 剪贴板路径。
6. 手工测试：Client ↔ 被控端文本复制粘贴。

**验收标准**

- `godesk_user_proxy.log` 有连接、收发、错误记录。
- Panel 剪贴板代码已注释，文本同步不依赖 Panel。
- 连接断开后 2 秒内自动重连，进程不退出。

### Phase 2 — 剪贴板文件 + OLE 虚拟文件流：**Done**（已人工验证复制/粘贴）

| 项 | 状态 |
|----|------|
| UserProxy CF_HDROP 本地文件列表读取/写入 | **Done** — `clipboard/content.rs`, `win_platform.rs` |
| UserProxy → Render `kRpClipboardFiles` | **Done** |
| Render → UserProxy 文件 metadata 应用（本地路径存在时） | **Done** |
| 文件剪贴板 echo 防回环（`files_signature` 回读登记 echo） | **Done** |
| OLE 虚拟文件 / `kClipboardReqBuffer` → Client → `kClipboardRespBuffer` 流式传输 | **Done** — `clipboard/virtual_file/{stream,coordinator,win_clipboard}.rs` |
| Render 转发 Client `kClipboardRespBuffer` → UserProxy（`data_channel=true`） | **Done** — `plugin_net_event_router.cpp` |
| `VirtualFileCoordinator` + COM `IDataObject`/`IStream` 粘贴拉流 | **Done** — `win_clipboard.rs`（`CFSTR_FILEDESCRIPTOR` / `CFSTR_FILECONTENTS`） |
| Client `ref_path` 与 UserProxy 对齐（目录/混合选择保留文件夹结构） | **Done** — `px_common_new/clipboard/clipboard_file_builder.cpp` + `content.rs` |
| 粘贴完成后清空 OLE 剪贴板（Explorer 粘贴菜单变灰） | **Done** — `clear_ole_clipboard_after_operation()` |
| Panel `panel_cp_virtual_file` / `panel_cp_file_stream` 注释 | Panel 剪贴板 inbound 已 `#if 0`；文件 WS 出站仍保留（无 UserProxy 替代前勿删） |

**Phase 2 联调修复要点**（与 Panel `panel_cp_virtual_file` 对齐，勿随意加回）：

| 问题 | 根因 | 修复 |
|------|------|------|
| Explorer 粘贴菜单可点但无反应 | 安装时 `OleFlushClipboard` + 伪造 `CF_HDROP` GetData | 移除；仅保留 STA 消息泵 + 安装前 `EmptyClipboard` 重试 |
| 粘贴文件 0 KB / 10s 超时 | `IStream::Read` 持锁等待，`on_resp_buffer` 同锁死锁 | `Arc<VirtualFileStreamCore>` + 内部 `Mutex<StreamState>`；等待时泵 COM 消息；读超时 60s |
| 粘贴后菜单仍可点 | `EndOperation` 未清 OLE 剪贴板 | `OleFlushClipboard` + `OleSetClipboard(None)` + `platform.clear()` |
| 文件夹结构丢失 / 混合选择丢文件 | `ref_path` 未按根目录分别处理 | 目录根前缀 `DirName/...`；目录+文件并列时各根独立展开 |
| 文本写入在 OLE 占用后失败 | `write_text` 未先 `OleSetClipboard(None)` | `try_write_text` 与 `try_write_file_paths` 一致先释放 OLE |

**自动化测试（Phase 2）**：82 项全通过（72 单元 + 10 集成），覆盖：
- `virtual_file/stream`：分块读、index 校验、超时/退出、EOF、跨线程 `complete_read`
- `virtual_file/coordinator`：session、outbound `data_channel` 封装、resp 路由
- `proto`：`kClipboardReqBuffer` / `kClipboardRespBuffer` / AtBegin/AtEnd roundtrip
- `mock_render`：自动应答 `ReqBuffer` 并返回 `RespBuffer`
- `content`：目录结构、`ref_path`、混合目录+并列文件
- 集成：虚拟文件 session 安装、`ReqBuffer`↔`RespBuffer` 往返、data_channel 下发

**人工验收（2026-07-03）**：Client → 被控端复制文件/文件夹可粘贴；文件夹层级正确；混合选择完整；粘贴完成后目标目录不再可重复粘贴。

### Phase 3 — Service 集成与发布：**Done**

| 项 | 状态 |
|----|------|
| Service `start_desktop` 后 WTS 拉起 UserProxy（仅用户令牌，不回退 SYSTEM） | **Done** — `px_service/src/user_proxy.rs` + `start_process_as_session_user` |
| Service `stop_desktop` 终止 UserProxy | **Done** |
| Service 3s 监控补拉 UserProxy（Render 存活时） | **Done** — `should_restart_user_proxy` |
| Guard **不**拉起 UserProxy | **Done** — 仅 Panel + SysInfo |
| `service_manager` / `px_uninstall` 清理列表 | **Done** |
| CMake + `collect_dist.py` | **Done** |
| 30s WS 心跳（UserProxy → Render） | **Done** |

### Phase 4：去 Panel 依赖（可选，远期）

- Panel 仅保留 UI/管理。
- 评估是否断开 Render `WsPanelClient`（非剪贴板部分仍可能依赖 Panel）。

---

## 10. 测试计划

> **原则**：能自动化的一律写单元/组件测试（Rust `cargo test`）；依赖真实 OS 剪贴板、多进程、网络的由人工执行，并记录日志路径与期望关键字。

### 10.1 Rust 单元测试（`cargo test -p px_user_proxy`，全自动）

#### 10.1.1 `config`

| 测试用例 | 断言 |
|----------|------|
| `shared_root_extends_public_dir` | `app_shared_root()` = `%PUBLIC%\GoDesk` |
| `user_proxy_log_root` | `user_proxy_log_root()` = `%PUBLIC%\GoDesk\gr_logs` |
| `user_proxy_log_file` | 文件名为 `godesk_user_proxy.log` |
| `default_render_port` | 默认 `20371`（与 Render 默认 `network_listen_port` 一致） |
| `default_reconnect_secs` | 固定 `2` |
| `render_ws_url_build` | `ws://127.0.0.1:{port}/user-proxy` 拼接正确 |
| `render_ws_url_custom_host` | `--render-host` 覆盖 host |
| `singleton_lock_name` | `GammaRayUserProxy.Singleton` 常量 |
| `cli_parse_defaults` | clap 缺省参数与 config 常量一致 |
| `cli_parse_overrides` | `--render-port` / `--reconnect-secs` 覆盖 |

#### 10.1.2 `echo`（对齐 `px_common_new/clipboard/clipboard_echo`）

| 测试用例 | 断言 |
|----------|------|
| `set_and_get_remote_echo` | `SetRemoteEcho` / `GetRemoteEcho` 往返 |
| `should_skip_when_matches_echo` | 与 echo 相同文本 → `ShouldSkipOutbound == true` |
| `should_not_skip_when_different` | 不同文本 → `false` |
| `suppress_blocks_outbound` | `BeginSuppressOutbound` 期间任意文本跳过 |
| `suppress_nested` | 嵌套 `Begin`/`End` 引用计数正确 |
| `empty_text_not_special` | 空串与 echo 空串行为一致 |

#### 10.1.3 `proto`（prost 生成，共用 C++ 同源 `.proto`）

| 测试用例 | 断言 |
|----------|------|
| `rp_hello_roundtrip` | `kRpHello` 序列化/反序列化 `type` 正确 |
| `rp_hello_resp_roundtrip` | `kRpHelloResp` |
| `rp_heartbeat_roundtrip` | `kRpHeartBeat` / `kRpHeartBeatResp` |
| `rp_clipboard_text_event` | `kRpClipboardEvent` + `kRpClipboardText` + `msg` 字段 |
| `rp_raw_render_message` | `kRpRawRenderMessage.msg` 承载内层字节 |
| `tc_clipboard_info_roundtrip` | 内层 `tc::Message` `kClipboardInfo` + `kClipboardText` |
| `tc_clipboard_info_resp_roundtrip` | `kClipboardInfoResp` |
| `nested_raw_render_carries_tc_message` | Rp 包一层 + tc 包一层，双往返 |
| `unknown_bytes_fails_gracefully` | 截断/随机字节 → 解析返回 `Err`（不 panic） |

#### 10.1.4 `render_client`（逻辑层，不启真实 WS）

| 测试用例 | 断言 |
|----------|------|
| `reconnect_interval_is_two_seconds` | 常量 `RECONNECT_SECS == 2` |
| `build_hello_message` | 连接后首包为 `kRpHello` 二进制 |
| `parse_hello_resp` | 收到 `kRpHelloResp` 置 `connected` 状态 |
| `parse_clipboard_event_text` | 从 `kRpClipboardEvent` 提取文本 |
| `build_clipboard_event` | 本地文本 → `kRpClipboardEvent` 字节 |
| `build_raw_render_from_tc_message` | `kClipboardInfo` → `kRpRawRenderMessage` |
| `connection_state_machine` | disconnected → connecting → connected → disconnected |

#### 10.1.5 `single_instance`

| 测试用例 | 断言 |
|----------|------|
| `first_acquire_succeeds` | 首次 `CreateMutexW` 成功 |
| `second_acquire_fails` | 同名 mutex 第二次失败 |
| `released_after_drop` | drop 后可再次 acquire |

#### 10.1.6 `logging`

| 测试用例 | 断言 |
|----------|------|
| `log_file_name_constant` | `godesk_user_proxy.log` |
| `log_root_under_godesk` | 目录在 `GoDesk/gr_logs` 下 |

### 10.2 Rust 组件 / 集成测试（Windows，`cargo test`，全自动）

| 模块 | 测试用例 | 断言 | 备注 |
|------|----------|------|------|
| `mock_render` | `mock_render_accepts_hello` | Mock WS 收到 `kRpHello` 并回 `kRpHelloResp` | 单元 |
| `tests/integration.rs` | `integration_connects_and_sends_hello` | Engine 连 Mock Render 并发送 hello | 集成 |
| `tests/integration.rs` | `integration_remote_clipboard_apply_and_resp` | 远端文本写入 InMemory 剪贴板并回 resp | 集成 |
| `tests/integration.rs` | `integration_local_clipboard_to_render` | 本地变更 → Render `kRpClipboardEvent` | 集成 |
| `tests/integration.rs` | `integration_echo_suppresses_outbound_loop` | echo 抑制不回传 | 集成 |
| `tests/integration.rs` | `integration_reconnects_after_server_drop` | Mock Render 断开后 2s 内重连 | 集成 |
| `clipboard::backend` | `InMemoryClipboard` 系列 | 读写 / notify 不依赖 Win32 | 单元 |
| `clipboard::win_platform` | `read_empty_after_clear` | Clear 后无文本 | 需 Windows，可 CI |
| `px_user_proxy::keepalive` | 进程名大小写不敏感匹配、tick 决策（缺谁拉谁）、initial check | 单元（原 `gr_guard::process_monitor` 用例迁入） |

**统一入口：**

```bat
scripts\test_user_proxy.bat
```

等价于 `cargo test -p px_user_proxy -p px_service -p service_core`（原 `gr_guard` 已并入 `px_user_proxy::keepalive`，随 `-p px_user_proxy` 一起测试）。

### 10.3 Service 拉起与守护 UserProxy（运行时）

| 行为 | 说明 |
|------|------|
| 拉起方 | **仅** `GammaRayService.start_desktop` |
| 参数 | `--render-port={network_listen_port}` |
| 监控 | Service `monitor_loop` 每 3s：Render 在、UserProxy 不在 → 补拉 |
| 停止 | `stop_desktop` / Service Stop → 杀 Render + UserProxy |
| 日志 | Service：`starting user proxy`；UserProxy：`godesk_user_proxy.log` |

### 10.4 C++ 侧（Phase 1 以人工 + 日志为主；后续可加 gtest）

| 场景 | 期望日志关键字 | 类型 |
|------|----------------|------|
| UserProxy 连上 | Render: `user-proxy connected`；UserProxy: `connected to render` | 人工 |
| 非 localhost 连 `/user-proxy` | Render: `user-proxy rejected non-local` | 人工（可用 `curl`/自定义 WS 客户端从 LAN IP 试连） |
| 双 UserProxy 实例 | 第二个连接后第一个被踢；log 有 `replacing existing user-proxy` | 人工 |
| Client `kClipboardInfo` | Render: `PostUserProxyMessage`；UserProxy: `apply remote clipboard` | 人工 |
| 无 UserProxy 时 Client 复制 | Render: `user-proxy not connected, drop clipboard` | 人工 |
| 被控端复制 | UserProxy: `===> new Text:`；Client 可粘贴 | 人工 |

### 10.5 端到端集成测试（人工，按顺序执行）

| # | 前置条件 | 操作 | 期望 | 日志 |
|---|----------|------|------|------|
| I1 | Service `start_desktop`（Render + UserProxy） | 启动 Service 并下发 start | 2s 内 WS 连通 | Service：`starting user proxy`；UserProxy：`connected to render`；Render：`user-proxy connected` |
| I2 | I1 已连通 | 结束 Render 进程 | UserProxy 不退出，每 2s `connect failed` / `retry in 2s` | UserProxy ERROR/WARN 周期出现 |
| I3 | I2 状态 | 重启 Render | UserProxy 自动连上，无需重启 UserProxy | 同 I1 |
| I4 | I1 + Client 远程桌面 | Client 复制 ASCII 文本 | 被控端记事本可粘贴 | UserProxy：`apply remote clipboard` |
| I5 | I4 | 被控端复制文本 | Client 可粘贴 | UserProxy：`===> new Text:` |
| I6 | I1 | **不启动 Panel** | I4/I5 仍成功 | 证明不依赖 Panel |
| I7 | I1 | 快速连续双向复制不同文本 | 无死循环、无重复回传 | echo 日志：`suppressed outbound` / `Same with remote` |
| I8 | I1 | 复制空文本 / 仅空白 | 不崩溃，可跳过同步 | UserProxy INFO：`no syncable text` |
| I9 | I1 | 复制大文本（如 64KB） | 往返成功或明确 ERROR | 两端正文一致 |
| I10 | I1 | 非本机 IP 访问 `:port/user-proxy` | 握手失败或被立即断开 | Render WARN/ERROR：`rejected non-local` |
| I11 | I1 | 启动第二个 UserProxy | 仅一个有效连接；剪贴板单写 | 见 10.3 双实例 |
| I12 | Panel 已启动 | 确认 Panel 剪贴板路径已注释 | Panel 复制 **不再** 触发 `kRpClipboardEvent`（Render 无对应 log） | `win_panel_message_loop` 已注释 |
| I13 | 完整三端 | `/media` 连接、画面、键鼠 | 与改动前一致 | 无新增 WS 错误 |
| I14 | 完整三端 | 文件传输通道 `/file/transfer` | 不受影响 | `ws_panel_client` `data_channel` 分支仍工作 |
| I15 | I1 + Client | Client 复制单个/多个文件 | 被控端可粘贴，大小正确 | UserProxy：`virtual file resp buffer applied` 早于 `IStream::Read done` |
| I16 | I15 | Client 复制含子目录的文件夹 | 目标目录保留文件夹层级 | `ref_path` 含 `FolderName/...` |
| I17 | I15 | Client 同时复制文件夹与并列文件 | 文件夹内容与并列文件均出现 | `content.rs` / `clipboard_file_builder` 各根独立 |
| I18 | I15 粘贴完成 | 再次在目标处右键 | 粘贴菜单不可用（灰） | `clear_ole_clipboard_after_operation` |

### 10.6 回归清单（人工）

| 区域 | 检查项 |
|------|--------|
| Client | 连接、画面、键鼠、Client 本地剪贴板监听日志 `===> new Text:` |
| Render | `/media`、`/file/transfer`、Panel `WsPanelClient` 管理消息 |
| Panel | 插件开关、`kRpCommandRenderer`、连接记录、文件传输审计 |
| Service | 无界面部署：**Service + Render + UserProxy**；Guard/Panel 可选 |
| 安装包 | `GammaRayUserProxy.exe` 随构建产出（CMake stage 后存在于 binary 目录） |

### 10.7 失败判定与日志采集

失败时打包以下文件（均在 `C:\Users\Public\GoDesk\gr_logs\`）：

- `godesk_user_proxy.log`
- `godesk_render_{port}.log`（或当前 Render 端口对应文件）
- `godesk.log`（Panel，若启动）
- `ct_plugin_client_clipboard.log`（Client 侧）

### 10.8 CI / 自动化边界

| 可进 CI | 不可进 CI（本机人工） |
|---------|----------------------|
| `scripts\test_user_proxy.bat`（= `cargo test -p px_user_proxy -p px_service -p service_core`，单元 + Mock Render 集成） | Client ↔ 被控端画面+剪贴板联调 |
| `cargo build -p px_user_proxy` | localhost 拒绝、双实例踢连接 |
| C++ 编译 `plugin_net_ws` | `win_listener` 真实剪贴板轮询（可选 `--ignored`） |

---

## 11. 回滚方案

1. 取消第 7 节所有 `USER_PROXY_MIGRATION` 注释。
2. 停止部署 `GammaRayUserProxy.exe`。
3. Render 侧 `/user-proxy` 路由可保留（无连接时无影响）或编译开关 `GR_USER_PROXY_ENABLED` 关闭。

建议在 `ws_server.cpp` 增加编译宏：

```cpp
#ifndef GR_USER_PROXY_ENABLED
#define GR_USER_PROXY_ENABLED 1
#endif
```

便于一键回滚到 Panel 剪贴板路径。

---

## 12. 风险与对策

| 风险 | 对策 |
|------|------|
| `/user-proxy` 暴露在 `0.0.0.0` | 强制 localhost 校验 + 后续 token |
| 双通道同时启用导致双写剪贴板 | 注释 Panel 路径；单 UserProxy 连接 |
| SYSTEM Render 与 UserProxy 时序 | UserProxy 永久重连；Render 无连接时 drop 并打 WARN |
| Rust/C++ proto 不一致 | 共用 `tc_render_panel_message.proto`；集成测试覆盖 |
| 剪贴板文件复杂度 | Phase 1 仅文本，文件放 Phase 2 |

---

## 13. 参考文件索引

| 用途 | 路径 |
|------|------|
| Panel WS 服务 | `src/px_panel/src/render_panel/network/ws_panel_server.cpp` |
| Render 连 Panel | `src/px_render/network/ws_panel_client.cpp` |
| Client 消息转发 Panel | `src/px_render/plugins/plugin_net_event_router.cpp` |
| Panel 剪贴板写入 | `src/px_panel/src/render_panel/clipboard/panel_clipboard_manager.cpp` |
| Panel 剪贴板监听 | `src/px_panel/src/render_panel/system/win/win_panel_message_loop.cpp` |
| net_ws 服务 | `src/px_render/plugins/net_ws/ws_server.cpp` |
| 协议定义 | `src/px_deps/px_message_new/tc_render_panel_message.proto` |
| Rust 日志工具 | `rust_base/px_base/src/log_util.rs` |
| Rust WS 重连示例 | `rust_client/px_sysinfo/src/sys_panel_client.rs` |
| Service 用户令牌启动 | `rust_client/px_service/src/windows_process.rs` |
| Client 剪贴板插件 | `src/px_client/plugins/clipboard/` |
| 公共剪贴板模块 | `src/px_deps/px_common_new/clipboard/` |

---

## 14. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-07-02 | 初稿：基于 Render/Panel/Client 剪贴板讨论与用户五项需求编写 |
| 2026-07-02 | 扩充 §10 测试计划：Rust 单元/组件测试矩阵 + 人工集成/回归清单 |
| 2026-07-02 | §10 更新：Mock Render 集成测试、Guard 自动拉起 UserProxy、`scripts/test_user_proxy.bat` 统一入口 |
| 2026-07-02 | Phase 2/3：CF_HDROP 文件剪贴板、Service 拉起/停止 UserProxy、心跳与日志增强；131 项自动化测试 |
| 2026-07-02 | 架构调整：UserProxy **仅**由 Service 拉起/守护；Guard 移除 UserProxy（服务端无界面 Service+Render） |
| 2026-07-02 | 复评修复：① 文件剪贴板 echo 回环（`apply_remote_files` 回读登记 `files_signature`）；② UserProxy 改为**仅 WTS 用户令牌**拉起（`start_process_as_session_user`，不再走 SYSTEM 令牌路径）；③ 端口解析兼容 `--flag value` 形式；129 项自动化测试全通过 |
| 2026-07-02 | Phase 2 完成：OLE 虚拟文件剪贴板（`VirtualFileCoordinator` + COM `IDataObject`/`IStream`）、`kClipboardReqBuffer`/`kClipboardRespBuffer` data_channel 双向流、Render `kClipboardRespBuffer`→UserProxy 转发；78 项自动化测试全通过 |
| 2026-07-03 | Phase 2 联调收尾：死锁修复、`ref_path` 目录/混合选择对齐、`EndOperation` 清 OLE 剪贴板、移除安装时伪造 HDROP；`clipboard_file_builder` 与 `collect_dist` 更新；人工验证 Client→被控端文件复制粘贴；82 项自动化测试全通过 |
