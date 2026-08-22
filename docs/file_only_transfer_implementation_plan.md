# 独立仅文件传输会话实施计划

> 状态：实施中（2026-08-22）
> 范围：Panel 设备菜单启动独立 `px_client.exe --mode=file-transfer`；第一期仅支持 RTC LAN、直连 WSS 与 Relay，不依赖 TURN。

## 1. 产品行为

设备卡片的更多菜单在“Only Viewing”后增加“File Transfer”。点击后由 Panel 创建一条仅含 `file` 权限的 CMS 连接票据，并启动**另一个** `px_client.exe` 进程。它仅展示文件管理窗口。

已有画面远控会话时，客户端控制栏的文件传输入口继续复用当前进程和既有 FT 通道；不创建第二个进程。

独立会话禁止视频、音频、输入与控制通道，Render 不得为其启动采集或编码。

## 2. 启动与票据

Panel 的 `AppStreamList::StartFileTransfer`：

1. 仅面向 CMS 已登录的设备条目；匿名/旧式密码连接不提供该入口。
2. 调用 `IssueDeviceTicket(device_id, nonce, {"file"})`，不启动应用实例。
3. 从 ticket 的 launch URL 取得 Render endpoint。
4. 选择 RTC LAN、WSS 或 Relay，并交给 `RunningStreamManager::StartFileTransfer`。
5. 启动同一份 `px_client.exe`，新增 `--mode=file-transfer`；进程表以 `file_session_id` 键控。

票据在现有启动参数中已按敏感参数脱敏日志。后续安全加强应以本机受限 IPC 传递 launch handle，避免在命令行携带原始票据；本期保持既有受控启动链路一致。

## 3. 通道选择

| 顺序 | 通道 | 条件 | 客户端行为 |
| --- | --- | --- | --- |
| 1 | RTC LAN | Panel 能直连 Render 且未强制 Relay | 只创建 `ft_data_channel`，无 media track |
| 2 | WSS | RTC LAN 失败且 Render WebSocket 可达 | 连接 `/file/transfer`，完成 file-only 认证 |
| 3 | Relay | 已有 Relay 信息且在线 | 走 `kFileTransfer` |

第一期提供开发强制选路开关，验收不能只验证自动降级。TURN 后续作为 RTC 的独立扩展，不阻塞本期。

## 4. 实现分层

### 4.1 Panel

- `AppStreamList` 添加 `File Transfer` 动作与 `StartFileTransfer`。
- `RunningStreamManager` 增加轻量启动方法，仅传文件模式必要参数。
- `StartStream` 保持原样，不能被文件模式调用。

### 4.2 px_client

- 解析 `--mode=file-transfer`。
- 文件模式保留最小 `Workspace` 作为既有插件宿主，但跳过 Vulkan/D3D、播放器、音频、输入钩子、游戏视图和屏幕相关 UI；只加载 `ft_client`。
- 从连接成功事件显示 FT 根窗口；连接失败可返回 Panel 可诊断错误。

### 4.3 Render / FT

- file-only ticket 仅能打开 FT 通道，拒绝 media/input/clipboard 通道。
- FT 状态按 `stream_id/session_id` 隔离。并发画面会话与独立文件会话不能共享“当前 stream”或同一个 engine 状态。
- WS 与 Relay 在进入引擎前也必须验证 ticket；不能只依赖客户端隐藏 UI。

## 5. 本机验收

每个 RTC LAN、WSS、Relay 用例均执行：上传、下载、目录、覆盖确认、取消、断线续传、SHA-256 比对。

额外检查：

- Panel 菜单启动第二个 `px_client.exe --mode=file-transfer`；
- 文件模式无播放器、音频与输入窗口；
- Render 无 video track、编码器或 input channel，仅有 FT channel；
- 画面会话内打开文件传输不增加进程；
- 过期/重复/跨设备 ticket 以及非 FT 消息被拒绝；
- 画面会话与独立会话并发时，任务、续传文件与审计记录互不串扰。

## 6. 分阶段完成定义

1. **Panel 启动骨架**：菜单、`file` ticket、独立客户端参数与可测试的启动选择。
2. **RTC LAN**：data-only 建连和端到端文件传输。
3. **WSS / Relay**：认证、精确路由、三通道本机 E2E。
4. **隔离与安全**：Render 多会话 engine、拒绝非 FT 通道与完整审计。
