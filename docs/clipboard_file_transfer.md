# 剪贴板文件传输：双向打通与踩坑记录

本文记录本地客户端 ↔ 远端主机之间剪贴板**文件**双向传输的实现要点与踩过的坑。文本传输（`CF_UNICODETEXT`）本身是通的，本文只覆盖文件。

## 架构

方向不同，虚拟文件数据对象落在哪一端也不同：

| 方向 | 谁复制 | 谁粘贴 | 虚拟文件对象在哪 | 对象实现 |
|------|--------|--------|------------------|----------|
| client → host | 客户端(16) | 主机(90) | 主机 90 | Rust `px_user_proxy` (`win_clipboard.rs`) |
| host → client | 主机(90) | 客户端(16) | 客户端 16 | C++ 客户端插件 `CpVirtualFile` |

数据链路：

- 元数据：`kClipboardInfo`（`ClipboardType::kClipboardFiles`）携带 `full_path` / `ref_path` / `total_size`。
- 取数：粘贴方 `IStream::Read` → 发 `kClipboardReqBuffer`（`full_name` + `req_index` + `req_start` + `req_size`）。
- 回数：复制方读本地磁盘 → 回 `kClipboardRespBuffer`（同样的字段 + `read_size` + `buffer`）。

```
client(CpFileStream) --kClipboardReqBuffer--> render --data_channel--> user_proxy --读90磁盘--> kClipboardRespBuffer
user_proxy(Rust stream) --kClipboardReqBuffer--> render --data_channel--> client --读16磁盘--> kClipboardRespBuffer
```

`kClipboardReqBuffer` / `kClipboardRespBuffer` 都走**文件传输通道**（`media_channel_=false` → `PostFileTransferMessage`），渲染端用 `RpRawRenderMessage.data_channel=true` 与 user_proxy 互通。

## 踩坑清单

### 1. 虚拟文件必须落在带消息泵的 STA 线程上（最关键）

**现象**：`OleSetClipboard` 返回成功、`EnumClipboardFormats` 也能枚举到 `FileGroupDescriptorW` 等格式，但 Explorer 右键**没有“粘贴”**，`Ctrl+V` 也没用。跨进程 `GetClipboardData(CF_HDROP / FileGroupDescriptorW)` 全部返回 NULL。

**根因**：`OleSetClipboard` 原本在**网络消息线程**上调用。该线程没有 STA 消息泵，数据对象无法被跨进程 marshal —— Explorer 取不到数据对象，只能看到缓存过的格式列表（`IsClipboardFormatAvailable` 为真），但真正取数据（`GetData`）时调用根本到不了我们进程。

**证据**：在数据对象的 `GetData`/`QueryGetData`/`EnumFormatEtc` 里加日志后，只能看到 `OleSetClipboard` 时系统缓存格式触发的 `EnumFormatEtc`，Explorer 右键后**没有任何** `QueryGetData`/`GetData` 日志；而 `GetClipboardData` 对三种格式全部返回 NULL。

**修复**：
- `WinMessageLoop::ThreadFunc` 启动时 `OleInitialize`，让剪贴板消息线程成为带消息泵的 STA。
- 新增 `WinMessageLoop/WinMessageWindow::PostTask`，把“清空 + `OleSetClipboard` + `OnClipboardFilesInfo`”整套**投递到该线程**执行（`ct_clipboard_manager.cpp` 文件分支）。

Rust 侧之所以一直正常，就是因为它专门在 `win_listener` 的隐藏窗口消息线程（带消息泵的 STA）上执行 `install_virtual_file_clipboard`。

### 2. “粘贴”菜单需要 CF_HDROP / CFSTR_PREFERREDDROPEFFECT

**现象**：数据对象只注册 `CFSTR_FILEDESCRIPTOR` + `CFSTR_FILECONTENTS` 时，Explorer 不亮“粘贴”。

**根因**：Explorer 判断“可粘贴文件”主要看 `IsClipboardFormatAvailable(CF_HDROP)`（走的是 `OleSetClipboard` 时缓存的格式列表）。只有 `FileGroupDescriptorW` 不够。

**修复**：`CpVirtualFile` 额外注册/提供：
- `CF_HDROP`（`DROPFILES`，内容填 `ref_path`，仅用于点亮菜单，真正取数走 `FileContents`）。
- `CFSTR_PREFERREDDROPEFFECT`（`DROPEFFECT_COPY`）。

参考：旧版 `px_render/plugins/clipboard/win/cp_virtual_file.cpp`（已注释）里本来就有 `m_cfHdrop` 和 `m_cfPreferredDropEffect`。

### 3. 单块大小不能超过 128 KiB

**现象**：粘贴能成功，但**每个文件只有 256 KiB**。日志出现 `invalid req index, send: 1, received: 0`。

**根因**：文件传输通道（SCTP data channel / UDP ft）单条消息上限约 256 KiB。客户端 `CpFileStream::Read` 之前按 Explorer 请求的 256 KiB 原样取数，回包 `256 KiB 数据 + protobuf 头` 超过上限被丢弃/错乱，第二次读拿到的是上一块旧回包，`req_index` 对不上，文件在 256 KiB 处被截断。

**修复**：`CpFileStream::Read` 把单次请求上限压到 `128 * 1024`（与 Rust 侧 `MAX_READ_CHUNK_SIZE` 一致），Explorer 会继续发 `IStream::Read` 取剩余字节。

### 4. 过期回包要容忍，不要直接判死

**现象**：`req_index` 不匹配时若直接 `return S_FALSE`，整个文件复制中止（放大第 3 点的破坏）。

**修复**：`CpFileStream::Read` 改成循环等待，遇到 `req_index` 不匹配的**过期回包**（网络层偶发重复包）时丢弃并继续等正确回包，直到超时。

### 5. render 与 user_proxy 都要补 kClipboardReqBuffer 处理

host → client 方向的取数链路原本是断的：
- `px_render/plugins/plugin_net_event_router.cpp` 原来只把 `kClipboardInfo`/`kClipboardRespBuffer` 转发给 user_proxy，**没有转发 `kClipboardReqBuffer`**。
- `px_user_proxy/src/render_client.rs` 原来只处理 `kClipboardRespBuffer`，**没有处理 `kClipboardReqBuffer`**（只有 `mock_render.rs` 里有一份参考实现）。

**修复**：
- render 把 `kClipboardReqBuffer` 与 `kClipboardRespBuffer` 一起按 `data_channel=true` 转发给 user_proxy。
- user_proxy `handle_inbound_data_channel` 新增 `dispatch_req_buffer`：按 `full_name`/`req_start`/`req_size` 读 90 本地文件，回 `kClipboardRespBuffer`（`req_index` 原样回显）。

### 6. DragQueryFileW 计数参数

`read_hdrop_paths` 里 `DragQueryFileW(drop, 0xFFFF, ...)` 在 windows-rs 下取到 0 个文件；改为 `u32::MAX` 后正常。

### 7. 事件驱动监听，不要轮询

主机侧剪贴板监听改为隐藏 `HWND_MESSAGE` 窗口 + `AddClipboardFormatListener` + `WM_CLIPBOARDUPDATE`，替换原来的 250ms 轮询。

### 8. 设置剪贴板期间要抑制回环读取

客户端文件分支里加 `clipboard::SuppressOutboundGuard`：否则 `Clear()` 触发本机 `WM_CLIPBOARDUPDATE` → `OnLocalClipboardUpdated` 抢剪贴板，导致 `OleSetClipboard` 反复失败（`Set clipboard failed!`）。

### 9. GetCanonicalFormatEtc 要写输出参数

`cp_data_object.h` 原实现 `pformatetcIn->ptd = NULL` 误改了**输入**参数，应为 `pFormatetcOut->ptd = NULL`。

## 关键常量

- `MAX_READ_CHUNK_SIZE = 128 * 1024`（客户端 `CpFileStream` 与 Rust `stream.rs` 一致）。
- 通道单条消息上限约 256 KiB（SCTP `max-message-size`）。
- 剪贴板格式：`CF_UNICODETEXT`=13、`CF_HDROP`=15、`CFSTR_FILEDESCRIPTOR`/`CFSTR_FILECONTENTS`/`CFSTR_PREFERREDDROPEFFECT` 为注册格式。

## 调试方法

- 客户端日志：`C:\Users\Public\GoDesk\px_logs\ct_plugin_client_clipboard.dll.log`（注意 `Get-Content -Tail` 对它可能返回空，用 `Select-String`）。
- 主机日志：`\\10.0.0.90\C$\Users\Public\GoDesk\px_logs\godesk_user_proxy.log`、`godesk_render_20371.log`、`plugin_net_ws.dll.log`。
- 验证剪贴板格式：`OpenClipboard` + `EnumClipboardFormats` + `IsClipboardFormatAvailable`；验证跨进程取数用 `GetClipboardData` 是否返回 NULL（NULL 即 marshal 断了）。
