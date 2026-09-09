# WebView 编辑快捷键与文本剪贴板修复

后续本机输入法、Qt/Web 双客户端文字面板与 game Hook 文本提交的统一计划见
[跨客户端应用文本输入设计](application_text_input_design_20260909.md)。
该计划尚待实施，本文的剪贴板验收不代表输入法功能已完成。

## 已确认的阻断点

1. WebView 的 ClipboardInfo 被路由到未连接的 user-proxy，Render 日志明确记录丢弃。
2. 原 OSR 按键转发未完成标准编辑快捷键行为；新增 CEF 显式编辑调用后，中文粘贴已替换原选区。
3. 21:44 首轮修复测试中，CEF 复制结果被 WS 的 `SESSION_CAPABILITY_DENIED operation=clipboard` 拦截。
   Console 普通应用票据原来只授予 view/input/audio，未授予 WebView clipboard。

## 实现与边界

- Console 登录用户和访客两条应用票据路径共用应用权限选择函数。
  WebView control 增加 clipboard，observe 仍只有 view/audio；game/RDP 权限集合不变。
- Render 已有控制租约校验之后，WebView 文本消息进入自身运行时，不再依赖 user-proxy。
- CEF UI 线程保存至多 1 MiB UTF-8 文本，Ctrl+A 使用聚焦 frame 的 SelectAll；
  Ctrl+C 从 CEF 的选区回调获取文本并发送，Ctrl+X 同时执行 Cut；
  Ctrl+V 通过已有 RAII 平台剪贴板实现写入文本后调用 frame Paste。
- 不轮询宿主剪贴板，不把其他应用的剪贴板变化自动发送给客户端。
  粘贴仍使用当前 Windows 会话剪贴板，WebView 不因此成为 Windows 用户隔离模式。
- 异步调用持有 CEF 引用，执行时检查 browser；失活时清除缓存与选区。
  保留传输层能力校验，没有把所有会话的 clipboard_allowed 改为 true。
- 本次范围是文本快捷键同步，不宣称图片、文件、HTML 格式、网页 Clipboard API 或右键菜单复制已验收。

## 构建及阶段验证

- Render 增量构建成功，build/dist/90 一致：
  `B7E5D3BD144BAFDFE1ABC542A5DC392B0DDFFB9C12128416FFD68C76A28245AE`。
- 90 上一版保留为 `C:\Program Files\PixelsRender\px_render.pre-clipboard-20260909.bak`，
  原桌面 Render 和 Windows 会话没有停止或注销。
- `common_clipboard_echo`、`client_clipboard_module_lifecycle`、`render_execution_context_lifecycle` 三项通过，合计 2.36 秒。
  这些为已有回归测试，不等价于新增 CEF 编辑逻辑的独立单元测试。
- 第一轮 `inst-219-ffca9ee8` / `app-cli-20260909134356`，69.53 秒：
  中文粘贴视觉成功，复制回读失败；当时 Console 尚未更新权限。
  Client 正常确认退出，精确实例停止及重复停止返回 stopped。
- C++ ownership 与 diff whitespace 检查通过。改动文件有既有整文件格式问题，未重排无关代码。
- Console 初次直接 Cargo 构建缺 MSVC/NASM 环境且 CMake generator 与缓存不一致；
  改用 VsDevCmd + 仓库 NASM + Ninja，不删除缓存或修改依赖。

## 最终复验

- Console release 构建成功，5 项 connection_ticket handler 测试全部通过，包含新增
  WebView control/observe 与 game/RDP 权限不扩张断言。
- Console build 与 `output/px_console/px_console.exe` 校验一致：
  `5D6BD9BDE213951B25D1013FB2E86FD136FA443C1B0DB366B1F40A1D8921CADE`。
  发布前确认无活动应用实例；旧程序保存在
  `output/px_console/rdp/console-runtime-backup-20260909135330.exe`，配置及主密钥未变。
- `app-cli-20260909135407` / `inst-109-59ea00cc`：
  `WEB_COPY_ASCII=True`，`WEB_CLIPBOARD_UNICODE_ROUNDTRIP=True`。
  截图确认中文替换原选区；回读使用 fresh sentinel 后重新 Ctrl+A/C，
  不是把本机原有剪贴板误算为远端复制结果。全选以完整替换/完整回读行为验证，
  不依赖选区高亮截图。Client 经退出确认关闭，ExitCode=1 / Forced=False。
- 用户已获通知恢复操作。本轮没有改数据库启动参数，也没有注销或删除任何 Windows 账号/profile。
- 最终批次含收尾 100.60 秒，脚本 exit 0；精确实例显式停止及重复停止均返回 stopped。
  35 秒观察内没有自然退出，仍按原验收边界记录，不宣称自然超时退出已通过。
