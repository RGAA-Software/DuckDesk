# BJ WebView 与普通桌面接入

日期：2026-10-10。接续单 Relay 主线；未变更 BJ 登记、Console 持久 iroh 配置或 Android。

## WebView

首测实例 `5b799633-840a-4e04-8b64-344ba45aa52c` 经 BJ 接入成功并发送输入，但两张截图均等待首帧。源端唯一 readiness 帧出现在客户端接入前，之后视频为 0 FPS。原始结果为 `20261010_bj_webview_results.json`，没有视频窗口，不能算通过。

`WebViewClient::SetActive(true)` 和 `RequestFrame()` 现在先从 Render 自己持有的 GPU 纹理/软件缓冲补发，再请求 CEF 重绘。不保留或重用借来的 CEF 共享句柄。首帧探测、无客户端暂停和退出宽限期保留。增加 `webview.activity`、`webview.frame_replay` 日志。预改文件含已有工作区修改完整保存在 `backup/webview_first_viewer_20261010`。

完整 Cloud Node 候选 `iroh-webview-viewer-20261010` 已安装在 90：

- 版本 3.3.97；314 项文件核验，px_service Running。
- Setup SHA-256：`BC766CB8188A589948CA01DC88E215CD6400C6C3A831C8AAF08F64F442E124C0`。
- Manifest SHA-256：`7B825873F65AE6156151161EF468C662339B1A50577F54C518F25DEE5B93712C`。
- 安装 Render SHA-256：`048A07C56EAFF5A5B53DA6D93725C9539DE9BAC51AA2DEF4E80E447947A872C4`。
- 安装文件位于 `D:\112233\iroh-webview-viewer-20261010`；本机候选已逐文件校验迁至 C 盘，见 `20261010_webview_viewer_candidate_archive.json`。

后测实例 `5da6e9a8-2a37-4af3-b5c1-fdcd5ba4cdf5`：Client 13:01:15.682 接受连接，13:01:15.751 收到重放 frame 2；源端 activity/replay 对应同一帧，两台机器墙钟有偏移，不直接相减。真实路径 iroh-relay。截图 frame-600/frame-2400 均已加载 Three.js 水面，视角不同，输入日志 sent=true。

15 个五秒窗口共 4222 帧，28.0–60.0 交付 FPS、14 次 >100ms，0 IDR/RFI。最长 943.1ms 位于加载初期，同帧源端间隔 921.811ms；页面正常绘制后的多个窗口约 57–60 FPS。此次网页导航在接入前完成，和首测网页未完成加载不同，不能把所有加载差异归于补帧。首帧功能恢复有明确证据，公网稳定性没有整体通过。数据见 `20261010_bj_webview_after_results.json`、`20261010_bj_webview_measurements.json`、`20261010_webview_source.log`。

## 普通桌面

首测 BJ 传输已到达 Render，但 admission 返回 `INVALID_ARGUMENT`。代码核对：Panel 的已登录免密码设备连接发送已有 Console session 授权；共用 `AuthenticateFrontendAsync` 对桌面无条件进入密码分支。因此不是简单补测试脚本的密码；必须接通现有桌面 Console 授权。

Service 和 Console 已支持 desktop grant，本次仅修正 Render：请求带现有 Console descriptor 时使用现有 admission；桌面 grant 必须匹配本 Render 设备，应用 grant 必须匹配本 Render 实例。未带 Console descriptor 的桌面密码连接保留。没有新票据、认证系统或协议字段。已有续期与关闭流程复用。

新增真实 QUIC 用例 `DesktopAcceptsExistingConsoleSessionWithoutDevicePassword` 在旧实现失败（`INVALID_ARGUMENT`），修正后通过。直连与私有 TLS Relay 各通过 `iroh_frontend`、`ws_frontend_admission`，包括既有密码、流路由、关闭及生命周期用例。证据为 `20261010_desktop_admission_before.log`、`20261010_desktop_admission_direct.log`、`20261010_desktop_admission_relay.log`。

首个桌面候选 `iroh-desktop-console-20261010` 已通过完整 Setup 安装（314 项校验，见 `20261010_desktop_console_install.json`），但后测仍失败：`target_kind=desktop device_match=true instance_match=false`。继续定位到 composition root 为桌面错误设置 `application_instance_id=device_id`。现已修正：桌面实例 ID 为空，GameHook/WebView 保留各自实例，RDP 保留其专用实例 ID。桌面空实例身份在 Panel 设置刷新后的保留回归通过。

该失败测试在 Console 会话先变成 connected 时提前判断成功，但 Client 随后被 Render 身份检查拒绝，没有解码画面。脚本现要求 Client `iroh.connect accepted` 与解码记录都存在才判定接通，截图只捕获自有 Client。失败批 `.cache/bj-desktop-20261010-131119` 不算通过。

第二个完整候选 `iroh-desktop-identity-20261010` 已安装并通过功能后测：

- 3.3.97、314 文件哈希一致、服务 Running，完整安装证据 `20261010_desktop_identity_install.json`。
- Setup SHA：`8A48501CB67AB3CEC8CB92B34F793131719F76110A05F9C17F5870A4B76A866F`。
- 安装 Render SHA：`936B82F78F2B7D2CD21FBBA4A780CED6F8C0621892424D3F1607D4E7FFF48AD4`。
- 开发 build/dist Render 同为 `E61F921031BEFD0EBCD779382A6CE5535E0EBD8F2DBAAB32256B7DB92BF82D3C`，记录 `20261010_webview_desktop_dist.json`。
- 会话 `1706bd23-81b4-4146-bb8f-dd17622ee595` 于 13:18:26.985 接入、13:18:27.311 收到 frame 4、13:18:27.335 解码完成。截图 `.cache/bj-desktop-20261010-131825/desktop.png` 确认是 90 Windows 桌面。
- 约 30 秒内保持连接并继续收到画面，实际路径 `iroh.path path=relay`，0 IDR/RFI。静态桌面中 27.045 秒交付间隔对应源端 27.050 秒未产生新帧，组帧 11us；本次不是动态桌面的 FPS/性能验收。
- 结果 `20261010_bj_desktop_results.json`，生命周期回归 `20261010_desktop_identity_lifecycle.log`。候选已逐文件验证可逆移至 C 盘，90 开发/安装文件仍在 `D:\112233`。

普通桌面测试只临时授予现有管理员设备访问权，结束后去除本次增加的 ID，并保留其他访问条目；记录 `20261010_bj_desktop_access_restore.json`。ACL 修改导致原会话失效，清理时重新登录再读写。两次失败测试权限均已恢复。

最终清理：本批自有 Client、云应用实例、桌面资源会话已结束，临时 ACL 恢复；90 仅桌面 Render 48064，Console/Relay/px_service 均 Running，QoS 0。Console 持久配置 SHA 未变化，BJ ready/fresh、空闲连接 1。见 `20261010_webview_desktop_cleanup.json`。

下一批从 90 Relay iroh 统一与双 Relay 切换接续，不重新安装本批候选或重复 BJ 登记；Android 最后。M1 和总计划尚未完成。
