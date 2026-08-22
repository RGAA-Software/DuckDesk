# 独立“仅文件传输”开发与验收说明

> 状态：已完成（2026-08-22）
>
> 首期通道：直连 WS、RTC LAN、Relay
> 不在本期：RTC TURN；Coturn 的 CMS 托管与状态展示是独立能力，不影响本功能验收。

## 1. 最终产品行为

设备卡片右侧菜单在“Only Viewing”后提供“File Transfer / 文件传输”。用户从这里启动时：

1. Panel 先检查该设备是否已有普通画面客户端连接到本机 Panel。
2. 已有连接时，Panel 通过本机 WebSocket 按 `stream_id` 定向下发
   `kCpOpenFileTransfer`，现有 `px_client` 打开内置文件窗口并复用当前 FT 通道；
   不申请新 ticket，也不启动第二个进程。
3. 没有连接时，Panel 才向 CMS 申请一张仅含 `file` 权限、绑定目标设备
   和 nonce 的一次性 ticket，并启动同一份
   `px_client.exe --mode=file-transfer`（不是新增第二种客户端程序）。
4. 独立进程只显示文件管理窗口，只加载 `ft_client` 插件；不显示远控画面，
   不接受键鼠、音频、剪贴板或媒体消息。其进程键为唯一
   `ft_<stream>_<timestamp>`，不会覆盖普通画面客户端。

独立窗口会先显示本地目录；FT 通道首次连接成功以及以后每次重连成功时，
客户端会主动刷新远端根目录。这样启动阶段过早发出的第一次 `ReadDir("/")`
即使尚未进入网络队列，也不会造成右侧永久空白。

## 2. 从哪里启动

正式入口：

1. 启动 CMS、Panel 和目标 Render。
2. 在 Panel 登录 CMS 用户；个人设备不做额外授权/分组管控，账号登录正确且
   目标设备在线即可看到并连接。
3. 在设备卡片右侧点击更多菜单。
4. 点击 `File Transfer / 文件传输`。

未登录、设备无 ID、目标离线、ticket 申请失败或没有可用 Relay 信息时，Panel 会停止启动并显示错误，不回退到静态密码模式。

## 3. 三通道选择规则

文件入口不增加隐藏命令，也不再做一个客户端。选路规则为：

| 设备设置 | 直连探测 | 实际通道 | 文件模式行为 |
| --- | --- | --- | --- |
| 已有普通画面客户端 | 任意 | 复用当前 WS / Relay / RTC LAN | 现有进程内打开窗口，进程数不增加 |
| 无普通客户端，`Force Relay / 强制使用中转服务` 开 | 任意 | Relay | 只创建 `ft_client_* -> ft_server_*` 文件房间 |
| 无普通客户端，Force Relay 关 | 成功 | WS | 只连接 `/file/transfer` |
| 无普通客户端，未强制 Relay | 失败 | Relay | 有 Relay 信息才启动，否则报错 |

独立文件进程统一使用可靠 WS，而不另占 RTC LAN：Render 的 RTC LAN 当前是
单会话，第二个 RTC 进程会与画面连接竞争并可能触发接管。普通客户端本身走
RTC LAN 时，文件窗口复用它已有的 `ft_data_channel`，因此 RTC LAN 文件传输
仍然受支持。`Use UDP` 同样复用普通会话的 WS 文件通道；没有普通会话时使用
独立 WS。RTC LAN 不配置 STUN/TURN，只使用本机/LAN ICE candidate。

## 4. 客户端进程与资源边界

`px_client --mode=file-transfer` 强制以下边界：

- ticket 和 nonce 都不能为空；只允许 `websocket`、`webrtc_direct`、`relay` 三种网络类型；
- `enable_video=false`、audio/clipboard/input 关闭；不安装全局键盘 hook；
- 跳过 Vulkan 能力探测、播放器、解码器、音频播放器和画面 UI；
- 插件管理器只保留 `ft_client.dll`；
- 保留一个隐藏的最小 Workspace 作为既有消息总线/插件宿主，避免异步监听器访问已销毁 UI；
- WS 和 Relay 都不创建媒体连接；RTC offer 不声明音频/视频 m-line，也不创建 media/input data channel；
- FT 窗口关闭后只终止该独立文件会话，不影响画面会话或 Render。

最后一项只适用于 `--mode=file-transfer` 独立进程；复用普通客户端时关闭 FT
窗口不会终止普通客户端。

## 5. 认证和会话隔离

### 5.1 Ticket

仅在没有可复用普通客户端时，Panel 调用：

```text
IssueDeviceTicket(device_id, nonce, ["file"])
```

客户端命令行中的 ticket 使用 Base64 包装；Panel 和客户端日志会对 ticket、nonce、appkey、token、密码参数脱敏。原始 ticket 是短时、一次性 capability，服务端校验目标设备、nonce、有效期、用户会话、未消费状态和 `file` 权限。

### 5.2 WS

独立客户端只连接：

```text
/file/transfer?...&file_only=1&ticket=<ticket>&client_nonce=<nonce>
```

Render 在创建 `WsFileTransferRouter` 前兑换 ticket。`file_only=1` 缺 ticket、ticket 无效或没有 `file` 权限时立即关闭连接；不会创建媒体 WebSocket。

### 5.3 RTC LAN

ticket 在 `/alloc/local/rtc` 信令阶段兑换。文件权限会传入 `RtcServer`：

- 客户端 offer 仅含 SCTP/FT data channel；
- 服务端只接受 `ft_data_channel`；
- 无 `view` 权限时不建立媒体通道/视频 track；无 `input` 权限时不建立输入通道。

### 5.4 Relay

Relay 握手携带 `file_only=1`、ticket、nonce 和目标 `ft_server_<device>`。CMS 在 WebSocket upgrade 前兑换 ticket，并把授权目标写入连接状态。后续建房必须同时满足：

- 发起端 ID 以 `ft_client_` 开头；
- 目标端 ID 精确等于 ticket 握手绑定的 `ft_server_<device>`；
- room 请求中的发起端等于当前连接 ID。

因此文件 ticket 不能用来建立 media room，也不能在兑换后换目标设备。Relay file-only 进程只创建一条 FT Relay 连接。

### 5.5 多会话

Render 的 FT 插件按 `stream_id` 保存独立 `FtEngine`。画面会话、多个独立文件会话的任务、回复、断线清理和审计记录不会再通过“最后一个活动 stream”互相串线。

## 6. 主要实现位置

| 层 | 文件 | 职责 |
| --- | --- | --- |
| Panel 菜单/选路 | `src/px_panel/src/render_panel/devices/app_stream_list.cpp` | 复用判断、ticket、独立 WS/Relay 选择 |
| Panel 命令与进程 | `src/px_panel/src/render_panel/devices/running_stream_manager.cpp` | 定向打开现有窗口；否则生成唯一会话并启动同一 `px_client.exe` |
| Panel 本机通道 | `src/px_panel/src/render_panel/network/ws_panel_server.cpp` | 按 `stream_id` 定向发送命令 |
| Panel/Client 协议 | `src/px_deps/px_message_new/px_client_panel_message.proto` | `kCpOpenFileTransfer` 命令 |
| 客户端模式 | `src/px_client/ct_main_ws.cpp`、`ct_base_workspace.cpp` | 参数约束、轻量 UI/插件宿主 |
| 客户端 Panel 接收 | `src/px_client/network/ct_panel_client.cpp` | 把定向命令转换成客户端打开 FT 事件 |
| 通道创建 | `src/px_deps/px_client_sdk_new/sdk_net_client.cpp` | file-only 不创建媒体连接 |
| RTC data-only | `src/px_deps/px_webrtc_client/rtc_connection.cpp` | 只协商 FT data channel |
| WS 认证 | `src/px_render/plugins/net_ws/ws_server.cpp` | 兑换 file ticket、拒绝未授权连接 |
| Relay 认证 | `rust_server/px_cms_server/src/cms_relay/relay_server.rs` | 握手兑换与建房目标绑定 |
| FT 隔离 | `src/px_render/plugins/ft/ft_plugin.cpp` | 每 stream 独立引擎 |

## 7. 编译与发布

客户端专用脚本会自动发现 Visual Studio，构建并发布客户端、RTC DLL 和 FT 插件：

```bat
scripts\build_px_client.bat build_official 8
```

输出必须同时存在：

```text
build_official\dist\px_client.exe
build_official\dist\px_client_rtc.dll
build_official\dist\deps\ct_plugins\ft_client.dll
```

CMS：

```bat
cd rust_server
cargo check -p px_cms_server
cargo test -p px_cms_server --no-fail-fast
cargo build -p px_cms_server --release
```

本机 VS 2026 环境如果覆盖了 vcpkg 路径，先进入 `VsDevCmd.bat`，并设置 `VCPKG_ROOT=C:\source\vcpkg`、`CMAKE_GENERATOR=Ninja`。

## 8. 本机测试方法

### 8.1 自动化基础测试

构建并运行四个 FT 测试程序：

```bat
cmake --build build_official --config RelWithDebInfo --target ^
  test_ft_path_security test_ft_compress test_ft_transfer_job test_ft_engine

build_official\src\px_deps\px_ft_engine\tests\test_ft_path_security.exe
build_official\src\px_deps\px_ft_engine\tests\test_ft_compress.exe
build_official\src\px_deps\px_ft_engine\tests\test_ft_transfer_job.exe
build_official\src\px_deps\px_ft_engine\tests\test_ft_engine.exe
```

当前结果：51/51 通过，覆盖目录遍历防护、压缩、上传/下载、目录递归、覆盖三分支、取消清理、续传、摘要生命周期、限速/背压和按连接断线清理。CMS 当前 130/130 单元测试通过。

### 8.2 三通道建连验收

本机已对同一 Render 分别注入三张短时、一次性、仅 `file` ticket，启动发布目录中的真实 `px_client.exe`：

| 通道 | ticket 被消费 | 客户端兑换后存活 | 关键运行证据 |
| --- | --- | --- | --- |
| WS | 是 | 是 | 仅 `/file/transfer`；日志明确 `media websocket disabled` |
| RTC LAN | 是 | 是 | SDP 约 673 字节；只出现 `ft_data_channel`，无 media/input channel、无 video track |
| Relay | 是 | 是 | 只建立 `ft_client_* -> ft_server_*` 房间 |

测试产生的临时 Mongo session/ticket 已全部删除。无 ticket/无效 ticket 的 WS file-only 连接和 Relay 握手均已验证被拒绝。

获得一张真实 CMS ticket 后，也可手动启动 WS smoke test：

```bat
scripts\test_file_transfer_only_wss.bat <device_id> <host> <port> <ticket_b64> <nonce>
```

脚本名称保留历史兼容；当前 C++ 原生直连实现使用明文 WS。

### 8.3 UI 最终验收清单

按 WS、RTC LAN、Relay 三种普通连接设置以及无普通连接的独立模式执行：

1. 没有画面客户端时从 Panel 启动，确认新增一个
   `px_client.exe --mode=file-transfer` 进程；远端根目录在连接完成后自动出现，
   不需要手工刷新，且不触发重连。
2. 新建目录；上传单文件和目录；下载回本机并做 SHA-256 比对。
3. 验证同名覆盖、跳过、取消、断开后续传。
4. 保持一个画面会话，再从 Panel 设备菜单点击文件传输，确认在现有客户端内
   打开窗口、`px_client.exe` 数量不增加、画面连接不重连或退出。
5. 分别让普通画面客户端使用 WS、Relay、RTC LAN，确认 FT 均复用对应会话；
   RTC 日志只保留原连接中的 FT data channel，不出现第二次 RTC alloc。
6. 关闭画面客户端后再次从 Panel 点击，确认独立直连固定走 WS；强制 Relay
   时只建立 FT room。

## 9. 后续扩展

TURN 不是这三条首期通道的阻塞项。后续若让 RTC 跨 NAT，应在现有 RTC 配置中加入 CMS 下发的 ICE server/临时 TURN credential，并把验收矩阵增加 `RTC TURN`；不要改变当前 Panel 入口或再生成一个客户端 EXE。

命令行仍可看到当前进程自己的 ticket 参数。进一步加固可改为 Panel 与客户端之间的一次性本机 IPC launch handle；这不会改变服务端现有的一次性、设备绑定和权限校验。
